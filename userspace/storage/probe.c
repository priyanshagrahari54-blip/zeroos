/*
 * ZEROOS Stage 3 Ring-3 storage probe. Exercises the file/VFS syscall ABI
 * from user mode against the volatile /ram filesystem (and /data when a
 * persistent ZJFS volume is mounted): I/O, positional I/O, seek, stat,
 * namespace ops, readdir, fsync, mmap coherence + truncate-while-mapped,
 * permissions/credential drop, and error paths (EFAULT/EBADF/ENOENT/
 * EEXIST/EACCES/EPERM/EBUSY). It deliberately exits with one descriptor
 * and one mapping still open so the kernel can verify teardown.
 */
#include <zeroos/storage.h>

static char line[256];
static uint8_t buffer[16384];
static uint8_t check[16384];
static struct zeroos_dirent entry;
static struct zeroos_stat st;
static struct zeroos_statfs sf;

static uint64_t slen(const char *s) {
    uint64_t n=0;
    while (s[n])
        ++n;
    return n;
}

static void say(const char *s) {
    (void)zeroos_write(1,s,slen(s));
}

static void fmt_num(char *out, int64_t v) {
    char tmp[24];
    int n=0, neg=v<0;
    uint64_t u=neg ? (uint64_t)(-v) : (uint64_t)v;
    do { tmp[n++]=(char)('0'+u%10U); u/=10U; } while (u);
    int k=0;
    if (neg)
        out[k++]='-';
    while (n)
        out[k++]=tmp[--n];
    out[k]=0;
}

static int fail(const char *what, int64_t value) {
    char num[24];
    uint64_t k=0;
    const char *prefix="ZEROOS: storage userspace probe FAILED: ";
    for (uint64_t i=0; prefix[i] && k<200; ++i) line[k++]=prefix[i];
    for (uint64_t i=0; what[i] && k<230; ++i) line[k++]=what[i];
    line[k++]=' ';
    line[k++]='(';
    fmt_num(num,value);
    for (uint64_t i=0; num[i] && k<250; ++i) line[k++]=num[i];
    line[k++]=')';
    line[k++]='\n';
    line[k]=0;
    say(line);
    return 1;
}

#define CHECK(cond, what, value) do { if (!(cond)) return fail((what), (int64_t)(value)); } while (0)

static int same(const uint8_t *a, const uint8_t *b, uint64_t n) {
    for (uint64_t i=0; i<n; ++i)
        if (a[i]!=b[i])
            return 0;
    return 1;
}

static int streq(const char *a, const char *b, uint64_t n) {
    for (uint64_t i=0; i<n; ++i)
        if (a[i]!=b[i])
            return 0;
    return b[n]==0;
}

int probe_main(void) {
    struct zeroos_abi_info info;
    int64_t rc=zeroos_abi_info(&info);
    CHECK(rc==0 && (info.features&ZEROOS_ABI_FEATURE_FILES),"abi files feature",rc);
    CHECK(zeroos_getcred()==0,"initial credentials root",zeroos_getcred());

    /* --- basic I/O --- */
    rc=zeroos_mkdir("/ram/probe",0755);
    CHECK(rc==0,"mkdir /ram/probe",rc);
    int64_t fd=zeroos_open("/ram/probe/f",ZEROOS_O_RDWR|ZEROOS_O_CREAT|ZEROOS_O_EXCL,0644);
    CHECK(fd>=0,"open create",fd);
    for (uint64_t i=0; i<sizeof(buffer); ++i)
        buffer[i]=(uint8_t)(i*7U+3U);
    rc=zeroos_file_write(fd,buffer,10000);
    CHECK(rc==10000,"write 10000",rc);
    CHECK(zeroos_seek(fd,0,ZEROOS_SEEK_SET)==0,"seek set",0);
    rc=zeroos_read(fd,check,sizeof(check));
    CHECK(rc==10000 && same(buffer,check,10000),"read back",rc);
    CHECK(zeroos_read(fd,check,16)==0,"read at EOF",0);
    rc=zeroos_pwrite(fd,"ZEROOS",6,8190);
    CHECK(rc==6,"pwrite",rc);
    rc=zeroos_pread(fd,check,6,8190);
    CHECK(rc==6 && same(check,(const uint8_t *)"ZEROOS",6),"pread",rc);
    buffer[8190]='Z'; buffer[8191]='E'; buffer[8192]='R';
    buffer[8193]='O'; buffer[8194]='O'; buffer[8195]='S';
    CHECK(zeroos_seek(fd,-4,ZEROOS_SEEK_END)==9996,"seek end",0);
    CHECK(zeroos_fstat(fd,&st)==0 && st.size==10000 &&
          (st.mode&ZEROOS_S_IFMT)==ZEROOS_S_IFREG && st.links==1,"fstat",st.size);
    CHECK(zeroos_fsync(fd,0)==0,"fsync",0);
    CHECK(zeroos_fsync(fd,0x80)==-ZEROOS_EINVAL,"fsync bad flags",0);

    /* --- error paths --- */
    CHECK(zeroos_stat("/ram/probe/missing",&st)==-ZEROOS_ENOENT,"ENOENT",0);
    CHECK(zeroos_open("/ram/probe/f",ZEROOS_O_CREAT|ZEROOS_O_EXCL|ZEROOS_O_RDWR,0644)==
          -ZEROOS_EEXIST,"EEXIST",0);
    CHECK(zeroos_read(31,check,4)==-ZEROOS_EBADF,"EBADF",0);
    CHECK(zeroos_stat("/ram",(struct zeroos_stat *)0x1000)==-ZEROOS_EFAULT,"EFAULT buffer",0);
    CHECK(zeroos_stat((const char *)0x1000,&st)==-ZEROOS_EFAULT,"EFAULT path",0);
    CHECK(zeroos_read(fd,(void *)0x1000,16)!=16,"read into unmapped",0);
    CHECK(zeroos_mkdir("/ram/probe/f/x",0755)==-ZEROOS_ENOTDIR,"ENOTDIR",0);
    CHECK(zeroos_rmdir("/ram")!=0,"rmdir mount root refused",0);

    /* --- namespace --- */
    CHECK(zeroos_rename("/ram/probe/f","/ram/probe/g")==0,"rename",0);
    CHECK(zeroos_link("/ram/probe/g","/ram/probe/h")==0,"link",0);
    CHECK(zeroos_stat("/ram/probe/h",&st)==0 && st.links==2,"link count 2",st.links);
    CHECK(zeroos_unlink("/ram/probe/h")==0,"unlink",0);
    int64_t dfd=zeroos_open("/ram/probe",ZEROOS_O_RDONLY|ZEROOS_O_DIRECTORY,0);
    CHECK(dfd>=0,"open dir",dfd);
    int dots=0, seen_g=0, others=0;
    while ((rc=zeroos_readdir(dfd,&entry))==1) {
        if (streq(entry.name,".",1) || streq(entry.name,"..",2))
            ++dots;
        else if (streq(entry.name,"g",1))
            ++seen_g;
        else
            ++others;
    }
    CHECK(rc==0 && dots==2 && seen_g==1 && others==0,"readdir",rc);
    CHECK(zeroos_close(dfd)==0,"close dir",0);
    CHECK(zeroos_close(dfd)==-ZEROOS_EBADF,"double close",0);
    int64_t dup=zeroos_dup(fd);
    CHECK(dup>=0 && dup!=fd,"dup",dup);
    CHECK(zeroos_close(dup)==0,"close dup",0);

    /* --- mmap coherence --- */
    int64_t map=zeroos_mmap(fd,0,8192,ZEROOS_MMAP_PROT_WRITE);
    CHECK(map>0,"mmap",map);
    volatile uint8_t *m=(volatile uint8_t *)(uint64_t)map;
    int mapped_ok=1;
    for (uint64_t i=0; i<8192; ++i)
        if (m[i]!=buffer[i])
            mapped_ok=0;
    CHECK(mapped_ok,"mmap contents match file",0);
    m[100]=0xA5;
    m[5000]=0x5A;
    CHECK(zeroos_msync((void *)(uint64_t)map)==0,"msync",0);
    CHECK(zeroos_pread(fd,check,1,100)==1 && check[0]==0xA5,"mmap store visible to read",check[0]);
    CHECK(zeroos_pwrite(fd,"\x11",1,6000)==1 && m[6000]==0x11,"write visible in mapping",m[6000]);
    CHECK(zeroos_ftruncate(fd,0)==-ZEROOS_EBUSY,"truncate below mapping refused",0);
    CHECK(zeroos_munmap((void *)(uint64_t)map)==0,"munmap",0);
    CHECK(zeroos_munmap((void *)(uint64_t)map)==-ZEROOS_EINVAL,"double munmap",0);
    CHECK(zeroos_mmap(fd,1,4096,0)==-ZEROOS_EINVAL,"mmap unaligned",0);
    CHECK(zeroos_mmap(fd,65536,4096,0)==-ZEROOS_ENXIO,"mmap past EOF",0);
    CHECK(zeroos_ftruncate(fd,4096)==0,"truncate after munmap",0);

    CHECK(zeroos_statfs("/ram",&sf)==0 && sf.block_size==4096 && sf.free_blocks>0 &&
          sf.free_blocks<=sf.total_blocks,"statfs",sf.free_blocks);

    /* --- persistent volume (optional) --- */
    if (zeroos_stat("/data",&st)==0) {
        int64_t pfd=zeroos_open("/data/user-probe.txt",
                                ZEROOS_O_WRONLY|ZEROOS_O_CREAT|ZEROOS_O_TRUNC,0644);
        CHECK(pfd>=0,"open /data file",pfd);
        CHECK(zeroos_file_write(pfd,"written from ring 3\n",20)==20,"write /data",0);
        CHECK(zeroos_fsync(pfd,0)==0,"fsync /data",0);
        CHECK(zeroos_close(pfd)==0,"close /data",0);
        say("ZEROOS: storage userspace probe wrote /data/user-probe.txt (fsync ok).\n");
    }

    /* --- permissions & credentials --- */
    int64_t sfd=zeroos_open("/ram/probe/secret",ZEROOS_O_WRONLY|ZEROOS_O_CREAT,0600);
    CHECK(sfd>=0 && zeroos_close(sfd)==0,"create secret",sfd);
    CHECK(zeroos_mkdir("/ram/probe/pub",0777)==0,"mkdir pub",0);
    CHECK(zeroos_setcred(1000,1000)==0,"drop to uid 1000",0);
    CHECK(zeroos_getcred()==(int64_t)(1000ULL|(1000ULL<<32)),"getcred after drop",0);
    CHECK(zeroos_open("/ram/probe/secret",ZEROOS_O_RDONLY,0)==-ZEROOS_EACCES,"EACCES",0);
    CHECK(zeroos_chmod("/ram/probe/secret",0666)==-ZEROOS_EPERM,"chmod not owner",0);
    CHECK(zeroos_open("/ram/probe/new",ZEROOS_O_WRONLY|ZEROOS_O_CREAT,0644)==-ZEROOS_EACCES,
          "create in root-owned 0755 dir",0);
    int64_t ufd=zeroos_open("/ram/probe/pub/mine",ZEROOS_O_RDWR|ZEROOS_O_CREAT,0600);
    CHECK(ufd>=0,"create in 0777 dir",ufd);
    CHECK(zeroos_fstat(ufd,&st)==0 && st.uid==1000 && st.gid==1000,"new file owner",st.uid);
    CHECK(zeroos_setcred(0,0)==-ZEROOS_EPERM,"cannot regain root",0);

    /* Exit with `ufd` open and a live mapping: the kernel verifies the
     * storage worker releases both after the process is reaped. */
    CHECK(zeroos_file_write(ufd,buffer,4096)==4096,"write mine",0);
    int64_t keep=zeroos_mmap(ufd,0,4096,0);
    CHECK(keep>0,"mmap kept at exit",keep);
    say("ZEROOS: storage userspace probe passed.\n");
    return 0;
}

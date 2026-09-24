/*
 * Boot-time VFS / ZJFS / page-cache certification on ramdisks.
 * Every check prints a stable marker; failures name the exact check.
 */
#include "ramdisk.h"
#include "storage.h"
#include "vfs.h"
#include "zjfs.h"
#include "../kstring.h"
#include "../memory.h"
#include "../task.h"
#include "../timer.h"

#define TF_CHECK(cond, what) do { if (!(cond)) { \
    klog("ZEROOS: storage fs test FAILED: %s (line %d).", what, __LINE__); \
    return -1; } } while (0)
#define TF_RUN(call, label) do { if ((call) != 0) return -1; \
    klog("ZEROOS: storage fs test %s passed.", label); } while (0)

static const struct vfs_cred tf_root={0,0};
static const struct vfs_cred tf_user={1000,1000};

static void tf_fill(uint8_t *buffer, uint64_t length, uint32_t seed) {
    uint32_t x=seed*2654435761U+1U;
    for (uint64_t i=0; i<length; ++i) {
        x^=x<<13; x^=x>>17; x^=x<<5;
        buffer[i]=(uint8_t)x;
    }
}

static int tf_write_file(const char *path, const uint8_t *data, uint64_t length,
                         int do_fsync) {
    struct vfs_file *f;
    int rc=vfs_open(&tf_root,path,VFS_O_CREAT|VFS_O_TRUNC|VFS_O_WRONLY,0644,&f);
    if (rc)
        return rc;
    int64_t n=vfs_write(f,data,length);
    rc=n<0 ? (int)n : ((uint64_t)n!=length ? -SE_NOSPC : 0);
    if (rc==0 && do_fsync)
        rc=vfs_fsync(f,0);
    vfs_file_put(f);
    return rc;
}

static int tf_verify_file(const char *path, const uint8_t *data, uint64_t length,
                          uint8_t *scratch) {
    struct vfs_file *f;
    int rc=vfs_open(&tf_root,path,VFS_O_RDONLY,0,&f);
    if (rc)
        return rc;
    struct vfs_stat st;
    (void)vfs_fstat(f,&st);
    rc=st.size==length ? 0 : -SE_UCLEAN;
    uint64_t done=0;
    while (rc==0 && done<length) {
        uint64_t chunk=length-done>65536U ? 65536U : length-done;
        int64_t n=vfs_read(f,scratch,chunk);
        if (n<=0 || memcmp(scratch,data+done,(uint64_t)n)!=0)
            rc=n<0 ? (int)n : -SE_UCLEAN;
        else
            done+=(uint64_t)n;
    }
    vfs_file_put(f);
    return rc;
}

static int tf_exists(const char *path) {
    struct vfs_stat st;
    return vfs_stat(&tf_root,path,&st)==0;
}

static uint64_t tf_free_blocks(const char *path) {
    struct vfs_statfs s;
    if (vfs_statfs(path,&s)!=0)
        return 0;
    return s.free_blocks;
}

static int tf_fsck_clean(struct block_device *device, const char *what) {
    struct zj_check_report report;
    int rc=zjfs_check(device,0,&report);
    if (rc || report.checksum_errors || report.structure_errors || report.leaked_blocks ||
        report.leaked_inodes || report.link_errors || report.counter_errors || report.fatal) {
        klog("ZEROOS: storage fs test FAILED: fsck not clean after %s (rc=%d).",what,rc);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------- basics */

static int tf_basic(uint8_t *a, uint8_t *b) {
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t",VFS_O_RDONLY|VFS_O_DIRECTORY,0,&f)==0,"open root");
    struct vfs_dirent de;
    int saw_lf=0, entries=0;
    while (vfs_readdir(f,&de)==1) {
        ++entries;
        if (de.name_len==10 && memcmp(de.name,"lost+found",10)==0)
            saw_lf=1;
    }
    vfs_file_put(f);
    TF_CHECK(saw_lf && entries==3,"root lists . .. lost+found");
    tf_fill(a,10000,1);
    TF_CHECK(tf_write_file("/t/hello",a,10000,0)==0,"create+write");
    TF_CHECK(tf_verify_file("/t/hello",a,10000,b)==0,"read back");
    TF_CHECK(vfs_open(&tf_root,"/t/hello",VFS_O_RDWR,0,&f)==0,"reopen rw");
    tf_fill(a+5000,100,2);
    TF_CHECK(vfs_pwrite(f,a+5000,100,5000)==100,"pwrite");
    TF_CHECK(vfs_pread(f,b,200,4950)==200 && memcmp(b,a+4950,200)==0,"pread");
    TF_CHECK(vfs_seek(f,0,VFS_SEEK_END)==10000,"seek end");
    TF_CHECK(vfs_seek(f,-1,VFS_SEEK_SET)==-SE_INVAL,"negative seek rejected");
    TF_CHECK(vfs_seek(f,20000,VFS_SEEK_SET)==20000,"seek past EOF");
    TF_CHECK(vfs_write(f,"Z",1)==1,"write after hole");
    TF_CHECK(vfs_pread(f,b,4096,12000)==4096,"read hole");
    int zeros=1;
    for (uint32_t i=0; i<4096; ++i)
        if (b[i])
            zeros=0;
    TF_CHECK(zeros,"hole reads zeros");
    struct vfs_stat st;
    TF_CHECK(vfs_fstat(f,&st)==0 && st.size==20001 && st.links==1 &&
             VFS_S_ISREG(st.mode) && st.mtime_ns!=0,"fstat");
    vfs_file_put(f);
    /* O_APPEND */
    TF_CHECK(vfs_open(&tf_root,"/t/hello",VFS_O_WRONLY|VFS_O_APPEND,0,&f)==0,"append open");
    TF_CHECK(vfs_write(f,"END",3)==3,"append write");
    vfs_file_put(f);
    TF_CHECK(vfs_stat(&tf_root,"/t/hello",&st)==0 && st.size==20004,"append size");
    /* error paths */
    TF_CHECK(vfs_open(&tf_root,"/t/missing",VFS_O_RDONLY,0,&f)==-SE_NOENT,"ENOENT");
    TF_CHECK(vfs_open(&tf_root,"/t/hello",VFS_O_CREAT|VFS_O_EXCL|VFS_O_RDWR,0644,&f)==-SE_EXIST,
             "O_EXCL");
    TF_CHECK(vfs_open(&tf_root,"/t/hello/x",VFS_O_RDONLY,0,&f)==-SE_NOTDIR,"ENOTDIR");
    TF_CHECK(vfs_open(&tf_root,"/t/lost+found",VFS_O_WRONLY,0,&f)==-SE_ISDIR,"EISDIR");
    TF_CHECK(vfs_open(&tf_root,"relative",VFS_O_RDONLY,0,&f)==-SE_INVAL,"relative path");
    char longname[300];
    longname[0]='/'; longname[1]='t'; longname[2]='/';
    for (uint32_t i=3; i<290; ++i) longname[i]='n';
    longname[290]=0;
    TF_CHECK(vfs_open(&tf_root,longname,VFS_O_CREAT|VFS_O_RDWR,0644,&f)==-SE_NAMETOOLONG,
             "ENAMETOOLONG");
    return 0;
}

/* ------------------------------------------------------- large + truncate */

static int tf_large(uint8_t *big, uint8_t *b) {
    uint64_t size=5U*1024U*1024U;               /* reaches double-indirect */
    uint64_t before=tf_free_blocks("/t");
    tf_fill(big,size,7);
    TF_CHECK(tf_write_file("/t/big",big,size,1)==0,"5 MiB write+fsync");
    TF_CHECK(tf_verify_file("/t/big",big,size,b)==0,"5 MiB verify");
    uint64_t used=before-tf_free_blocks("/t");
    TF_CHECK(used>=1280U && used<=1290U,"block accounting incl. indirect blocks");
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t/big",VFS_O_RDWR,0,&f)==0,"open big");
    TF_CHECK(vfs_ftruncate(f,1000000)==0,"truncate to 1000000");
    TF_CHECK(vfs_ftruncate(f,1200000)==0,"extend to 1200000");
    TF_CHECK(vfs_pread(f,b,8192,999000)==8192,"read across old EOF");
    TF_CHECK(memcmp(b,big+999000,1000)==0,"kept prefix");
    int zeros=1;
    for (uint32_t i=1000; i<8192; ++i)
        if (b[i])
            zeros=0;
    TF_CHECK(zeros,"truncated tail reads zeros after extension");
    TF_CHECK(vfs_fsync(f,0)==0,"fsync after truncate");
    vfs_file_put(f);
    uint64_t now_used=before-tf_free_blocks("/t");
    TF_CHECK(now_used<=250U,"truncate freed blocks");
    TF_CHECK(vfs_unlink(&tf_root,"/t/big")==0,"unlink big");
    TF_CHECK(tf_free_blocks("/t")==before,"all blocks returned");
    return 0;
}

/* ----------------------------------------------------------- namespace */

static int tf_namespace(uint8_t *a, uint8_t *b) {
    TF_CHECK(vfs_mkdir(&tf_root,"/t/d1",0755)==0,"mkdir d1");
    TF_CHECK(vfs_mkdir(&tf_root,"/t/d1/d2",0755)==0,"mkdir d2");
    TF_CHECK(vfs_mkdir(&tf_root,"/t/d1",0755)==-SE_EXIST,"mkdir EEXIST");
    tf_fill(a,3000,3);
    TF_CHECK(tf_write_file("/t/d1/f",a,3000,0)==0,"file in d1");
    TF_CHECK(vfs_rmdir(&tf_root,"/t/d1")==-SE_NOTEMPTY,"rmdir ENOTEMPTY");
    TF_CHECK(vfs_rename(&tf_root,"/t/d1/f","/t/d1/d2/g")==0,"rename across dirs");
    TF_CHECK(!tf_exists("/t/d1/f") && tf_verify_file("/t/d1/d2/g",a,3000,b)==0,
             "rename moved data");
    TF_CHECK(vfs_rename(&tf_root,"/t/d1","/t/d1/d2/x")==-SE_INVAL,"dir into own subtree");
    /* rename over an existing file is atomic replacement */
    tf_fill(a+4000,500,4);
    TF_CHECK(tf_write_file("/t/other",a+4000,500,0)==0,"other");
    TF_CHECK(vfs_rename(&tf_root,"/t/other","/t/d1/d2/g")==0,"rename replace");
    TF_CHECK(tf_verify_file("/t/d1/d2/g",a+4000,500,b)==0 && !tf_exists("/t/other"),
             "replacement content");
    /* hard links */
    TF_CHECK(vfs_link(&tf_root,"/t/d1/d2/g","/t/hard")==0,"link");
    struct vfs_stat st;
    TF_CHECK(vfs_stat(&tf_root,"/t/hard",&st)==0 && st.links==2,"links=2");
    TF_CHECK(vfs_link(&tf_root,"/t/d1","/t/dlink")==-SE_PERM,"no dir hard links");
    TF_CHECK(vfs_unlink(&tf_root,"/t/d1/d2/g")==0,"unlink one name");
    TF_CHECK(tf_verify_file("/t/hard",a+4000,500,b)==0,"data via other link");
    TF_CHECK(vfs_stat(&tf_root,"/t/hard",&st)==0 && st.links==1,"links=1");
    /* directory link counts and rename of directories */
    TF_CHECK(vfs_stat(&tf_root,"/t/d1",&st)==0 && st.links==3,"d1 links=3");
    TF_CHECK(vfs_mkdir(&tf_root,"/t/d3",0755)==0,"mkdir d3");
    TF_CHECK(vfs_rename(&tf_root,"/t/d1/d2","/t/d3/d2")==0,"move dir");
    TF_CHECK(vfs_stat(&tf_root,"/t/d1",&st)==0 && st.links==2,"d1 links=2");
    TF_CHECK(vfs_stat(&tf_root,"/t/d3",&st)==0 && st.links==3,"d3 links=3");
    TF_CHECK(tf_exists("/t/d3/d2/.."),"dotdot resolves");
    TF_CHECK(vfs_rmdir(&tf_root,"/t/d3/d2")==0 && vfs_rmdir(&tf_root,"/t/d3")==0 &&
             vfs_rmdir(&tf_root,"/t/d1")==0,"rmdir tree");
    TF_CHECK(vfs_unlink(&tf_root,"/t/lost+found")==-SE_ISDIR,"unlink dir -> EISDIR");
    TF_CHECK(vfs_rmdir(&tf_root,"/t/hard")==-SE_NOTDIR,"rmdir file -> ENOTDIR");
    /* many entries: multi-block directory */
    TF_CHECK(vfs_mkdir(&tf_root,"/t/many",0755)==0,"mkdir many");
    char path[64];
    for (uint32_t i=0; i<200; ++i) {
        ksnprintf(path,sizeof(path),"/t/many/entry-with-a-long-name-%u",i);
        struct vfs_file *f;
        TF_CHECK(vfs_open(&tf_root,path,VFS_O_CREAT|VFS_O_WRONLY,0644,&f)==0,"create many");
        vfs_file_put(f);
    }
    struct vfs_file *d;
    TF_CHECK(vfs_open(&tf_root,"/t/many",VFS_O_RDONLY,0,&d)==0,"open many");
    struct vfs_dirent de;
    uint32_t count=0;
    while (vfs_readdir(d,&de)==1)
        ++count;
    vfs_file_put(d);
    TF_CHECK(count==202,"readdir sees 200 entries + . ..");
    TF_CHECK(vfs_stat(&tf_root,"/t/many",&st)==0 && st.size==2U*4096U,"multi-block dir (200 x 40-byte records = 2 blocks)");
    for (uint32_t i=0; i<200; ++i) {
        ksnprintf(path,sizeof(path),"/t/many/entry-with-a-long-name-%u",i);
        TF_CHECK(vfs_unlink(&tf_root,path)==0,"unlink many");
    }
    TF_CHECK(vfs_rmdir(&tf_root,"/t/many")==0,"rmdir many");
    return 0;
}

/* ------------------------------------------------ unlink while open */

static int tf_open_unlink(uint8_t *a, uint8_t *b) {
    uint64_t before=tf_free_blocks("/t");
    tf_fill(a,65536,5);
    TF_CHECK(tf_write_file("/t/ghost",a,65536,1)==0,"ghost");
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t/ghost",VFS_O_RDWR,0,&f)==0,"open ghost");
    TF_CHECK(vfs_unlink(&tf_root,"/t/ghost")==0,"unlink open file");
    TF_CHECK(!tf_exists("/t/ghost"),"name gone");
    TF_CHECK(vfs_pread(f,b,65536,0)==65536 && memcmp(a,b,65536)==0,"still readable");
    TF_CHECK(vfs_pwrite(f,a,100,70000)==100,"still writable");
    TF_CHECK(tf_free_blocks("/t")<before,"blocks held while open");
    vfs_file_put(f);
    TF_CHECK(tf_free_blocks("/t")==before,"blocks freed at last close");
    return 0;
}

/* --------------------------------------------------------- permissions */

static int tf_permissions(uint8_t *a) {
    struct vfs_file *f;
    tf_fill(a,100,6);
    TF_CHECK(tf_write_file("/t/secret",a,100,0)==0,"secret");
    TF_CHECK(vfs_chmod(&tf_root,"/t/secret",0600)==0,"chmod 0600");
    TF_CHECK(vfs_open(&tf_user,"/t/secret",VFS_O_RDONLY,0,&f)==-SE_ACCES,"uid1000 read denied");
    TF_CHECK(vfs_open(&tf_user,"/t/newfile",VFS_O_CREAT|VFS_O_WRONLY,0644,&f)==-SE_ACCES,
             "uid1000 create in root 0755 dir denied");
    TF_CHECK(vfs_unlink(&tf_user,"/t/secret")==-SE_ACCES,"uid1000 unlink denied");
    TF_CHECK(vfs_chmod(&tf_user,"/t/secret",0666)==-SE_PERM,"chmod by non-owner");
    TF_CHECK(vfs_chown(&tf_user,"/t/secret",1000,1000)==-SE_PERM,"chown by non-root");
    TF_CHECK(vfs_mkdir(&tf_root,"/t/home",0755)==0,"mkdir home");
    TF_CHECK(vfs_chown(&tf_root,"/t/home",1000,1000)==0,"chown home");
    TF_CHECK(vfs_open(&tf_user,"/t/home/mine",VFS_O_CREAT|VFS_O_RDWR,0400,&f)==0,
             "user creates in own dir (creator keeps rw on 0400)");
    TF_CHECK(vfs_write(f,"ok",2)==2,"creator writes");
    vfs_file_put(f);
    struct vfs_stat st;
    TF_CHECK(vfs_stat(&tf_root,"/t/home/mine",&st)==0 && st.uid==1000 && st.gid==1000 &&
             (st.mode&0777U)==0400U,"owner/mode recorded");
    TF_CHECK(vfs_open(&tf_user,"/t/home/mine",VFS_O_WRONLY,0,&f)==-SE_ACCES,
             "0400 reopen for write denied");
    TF_CHECK(vfs_chmod(&tf_root,"/t/home",0700)==0,"home 0700");
    TF_CHECK(vfs_stat(&tf_user,"/t/home/mine",&st)==0,"owner traverses 0700");
    TF_CHECK(vfs_chown(&tf_root,"/t/home",0,0)==0,"home back to root");
    TF_CHECK(vfs_stat(&tf_user,"/t/home/mine",&st)==-SE_ACCES,"search permission enforced");
    TF_CHECK(vfs_unlink(&tf_root,"/t/home/mine")==0 && vfs_rmdir(&tf_root,"/t/home")==0 &&
             vfs_unlink(&tf_root,"/t/secret")==0,"cleanup");
    return 0;
}

/* -------------------------------------------------------------- ENOSPC */

static int tf_enospc(struct block_device *device, uint8_t *a, uint8_t *b) {
    uint64_t before=tf_free_blocks("/t");
    tf_fill(a,65536,8);
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t/fill",VFS_O_CREAT|VFS_O_WRONLY,0644,&f)==0,"fill open");
    int64_t last=0;
    uint64_t written=0;
    for (uint32_t i=0; i<100000; ++i) {
        last=vfs_write(f,a,65536);
        if (last<=0)
            break;
        written+=(uint64_t)last;
        if (last<65536)
            continue;       /* short write: next call reports ENOSPC */
    }
    TF_CHECK(last==-SE_NOSPC,"data write reports ENOSPC");
    TF_CHECK(vfs_fsync(f,0)==0,"fsync of full file succeeds");
    vfs_file_put(f);
    /* Metadata operations at ENOSPC fail cleanly (no partial state). */
    uint32_t creates_failed=0;
    char path[48];
    for (uint32_t i=0; i<64; ++i) {
        ksnprintf(path,sizeof(path),"/t/full-%u",i);
        int rc=vfs_mkdir(&tf_root,path,0755);
        if (rc==-SE_NOSPC) {
            ++creates_failed;
            break;
        }
        TF_CHECK(rc==0,"mkdir before full");
    }
    TF_CHECK(creates_failed==1,"mkdir at ENOSPC -> ENOSPC");
    TF_CHECK(!tf_exists(path),"failed mkdir left no entry");
    /* Verify content that did fit, then free space. */
    TF_CHECK(vfs_open(&tf_root,"/t/fill",VFS_O_RDONLY,0,&f)==0,"reopen fill");
    TF_CHECK(vfs_pread(f,b,65536,0)==65536 && memcmp(a,b,65536)==0,"full file intact");
    vfs_file_put(f);
    TF_CHECK(vfs_unlink(&tf_root,"/t/fill")==0,"unlink fill");
    for (uint32_t i=0; i<64; ++i) {
        ksnprintf(path,sizeof(path),"/t/full-%u",i);
        if (tf_exists(path))
            TF_CHECK(vfs_rmdir(&tf_root,path)==0,"rmdir fillers");
    }
    TF_CHECK(tf_free_blocks("/t")==before,"space fully recovered");
    klog("ZEROOS: storage fs ENOSPC: wrote %llu bytes before ENOSPC on %s.",written,
         device->name);
    return 0;
}

/* ------------------------------------------------------ memory pressure */

static int tf_pressure(uint8_t *big, uint8_t *b) {
    struct pc_stats before, after;
    pc_stats_snapshot(&before);
    pc_set_limit(32);
    uint64_t size=2U*1024U*1024U;
    tf_fill(big,size,9);
    TF_CHECK(tf_write_file("/t/press",big,size,0)==0,"write 2 MiB with 32-page cache");
    TF_CHECK(tf_verify_file("/t/press",big,size,b)==0,"verify under pressure");
    pc_stats_snapshot(&after);
    TF_CHECK(after.pages<=32U,"cache bounded");
    TF_CHECK(after.evictions>before.evictions+400U,"evictions happened");
    TF_CHECK(after.throttled>before.throttled,"dirty throttling engaged");
    pc_set_limit(before.limit);
    TF_CHECK(vfs_unlink(&tf_root,"/t/press")==0,"unlink press");
    klog("ZEROOS: storage page cache pressure: limit=32 evictions=%llu throttled=%llu writebacks=%llu.",
         after.evictions-before.evictions,after.throttled-before.throttled,
         after.writebacks-before.writebacks);
    return 0;
}

/* ------------------------------------------------------------- errors */

static int tf_io_errors(struct block_device *device, uint8_t *a, uint8_t *b) {
    tf_fill(a,8192,10);
    TF_CHECK(tf_write_file("/t/eio",a,8192,1)==0,"eio file");
    /* Read error on a cold page -> EIO, never stale/garbage data. */
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount to drop cache");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"remount");
    struct block_fault fault;
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_EIO;
    fault.op_mask=1U<<BLOCK_OP_READ;
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t/eio",VFS_O_RDWR,0,&f)==0,"open eio");
    block_fault_set(device,&fault);
    TF_CHECK(vfs_pread(f,b,4096,0)==-SE_IO,"read error -> EIO");
    block_fault_clear(device);
    TF_CHECK(vfs_pread(f,b,8192,0)==8192 && memcmp(a,b,8192)==0,"read after fault clears");
    /* (A) One failed data writeback: fsync reports EIO once for this
     * description; the page stays dirty and the retry succeeds. */
    fault.op_mask=1U<<BLOCK_OP_WRITE;
    fault.remaining=3;          /* outlasts the block layer's 2 retries */
    tf_fill(a,4096,11);
    TF_CHECK(vfs_pwrite(f,a,4096,0)==4096,"buffered write");
    block_fault_set(device,&fault);
    TF_CHECK(vfs_fsync(f,0)==-SE_IO,"fsync reports writeback EIO");
    block_fault_clear(device);
    TF_CHECK(vfs_fsync(f,0)==0,"retry fsync succeeds (error reported once)");
    vfs_file_put(f);
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount after data error");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"remount after data error");
    TF_CHECK(vfs_open(&tf_root,"/t/eio",VFS_O_RDONLY,0,&f)==0,"reopen eio");
    TF_CHECK(vfs_pread(f,b,4096,0)==4096,"read rewritten page");
    vfs_file_put(f);
    tf_fill(a,4096,11);
    TF_CHECK(memcmp(a,b,4096)==0,"retried data is durable");
    /* (B) Journal write failure: the filesystem aborts (read-only), the
     * ERROR state is persisted, the next mount is read-only until fsck. */
    uint8_t *sbp=(uint8_t *)page_alloc();
    TF_CHECK(sbp && block_rw(device,BLOCK_OP_READ,0,sbp,4096,BLOCK_PRIO_NORMAL)==0,"sb");
    uint32_t spb=4096U/block_root(device)->sector_size;
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_EIO;
    fault.op_mask=1U<<BLOCK_OP_WRITE;
    fault.lba_start=device->start_lba+spb;
    fault.lba_end=device->start_lba+spb*(1U+((struct zj_super *)sbp)->journal_blocks);
    page_free(sbp);
    block_fault_set(device,&fault);
    int rc=vfs_mkdir(&tf_root,"/t/doomed",0755);
    struct vfs_file *d;
    if (rc==0 && vfs_open(&tf_root,"/t/doomed",VFS_O_RDONLY,0,&d)==0) {
        rc=vfs_fsync(d,0);
        vfs_file_put(d);
    }
    block_fault_clear(device);
    TF_CHECK(rc==-SE_IO,"journal write failure surfaces EIO");
    struct vfs_statfs s;
    TF_CHECK(vfs_statfs("/t",&s)==0 && (s.flags&VFS_SB_ERROR),"filesystem aborted read-only");
    TF_CHECK(vfs_mkdir(&tf_root,"/t/after-abort",0755)==-SE_IO,"no writes after abort");
    (void)vfs_unmount("/t",1);
    TF_CHECK(vfs_mount(device,"/t",0)==0,"mount after abort");
    TF_CHECK(vfs_statfs("/t",&s)==0 && (s.flags&VFS_SB_RDONLY),"ERROR state -> read-only mount");
    TF_CHECK(!tf_exists("/t/doomed"),"uncommitted mkdir not visible");
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount ro");
    struct zj_check_report report;
    TF_CHECK(zjfs_check(device,1,&report)==0 && report.fatal==0,"fsck repair clears ERROR");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"mount rw after repair");
    TF_CHECK(vfs_statfs("/t",&s)==0 && !(s.flags&(VFS_SB_RDONLY|VFS_SB_ERROR)),"rw again");
    TF_CHECK(vfs_unlink(&tf_root,"/t/eio")==0,"cleanup eio");
    return 0;
}

static int tf_corruption(struct block_device *device, uint8_t *a) {
    /* Corrupt the root directory block: lookups report EUCLEAN, fsck
     * detects it; restore afterwards. */
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount for corruption");
    uint32_t spb=4096U/block_root(device)->sector_size;
    uint8_t *sb=a;
    TF_CHECK(block_rw(device,BLOCK_OP_READ,0,sb,4096,BLOCK_PRIO_NORMAL)==0,"read sb");
    uint64_t root_block=((struct zj_super *)sb)->data_start;
    uint8_t *saved=a+4096;
    uint8_t *work=a+8192;
    uint64_t lba=device->start_lba+root_block*spb;
    TF_CHECK(ramdisk_peek(device,lba,saved,spb)==0,"peek root dir");
    memcpy(work,saved,4096);
    work[100]^=0x5a;
    TF_CHECK(ramdisk_poke(device,lba,work,spb)==0,"poke corrupt");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"mount with corrupt dir");
    struct vfs_stat st;
    TF_CHECK(vfs_stat(&tf_root,"/t/anything",&st)==-SE_UCLEAN,"corrupt dir -> EUCLEAN");
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount corrupt");
    struct zj_check_report report;
    (void)zjfs_check(device,0,&report);
    TF_CHECK(report.checksum_errors>0 || report.fatal>0,"fsck detects corrupt directory");
    TF_CHECK(ramdisk_poke(device,lba,saved,spb)==0,"restore dir");
    /* Primary superblock destroyed -> backup superblock used. */
    uint64_t sb_lba=device->start_lba;
    TF_CHECK(ramdisk_peek(device,sb_lba,saved,spb)==0,"peek sb");
    memset(work,0xee,4096);
    TF_CHECK(ramdisk_poke(device,sb_lba,work,spb)==0,"poke sb");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"mount from backup superblock");
    TF_CHECK(tf_exists("/t/lost+found"),"tree intact via backup");
    TF_CHECK(vfs_unmount("/t",0)==0,"unmount rewrites primary");
    TF_CHECK(block_rw(device,BLOCK_OP_READ,0,work,4096,BLOCK_PRIO_NORMAL)==0 &&
             ((struct zj_super *)work)->magic==ZJ_MAGIC,"primary rewritten at unmount");
    /* Both superblocks destroyed -> mount refused (never formatted). */
    uint64_t last_lba=device->start_lba+(device->sectors/spb-1U)*spb;
    uint8_t *backup=a+12288;
    TF_CHECK(ramdisk_peek(device,last_lba,backup,spb)==0,"peek backup");
    memset(work,0xee,4096);
    TF_CHECK(ramdisk_poke(device,sb_lba,work,spb)==0 && ramdisk_poke(device,last_lba,work,spb)==0,
             "destroy both");
    TF_CHECK(vfs_mount(device,"/t",0)!=0,"mount refused without valid superblock");
    TF_CHECK(zjfs_probe(device)!=0,"probe rejects");
    TF_CHECK(ramdisk_poke(device,sb_lba,saved,spb)==0 && ramdisk_poke(device,last_lba,backup,spb)==0,
             "restore superblocks");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"remount restored");
    return 0;
}

/* --------------------------------------------------------- concurrency */

struct tf_worker {
    uint32_t index;
    int result;
    struct kcompletion done;
};

static void tf_worker_main(void *argument) {
    struct tf_worker *w=(struct tf_worker *)argument;
    uint8_t *a=(uint8_t *)page_alloc_contiguous(4);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(4);
    char dir[32], path[48], moved[48];
    w->result=(a && b) ? 0 : -1;
    ksnprintf(dir,sizeof(dir),"/t/w%u",w->index);
    if (w->result==0 && vfs_mkdir(&tf_root,dir,0755)!=0)
        w->result=-2;
    for (uint32_t i=0; i<24 && w->result==0; ++i) {
        ksnprintf(path,sizeof(path),"%s/f%u",dir,i);
        ksnprintf(moved,sizeof(moved),"%s/g%u",dir,i);
        uint64_t length=1000U+((w->index*7U+i*13U)%12000U);
        tf_fill(a,length,w->index*1000U+i);
        if (tf_write_file(path,a,length,(i%6U)==0)!=0)
            w->result=-3;
        else if (tf_verify_file(path,a,length,b)!=0)
            w->result=-4;
        else if (vfs_rename(&tf_root,path,moved)!=0)
            w->result=-5;
        else if ((i&1U) && vfs_unlink(&tf_root,moved)!=0)
            w->result=-6;
        else if (!(i&1U) && tf_verify_file(moved,a,length,b)!=0)
            w->result=-7;
        if ((i%8U)==7U)
            task_yield();
    }
    if (a) page_free_contiguous(a,4);
    if (b) page_free_contiguous(b,4);
    kcompletion_signal(&w->done);
    task_exit();
}

static int tf_concurrency(void) {
    static struct tf_worker workers[4];
    for (uint32_t i=0; i<4; ++i) {
        uint64_t id;
        workers[i].index=i;
        workers[i].result=-1;
        kcompletion_init(&workers[i].done);
        TF_CHECK(task_create(tf_worker_main,&workers[i],&id)==0,"worker create");
    }
    int ok=1;
    for (uint32_t i=0; i<4; ++i) {
        kcompletion_wait(&workers[i].done);
        if (workers[i].result!=0) {
            klog("ZEROOS: storage fs worker %u result %d.",i,workers[i].result);
            ok=0;
        }
    }
    TF_CHECK(ok,"4 concurrent workers (create/write/verify/rename/unlink)");
    char path[48];
    for (uint32_t w=0; w<4; ++w)
        for (uint32_t i=0; i<24; i+=2) {
            ksnprintf(path,sizeof(path),"/t/w%u/g%u",w,i);
            TF_CHECK(vfs_unlink(&tf_root,path)==0,"cleanup worker files");
        }
    for (uint32_t w=0; w<4; ++w) {
        ksnprintf(path,sizeof(path),"/t/w%u",w);
        TF_CHECK(vfs_rmdir(&tf_root,path)==0,"cleanup worker dirs");
    }
    return 0;
}

/* ------------------------------------------------------------- unmount */

static int tf_unmount_rules(struct block_device *device) {
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/t/busy",VFS_O_CREAT|VFS_O_RDWR,0644,&f)==0,"busy file");
    TF_CHECK(vfs_unmount("/t",0)==-SE_BUSY,"unmount with open file -> EBUSY");
    vfs_file_put(f);
    TF_CHECK(vfs_unlink(&tf_root,"/t/busy")==0,"unlink busy");
    TF_CHECK(vfs_unmount("/t",0)==0,"clean unmount");
    uint8_t *page=(uint8_t *)page_alloc();
    TF_CHECK(page && block_rw(device,BLOCK_OP_READ,0,page,4096,BLOCK_PRIO_NORMAL)==0,"read sb");
    int clean=((struct zj_super *)page)->state==ZJ_STATE_CLEAN;
    page_free(page);
    TF_CHECK(clean,"superblock CLEAN after unmount");
    TF_CHECK(tf_fsck_clean(device,"functional suite")==0,"fsck clean");
    return 0;
}

int storage_selftest_fs(void) {
    struct block_device *device=ramdisk_create("ramfs0",24U*1024U*1024U,BLOCK_MEDIA_SSD,8);
    uint8_t *a=(uint8_t *)page_alloc_contiguous(16);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(16);
    uint8_t *big=(uint8_t *)page_alloc_contiguous(1280);
    TF_CHECK(device && a && b && big,"setup");
    struct zj_format_options options;
    memset(&options,0,sizeof(options));
    options.label="selftest";
    TF_CHECK(zjfs_format(device,&options)==0,"format");
    TF_CHECK(zjfs_probe(device)==0,"probe");
    TF_CHECK(tf_fsck_clean(device,"format")==0,"fresh fsck");
    TF_CHECK(vfs_mount(device,"/t",0)==0,"mount");
    TF_RUN(tf_basic(a,b),"basic-io");
    TF_RUN(tf_large(big,b),"large-file-truncate");
    TF_RUN(tf_namespace(a,b),"namespace");
    TF_RUN(tf_open_unlink(a,b),"unlink-while-open");
    TF_RUN(tf_permissions(a),"permissions");
    TF_RUN(tf_enospc(device,a,b),"enospc");
    TF_RUN(tf_pressure(big,b),"memory-pressure");
    TF_RUN(tf_concurrency(),"concurrency");
    TF_RUN(tf_io_errors(device,a,b),"io-errors");
    TF_RUN(tf_corruption(device,a),"corruption");
    TF_CHECK(vfs_unlink(&tf_root,"/t/hello")==0 && vfs_unlink(&tf_root,"/t/hard")==0,
             "final cleanup");
    TF_RUN(tf_unmount_rules(device),"unmount-fsck");
    page_free_contiguous(a,16);
    page_free_contiguous(b,16);
    page_free_contiguous(big,1280);
    return 0;
}

/* ------------------------------------------------------- crash testing */

/*
 * Each round: fresh filesystem; a durable set of files is written and
 * fsynced; then an un-synced metadata/data workload runs while a power cut
 * is armed at the K-th device write with a given persistence policy. After
 * power-on, mount must replay/recover, every durable file must be intact,
 * and a full check must find zero inconsistencies.
 */
static int tf_crash_round(struct block_device *device, uint32_t round, uint32_t after,
                          uint32_t policy, uint8_t *a, uint8_t *b) {
    ramdisk_set_cache_tracking(device,0);
    TF_CHECK(zjfs_format(device,0)==0,"crash format");
    ramdisk_set_cache_tracking(device,1);
    TF_CHECK(vfs_mount(device,"/c",0)==0,"crash mount");
    char path[48], other[48];
    for (uint32_t i=0; i<4; ++i) {
        ksnprintf(path,sizeof(path),"/c/durable%u",i);
        tf_fill(a,5000U+i*3000U,round*100U+i);
        TF_CHECK(tf_write_file(path,a,5000U+i*3000U,1)==0,"durable write+fsync");
    }
    TF_CHECK(vfs_mkdir(&tf_root,"/c/dir",0755)==0,"durable dir");
    struct vfs_file *f;
    TF_CHECK(vfs_open(&tf_root,"/c/dir",VFS_O_RDONLY,0,&f)==0,"open dir");
    TF_CHECK(vfs_fsync(f,0)==0,"fsync dir");
    vfs_file_put(f);
    struct block_fault fault;
    memset(&fault,0,sizeof(fault));
    fault.mode=BLOCK_FAULT_POWER_CUT;
    fault.op_mask=(1U<<BLOCK_OP_WRITE)|(1U<<BLOCK_OP_FLUSH);
    fault.after=after;
    fault.power_policy=policy;
    fault.power_seed=round*7919U+after;
    block_fault_set(device,&fault);
    /* Un-synced workload; runs until the device dies. */
    int ops=0;
    for (uint32_t i=0; i<200; ++i) {
        ksnprintf(path,sizeof(path),"/c/dir/n%u",i);
        ksnprintf(other,sizeof(other),"/c/dir/m%u",i);
        tf_fill(a,2000U+(i%5U)*4096U,i);
        int rc=tf_write_file(path,a,2000U+(i%5U)*4096U,(i%10U)==9U);
        if (rc==0)
            rc=vfs_rename(&tf_root,path,other);
        if (rc==0 && (i%3U)==0)
            rc=vfs_unlink(&tf_root,other);
        if (rc==0 && (i%7U)==0)
            rc=vfs_mkdir(&tf_root,path,0700);
        if (rc==0 && (i%4U)==1) {
            if (vfs_open(&tf_root,"/c/dir",VFS_O_RDONLY,0,&f)==0) {
                rc=vfs_fsync(f,0);
                vfs_file_put(f);
            }
        }
        if (rc)
            break;
        ++ops;
    }
    block_fault_clear(device);
    (void)vfs_unmount("/c",1);
    ramdisk_power_on(device);
    ramdisk_set_cache_tracking(device,0);
    TF_CHECK(vfs_mount(device,"/c",0)==0,"mount after power cut");
    for (uint32_t i=0; i<4; ++i) {
        ksnprintf(path,sizeof(path),"/c/durable%u",i);
        tf_fill(a,5000U+i*3000U,round*100U+i);
        TF_CHECK(tf_verify_file(path,a,5000U+i*3000U,b)==0,"durable file intact after crash");
    }
    TF_CHECK(tf_exists("/c/dir"),"durable dir exists");
    TF_CHECK(vfs_unmount("/c",0)==0,"unmount after recovery");
    TF_CHECK(tf_fsck_clean(device,"crash recovery")==0,"fsck clean after crash");
    struct ramdisk_info info;
    ramdisk_info(device,&info);
    klog("ZEROOS: storage crash round %u: cut_after=%u policy=%u ops_before_cut=%d dropped=%llu torn=%llu -> recovered, fsck clean.",
         round,after,policy,ops,info.writes_dropped,info.writes_torn);
    return 0;
}

int storage_selftest_crash(void) {
    /* Reuses the functional-test ramdisk (ramdisk count and memory are
     * bounded; CI runs QEMU with its default 128 MiB). */
    struct block_device *device=block_find("ramfs0");
    uint8_t *a=(uint8_t *)page_alloc_contiguous(6);
    uint8_t *b=(uint8_t *)page_alloc_contiguous(6);
    TF_CHECK(device && a && b,"crash setup");
    static const uint32_t cuts[]={1,4,9,17,30,55,90,140};
    static const uint32_t policies[]={BLOCK_POWER_DROP_ALL,BLOCK_POWER_SUBSET,
                                      BLOCK_POWER_TORN,BLOCK_POWER_KEEP_ALL};
    for (uint32_t round=0; round<8; ++round)
        if (tf_crash_round(device,round,cuts[round],policies[round%4U],a,b)!=0)
            return -1;
    page_free_contiguous(a,6);
    page_free_contiguous(b,6);
    return 0;
}

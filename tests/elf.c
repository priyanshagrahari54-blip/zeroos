#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "elf.h"

static uint8_t image[4096];

static void make_base(void) {
    struct elf64_ehdr *eh=(struct elf64_ehdr *)image;
    struct elf64_phdr *ph=(struct elf64_phdr *)(image+sizeof(*eh));
    memset(image,0,sizeof(image));
    eh->ident[0]=0x7f; eh->ident[1]='E'; eh->ident[2]='L'; eh->ident[3]='F';
    eh->ident[4]=2; eh->ident[5]=1; eh->ident[6]=1;
    eh->type=2; eh->machine=62; eh->version=1;
    eh->entry=0x00007f0000001000ULL;
    eh->phoff=sizeof(*eh); eh->phentsize=sizeof(*ph); eh->phnum=1;
    eh->ehsize=sizeof(*eh);
    ph->type=1; ph->flags=ZEROOS_PF_R|ZEROOS_PF_X;
    ph->offset=0x1000; ph->vaddr=0x00007f0000001000ULL;
    ph->filesz=4; ph->memsz=0x1000; ph->align=0x1000;
    image[0x1000]=0xc3;
}

int main(void) {
    struct elf_image out;
    make_base();
    assert(elf64_validate_image(image,sizeof(image),&out)==0);
    assert(out.entry==0x00007f0000001000ULL && out.segment_count==1);

    ((struct elf64_ehdr *)image)->machine=3;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    make_base();
    ((struct elf64_phdr *)(image+sizeof(struct elf64_ehdr)))->filesz=0x2000;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    make_base();
    ((struct elf64_phdr *)(image+sizeof(struct elf64_ehdr)))->vaddr=0x1000;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    make_base();
    ((struct elf64_phdr *)(image+sizeof(struct elf64_ehdr)))->align=3;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    make_base();
    ((struct elf64_ehdr *)image)->entry=0x00007f0000002000ULL;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    make_base();
    ((struct elf64_phdr *)(image+sizeof(struct elf64_ehdr)))->offset=0x2000;
    ((struct elf64_phdr *)(image+sizeof(struct elf64_ehdr)))->filesz=0x2000;
    assert(elf64_validate_image(image,sizeof(image),&out)==-1);
    puts("PASS: ELF64 executable header/segment/range/entry validation");
}

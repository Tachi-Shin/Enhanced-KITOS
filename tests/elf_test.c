#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/elf.h>
#include <kernel/error.h>
static unsigned int checks, calls;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while (0)
static uint8_t image[0x3000];
static void u16(size_t offset, uint16_t n) { memcpy(image + offset, &n, 2); }
static void u32(size_t offset, uint32_t n) { memcpy(image + offset, &n, 4); }
static void u64(size_t offset, uint64_t n) { memcpy(image + offset, &n, 8); }
static void fixture(void)
{
    memset(image, 0, sizeof(image)); memcpy(image,"\177ELF\2\1\1",7);
    u16(16,2);u16(18,183);u32(20,1);u64(24,0x400080);u64(32,64);
    u16(52,64);u16(54,56);u16(56,2);
    u32(64,1);u32(68,5);u64(72,0x1080);u64(80,0x400080);
    u64(96,128);u64(104,128);u64(112,4096);
    u32(120,1);u32(124,6);u64(128,0x2000);u64(136,0x403000);
    u64(152,24);u64(160,8193);u64(168,4096);
    memset(image+0x1080,0xAA,128); memset(image+0x2000,0xBB,24);
}
static int read_image(void *ctx, uint64_t offset, void *data, uint32_t size)
{
    (void)ctx; ++calls;
    CHECK(offset <= sizeof(image) && size <= sizeof(image) - offset);
    memcpy(data,image+offset,size);return 0;
}
static int broken_read(void *ctx,uint64_t off,void *data,uint32_t len)
{ (void)ctx;(void)off;(void)data;(void)len;return -K_EIO; }
static struct {uint64_t va;unsigned int perms;uint8_t bytes[PAGE_SIZE];} pages[8];
static unsigned int count, fail_after;
int vm_map_new(struct vm_space *space,uint64_t va,unsigned int perms,const void *initial,size_t size)
{
    (void)space;
    if (count == fail_after) return -K_ENOMEM;
    CHECK(count < 8 && size == PAGE_SIZE);
    pages[count].va=va;pages[count].perms=perms;memcpy(pages[count++].bytes,initial,size);return 0;
}
int main(void)
{
    struct image_reader reader={NULL,sizeof(image),read_image}; struct elf_plan plan;
    fixture(); CHECK(elf_validate(&reader,183,&plan)==0);
    CHECK(plan.count==2 && plan.entry==0x400080 && plan.end==0x406000);
    struct vm_space space={1}; count=0;fail_after=8;
    CHECK(elf_load(&reader,&plan,&space)==0 && count==4);
    CHECK(pages[0].va==0x400000 && pages[0].perms==(VM_READ|VM_EXEC));
    for(unsigned int i=0;i<PAGE_SIZE;++i) CHECK(pages[0].bytes[i]==(i>=128&&i<256?0xAA:0));
    for(unsigned int i=0;i<PAGE_SIZE;++i) CHECK(pages[1].bytes[i]==(i<24?0xBB:0));
    for(unsigned int p=2;p<4;++p) for(unsigned int i=0;i<PAGE_SIZE;++i) CHECK(pages[p].bytes[i]==0);
    for(unsigned int n=0;n<4;++n) { count=0;fail_after=n;CHECK(elf_load(&reader,&plan,&space)==-K_ENOMEM && count==n); }
    for(unsigned int n=0;n<64;++n) { reader.size=n;CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC); }reader.size=sizeof(image);
    CHECK(elf_validate(&reader,62,&plan)==-K_ENOEXEC);
    unsigned int offsets[]={0,4,5,6,8,16,18,20,24,32,48,52,54,56};
    for(unsigned int i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {fixture();image[offsets[i]]^=0x80;CHECK(elf_validate(&reader,183,&plan)<0);}
    fixture();u32(68,7);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();u64(104,127);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();u64(72,~0ULL);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();u64(136,0x400000);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();u64(168,3);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();u64(160,PROCESS_IMAGE_BYTES_MAX+4096ULL);CHECK(elf_validate(&reader,183,&plan)==-K_ENOMEM);
    fixture();u32(120,3);CHECK(elf_validate(&reader,183,&plan)==-K_ENOTSUP);
    fixture();u32(120,2);CHECK(elf_validate(&reader,183,&plan)==-K_ENOTSUP);
    fixture();u32(120,7);CHECK(elf_validate(&reader,183,&plan)==-K_ENOTSUP);
    fixture();u32(120,0x6474e551);u32(124,7);CHECK(elf_validate(&reader,183,&plan)==-K_ENOEXEC);
    fixture();reader.read=broken_read;CHECK(elf_validate(&reader,183,&plan)==-K_EIO);reader.read=read_image;
    fixture();
    for(unsigned int offset=0;offset<176;++offset) {
        uint8_t saved=image[offset];
        for(unsigned int bit=0;bit<8;++bit) {image[offset]=saved^(1U<<bit);CHECK(elf_validate(&reader,183,&plan)<=0);}
        image[offset]=saved;
    }
    printf("ELF64 validation/load: %u checks PASS (%u bounded reads)\n",checks,calls);
    return 0;
}

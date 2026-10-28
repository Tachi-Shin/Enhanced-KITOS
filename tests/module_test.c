#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/module.h>
#include <kernel/error.h>
static unsigned int checks,pages,fail_after,relocs;
static uint8_t *image,*saved;
static size_t length;
static struct module_plan plan;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int read_image(void *ctx,uint64_t offset,void *out,uint32_t n)
{(void)ctx;CHECK(offset<=length&&n<=length-offset);memcpy(out,image+offset,n);return 0;}
static int page(void *ctx,uint64_t off,unsigned int perms,const uint8_t *data)
{
    (void)ctx;(void)data;CHECK(!(off&4095)&&off<MODULE_IMAGE_MAX);CHECK((perms&(VM_WRITE|VM_EXEC))!=(VM_WRITE|VM_EXEC));
    if(pages==fail_after)return -K_ENOMEM;
    ++pages;return 0;
}
static int relocate(void *ctx,uint64_t off,uint64_t value)
{(void)ctx;CHECK(!(off&7)&&off<MODULE_IMAGE_MAX&&value>=MODULE_VIRTUAL_BASE&&value<MODULE_VIRTUAL_BASE+MODULE_IMAGE_MAX);++relocs;return 0;}
static void put64(uint64_t off,uint64_t v){CHECK(off+8<=length);memcpy(image+off,&v,8);}
static void test_file(const char *path)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));length=(size_t)ftell(f);CHECK(length<MODULE_FILE_MAX);rewind(f);
    image=malloc(length);saved=malloc(length);CHECK(image&&saved);CHECK(fread(image,1,length,f)==length);fclose(f);memcpy(saved,image,length);
    struct image_reader reader={NULL,length,read_image};const struct module_memory_ops ops={page,relocate};
    CHECK(module_validate(&reader,&plan)==0);fail_after=~0U;pages=relocs=0;
    CHECK(module_image_load(&reader,&plan,MODULE_VIRTUAL_BASE,&ops,NULL)==0);CHECK(pages&&relocs==plan.rela_count);
    unsigned int total=pages;
    for(unsigned int n=0;n<total;++n){pages=0;fail_after=n;CHECK(module_image_load(&reader,&plan,MODULE_VIRTUAL_BASE,&ops,NULL)==-K_ENOMEM&&pages==n);}
    fail_after=~0U;
    uint64_t rela=plan.rela_offset;
    CHECK(plan.rela_count>0);
    put64(rela+8,257);CHECK(module_validate(&reader,&plan)==-K_ENOEXEC);memcpy(image,saved,length);
    CHECK(module_validate(&reader,&plan)==0);put64(rela,0);CHECK(module_validate(&reader,&plan)==-K_ENOEXEC);memcpy(image,saved,length);
    CHECK(module_validate(&reader,&plan)==0);put64(rela+16,~0ULL);CHECK(module_validate(&reader,&plan)==-K_ENOEXEC);memcpy(image,saved,length);
    /* Every header/program-header bit is untrusted. No mutation may escape
     * bounded reads or produce writable executable mappings. */
    for(unsigned int off=0;off<256&&off<length;++off)for(unsigned int bit=0;bit<8;++bit){
        memcpy(image,saved,length);image[off]^=1U<<bit;int rc=module_validate(&reader,&plan);CHECK(rc<=0);
        if(!rc){pages=relocs=0;CHECK(module_image_load(&reader,&plan,MODULE_VIRTUAL_BASE,&ops,NULL)==0);}
    }
    memcpy(image,saved,length);for(unsigned int n=0;n<64;++n){reader.size=n;CHECK(module_validate(&reader,&plan)==-K_ENOEXEC);}reader.size=length;
    free(saved);free(image);
}
int main(int argc,char **argv)
{CHECK(argc==13);for(int i=1;i<argc;++i)test_file(argv[i]);printf("Driver ELF validation/load/failure injection: %u checks PASS\n",checks);}

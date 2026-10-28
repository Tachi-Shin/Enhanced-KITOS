#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/pci/bcm2711.h>
#include <kernel/error.h>
static unsigned int checks,mode,fw_calls,writes;
static uint64_t now;
static uint32_t regs[0x10000/4];
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t clock_ms(void){return now;}
static void sleep_ms(uint32_t ms){now+=ms;}
static void log_msg(const char *m,uint64_t a,uint64_t b){(void)m;(void)a;(void)b;}
static uint32_t read32(uint64_t address)
{
    CHECK(address>=0xFD500000ULL&&address<0xFD510000ULL&&!(address&3));
    unsigned int r=(unsigned int)(address-0xFD500000ULL);
    if(r>=0x8000&&r<0x9000)CHECK(regs[0x9000/4]==0x100000);
    if(r==0x8010&&regs[r/4]==~0U)return mode==4?0xFFFE0004:0xFFFFC004;
    return regs[r/4];
}
static void write32(uint64_t address,uint32_t v)
{CHECK(address>=0xFD500000ULL&&address<0xFD510000ULL&&!(address&3));if(address==0xFD508004ULL)v=(v&65535)|(regs[0x8004/4]&0xffff0000);regs[(address-0xFD500000ULL)/4]=v;++writes;}
static int firmware(uint32_t bdf)
{CHECK(bdf==0x100000&&now>=102);++fw_calls;return mode==3?-K_EIO:0;}
static void fixture(unsigned int m)
{
    mode=m;now=0;fw_calls=0;writes=0;memset(regs,0,sizeof(regs));
    regs[0x4068/4]=m==1?0:0xb0;regs[12/4]=0x10000;
    regs[0x8000/4]=m==2?~0U:0x34831106;regs[0x8008/4]=0x0c033000;
    regs[0x8010/4]=4;regs[0x8014/4]=0;
    if(m==5){regs[0x8004/4]=1U<<20;regs[0x8034/4]=0x50;regs[0x8050/4]=0x5005;}
}
int main(void)
{
    struct ek_driver_api a={.now_ms=clock_ms,.sleep_ms=sleep_ms,.log=log_msg,.read32=read32,.write32=write32,.firmware_xhci_reset=firmware};
    uint64_t bar;
    fixture(0);CHECK(bcm2711_vl805_start(&a,&bar)==0);CHECK(bar==0x600000000ULL&&fw_calls==1);
    CHECK(regs[0x4034/4]==15&&regs[0x4038/4]==0);CHECK(regs[0x4070/4]==0x03f00000&&regs[0x4080/4]==6&&regs[0x4084/4]==6);
    CHECK(regs[0x8010/4]==0xf8000004&&regs[0x8004/4]==0x406);
    fixture(1);CHECK(bcm2711_vl805_start(&a,&bar)==-K_ETIMEDOUT);CHECK(now==1102&&fw_calls==0&&writes<100);
    fixture(2);CHECK(bcm2711_vl805_start(&a,&bar)==-K_ENODEV&&fw_calls==0);
    fixture(3);CHECK(bcm2711_vl805_start(&a,&bar)==-K_EIO&&fw_calls==1&&regs[0x8010/4]==4);
    fixture(4);CHECK(bcm2711_vl805_start(&a,&bar)==-K_ENOTSUP&&regs[0x8004/4]==0x400);
    fixture(5);CHECK(bcm2711_vl805_start(&a,&bar)==-K_EINVAL&&(regs[0x8004/4]&65535)==0x400);
    printf("BCM2711 PCIe sequence/error bounds: %u checks PASS\n",checks);
}

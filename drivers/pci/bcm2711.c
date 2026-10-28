/* BCM2711 PCIe RC register programming, with a 1 GiB identity DMA aperture.
 * Register definitions: Raspberry Pi bcm2711 DT and Linux pcie-brcmstb.
 * All accesses use the SDK so this sequence is also exercised by host tests. */
#include <drivers/pci/bcm2711.h>
#define RC 0xFD500000ULL
#define CPU_WINDOW 0x600000000ULL
#define PCI_WINDOW 0xF8000000U
static uint32_t rd(const struct ek_driver_api *a,unsigned int r){return a->read32(RC+r);}
static void wr(const struct ek_driver_api *a,unsigned int r,uint32_t v){a->write32(RC+r,v);}
static void change(const struct ek_driver_api *a,unsigned int r,uint32_t mask,uint32_t bits)
{wr(a,r,(rd(a,r)&~mask)|bits);}
static uint32_t cfg(const struct ek_driver_api *a,unsigned int r)
{wr(a,0x9000,0x100000);return rd(a,0x8000+r);}
static void putcfg(const struct ek_driver_api *a,unsigned int r,uint32_t v)
{wr(a,0x9000,0x100000);wr(a,0x8000+r,v);}
int bcm2711_vl805_start(const struct ek_driver_api *a,uint64_t *bar)
{
    if(!a||!bar)return -EK_EINVAL;
    change(a,0x9210,3,3);a->sleep_ms(1);
    change(a,0x9210,2,0);change(a,0x4204,1U<<27,0);a->sleep_ms(1);
    change(a,0x4008,0xF8300000U,(15U<<27)|(1U<<12)|(1U<<13));
    wr(a,0x4034,15);wr(a,0x4038,0); /* BAR2: DMA bus 0 -> CPU 0, 1 GiB */
    change(a,0x402c,31,0);change(a,0x403c,31,0);
    wr(a,0x4308,~0U);wr(a,0x4310,~0U);wr(a,0x4508,~0U);wr(a,0x4510,~0U);wr(a,0x4044,0);
    change(a,0xb8,15,2);change(a,0xdc,15,2); /* Gen2 */
    wr(a,0x400c,PCI_WINDOW);wr(a,0x4010,0);
    wr(a,0x4070,0x03F00000);wr(a,0x4080,6);wr(a,0x4084,6);
    change(a,0x043c,0xFFFFFF,0x060400);change(a,0x0188,12,0);
    change(a,0x4204,(1U<<21)|2,2);
    change(a,0xbc,3,0); /* ASPM disabled; driver currently polls */
    change(a,0x9210,1,0);a->sleep_ms(100);
    uint64_t deadline=a->now_ms()+1000;
    while((rd(a,0x4068)&0x30)!=0x30){if(a->now_ms()>=deadline)return -EK_ETIMEDOUT;a->sleep_ms(1);}
    if(!(rd(a,0x4068)&0x80) || ((rd(a,0x0c)>>16)&0x7f)!=1)return -EK_ENODEV;
    change(a,0x18,0xFFFFFF,0x010100);wr(a,0x20,0xFBF0F800);
    wr(a,0x24,0x0000FFF0);wr(a,0x04,0x406);
    if(cfg(a,0)!=0x34831106 || (cfg(a,8)>>8)!=0x0C0330 || ((cfg(a,12)>>16)&0x7f))return -EK_ENODEV;
    /* Pi 4 firmware reinstates the VL805 firmware after PERST#. No EEPROM writes. */
    int rc=a->firmware_xhci_reset(0x100000);if(rc)return rc;
    a->sleep_ms(1); /* VL805 startup after the firmware reply */
    putcfg(a,4,0x400);
    uint32_t oldlo=cfg(a,0x10),oldhi=cfg(a,0x14);
    if((oldlo&7)!=4)return -EK_ENOTSUP;
    putcfg(a,0x10,~0U);putcfg(a,0x14,~0U);
    uint64_t mask=((uint64_t)cfg(a,0x14)<<32)|(cfg(a,0x10)&~15U);
    putcfg(a,0x10,oldlo);putcfg(a,0x14,oldhi);
    uint64_t size=~mask+1;
    if(!size || (size&(size-1)) || size>0x10000 || size<0x1000)return -EK_ENOTSUP;
    putcfg(a,0x10,PCI_WINDOW|4);putcfg(a,0x14,0);
    /* Disable MSI and MSI-X rather than leaving firmware interrupt state live. */
    if(cfg(a,4)&(1U<<20)){
        unsigned int p=cfg(a,0x34)&0xfc;uint64_t visited=0;
        for(unsigned int n=0;p;++n){
            if(n>=48||p<0x40||p>0xfc||(visited&(1ULL<<(p/4))))return -EK_EINVAL;
            visited|=1ULL<<(p/4);uint32_t cap=cfg(a,p);
            if((cap&255)==5)putcfg(a,p,cap&~(1U<<16));
            if((cap&255)==17)putcfg(a,p,(cap&~(1U<<31))|(1U<<30));
            p=(cap>>8)&0xfc;
        }
    }
    putcfg(a,4,0x406);*bar=CPU_WINDOW;
    a->log("VL805 PCIe link ready",rd(a,0x4068),size);return 0;
}

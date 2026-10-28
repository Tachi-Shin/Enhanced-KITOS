#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/input.h>
#include <kernel/error.h>
static unsigned int checks, count;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"USB line=%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static struct input_event events[32];
static void emit(const struct input_event *e, void *context)
{ CHECK(context == events && count < 32); events[count++] = *e; }
static void descriptor_test(void)
{
    uint8_t config[] = {9,2,34,0,1,7,0,0x80,50, 9,4,2,0,1,3,1,1,0,
        9,0x21,0x11,1,0,1,0x22,63,0, 7,5,0x81,3,8,0,10};
    struct usb_device dev={.speed=USB_FULL}; uint8_t selected;
    CHECK(usb_parse_config(config,sizeof(config),&dev,&selected)==0 && selected==7);
    CHECK(dev.interface_count==1 && dev.interfaces[0].number==2 && dev.interfaces[0].protocol==1);
    CHECK(dev.interfaces[0].endpoint_count==1 && dev.interfaces[0].endpoints[0].max_packet==8);
    for (unsigned int n=0;n<sizeof(config);++n) CHECK(usb_parse_config(config,n,&dev,&selected)<0);
    CHECK(usb_parse_config(NULL,sizeof(config),&dev,&selected)<0);
    CHECK(usb_parse_config(config,sizeof(config),NULL,&selected)<0);
    unsigned int offsets[]={0,1,2,4,5,7,9,13,27,29,30,31};
    for (unsigned int i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        unsigned int p=offsets[i]; uint8_t saved=config[p]; config[p]=0;
        CHECK(usb_parse_config(config,sizeof(config),&dev,&selected)<0); config[p]=saved;
    }
    /* Endpoint aliases across interfaces must not bind two drivers to one pipe. */
    uint8_t composite[50]; memcpy(composite,config,34); memcpy(composite+34,config+9,9); memcpy(composite+43,config+27,7);
    composite[2]=50;composite[4]=2;composite[36]=3;
    CHECK(usb_parse_config(composite,50,&dev,&selected)<0);
    composite[45]=0x82;
    CHECK(usb_parse_config(composite,50,&dev,&selected)==0 && dev.interface_count==2);
    /* Every one-bit mutation remains bounded under ASan/UBSan. */
    for (unsigned int i=0;i<sizeof(config);++i) for(unsigned int bit=0;bit<8;++bit) {
        config[i]^=1U<<bit; CHECK(usb_parse_config(config,sizeof(config),&dev,&selected)<=0); config[i]^=1U<<bit;
    }
}
static void keyboard_test(void)
{
    struct usb_interface keyboard={.subclass=1,.protocol=1}; uint8_t report[8]={0,0,4,4};
    count=0; CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && count==1);
    CHECK(events[0].code==4 && events[0].value==1 && events[0].device==2);
    CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && count==1); /* no duplicate/held key repeat */
    report[0]=2;report[2]=5;report[3]=0;
    CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && count==4);
    CHECK(events[1].code==0xE1 && events[1].value==1);
    CHECK(events[2].code==4 && !events[2].value && events[3].code==5 && events[3].modifiers==2);
    CHECK(usb_key_ascii(events[3].code,events[3].modifiers,false)=='B');
    uint8_t saved[8];memcpy(saved,keyboard.keys,8);
    report[2]=1; CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==-K_EAGAIN && !memcmp(saved,keyboard.keys,8));
    report[2]=0x39;report[0]=0;
    CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && keyboard.caps_lock);
    unsigned int before=count;
    CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && count==before && keyboard.caps_lock);
    memset(report,0,8);
    CHECK(usb_hid_decode(&keyboard,2,report,8,emit,events)==0 && events[count-1].code==0x39 && !events[count-1].value);
    for (unsigned int n=0;n<8;++n) CHECK(usb_hid_decode(&keyboard,2,report,n,emit,events)==-K_EINVAL);
    CHECK(usb_key_ascii(4,0,false)=='a' && usb_key_ascii(4,2,true)=='a' && usb_key_ascii(4,0,true)=='A');
    CHECK(usb_key_ascii(4,1,false)==1 && usb_key_ascii(40,0,false)=='\r' && usb_key_ascii(56,2,false)=='?');
    CHECK(usb_key_ascii(57,0,false)==0 && usb_key_ascii(0xFFFF,0,false)==0);
}
static void mouse_test(void)
{
    struct usb_interface mouse={.subclass=1,.protocol=2}; uint8_t report[4]={1,0x80,0x7F,0xFF}; count=0;
    CHECK(usb_hid_decode(&mouse,3,report,4,emit,events)==0 && count==1);
    CHECK(events[0].type==2 && events[0].code==1 && events[0].x==-128 && events[0].y==127 && events[0].wheel==-1);
    memset(report+1,0,3);
    CHECK(usb_hid_decode(&mouse,3,report,3,emit,events)==0 && count==1);
    report[0]=0;
    CHECK(usb_hid_decode(&mouse,3,report,3,emit,events)==0 && count==2 && !events[1].code);
    CHECK(usb_hid_decode(&mouse,3,report,2,emit,events)==-K_EINVAL);
    mouse.protocol=0; CHECK(usb_hid_decode(&mouse,3,report,4,emit,events)==-K_ENOTSUP);
}
int main(void)
{
    descriptor_test(); keyboard_test(); mouse_test();
    printf("USB descriptors/boot-HID: %u checks PASS\n",checks);
    return 0;
}

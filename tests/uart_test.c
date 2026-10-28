#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/uart/uart.h>
#include <arch/irq.h>
static unsigned int checks,drains;static char fifo[128];static size_t head,tail;static bool hardware_error;
extern uint64_t test_irq_mask;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"uart:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int init(struct uart_device *d){(void)d;head=tail=0;return 0;}
static void enable(struct uart_device *d){(void)d;CHECK(test_irq_mask==128);}
static void drain(struct uart_device *d)
{
    CHECK(test_irq_mask==128);++drains;
    if(hardware_error){uart_rx_overrun(d);hardware_error=false;}
    for(unsigned int budget=0;budget<64&&tail<head;++budget)uart_rx_push(d,fifo[tail++]);
}
static void incoming(const char *s){size_t n=strlen(s);CHECK(n<=sizeof(fifo)-head);memcpy(fifo+head,s,n);head+=n;}
int main(void)
{
    const struct uart_ops ops={.init=init,.enable_rx_irq=enable,.handle_irq=drain};
    struct uart_device d={.ops=&ops,.irq_intid=153};char c=0;
    CHECK(!uart_init(&d));CHECK(uart_try_getc(&d,&c)==-1);CHECK(!uart_enable_rx_irq(&d));CHECK(test_irq_mask==0);
    /* No IRQ at all: a single byte must still reach the software queue. */
    incoming("a");CHECK(uart_try_getc(&d,&c)==1&&c=='a');CHECK(d.rx_stats.interrupts==0&&d.rx_stats.bytes==1);
    CHECK(uart_try_getc(&d,&c)==0);CHECK(test_irq_mask==0);
    incoming("bc");CHECK(uart_try_getc(&d,&c)==1&&c=='b');unsigned int n=drains;
    CHECK(uart_try_getc(&d,&c)==1&&c=='c');CHECK(drains==n);CHECK(uart_try_getc(&d,&c)==0);
    /* An IRQ and polling share one FIFO drain; no duplicates or lost bytes. */
    CHECK(!uart_register(&d));incoming("de");uint64_t saved=arch_irq_save();CHECK(uart_handle_irq(153));arch_irq_restore(saved);
    CHECK(uart_try_getc(&d,&c)==1&&c=='d');CHECK(uart_try_getc(&d,&c)==1&&c=='e');CHECK(uart_try_getc(&d,&c)==0);
    CHECK(d.rx_stats.interrupts==1&&d.rx_stats.bytes==5);
    incoming("bad");hardware_error=true;CHECK(uart_try_getc(&d,&c)==UART_RX_LOST);CHECK(uart_try_getc(&d,&c)==0);
    incoming("ok");CHECK(uart_try_getc(&d,&c)==1&&c=='o');CHECK(uart_try_getc(&d,&c)==1&&c=='k');
    CHECK(!d.rx_lost&&d.rx_stats.overruns==1);CHECK(uart_try_getc(&d,NULL)==-1);
    CHECK(uart_try_getc(NULL,&c)==-1);CHECK(test_irq_mask==0);
    printf("UART FIFO fallback/IRQ interleaving/error recovery: %u checks PASS\n",checks);return 0;
}

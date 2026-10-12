/* 8250/16550 UART on COM1, used as a debug log channel (output only). */
#include <serial.h>
#include <io.h>

#define COM1            0x3F8
#define UART_DATA       0       /* THR/RBR, or divisor low when DLAB=1 */
#define UART_IER        1       /* interrupt enable, divisor high when DLAB=1 */
#define UART_FCR        2
#define UART_LCR        3
#define UART_MCR        4
#define UART_LSR        5
#define LCR_DLAB        0x80
#define LCR_8N1         0x03
#define LSR_THR_EMPTY   0x20
#define MCR_LOOPBACK    0x10
#define MCR_DTR_RTS_OUT2 0x0B
#define UART_CLOCK      115200
#define BAUD            38400
#define TX_TIMEOUT      100000

static bool present;

bool serial_init(void)
{
    u16 div = UART_CLOCK / BAUD;
    outb(COM1 + UART_IER, 0x00);            /* no UART interrupts */
    outb(COM1 + UART_LCR, LCR_DLAB);
    outb(COM1 + UART_DATA, (u8)div);
    outb(COM1 + UART_IER, (u8)(div >> 8));
    outb(COM1 + UART_LCR, LCR_8N1);
    outb(COM1 + UART_FCR, 0xC7);            /* enable+clear FIFOs (ignored on 8250) */

    /* loopback self-test: a byte written must come back */
    outb(COM1 + UART_MCR, MCR_LOOPBACK | 0x0E);
    outb(COM1 + UART_DATA, 0xA5);
    present = (inb(COM1 + UART_DATA) == 0xA5);
    outb(COM1 + UART_MCR, MCR_DTR_RTS_OUT2);
    return present;
}

bool serial_present(void) { return present; }

void serial_putc(char c)
{
    if (!present)
        return;
    if (c == '\n')
        serial_putc('\r');
    for (u32 i = 0; i < TX_TIMEOUT; i++)
        if (inb(COM1 + UART_LSR) & LSR_THR_EMPTY)
            break;
    outb(COM1 + UART_DATA, (u8)c);
}

void serial_write(const char *s)
{
    while (*s)
        serial_putc(*s++);
}

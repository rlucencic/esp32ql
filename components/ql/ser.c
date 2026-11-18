/*
 * The serial ports, SER1 and SER2 (docs/ql-hardware.md 3.7, 3.8, 4.3, 4.10).
 *
 * Sending is the ZX8302's: in serial mode ($18002 bits 4-3: 00 SER1, 01
 * SER2) a byte written to $18022 goes out at $18002's baud rate as a start
 * bit, 8 bits and 2 stop bits; $18020 bit 1 (TXFL) stays set meanwhile, and
 * when the byte is out the transmit interrupt ($18021 bit 2) asks QDOS for
 * the next one (JS $2D70: one byte a call). $18020 bits 4 and 5 are the
 * ports' handshake lines (SER1's DTR, SER2's CTS): 0 lets QDOS send; they go
 * to 1 while the host's queue is nearly full.
 *
 * Receiving is the IPC's: commands 2/3 open a port, 4/5 close it, 6/7
 * (RDS1, RDS2) hand over what it holds: a byte (bits 5-0 how many, up to
 * 23; bits 6-7 framing errors) and the bytes; the status (command 1) bits
 * 4/5 say a port has some. Here the bytes the host gives wait in a queue
 * per port, 23 at a time as QDOS asks (the IPC's buffer).
 *
 * The host takes what the QL sent and gives what it should receive from
 * other tasks (single producer, single consumer queues). A port nobody
 * listens on (ql_ser_connect()) loses what the QL sends, as one with
 * nothing plugged in.
 */
#include <string.h>
#include "ql_internal.h"

#define RING            4096            /* a power of two */
#define NEARLY_FULL     (RING - 256)
#define IPC_BUFFER      23              /* the IPC's bytes a port, per RDS */

typedef struct {
    uint8_t data[RING];
    volatile uint32_t head, tail;
} ring_t;

static ring_t *s_out, *s_in;            /* QL_SER_PORTS each (QL_SLOW_ALLOC) */
static volatile bool s_connected[QL_SER_PORTS];
static bool s_open[QL_SER_PORTS];       /* the IPC receives on it */

/* The transmitter */
static uint64_t s_tx_until;             /* the byte being sent goes out until then */
static bool s_tx_irq;                   /* its transmit interrupt still to come */

static const uint32_t s_baud[8] = { 19200, 9600, 4800, 2400, 1200, 600, 300, 75 };

static uint32_t ring_count(const ring_t *r)
{
    return r->head - r->tail;
}

bool ql_ser_init(void)
{
    if (s_out == NULL) {
        ring_t *r = QL_SLOW_ALLOC(4 * sizeof(ring_t));
        if (r == NULL) {
            return false;
        }
        s_in = r + QL_SER_PORTS;
        s_out = r;
    }
    return true;
}

void ql_ser_reset(void)
{
    s_tx_until = 0;
    s_tx_irq = false;
    for (int p = 0; p < QL_SER_PORTS; p++) {
        s_open[p] = false;
    }
}

/* ---- The ZX8302's transmitter ---- */

/* $18022 written in serial mode (tmode: $18002) */
void ql_ser_send(uint8_t tmode, uint8_t v, uint64_t clock)
{
    int port = (tmode >> 3) & 1;
    if (s_out == NULL) {
        return;
    }
    ring_t *r = &s_out[port];
    if (s_connected[port] && ring_count(r) < RING) {
        r->data[r->head % RING] = v;
        r->head++;
    }
    uint64_t start = clock > s_tx_until ? clock : s_tx_until;
    s_tx_until = start + (uint64_t)11 * QL_CPU_HZ / s_baud[tmode & 7];
    s_tx_irq = true;
}

/* $18020 bit 1: a byte still going out */
bool ql_ser_busy(uint64_t clock)
{
    return clock < s_tx_until;
}

/* $18020 bits 4 and 5: the ports' handshake inputs (1: hold on) */
uint8_t ql_ser_handshake(void)
{
    uint8_t v = 0;
    for (int p = 0; p < QL_SER_PORTS && s_out; p++) {
        if (s_connected[p] && ring_count(&s_out[p]) >= NEARLY_FULL) {
            v |= 0x10 << p;
        }
    }
    return v;
}

/* At each line's end: true when a byte has gone out (the transmit interrupt) */
bool ql_ser_line(uint64_t clock)
{
    if (s_tx_irq && clock >= s_tx_until) {
        s_tx_irq = false;
        return true;
    }
    return false;
}

/* ---- The IPC's receiver ---- */

void ql_ser_open(int port, bool open)
{
    if (port >= 0 && port < QL_SER_PORTS) {
        s_open[port] = open;
    }
}

/* Bytes the IPC holds for the port (status bits 4/5) */
int ql_ser_waiting(int port)
{
    if (!s_open[port] || s_in == NULL) {
        return 0;
    }
    uint32_t n = ring_count(&s_in[port]);
    return n > IPC_BUFFER ? IPC_BUFFER : (int)n;
}

/* RDS: up to 23 of them, oldest first */
int ql_ser_receive(int port, uint8_t *buf)
{
    int n = ql_ser_waiting(port);
    ring_t *r = &s_in[port];
    for (int i = 0; i < n; i++) {
        buf[i] = r->data[r->tail % RING];
        r->tail++;
    }
    return n;
}

/* ---- The host's side ---- */

int ql_ser_take(int port, uint8_t *buf, int max)
{
    if (port < 0 || port >= QL_SER_PORTS || s_out == NULL) {
        return 0;
    }
    ring_t *r = &s_out[port];
    int n = 0;
    while (n < max && r->tail != r->head) {
        buf[n++] = r->data[r->tail % RING];
        r->tail++;
    }
    return n;
}

int ql_ser_give(int port, const uint8_t *buf, int len)
{
    if (port < 0 || port >= QL_SER_PORTS || s_in == NULL) {
        return 0;
    }
    ring_t *r = &s_in[port];
    int n = 0;
    while (n < len && ring_count(r) < RING) {
        r->data[r->head % RING] = buf[n++];
        r->head++;
    }
    return n;
}

void ql_ser_connect(int port, bool connected)
{
    if (port >= 0 && port < QL_SER_PORTS) {
        s_connected[port] = connected;
        if (!connected && s_out) {
            s_out[port].tail = s_out[port].head;    /* what nobody took is lost */
        }
    }
}

bool ql_ser_is_open(int port)
{
    return port >= 0 && port < QL_SER_PORTS && s_open[port];
}

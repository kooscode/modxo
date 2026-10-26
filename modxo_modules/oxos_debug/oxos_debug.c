// OXOS debug additions for Modxo (BSD-2-Clause, as Modxo). Turns the modchip into a boot-debugging rig for an
// Xbox: the host side is OXOS's tools/modxo/console.py; the protocol and wiring are in README.md in this folder.
//
// One USB CDC stream carries, interleaved:
//  - the emulated 16550 UART's output (COM1, stock Modxo), as plain bytes;
//  - port 0x80 (POST code) writes as the two bytes 0xFE <code> (one LPC I/O write per code on the Xbox side; queued
//    here and sent from the timer);
//  - status lines, 0xFC <text> 0x0A, from a 20 ms timer:
//      "3v3=.. lreset=.. d0=.. ior=n/addr iow=n/addr memw=n/addr memr=n/addr tr=.. rst=on/off pwr=n last-iow ..."
//        when anything changed, at most every 0.5 s, and every 5 s regardless: pin levels, the count and last
//        address of each LPC cycle type, resets and power-ups seen, and the last 8 I/O writes (port=data);
//      "trace NN: addr=data x4"  the first 48 memory reads after each power-up (the boot ROM / X-codes fetching);
//      "odd N: Raddr=data x4"    memory cycles outside the flash window 0xFF000000-0xFFFFFFFF;
//      "flash 10040000: ..."     every 5 s, the first bytes of flash bank 1 (where the BIOS image is flashed);
//    status lines are sent only while the Xbox is quiet (no I/O writes in the last tick).
//  0xFE and 0xFC are reserved: text with those bytes (binary output on COM1, e.g. a kernel debugger's packets) is
//  ambiguous for the host.
//
// From the host: 0xFD 'P' presses the power button: OXOS_POWER_PIN high for 2 s (it drives an NPN transistor across
// the Xbox's power switch), low otherwise. Other bytes are left for the emulated UART.
//
// The stock UART and Super I/O answer only while the host holds DTR (tud_cdc_connected()); so does this module.
#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include <modxo.h>
#include <modxo/lpc_interface.h>
#include <oxos_debug.h>
#include <modxo_pinout.h>

#ifndef OXOS_POWER_PIN
#define OXOS_POWER_PIN 28 // the Tiny's SERIRQ pin; the SERIRQ module is not registered (src/main.c)
#endif
#define OXOS_POWER_PRESS_MS 2000

extern volatile uint32_t lpc_cycle_count[4];
extern volatile uint32_t lpc_last_address[4];
extern volatile uint32_t lpc_trace_addr[48];
extern volatile uint8_t lpc_trace_data[48];
extern volatile uint32_t lpc_trace_n;
static uint32_t oxos_trace_sent;
extern volatile uint32_t lpc_odd_addr[64];
extern volatile uint8_t lpc_odd_data[64];
extern volatile uint8_t lpc_odd_write[64];
extern volatile uint32_t lpc_odd_n;
extern volatile uint32_t lpc_iow_ring[16];
extern volatile uint32_t lpc_iow_n;
static uint32_t oxos_odd_sent, oxos_last_iow;
static volatile uint32_t oxos_resets_on, oxos_resets_off, oxos_powerups;

static repeating_timer_t oxos_timer;
static uint32_t oxos_ticks;
static uint32_t oxos_last_sig;
static char oxos_out[400];   // status line still to send (the CDC buffer takes 64 bytes at a time)
static int oxos_out_len, oxos_out_pos;
static absolute_time_t oxos_release;
static bool oxos_pressing;

// POST codes are queued here and sent from the timer (the CDC buffer may be full when the code arrives).
static volatile uint8_t oxos_post_q[1024];
static volatile uint32_t oxos_post_head, oxos_post_tail;

static void post_write(uint16_t address, uint8_t *data)
{
    if (oxos_post_head - oxos_post_tail < sizeof oxos_post_q)
        oxos_post_q[oxos_post_head++ % sizeof oxos_post_q] = *data;
}

static void post_read(uint16_t address, uint8_t *data)
{
    *data = 0xFF;
}

static bool oxos_tick(repeating_timer_t *t)
{
    if (oxos_pressing && absolute_time_diff_us(get_absolute_time(), oxos_release) <= 0) {
        gpio_put(OXOS_POWER_PIN, 0);
        oxos_pressing = false;
    }
    // First memory reads after each LPC reset: one 0xFC line per 4 reads once a line is free.
    // Drain queued POST codes first (two bytes each), between status lines only.
    if (oxos_out_pos == oxos_out_len && tud_cdc_connected()) {
        bool sent = false;
        while (oxos_post_tail != oxos_post_head && tud_cdc_write_available() >= 2) {
            uint8_t rec[2] = {0xFE, oxos_post_q[oxos_post_tail % sizeof oxos_post_q]};
            tud_cdc_write(rec, 2);
            oxos_post_tail++;
            sent = true;
        }
        if (sent)
            tud_cdc_write_flush();
    }
    uint32_t iow = lpc_cycle_count[1];
    bool quiet = iow == oxos_last_iow;
    oxos_last_iow = iow;
    // Memory cycles outside the flash window, 4 per line (they show the kernel touching physical memory nobody owns).
    if (quiet && oxos_out_pos == oxos_out_len && tud_cdc_connected() && oxos_odd_sent < lpc_odd_n) {
        if (lpc_odd_n - oxos_odd_sent > 64)
            oxos_odd_sent = lpc_odd_n - 64;
        int n = snprintf(oxos_out, sizeof oxos_out, "\xfc" "odd %lu:", oxos_odd_sent);
        for (int k = 0; k < 4 && oxos_odd_sent < lpc_odd_n; k++, oxos_odd_sent++) {
            uint32_t i = oxos_odd_sent % 64;
            n += snprintf(oxos_out + n, sizeof oxos_out - n, " %c%08lx=%02x", lpc_odd_write[i] ? 'W' : 'R',
                          lpc_odd_addr[i], lpc_odd_data[i]);
        }
        oxos_out[n++] = '\n';
        oxos_out_len = n;
        oxos_out_pos = 0;
    }
    if (quiet && oxos_out_pos == oxos_out_len && tud_cdc_connected() && oxos_trace_sent + 4 <= lpc_trace_n) {
        int n = snprintf(oxos_out, sizeof oxos_out, "\xfc" "trace %2lu:", oxos_trace_sent);
        for (int k = 0; k < 4; k++, oxos_trace_sent++)
            n += snprintf(oxos_out + n, sizeof oxos_out - n, " %08lx=%02x", lpc_trace_addr[oxos_trace_sent],
                          lpc_trace_data[oxos_trace_sent]);
        oxos_out[n++] = '\n';
        oxos_out_len = n;
        oxos_out_pos = 0;
    }
    // Every 5 s: the first bytes of flash bank 1 (where the BIOS image lives) and of offset 0x70 in it.
    if (quiet && oxos_ticks % 250 == 125 && oxos_out_pos == oxos_out_len && tud_cdc_connected()) {
        const volatile uint8_t *f = (const volatile uint8_t *)0x10040000;
        int n = snprintf(oxos_out, sizeof oxos_out, "\xfc" "flash 10040000:");
        for (int k = 0; k < 16; k++)
            n += snprintf(oxos_out + n, sizeof oxos_out - n, " %02x", f[k]);
        n += snprintf(oxos_out + n, sizeof oxos_out - n, " +70:");
        for (int k = 0x70; k < 0x78; k++)
            n += snprintf(oxos_out + n, sizeof oxos_out - n, " %02x", f[k]);
        oxos_out[n++] = '\n';
        oxos_out_len = n;
        oxos_out_pos = 0;
    }
    if (++oxos_ticks % 25 == 0 && tud_cdc_connected()) {
        uint32_t sig = gpio_get(LPC_ON) | gpio_get(LPC_RESET) << 1 | gpio_get(GPIO_D0) << 2;
        for (int i = 0; i < 4; i++)
            sig = sig * 31 + lpc_cycle_count[i];
        if (quiet && (sig != oxos_last_sig || oxos_ticks % 250 == 0) && oxos_out_pos == oxos_out_len) {
            int n = snprintf(oxos_out, sizeof oxos_out,
                             "\xfc" "3v3=%d lreset=%d d0=%d ior=%lu/%04lx iow=%lu/%04lx memw=%lu/%08lx memr=%lu/%08lx tr=%lu/%lu rst=%lu/%lu pwr=%lu last-iow",
                             gpio_get(LPC_ON), gpio_get(LPC_RESET), gpio_get(GPIO_D0),
                             lpc_cycle_count[0], lpc_last_address[0], lpc_cycle_count[1], lpc_last_address[1],
                             lpc_cycle_count[2], lpc_last_address[2], lpc_cycle_count[3], lpc_last_address[3], lpc_trace_n, oxos_trace_sent, oxos_resets_on, oxos_resets_off, oxos_powerups);
            for (uint32_t k = lpc_iow_n > 8 ? lpc_iow_n - 8 : 0; k < lpc_iow_n && n < (int)sizeof oxos_out - 12; k++)
                n += snprintf(oxos_out + n, sizeof oxos_out - n, " %04lx=%02lx", lpc_iow_ring[k % 16] >> 8,
                              lpc_iow_ring[k % 16] & 0xFF);
            oxos_out[n++] = '\n';
            oxos_out_len = n < (int)sizeof oxos_out ? n : (int)sizeof oxos_out - 1;
            oxos_out_pos = 0;
            oxos_last_sig = sig;
        }
    }
    if (oxos_out_pos < oxos_out_len && tud_cdc_connected()) {
        oxos_out_pos += tud_cdc_write(oxos_out + oxos_out_pos, oxos_out_len - oxos_out_pos);
        tud_cdc_write_flush();
    }
    if (tud_cdc_connected() && tud_cdc_available() >= 2) {
        uint8_t b;
        if (tud_cdc_peek(&b) && b == 0xFD) {
            uint8_t cmd[2];
            tud_cdc_read(cmd, 2);
            if (cmd[1] == 'P' && !oxos_pressing) {
                gpio_put(OXOS_POWER_PIN, 1);
                oxos_release = make_timeout_time_ms(OXOS_POWER_PRESS_MS);
                oxos_pressing = true;
            }
        }
    }
    return true;
}

static void oxos_debug_init(void)
{
    gpio_init(OXOS_POWER_PIN);
    gpio_put(OXOS_POWER_PIN, 0);
    gpio_set_dir(OXOS_POWER_PIN, GPIO_OUT);
    gpio_pull_down(OXOS_POWER_PIN);
    lpc_interface_add_io_handler(0x0080, 0xFFFF, post_read, post_write);
    add_repeating_timer_ms(20, oxos_tick, NULL, &oxos_timer);
}

// Power-up (LPC 3.3V rising): start a new trace of the first memory reads.
static void oxos_powerup(void)
{
    oxos_powerups++;
    lpc_odd_n = 0;
    oxos_odd_sent = 0;
    lpc_trace_n = 0;
    oxos_trace_sent = 0;
}

static void oxos_reset_on(void) { oxos_resets_on++; }
static void oxos_reset_off(void) { oxos_resets_off++; }

MODXO_TASK oxos_debug_hdlr = {
    .init = oxos_debug_init,
    .powerup = oxos_powerup,
    .lpc_reset_on = oxos_reset_on,
    .lpc_reset_off = oxos_reset_off,
};

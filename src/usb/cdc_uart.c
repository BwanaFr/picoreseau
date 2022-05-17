/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <pico/stdlib.h>

#include "tusb.h"
#include <pico/stdio/driver.h>



#define STDIO_USB_STDOUT_TIMEOUT_US 500000

static void stdio_usb_out_chars(uint8_t itf, const char *buf, int length) {
    static uint64_t last_avail_time;
    if (tud_cdc_n_connected(itf)) {
        for (int i = 0; i < length;) {
            int n = length - i;
            int avail = tud_cdc_n_write_available(itf);
            if (n > avail) n = avail;
            if (n) {
                int n2 = tud_cdc_n_write(itf, buf + i, n);
                tud_task();
                tud_cdc_n_write_flush(itf);
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task();
                tud_cdc_n_write_flush(itf);
                if (!tud_cdc_n_connected(itf) ||
                    (!tud_cdc_n_write_available(itf) && time_us_64() > last_avail_time + STDIO_USB_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    } else {
        // reset our timeout
        last_avail_time = 0;
    }
}
static int stdio_usb_in_chars(uint8_t itf, char *buf, int length) {
    int rc = PICO_ERROR_NO_DATA;
    if (tud_cdc_n_connected(itf) && tud_cdc_n_available(itf)) {
        int count = tud_cdc_n_read(itf, buf, length);
        rc =  count ? count : PICO_ERROR_NO_DATA;
    }
    return rc;
}

static void stdio_usb_cdc0_out_chars(const char *buf, int len) {
  stdio_usb_out_chars(0, buf, len);
}
static int stdio_usb_cdc0_in_chars(char *buf, int len) {
  return stdio_usb_in_chars(0, buf, len);
}

static stdio_driver_t stdio_usb_cdc_driver =
{
.out_chars = stdio_usb_cdc0_out_chars,
.in_chars = stdio_usb_cdc0_in_chars,
.crlf_enabled = true
};

void cdc_uart_init(void) {
    stdio_set_driver_enabled(&stdio_usb_cdc_driver, true);
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    //picoprobe_info("New baud rate %d\n", line_coding->bit_rate);
    //uart_init(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
}

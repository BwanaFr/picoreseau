#include "clock_detect.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hdlc_rx.h"

#define CLK_IN_PIN 1
#define CLK_TEST_CYCLES 10
#define CLK_FREQ 500

static repeating_timer_t timer;
static uint slice_num = 0;
static volatile bool clock_detected = false;

/**
 * Clock presence timer callback
 */
bool clk_presence_timer_callback(repeating_timer_t *rt) {
    uint16_t counts = pwm_get_counter(slice_num);
    clock_detected = ((counts > (CLK_TEST_CYCLES-3)) && (counts < (CLK_TEST_CYCLES+3)));
    pwm_set_counter(slice_num, 0);
    gpio_put(PICO_DEFAULT_LED_PIN, clock_detected);
    //Clock is detected if we count about 10 edges
    return true;
}

void initialize_clock_detect() {
    slice_num = pwm_gpio_to_slice_num(CLK_IN_PIN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv(&cfg, 1);
    pwm_init(slice_num, &cfg, false);
    gpio_set_function(CLK_IN_PIN, GPIO_FUNC_PWM);

    //We expect to have a clock frequency of 500KHz
    //Counting 10 edges is enough to know the frequency
    add_repeating_timer_us((-1000 / (CLK_FREQ/CLK_TEST_CYCLES)), clk_presence_timer_callback, NULL, &timer);
    pwm_set_enabled(slice_num, true);
}

bool is_clock_detected(bool fast) {
    if(fast){
        cancel_repeating_timer(&timer);
        pwm_set_counter(slice_num, 0);
        sleep_us(1000/(CLK_FREQ/2));
        bool ret =  pwm_get_counter(slice_num) != 0;
        add_repeating_timer_us((-1000 / (CLK_FREQ/CLK_TEST_CYCLES)), clk_presence_timer_callback, NULL, &timer);
        return ret;
    }
    return clock_detected;
}
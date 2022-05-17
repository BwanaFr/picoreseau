#include "clock_detect.h"
#include "hardware/pwm.h"
#include "pico/time.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hdlc_rx.h"

#define CLK_IN_PIN 1
#define CLK_FREQ 500

static uint slice_num = 0;  //PWM slice number


void initialize_clock_detect() {
    slice_num = pwm_gpio_to_slice_num(CLK_IN_PIN);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&cfg, PWM_DIV_B_RISING);
    pwm_config_set_clkdiv(&cfg, 1);
    pwm_init(slice_num, &cfg, false);
    gpio_set_function(CLK_IN_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(slice_num, true);
}

void wait_for_no_clock() {
    while(is_clock_detected()){
        tight_loop_contents();
    }
}

bool is_clock_detected(uint nbCycles) {
    pwm_set_counter(slice_num, 0);
    sleep_us(1000 / (CLK_FREQ/nbCycles));
    bool ret =  pwm_get_counter(slice_num) != 0;
    return ret;
}

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "picoreseau.pio.h"

#include "clock_detect.h"


int main() {
    stdio_init_all();
    initialize_clock_detect();

    gpio_init(28);
    gpio_set_dir(28, GPIO_OUT);

    while(true){
        bool clockDetected = is_clock_detected();
        gpio_put(28, clockDetected);
        if(!clockDetected){
            printf("Clock missed!\n");
        }
        sleep_ms(10);
    }
}
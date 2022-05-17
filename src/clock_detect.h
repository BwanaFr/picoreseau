#ifndef __CLOCK_DETECT__
#define __CLOCK_DETECT__
#include "pico/types.h"

/**
 * Initializes the clock detection
 **/
void initialize_clock_detect();

/**
 * Gets if clock is detected
 * @param nbCycles Number of clock cycles to detect
 * @return true if clock is detected during at least 10 clock edges
 */
bool is_clock_detected(uint nbCycles = 2);

void wait_for_no_clock();

#endif
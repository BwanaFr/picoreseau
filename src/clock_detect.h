#ifndef __CLOCK_DETECT__
#define __CLOCK_DETECT__

/**
 * Initializes the clock detection
 **/
void initialize_clock_detect();

/**
 * Gets if clock is detected
 * @param fast If true, only two clock cycles will be used (blocking)
 * @return true if clock is detected during at least 10 clock edges
 */
bool is_clock_detected(bool fast = false);


#endif
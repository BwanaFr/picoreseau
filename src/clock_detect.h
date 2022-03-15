#ifndef __CLOCK_DETECT__
#define __CLOCK_DETECT__

/**
 * Initializes the clock detection
 **/
void initialize_clock_detect();

/**
 * Gets if clock is detected
 * @return true if clock is detected during at least 10 clock edges
 */
bool is_clock_detected();

#endif
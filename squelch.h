#ifndef _SQUELCH_H
#define _SQUELCH_H

#include <cstddef> // needed for size_t

#ifdef DEBUG_SQUELCH
#include <stdio.h>  // needed for debug file output
#endif

/*
 Theory of operation:

 Squelch has 5 states, OPEN (has audio), CLOSED (no audio), OPENING (transitioning from CLOSED to OPEN),
 CLOSING (transitioning from OPEN to CLOSED), and LOW_POWER_ABORT (same as CLOSING but because of a constant
 power drop).

 Squelch is considered "open" when the state is OPEN or CLOSING and squelch is considered "closed" when the
 state is OPENING, LOW_POWER_ABORT, or CLOSED.

 Noise floor is computed using a low pass filter and updated with the current sample or prior value, whatever
 is lower.  Noise floor is updated every 16 stamples, except when squelch is open.

 Low pass filters are also used to track the current power levels.  One power level is for the sample before
 filtering, the second for post signal filtering (if any).  The pre-filter power level is updated for every
 sample.  The post-filter power level is optional.  When used, the post-filter power level is compared to a
 delayed pre-filter value.  The post-filter is set to a fraction of the pre-filtered value each time state
 transitions to OPENING, and is not updated while state is CLOSED.

 Squelch level can be set manually or is computed as a function of the noise floor.

 When the power level exceeds the squelch level, the state transitions to OPENING and a delay counter starts,
 then once the counter is over the state moves to OPEN if there is power, otherwise back to CLOSED. The same
 (but opposite) happens when the power level drops below the squelch level.

 While the squelch is OPEN, a count of continuous samples that are below the squelch level is maintained.  If
 this count exceeds a threshold then the state moves to LOW_POWER_ABORT.  This allows the squelch to close
 after a sharp drop off in power before the power level has caught up.

 A count of "recent opens" is maintained as a way to detect squelch flapping (ie rapidly opening and closing).
 When flapping is detected the squelch level is decreased in an attempt to keep squelch open longer.
 */

class Squelch {
public:
	Squelch(int manual = -1);

	void process_raw_sample(const float &sample);
	void process_filtered_sample(const float &sample);

	bool is_open(void) const;
	bool should_filter_sample(void) const;

	bool first_open_sample(void) const;
	bool last_open_sample(void) const;

	const float & noise_floor(void) const;
	const float & power_level(void) const;
	const size_t & open_count(void) const;
	const size_t & flappy_count(void) const;
	float squelch_level(void) const;

#ifdef DEBUG_SQUELCH
	~Squelch(void);
	void set_debug_file(const char *filepath);
#endif

private:
	enum State {
		CLOSED,				// Audio is suppressed
		OPENING,			// Transitioning closed -> open
		CLOSING,			// Transitioning open -> closed
		LOW_POWER_ABORT,	// Like CLOSING but is_open() is false
		OPEN				// Audio not suppressed
	};

	int manual_;				// manually configured squelch level, < 0 for disabled

	float noise_floor_;			// noise level
	float pre_filter_avg_;		// average power for reference sample
	float post_filter_avg_;		// average power for post-filter sample

	bool using_post_filter_;	// if the caller is providing filtered samples

	int open_delay_;			// how long to wait after power crosses squelch to open
	int close_delay_;			// how long to wait after power crosses squelch to close
	int low_power_abort_;		// number of repeated samples below squelch to cause a close
	float pre_vs_post_factor_;	// multiplier when doing pre vs post filter compaison

	State next_state_;
	State current_state_;

	int delay_;				// samples to wait before making next squelch decision
	size_t open_count_;		// number of times squelch is opened
	size_t sample_count_;	// number of samples processed (for logging)
	size_t flappy_count_;	// number of times squelch was detected as flapping OPEN/CLOSED
	int low_power_count_;	// number of repeated samples below squelch

	// Flap detection parameters
	size_t recent_sample_size_;		// number of samples defined as "recent"
	size_t flap_opens_threshold_;	// number of opens to count as flapping
	size_t recent_open_count_;		// number of times squelch recently opened
	size_t closed_sample_count_;	// number of continuous samples where squelch has been CLOSED

	// Buffered pre_filter_avg_ values
	int buffer_size_;		// size of buffer
	int buffer_head_;		// index to add new values
	int buffer_tail_;		// index to read buffered values
	float *buffer_;			// buffer

	void set_state(State update);
	void update_current_state(void);
	bool has_power(void) const;
	bool is_manual(void) const;
	void update_average_power(float &avg, const float &sample);
	bool currently_flapping(void) const;

#ifdef DEBUG_SQUELCH
	FILE *debug_file_;
	float raw_input_;
	float filtered_input_;
	void debug_value(const float &value);
	void debug_value(const int &value);
	void debug_state(void);
#endif
};

#endif

#ifndef _SQUELCH_H
#define _SQUELCH_H

#include <cstddef> // needed for size_t

/*
 Theory of operation:

 Squelch has 4 states, OPEN (has audio), CLOSED (no audio), OPENING (transitioning from CLOSED to OPEN) and
 CLOSING (transitioning from OPEN to CLOSED)

 Noise floor is computed using a low pass filter and updated with the current sample or prior value, whatever
 is lower.

 Low pass filters are also used to track the current power levels.  One power level is for the sample before
 filtering, the second for post signal filtering (if any).  The pre-filter power level is updated for every
 sample.  The post-filter power level is optional.  When used, the post-filter power level is initialized to
 the pre-filter value each time state transitions to OPENING, and is not updated while state is CLOSING or
 CLOSED.

 Squelch level can be set manually or is computed as a function of the noise floor.

 When the power level exceeds the squelch level, the state transitions to OPENING and a delay counter starts,
 then once the counter is over the state moves to OPEN. The same (but opposite) happens when the power level
 drops below the squelch level.

 While the squelch is OPEN, a count of continuous samples that are below the squelch level is maintained.  If
 this count exceeds a threshold then the state moves to CLOSING.  This allows the squelch to close after a
 sharp drop off in power before the power level has caught up.

 When using a post-filter power level it can not be compared directly to the squelch level.  It can however be
 compared to the pre-filter power level.
 
 */

class Squelch {
public:

	enum State {
		OPENING,	// Transitioning closed -> open
		OPEN,		// Audio not suppressed
		CLOSING,	// Transitioning open -> closed
		CLOSED		// Audio is suppressed
	};

	Squelch(int manual = -1);

	void process_reference_sample(const float &sample);
	void process_filtered_sample(const float &sample);

	bool is_open(void) const;
	bool should_filter_sample(void) const;

	bool first_open_sample(void) const;
	bool last_open_sample(void) const;

	const State & get_state(void) const;
	const float & noise_floor(void) const;
	const float & power_level(void) const;
	const size_t & open_count(void) const;
	float squelch_level(void) const;

private:
	int open_delay_;			// how long to wait after power crosses squelch to open
	int close_delay_;			// how long to wait after power crosses squelch to close
	int low_power_abort_;		// number of repeated samples below squelch to cause a close
	int manual_;				// manually configured squelch level, < 0 for disabled

	float noise_floor_;			// noise level
	float pre_filter_avg_;		// average power for reference sample
	float post_filter_avg_;		// average power for post-filter sample

	bool using_post_filter_;	// if the caller is providing filtered samples

	State next_state_;
	State current_state_;

	int delay_;				// samples to wait before making next squelch decision
	size_t open_count_;		// number of times squelch is opened
	size_t sample_count_;	// number of samples processed (for logging)
	int low_power_count_;	// number of repeated samples below squelch

	void set_state(State update);
	void update_current_state(void);
	bool has_power(void) const;
	bool is_manual(void) const;
};

#endif

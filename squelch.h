#ifndef _SQUELCH_H
#define _SQUELCH_H

#include <iostream> // needed for std::ostream

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

	bool should_fade_in(void) const;
	bool should_fade_out(void) const;

	const State & get_state(void) const;
	const float & noise_floor(void) const;
	const float & power_level(void) const;
	const size_t & open_count(void) const;
	float squelch_level(void) const;

private:
	int flap_delay_;			// how long to wait after opening/closing before changing
	int low_power_abort_;		// number of repeated samples below squelch to cause a close
	int manual_;				// manually configured squelch level, < 0 for disabled

	float agcmin_;				// noise level
	float agcavgslow_;			// average power for reference sample
	float post_filter_avg_;		// average power for post-filter sample

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

	friend std::ostream & operator << (std::ostream &out, const Squelch &squelch);
	friend std::ostream & operator << (std::ostream &out, const Squelch::State &state);
};

#endif

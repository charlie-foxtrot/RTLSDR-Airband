#include "squelch.h"

#include "rtl_airband.h" // needed for debug_print()

using namespace std;

Squelch::Squelch(int manual) :
	manual_(manual)
{
	agcmin_ = 100.0f;
	agcavgslow_ = 0.5f;
	post_filter_avg_ = 0.5f;

	// TODO: Possible Improvement - revisit magic numbers
	flap_delay_ = AGC_EXTRA * 2 - 1;
	low_power_abort_ = AGC_EXTRA - 12;

	next_state_ = CLOSED;
	current_state_ = CLOSED;

	delay_ = 0;
	open_count_ = 0;
	sample_count_ = 0;
	low_power_count_ = 0;

	debug_print("Created Squelch, flap_delay: %d, low_power_abort: %d, manual: %d\n", flap_delay_, low_power_abort_, manual_);
}

bool Squelch::is_open(void) const {
	// TODO: Causes Diff - remove checks on next_state_
	return (current_state_ == OPEN && next_state_ != CLOSING);
}

bool Squelch::should_filter_sample(void) const {
	// TODO: Causes Diff - remove checks on next_state_
	if (next_state_ == CLOSING) {
		return false;
	}
	if (current_state_ == OPEN || current_state_ == OPENING || next_state_ == OPENING) {
		return true;
	}
	return false;
}

bool Squelch::should_fade_in(void) const {
	return (next_state_ == OPENING && current_state_ != OPENING);
}

bool Squelch::should_fade_out(void) const {
	return (next_state_ == CLOSING && current_state_ != CLOSING);
}

const Squelch::State & Squelch::get_state(void) const {
	return current_state_;
}

const float & Squelch::noise_floor(void) const {
	return agcmin_;
}

const float & Squelch::power_level(void) const {
	return agcavgslow_;
}

const size_t & Squelch::open_count(void) const {
	return open_count_;
}

float Squelch::squelch_level(void) const {
	if (is_manual()) {
		return manual_;
	}
	return 3.0f * noise_floor();
}

bool Squelch::is_manual(void) const {
	return manual_ >= 0;
}

bool Squelch::has_power(void) const {
	return power_level() >= squelch_level();
}

void Squelch::process_reference_sample(const float &sample) {

	// Update current state based on previous state from last iteration
	update_current_state();

	sample_count_++;

	// auto noise floor
	// TODO: Possible Improvement - update noise floor with each sample
	// TODO: Causes Diff - remove +3
	if ((sample_count_ + 3) % 16 == 0) {
		agcmin_ = agcmin_ * 0.97f + std::min(agcavgslow_, agcmin_) * 0.03f + 0.0001f;
	}

	// average power
	agcavgslow_ = agcavgslow_ * 0.99f + sample * 0.01f;

	// Check power against thresholds
	if (current_state_ == OPEN && has_power() == false) {
		debug_print("Closing at %zu: no power after timeout (%f < %f)\n", sample_count_, power_level(), squelch_level());
		set_state(CLOSING);
	}

	if (current_state_ == CLOSED && has_power() == true) {
		debug_print("Opening at %zu: power (%f >= %f)\n", sample_count_, power_level(), squelch_level());
		set_state(OPENING);
	}

	// Override squelch and close if there are repeated samples under the squelch level
	if((current_state_ == OPEN || current_state_ == OPENING) && next_state_ != CLOSING) {
		if (sample >= squelch_level()) {
			low_power_count_ = 0;
		} else {
			low_power_count_++;
			if (low_power_count_ >= low_power_abort_) {
				debug_print("Closing at %zu: low power count %d\n", sample_count_, low_power_count_);
				set_state(CLOSING);
			}
		}
	}
}

void Squelch::process_filtered_sample(const float &sample) {
	if (should_filter_sample() == false) {
		return;
	}

	// average power
	post_filter_avg_ = post_filter_avg_ * 0.999f + sample * 0.001f;

	if ((current_state_ == OPEN || current_state_ == OPENING || next_state_ == OPEN || next_state_ == OPENING) && post_filter_avg_ < squelch_level()) {
		debug_print("Closing at %zu: power post filter (%f < %f)\n", sample_count_, post_filter_avg_, squelch_level());
		set_state(CLOSING);
	}
}

void Squelch::set_state(State update) {

	// Valid transitions (current_state_ -> next_state_) are:
	//  - OPENING -> OPENING
	//  - OPENING -> CLOSING
	//  - OPENING -> OPEN
	//  - OPEN -> OPEN
	//  - OPEN -> CLOSING
	//  - CLOSING -> OPENING
	//  - CLOSING -> CLOSING
	//  - CLOSING -> CLOSED
	//  - CLOSED -> CLOSED
	//  - CLOSED -> OPENING

	// Invalid transistions (current_state_ -> next_state_) are:
	//  - OPENING -> CLOSED (must go through CLOSING to get to CLOSED)
	//  - OPEN -> OPENING (if already OPEN cant go backwards)
	//  - OPEN -> CLOSED (must go through CLOSING to get to CLOSED)
	//  - CLOSING -> OPEN (must go through OPENING to get to OPEN)
	//  - CLOSED -> CLOSING (if already CLOSED cant go backwards)
	//  - CLOSED -> OPEN (must go through OPENING to get to OPEN)

	// must go through OPENING to get to OPEN (unless already OPEN)
	if (update == OPEN && current_state_ != OPEN && current_state_ != OPENING) {
		update = OPENING;
	}

	// must go through CLOSING to get to CLOSED (unless already CLOSED)
	if (update == CLOSED && current_state_ != CLOSING && current_state_ != CLOSED) {
		update = CLOSING;
	}

	// if already OPEN cant go backwards
	if (update == OPENING && current_state_ == OPEN) {
		update = OPEN;
	}

	// if already CLOSED cant go backwards
	if (update == CLOSING && current_state_ == CLOSED) {
		update = CLOSED;
	}

	next_state_ = update;
}

void Squelch::update_current_state(void) {
	if (next_state_ == OPENING) {
		if (current_state_ != OPENING) {
			open_count_++;
			delay_ = flap_delay_;
			low_power_count_ = 0;
			// TODO: Causes Diff - re-initialize post_filter_avg_ = agcavgslow_
			current_state_ = next_state_;
		} else {
			delay_--;
			// TODO: Causes Diff - remove start
			if (delay_ == 2) {
				delay_ = 0;
			}
			// TODO: Causes Diff - remove end
			if (delay_ <= 2) {
				next_state_ = OPEN;
			}
		}
	} else if (next_state_ == CLOSING) {
		if (current_state_ != CLOSING) {
			delay_ = flap_delay_;
			current_state_ = next_state_;
		} else {
			delay_--;
			// TODO: Causes Diff - remove start
			if (delay_ == 2) {
				delay_ = 0;
			}
			// TODO: Causes Diff - remove end
			if (delay_ == 0) {
				next_state_ = CLOSED;
			}
		}
	} else {
		current_state_ = next_state_;
	}
}

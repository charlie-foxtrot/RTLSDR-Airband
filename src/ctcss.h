#ifndef ctcss_h
#define ctcss_h

#include <vector>
#include <cstddef> // needed for size_t

class ToneDetector {
public:
	ToneDetector(float tone_freq, float sample_freq, int window_size);
	void process_sample(const float &sample);
	void reset(void);

	const float & relative_power(void) const { return magnitude_; }
	const float & freq(void) const { return tone_freq_; }
	const float & coefficient (void) const { return coeff_; }
	bool enabled(void) const { return enabled_; }

private:
	bool enabled_;
	float tone_freq_;
	float magnitude_;

	int window_size_;
	float coeff_;

	int count_;
	float q0_;
	float q1_;
	float q2_;
};


class ToneDetectorSet {
public:
	struct PowerIndex {
		float power;
		float freq;
	};

	ToneDetectorSet() {}
	
	bool add(const float & tone_freq, const float & sample_freq, int window_size);
	void process_sample(const float &sample);
	void reset(void);

	float sorted_powers(std::vector<PowerIndex> &powers);

private:
	std::vector<ToneDetector> tones_;
};


class CTCSS {
public:
	CTCSS(void) : enabled_(false), found_count_(0), not_found_count_(0) {} 
	CTCSS(const float & ctcss_freq, const float & sample_rate, int window_size);
	void process_audio_sample(const float &sample);
	void reset(void);
	
	const size_t & found_count(void) const { return found_count_; }
	const size_t & not_found_count(void) const { return not_found_count_; }

	bool is_enabled(void) const { return enabled_; }
	bool enough_samples(void) const { return enough_samples_; }
	bool has_tone(void) const { return !enabled_ || has_tone_; }

private:
	bool enabled_;
	float ctcss_freq_;
	int window_size_;
	size_t found_count_;
	size_t not_found_count_;

	ToneDetectorSet powers_;
	
	bool enough_samples_;
	int sample_count_;
	bool has_tone_;

};

#endif /* ctcss_h */

// vim: noet ts=4

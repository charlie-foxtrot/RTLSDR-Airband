/*
 * test_generate_signal.cpp
 *
 * Copyright (C) 2023 charlie-foxtrot
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>

#include "test_base_class.h"

#include "generate_signal.h"

using namespace std;

class ToneTest : public TestBaseClass {};

TEST_F(ToneTest, simple_object)
{
	// simple case the sample rate is a multiple of the frequency so specific points can be measured
	float tone_freq = 100; // tone at 100 Hz
	float sample_rate = 1000 * tone_freq; // set sample rate to 1000 times the tone so there will be 250 samples per quarter
	float amplitude = 5.0;

	Tone tone(sample_rate, tone_freq, amplitude);

	float last_sample = 0.0;
	float this_sample = 0.0;

	// loop through some number of cycles
	for (int j = 0 ; j < 10 ; ++j) {
		// first 249 samples will be positive and increasing
		for (int i = 0; i < 249 ; ++i) {
			this_sample = tone.get_sample();
			ASSERT_GT(this_sample, 0.0);
			ASSERT_GT(this_sample, last_sample);
			last_sample = this_sample;
		}

		// sample 250 will be the amp
		this_sample = tone.get_sample();
		ASSERT_EQ(this_sample, amplitude);
		ASSERT_GT(this_sample, last_sample);
		last_sample = this_sample;

		// next 249 samples will be positive and decreasing
		for (int i = 0; i < 249 ; ++i) {
			this_sample = tone.get_sample();
			ASSERT_GT(this_sample, 0.0);
			ASSERT_LT(this_sample, last_sample);
			last_sample = this_sample;
		}

		// sample 500 will be zero-ish
		this_sample = tone.get_sample();
		ASSERT_LT(this_sample, 0.000001);
		ASSERT_LT(this_sample, last_sample);
		last_sample = this_sample;

		// next 249 samples will be negative and decreasing
		for (int i = 0; i < 249 ; ++i) {
			this_sample = tone.get_sample();
			ASSERT_LT(this_sample, 0.0);
			ASSERT_LT(this_sample, last_sample);
			last_sample = this_sample;
		}

		// sample 750 will be negative amp
		this_sample = tone.get_sample();
		ASSERT_EQ(this_sample, -1.0 * amplitude);
		ASSERT_LT(this_sample, last_sample);
		last_sample = this_sample;

		// next 249 samples will be negative and increasing
		for (int i = 0; i < 249 ; ++i) {
			this_sample = tone.get_sample();
			ASSERT_LT(this_sample, 0.0);
			ASSERT_GT(this_sample, last_sample);
			last_sample = this_sample;
		}

		// sample 1000 will be zero-ish
		this_sample = tone.get_sample();
		ASSERT_LT(this_sample, 0.000001);
		ASSERT_GT(this_sample, last_sample);
		last_sample = this_sample;
	}
}

class NoiseTest : public TestBaseClass {};

TEST_F(NoiseTest, simple_object)
{
	float ampl = 0.2;
	Noise noise(ampl);

	// never zero and always between +/- ampl
	for (int i = 0 ; i < 10000 ; ++i) {
		float sample = noise.get_sample();
		ASSERT_NE(sample, 0.0);
		ASSERT_LT(sample, ampl);
		ASSERT_GT(sample, -1 * ampl);
	}
}

class GenerateSignalTest : public TestBaseClass {
protected:
	int sample_rate;
	void SetUp(void) {
		TestBaseClass::SetUp();
		sample_rate = 8000;
	}
};

TEST_F(GenerateSignalTest, default_object)
{
	GenerateSignal signal(8000);
	EXPECT_EQ(signal.get_sample(), 0.0);
}

TEST_F(GenerateSignalTest, generate_file) {
	
	float file_seconds = 10.5;
	GenerateSignal signal(sample_rate);

	string test_filepath = temp_dir + "/10_sec_file.dat";
	signal.write_file(test_filepath, file_seconds);

	// make sure the file exists and is the right size
	struct stat info;
	ASSERT_EQ(stat(test_filepath.c_str(), &info), 0);
	EXPECT_TRUE(S_ISREG(info.st_mode));
	EXPECT_EQ(info.st_size, sample_rate * file_seconds * sizeof(float));

}

TEST_F(GenerateSignalTest, get_sample_no_signals) {
	GenerateSignal signal(sample_rate);
	for (int i = 0 ; i < 60 * sample_rate ; ++i) {
		ASSERT_EQ(signal.get_sample(), 0.0);
	}
}

TEST_F(GenerateSignalTest, get_sample_single_tone_only) {
	float tone_freq = 123.34;
	float tone_ampl = 0.32;

	GenerateSignal signal(sample_rate);
	signal.add_tone(tone_freq, tone_ampl);
	Tone tone(sample_rate, tone_freq, tone_ampl);
	for (int i = 0 ; i < 60 * sample_rate ; ++i) {
		ASSERT_EQ(signal.get_sample(), tone.get_sample());
	}
}

TEST_F(GenerateSignalTest, get_sample_two_tones) {
	float tone1_freq = 123.34;
	float tone2_freq = 231.43;
	float tone1_ampl = 0.32;
	float tone2_ampl = 0.05;

	GenerateSignal signal(sample_rate);
	signal.add_tone(tone1_freq, tone1_ampl);
	signal.add_tone(tone2_freq, tone2_ampl);
	Tone tone1(sample_rate, tone1_freq, tone1_ampl);
	Tone tone2(sample_rate, tone2_freq, tone2_ampl);
	for (int i = 0 ; i < 60 * sample_rate ; ++i) {
		ASSERT_EQ(signal.get_sample(), tone1.get_sample() + tone2.get_sample());
	}
}

TEST_F(GenerateSignalTest, get_sample_only_noise) {

	float ampl = 0.3;
	GenerateSignal signal(sample_rate);
	signal.add_noise(ampl);

	float max_value = 0;
	float min_value = 0;
	for (int i = 0 ; i < 600 * sample_rate ; ++i) {
		float sample = signal.get_sample();
		min_value = min(sample, min_value);
		max_value = max(sample, max_value);
	}

	EXPECT_GT(max_value, 0);
	EXPECT_LT(max_value, ampl * 0.6);

	EXPECT_LT(min_value, 0);
	EXPECT_GT(min_value, ampl * -0.6);
}

TEST_F(GenerateSignalTest, get_sample_two_tones_and_noise) {

	float tone1_freq = 123.34;
	float tone2_freq = 231.43;
	float tone1_ampl = 0.01;
	float tone2_ampl = 0.02;
	float noise_ampl = 0.2;

	GenerateSignal signal(sample_rate);
	signal.add_tone(tone1_freq, tone1_ampl);
	signal.add_tone(tone2_freq, tone2_ampl);
	signal.add_noise(noise_ampl);

	Tone tone1(sample_rate, tone1_freq, tone1_ampl);
	Tone tone2(sample_rate, tone2_freq, tone2_ampl);
	for (int i = 0 ; i < 60 * sample_rate ; ++i) {
		float sample_noise = signal.get_sample() - tone1.get_sample() - tone2.get_sample();
		ASSERT_NE(sample_noise, 0.0);
		ASSERT_LT(sample_noise, noise_ampl * 0.6);
		ASSERT_GT(sample_noise, noise_ampl * -0.6);
	}
}

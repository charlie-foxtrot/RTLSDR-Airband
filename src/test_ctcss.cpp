/*
 * test_ctcss.cpp
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

#include "test_base_class.h"
#include "generate_signal.h"

#include "ctcss.h"

using namespace std;

class CTCSSTest : public TestBaseClass {
protected:
	int sample_rate;
	int fast_window_size;
	int slow_window_size;

	void SetUp(void) {
		TestBaseClass::SetUp();
		sample_rate = 8000;
		fast_window_size = sample_rate * 0.05;
		slow_window_size = sample_rate * 0.4;
	}

	void test_all_tones(GenerateSignal &signal, const float &tone = 0) {
		for (auto standard_tone : CTCSS::standard_tones) {
			if (standard_tone == tone) {
				continue;
			}

			CTCSS ctcss(standard_tone, sample_rate, slow_window_size);
			run_signal(ctcss, signal);
			EXPECT_FALSE(ctcss.has_tone());
		}
		if (tone != 0) {
			CTCSS ctcss(tone, sample_rate, slow_window_size);
			run_signal(ctcss, signal);
			EXPECT_TRUE(ctcss.has_tone());
		}
	}

	void run_signal(CTCSS &ctcss, GenerateSignal &signal) {
		EXPECT_TRUE(ctcss.is_enabled());
		while( !ctcss.enough_samples() ) {
			ctcss.process_audio_sample(signal.get_sample());
		}
	}
};

TEST_F(CTCSSTest, creation)
{
	CTCSS ctcss;
	EXPECT_FALSE(ctcss.is_enabled());
}

TEST_F(CTCSSTest, no_signal)
{
	GenerateSignal signal(sample_rate);
	test_all_tones(signal);
}

TEST_F(CTCSSTest, only_noise)
{
	GenerateSignal signal(sample_rate);
	signal.add_noise(0.5);
	test_all_tones(signal);
}

TEST_F(CTCSSTest, has_tone)
{
	float tone = CTCSS::standard_tones[0];
	GenerateSignal signal(sample_rate);
	signal.add_tone(tone, 0.05);
	signal.add_noise(0.5);
	test_all_tones(signal, tone);
}

TEST_F(CTCSSTest, has_non_standard_tone)
{
	float tone = (CTCSS::standard_tones[0] + CTCSS::standard_tones[0]) / 2;
	GenerateSignal signal(sample_rate);
	signal.add_tone(tone, 0.05);
	signal.add_noise(0.5);
	test_all_tones(signal, tone);
}

TEST_F(CTCSSTest, has_tone_each_standard_tone)
{
	for (auto tone : CTCSS::standard_tones) {
		GenerateSignal signal(sample_rate);
		signal.add_tone(tone, 0.05);
		signal.add_noise(0.5);
		test_all_tones(signal, tone);
	}
}

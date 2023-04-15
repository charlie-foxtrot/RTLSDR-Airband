/*
 * generate_signal.h
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

#ifndef _GENERATE_SIGNAL_H
#define _GENERATE_SIGNAL_H

#include <vector>
#include <string>
#include <random>

class Tone {
public:
    static float WEAK;
    static float NORMAL;
    static float STRONG;

    Tone(int sample_rate, const float &freq, const float &ampl);
    float get_sample(void);

private:
    int sample_rate_;
    float freq_;
    float ampl_;
    size_t sample_count_;
};

class Noise {
public:
    static float WEAK;
    static float NORMAL;
    static float STRONG;

    Noise(const float &ampl);
    float get_sample(void);

private:
    float ampl_;
    std::mt19937 generator;
    std::normal_distribution<float> distribution;
};

class GenerateSignal {
public:
    GenerateSignal(int sample_rate);

    void add_tone(const float &freq, const float &ampl);
    void add_noise(const float &ampl);

    float get_sample(void);

    void write_file(const std::string &filepath, const float &seconds);

private:
    int sample_rate_;
    std::vector<Tone> tones_;
    std::vector<Noise> noises_;
};

#endif /* _GENERATE_SIGNAL_H */

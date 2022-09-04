#pragma once
//#define OLC_PGE_APPLICATION
//#include "olcPixelGameEngine.h"

//#define OLC_SOUNDWAVE
#include "olcSoundWaveEngine.h"
#include <array>

constexpr double pi = 3.14159265358979323846;

class BiquadFilter : public olc::sound::synth::Module {
public:
	enum class Type {
		LowPass,
		HighPass,
		BandPass,
		Notch,
		AllPass,
		Peak,
		LowShelf,
		HighShelf
	};
	BiquadFilter() : z0(0.9329157274413206), z1(-1.8658314548826411), z2(0.9329157274413206), p1(-1.7732296471466154), p2(0.9584332626186669) {};
	//{ 0.9329157274413206 , -1.8658314548826411 , 0.9329157274413206 , -1.7732296471466154, 0.9584332626186669 }
	BiquadFilter(double z0_, double z1_, double z2_, double p1_, double p2_) : z0(z0_), z1(z1_), z2(z2_), p1(p1_), p2(p2_) {};
	double z0 = 0.0;
	double z1 = 0.0;
	double z2 = 0.0;
	double p1 = 0.0;
	double p2 = 0.0;

	olc::sound::synth::Property input;
	olc::sound::synth::Property output;

	std::array<double, 2> i_state = {};
	std::array<double, 2> o_state = {};

	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		double out = (input.value * z0) + (i_state[0] * z1) + (i_state[0] * z2) - (o_state[0] * p1) - (o_state[1] * p2);
		o_state[1] = o_state[0];
		i_state[1] = i_state[0];
		o_state[0] = out;
		i_state[0] = input.value;

		output.value = out;
	}

	//Maths from https://www.earlevel.com/main/2021/09/02/biquad-calculator-v3/
	void Configure(uint32_t nSampleRate, double Fc, double Q, double Gain, Type eType) {
		double V = std::pow(10.0, std::abs(Gain) / 20);
		double K = std::tan(pi * Fc / nSampleRate);
		double N = 0.0;


		switch (eType) {
		case Type::LowPass:
			N = 1 / (1 + K / Q + K * K);
			z0 = K * K * N;
			z1 = 2 * z0;
			z2 = z0;
			p1 = 2 * (K * K - 1) * N;
			p2 = (1 - K / Q + K * K) * N;
			break;
		case Type::HighPass:
			N = 1 / (1 + K / Q + K * K);
			z0 = N;
			z1 = -2 * N;
			z2 = N;
			p1 = 2 * (K * K - 1) * N;
			p2 = (1 - K / Q + K * K) * N;
			break;
		case Type::BandPass:
			N = 1 / (1 + K / Q + K * K);
			z0 = K / Q * N;
			z1 = 0;
			z2 = -z0;
			p1 = 2 * (K * K - 1) * N;
			p2 = (1 - K / Q + K * K) * N;
			break;
		case Type::Notch:
			N = 1 / (1 + K / Q + K * K);
			z0 = (1 + K * K) * N;
			z1 = 2 * (K * K - 1) * N;
			z2 = z0;
			p1 = z1;
			p2 = (1 - K / Q + K * K) * N;
			break;
		case Type::AllPass:
			N = 1 / (1 + K / Q + K * K);
			z0 = (1 - K / Q + K * K) * N;
			z1 = 2 * (K * K - 1) * N;
			z2 = z0;
			p1 = z1;
			p2 = z0;
			break;
		case Type::Peak:
			if (Gain >= 0) {
				N = 1 / (1 + 1 / Q * K + K * K);
				z0 = (1 + V / Q * K + K * K) * N;
				z1 = 2 * (K * K - 1) * N;
				z2 = (1 - V / Q * K + K * K) * N;
				p1 = z1;
				p2 = (1 - 1 / Q * K + K * K) * N;
			}
			else {
				N = 1 / (1 + V / Q * K + K * K);
				z0 = (1 + 1 / Q * K + K * K) * N;
				z1 = 2 * (K * K - 1) * N;
				z2 = (1 - 1 / Q * K + K * K) * N;
				p1 = z1;
				p2 = (1 - V / Q * K + K * K) * N;
			}
			break;
		case Type::LowShelf:
			if (Gain >= 0) {
				N = 1 / (1 + std::sqrt(2.0) * K + K * K);
				z0 = (1 + std::sqrt(2.0 * V) * K + V * K * K) * N;
				z1 = 2 * (V * K * K - 1) * N;
				z2 = (1 - std::sqrt(2.0 * V) * K + V * K * K) * N;
				p1 = 2 * (K * K - 1) * N;
				p2 = (1 - std::sqrt(2.0) * K + K * K) * N;
			}
			else {
				N = 1 / (1 + std::sqrt(2.0 * V) * K + V * K * K);
				z0 = (1 + std::sqrt(2.0) * K + K * K) * N;
				z1 = 2 * (K * K - 1) * N;
				z2 = (1 - std::sqrt(2.0) * K + K * K) * N;
				p1 = 2 * (V * K * K - 1) * N;
				p2 = (1 - std::sqrt(2.0 * V) * K + V * K * K) * N;
			}
			break;
		case Type::HighShelf:
			if (Gain >= 0) {
				N = 1 / (1 + std::sqrt(2.0) * K + K * K);
				z0 = (V + std::sqrt(2.0 * V) * K + K * K) * N;
				z1 = 2 * (K * K - V) * N;
				z2 = (V - std::sqrt(2.0 * V) * K + K * K) * N;
				p1 = 2 * (K * K - 1) * N;
				p2 = (1 - std::sqrt(2.0) * K + K * K) * N;
			}
			else {
				N = 1 / (V + std::sqrt(2.0 * V) * K + K * K);
				z0 = (1 + std::sqrt(2.0) * K + K * K) * N;
				z1 = 2 * (K * K - 1) * N;
				z2 = (1 - std::sqrt(2.0) * K + K * K) * N;
				p1 = 2 * (K * K - V) * N;
				p2 = (V - std::sqrt(2.0 * V) * K + K * K) * N;
			}
			break;
		}
	}
};


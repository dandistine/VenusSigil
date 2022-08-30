#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"

#define OLC_SOUNDWAVE
#include "olcSoundWaveEngine.h"

#include <mutex>
#include <numeric>

template<typename T>
[[nodiscard]] T lerp(const T& v0, const T& v1, float t) noexcept {
	return v0 * (1.0f - t) + v1 * t;
}
//Map a value val that is between in_start and end_start to the range out_start out_end
template<class T, class U>
[[nodiscard]] U map(T in_start, T in_end, U out_start, U out_end, T val) noexcept {
	auto t = std::clamp<U>((val - in_start) / (in_end - in_start), 0.0, 1.0);

	return lerp(out_start, out_end, t);
}

//A simple mixer with N inputs
template<size_t N>
class Mixer : public olc::sound::synth::Module {
public:
	std::array<olc::sound::synth::Property, N> inputs = {};
	std::array<olc::sound::synth::Property, N> amplitude = {};
	olc::sound::synth::Property output;

	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		double out = 0.0f;

		for (size_t i = 0; i < N; i++) {
			out += amplitude[i].value * inputs[i].value;
		}

		output = out / N;
	}
};

template<size_t max_ms, size_t samplerate>
class Delay : public olc::sound::synth::Module {

public:
	olc::sound::synth::Property input = 0.0;
	olc::sound::synth::Property output = 0.0;
	olc::sound::synth::Property decay = 1.0;
	olc::sound::synth::Property delay = 1.0;
	std::array<double, (max_ms * samplerate) / 1000> state{0.0};
private:
	double max_delay = static_cast<double>(max_ms) / 1000.0;
	size_t input_index = 0;
	size_t output_index = 1;
	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		state[input_index] = input.value * decay.value;
		input_index = (input_index + 1) % state.size();

		//Determine where we should sample from based on the desired delay amount
		size_t out_dist = std::min(state.size() - 1, static_cast<size_t>(delay.value * max_delay * samplerate));
		output_index = (input_index - out_dist + state.size()) % state.size();
		output = state[output_index];
	}
};

//A simple DSP style first order filter
class FirstOrderFilter : public olc::sound::synth::Module {
public:
	FirstOrderFilter(double p, double z) : pole(p), zero(z) {};
	olc::sound::synth::Property pole;
	olc::sound::synth::Property zero;
	olc::sound::synth::Property state = 0.0;
	olc::sound::synth::Property input = 0.0;
	olc::sound::synth::Property output = 0.0;
	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		double new_state = input.value + pole.value * state.value;
		output = new_state - zero.value * state.value;
		state = new_state;
	}
};

class Gain : public olc::sound::synth::Module {
private:
	double max_gain = 2;
public:
	olc::sound::synth::Property gain = 1.0;
	olc::sound::synth::Property input = 0.0;
	olc::sound::synth::Property output = 0.0;
	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		output.value = max_gain * gain.value * input.value;
	}
};

class LPF : public olc::sound::synth::Module {
public:
	std::array<double, 13> taps = { 0.000035,
0.000928,
0.004561,
0.012669,
0.024443,
0.035453,
0.040000,
0.035453,
0.024443,
0.012669,
0.004561,
0.000928,
0.000035 };
	std::array<double, 13> state = { 0 };
	olc::sound::synth::Property input = 0.0;
	olc::sound::synth::Property output = 0.0;
	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		double o = 0.0;
		for (size_t i = 12; i > 0; i--) {
			state[i] = state[i - 1];
		}

		state[0] = input.value;

		for (size_t i = 0; i < 13; i++) {
			o += taps[i] * state[i];
		}
		output.value = 2*o;
	}

};

//Approximately filters white noise into pink noise
class Pinkifier : public olc::sound::synth::Module {
	FirstOrderFilter f1 = { 0.99572754 , 0.98443604};
	FirstOrderFilter f2 = { 0.94790649 , 0.83392334 };
	FirstOrderFilter f3 = { 0.53567505 , 0.07568359 };

public:
	olc::sound::synth::Property input = 0.0f;
	olc::sound::synth::Property output = 0.0f;
	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		f1.input = input;
		f1.Update(nChannel, dTime, dTimeStep);
		f2.input = f1.output;
		f2.Update(nChannel, dTime, dTimeStep);
		f3.input = f2.output;
		f3.Update(nChannel, dTime, dTimeStep);
		output = f3.output;
	}
};

class ADSREnvelope : public olc::sound::synth::Module {
private:
	enum class ADSR_STATE {
		INACTIVE,
		ATTACK,
		DECAY,
		SUSTAIN,
		RELEASE
	} mState = ADSR_STATE::INACTIVE;

	std::mutex m;

public:
	olc::sound::synth::Property mInput = 0.0;
	olc::sound::synth::Property mAttack = 0.0f;
	olc::sound::synth::Property mDecay = 0.00f;
	olc::sound::synth::Property mSustain = 1.0f;
	olc::sound::synth::Property mRelease = 1.0f;
	//double mRelease = 4.0f;
	olc::sound::synth::Property mAmplitude = 0.0f;
	olc::sound::synth::Property mReleaseAmplitude = 0.00f;
	olc::sound::synth::Property mOutput = 0.0f;
	olc::sound::synth::Property mReleaseTime = 0.0f;
	double mTotalTime = 0.0f;

public:

	void Begin() {
		std::scoped_lock lock(m);
		mTotalTime = 0.0f;
		mReleaseTime = 0.0f;
		mState = ADSR_STATE::ATTACK;
	}

	void End() {
		std::scoped_lock lock(m);
		mReleaseTime = mTotalTime;
		mReleaseAmplitude = mAmplitude;
		mState = ADSR_STATE::RELEASE;
	}

	virtual void Update(uint32_t nChannel, double dTime, double dTimeStep) override {
		std::scoped_lock lock(m);

		mTotalTime += dTimeStep;

		switch (mState) {
		case ADSR_STATE::INACTIVE:
			mOutput = 0.0;
			break;
		case ADSR_STATE::ATTACK:
			mAmplitude = map<>(0.0, mAttack.value, 0.0, 1.0, mTotalTime);
			if (mTotalTime > mAttack.value) mState = ADSR_STATE::DECAY;
			break;
		case ADSR_STATE::DECAY:
			mAmplitude = map<>(mAttack.value, mAttack.value + mDecay.value, 1.0, mSustain.value, mTotalTime);
			if (mTotalTime > mAttack.value + mDecay.value) mState = ADSR_STATE::SUSTAIN;
			break;
		case ADSR_STATE::SUSTAIN:
			mReleaseTime = mTotalTime;
			mReleaseAmplitude = mAmplitude;
			mState = ADSR_STATE::RELEASE;
			break;
		case ADSR_STATE::RELEASE:
			mAmplitude = map<>(mReleaseTime.value, mReleaseTime.value + mRelease.value, mReleaseAmplitude.value, 0.0, mTotalTime);
			break;
			//No default as all cases are taken care of
		}

		mOutput = mAmplitude.value * mInput.value;
	}
};

struct LineSegment {
	olc::vf2d start;
	olc::vf2d end;
	olc::Pixel color = { 255, 255, 255, 255 };
};

olc::vf2d MidPoint(const olc::vf2d s, const olc::vf2d e) {
	return (s + e) / 2.0f;
}

float rand_float() {
	return ((float)rand()) / RAND_MAX;
}

const uint32_t samplerate = 44100;
const float split_chance = 0.3f;
const float split_alpha_mod = 0.5f;
const olc::Pixel white = { 255, 255, 255, 255 };

class Bolt {
public:
	Bolt() {};
	Bolt(olc::vf2d start, olc::vf2d end) { segments.push_back({ start , end , white }); };

	std::vector<LineSegment> segments;

	void Iterate() {
		std::vector<LineSegment> new_segments;

		for (const auto& s : segments) {
			olc::vf2d m = MidPoint(s.start, s.end);
			olc::vf2d sl = m - s.start;

			//sl will be perpensidcular to the segment (s, m)
			sl = { -sl.y, sl.x };

			//move m perpendicularly a little bit
			m = m + (rand_float() - 0.5f) * sl;

			//Randomize the color of new segments a little bit
			//Keeping the blue at full gives a nice appearance
			float r = 0.7f + rand_float() / 3.34f;
			float g = 0.8f + rand_float() / 5.34f;

			olc::Pixel c = olc::PixelF(r, g, 1.0f, s.color.a / 255.0f);

			new_segments.emplace_back(LineSegment{ s.start, m, c });
			new_segments.emplace_back(LineSegment{ m, s.end, c });

			//If we're going to split, make the split a reflection
			//over the (s, m) line and give it a little bit of alpha
			if (rand_float() < split_chance) {
				olc::vf2d x = m + (m - s.start);
				olc::vf2d ne = x + (x - s.end);
				c.a *= split_alpha_mod;
				new_segments.emplace_back(LineSegment{ m, ne, c});
			}
		}

		segments = new_segments;
	}
};

std::vector<olc::vf2d> collision_points = {
{ -3 , -1 },
{ -3 , 0 },
{ -3 , 1 },
{ -2 , -2 },
{ -2 , -1 },
{ -2 , 0 },
{ -2 , 1 },
{ -2 , 2 },
{ -1 , -3 },
{ -1 , -2 },
{ -1 , -1 },
{ -1 , 0 },
{ -1 , 1 },
{ -1 , 2 },
{ -1 , 3 },
{ 0 , -3 },
{ 0 , -2 },
{ 0 , -1 },
{ 0 , 0 },
{ 0 , 1 },
{ 0 , 2 },
{ 0 , 3 },
{ 1 , -3 },
{ 1 , -2 },
{ 1 , -1 },
{ 1 , 0 },
{ 1 , 1 },
{ 1 , 2 },
{ 1 , 3 },
{ 2 , -2 },
{ 2 , -1 },
{ 2 , 0 },
{ 2 , 1 },
{ 2 , 2 },
{ 3 , -1 },
{ 3 , 0 },
{ 3 , 1 },
};

enum class eMode {
	START, //The beginning of the game
	IDLE, //Nothing is shown on screen
	HINT, //Show the upcoming lightning bolt around a point
	TRIGGER, //Show the full lightning bolt
	SHOW,
	FADEOUT,
	DIE
};

// Override base class with your custom functionality
class Example : public olc::PixelGameEngine
{
public:
	Example()
	{
		// Name your application
		sAppName = "VENUS SIGIL";
	}

public:
	

	bool OnUserCreate() override
	{
		// Called once at the start, so create things here
		srand(time(NULL));
		canvas = GetDrawTarget();

		for (int i = 0; i < 11; i++) {
			title_colors[i] = olc::WHITE;
			title_phase[i] = rand_float() * 2 * 3.14159;
			title_fmod[i] = rand_float();
		}

		osc1.waveform = olc::sound::synth::modules::Oscillator::Type::Noise;
		osc2.waveform = olc::sound::synth::modules::Oscillator::Type::Saw;

		osc1.frequency = .25;
		osc1.amplitude = 1.0;
		osc1.parameter = 0.5;

		osc1.amplitude = 1.0;

		adsr2.mRelease = 2.5;
		mixer.amplitude[0] = 1.0;
		mixer.amplitude[1] = 1.0;
		mixer.amplitude[2] = 1.0;

		delay.decay = .55;

		synth.AddModule(&osc1);
		synth.AddModule(&osc2);
		synth.AddModule(&pink_filter);
		synth.AddModule(&adsr);
		synth.AddModule(&adsr2);
		synth.AddModule(&delay);
		synth.AddModule(&mixer);
		synth.AddModule(&lpf);
		synth.AddModule(&gain);

		synth.AddPatch(&osc1.output, &delay.input);
		synth.AddPatch(&osc1.output, &mixer.inputs[0]);
		synth.AddPatch(&osc1.output, &lpf.input);
		synth.AddPatch(&delay.output, &mixer.inputs[1]);
		synth.AddPatch(&lpf.output, &mixer.inputs[2]);
		synth.AddPatch(&mixer.output, &gain.input);
		synth.AddPatch(&gain.output, &pink_filter.input);
		synth.AddPatch(&pink_filter.output, &adsr.mInput);
		synth.AddPatch(&adsr.mOutput, &adsr2.mInput);

		engine.InitialiseAudio(samplerate, 1, 8, 512);

		engine.SetCallBack_NewSample([this](double dTime) {return Synthesizer_OnNewCycleRequest(dTime); });
		engine.SetCallBack_SynthFunction([this](uint32_t nChannel, double dTime) {return Synthesizer_OnGetSample(nChannel, dTime); });

		return true;
	}

	bool OnUserDestroy() {
		engine.DestroyAudio();
		return true;
	}

	void Synthesizer_OnNewCycleRequest(double dTime)
	{
		synth.UpdatePatches();
	}

	// Called individually per sample per channel
	// Useful for actual sound synthesis
	float Synthesizer_OnGetSample(uint32_t nChannel, double dTime)
	{
		static double last_time = 0.0;
		//synth.Update(nChannel, dTime, dTime - last_time);
		synth.Update(nChannel, dTime, 1.0 / 44100.0);
		last_time = dTime;
		return adsr2.mOutput.value;
	}


	olc::sound::WaveEngine engine;
	olc::sound::synth::ModularSynth synth;
	olc::sound::synth::modules::Oscillator osc1;
	olc::sound::synth::modules::Oscillator osc2;
	ADSREnvelope adsr;
	ADSREnvelope adsr2;
	Pinkifier pink_filter;
	Delay<2000, samplerate> delay;
	Mixer<3> mixer;
	LPF lpf;
	Gain gain;

	int times_called = 0;

	std::string title = "VENUS SIGIL";
	std::array<olc::Pixel, 11> title_colors;
	std::array<float, 11> title_phase;
	std::array<float, 11> title_fmod;

	eMode mode = eMode::START;

	Bolt bolt;

	float fTotalTime = 0.0f;
	float fStateTimer = 0.0f;
	float fIdleThreshold = 4.0f;
	float fIdleThresholdMax = 4.0f;
	float fHintThreshold = 1.0f;
	float fTriggerThreshold = .5f;
	float fShowThreshold = 2.0f;
	float fFadeoutThreshold = 0.3f;


	float fSpeed = 19.0f;

	int bolts_dodged = 0;

	olc::Sprite* canvas;

	olc::vf2d hint_point = { 128, 120 };

	void Reset() {
		fStateTimer = 0.0f;
		fIdleThreshold = fIdleThresholdMax;
		fHintThreshold = 1.0f;
		fTriggerThreshold = .5f;
		fShowThreshold = 2.0f;
		fFadeoutThreshold = 0.3f;
		bolts_dodged = 0;
		mode = eMode::IDLE;
	}

	void DrawTitle(float fElapsedTime) {
		std::string title = "VENUS SIGIL";

		for (int i = 0; i < 11; i++) {
			DrawString(40 + i * 16, 20, std::string{ title[i] }, title_colors[i], 2);

			float s1 = 0.5f * (sin((1 + title_fmod[i]) / 3 * fTotalTime + title_phase[i]) + 1);

			float d_r = 255 * (1.0f - s1 / 7.0f);
			float d_g = 255 * (1.0f - s1 / 7.0f);

			float r = title_colors[i].r  - 255 * (rand_float() / 100.0f) + 255 * (d_r / 3.0f);
			float g = title_colors[i].g - 255 * (rand_float() / 100.0f) + 255 * (d_g / 3.0f);
			title_colors[i].r = d_r;
			title_colors[i].g = d_g;
		}
	}

	//State function for the beginning of the game
	//Provides the goal, controls, and draws the title
	void StartFunction(float fElapsedTime) {
		DrawTitle(fElapsedTime);
		DrawString(44, 50, "Dodge as many bolts of");
		DrawString(48, 60, "Lightning as possible");
		DrawString(68, 70, "W A S D to move");

		olc::Pixel border_color;
		olc::Pixel inner_color;

		olc::vi2d pos = { 97, 117 };
		olc::vi2d size = { 59, 12 };
		olc::vi2d mouse = GetMousePos();
		if (mouse.x >= pos.x && mouse.x <= pos.x + size.x && mouse.y >= pos.y && mouse.y <= pos.y + size.y) {
			border_color = olc::VERY_DARK_BLUE;
			inner_color = olc::DARK_BLUE;
			if (GetMouse(0).bPressed) {
				Reset();
			}
		}
		else {
			border_color = olc::DARK_BLUE;
			inner_color = olc::BLUE;
		}

		FillRect(pos, size, inner_color);
		DrawRect(pos, size, border_color);
		DrawString(108, 120, "Start");

		DrawString(60, 220, "If you played FFX");
		DrawString(72, 230, "I am not sorry");
	}

	//Idle state function
	//After dodging a bolt the player is given a few seconds
	//to prepare for the next bolt.  Nothing happens during
	//this state.
	//At the end of the state, a ne bolt is generated and we
	//proceed to the Hint state
	void IdleFunction(float fElapsedTime) {
		HandleMovement(fElapsedTime);
		if (fStateTimer > fIdleThreshold) {
			fStateTimer -= fIdleThreshold;
			mode = eMode::HINT;
			
			float x1 = rand_float() * ScreenWidth();
			float x2 = rand_float() * ScreenWidth();
			float m_x = rand_float() * 30.0f - 15.0f;
			float m_y = rand_float() * 30.0f - 15.0f;
			olc::vf2d mp = { hint_point.x + m_x, hint_point.y + m_y };

			//bolt = Bolt({ x1, 0.0f }, { x2, (float)ScreenHeight() });
			bolt = Bolt({ x1, 0.0f }, { mp });
			bolt.segments.push_back({ mp, { x2, (float)ScreenHeight() } , white });

			float r = rand_float();
			float limit = r * 6 + 4;

			delay.delay = 0.1 + r * (0.9);
			mixer.amplitude[2] = rand_float();
			gain.gain = r;

			fTriggerThreshold = 0.1f + r / 10.0f;

			for (float i = 0; i < limit; i += 1) {
				bolt.Iterate();
			}

			fIdleThreshold = std::max(.50f, fIdleThresholdMax - bolts_dodged * 0.1f);
		}
	}

	//Hint state function.  Over the hint duration a circle will
	//expand from the player to a radius of 50 units and show any
	//upcoming lightning bolt segments in that radius.
	void HintFunction(float fElapsedTime) {
		HandleMovement(fElapsedTime);
		if (fStateTimer > fHintThreshold) {
			fStateTimer -= fHintThreshold;
			mode = eMode::TRIGGER;
			adsr.Begin();
			adsr2.Begin();
		}

		for (const auto& s : bolt.segments) {
			if ((hint_point - s.start).mag2() < (2500 * fStateTimer)) {
				olc::Pixel c = { s.color.r, s.color.g, s.color.b, (uint8_t)(s.color.a * 0.25f) };
				DrawLine(s.start, s.end, c);
			}
		}
	}

	//Trigger state function.  Scan down the screen from the top
	//quickly and show segments over time.  This gives the appearance
	//of the lightning coming down from the sky
	void TriggerFunction(float fElapsedTime) {
		HandleMovement(fElapsedTime);
		float threshold = ScreenHeight() * (fStateTimer / fTriggerThreshold);
		if (fStateTimer > fTriggerThreshold) {
			fStateTimer -= fTriggerThreshold;
			mode = eMode::SHOW;
		}


		for (const auto& s : bolt.segments) {
			if (s.start.y < threshold) {
				DrawLine(s.start, s.end, s.color);
			}
		}
	}

	//Show state function.  The bolt is fully visible.
	void ShowFunction(float fElapsedTime) {
		HandleMovement(fElapsedTime);
		if (fStateTimer > fShowThreshold) {
			fStateTimer -= fShowThreshold;
			mode = eMode::FADEOUT;
			fShowThreshold = std::max(0.5f, 2.0f - bolts_dodged * 0.007f);

		}
		for (const auto& s : bolt.segments) {
			DrawLine(s.start, s.end, s.color);
		}
	}

	//Fadeout state function.  Draw segments with increasing alpha so it
	//looks like the bolt is fading away.  Any forks or off-shoots will
	//appear to fade before the main bolt.
	void FadeoutFunction(float fElapsedTime) {
		HandleMovement(fElapsedTime);
		float a = std::max(0.0f, 1.0f - fStateTimer / fFadeoutThreshold);

		for (const auto& s : bolt.segments) {
			olc::Pixel c = { s.color.r, s.color.g, s.color.b, (uint8_t)(s.color.a * a)};
			DrawLine(s.start, s.end, c);
		}

		if (fStateTimer > fFadeoutThreshold) {
			fStateTimer -= fFadeoutThreshold;
			bolts_dodged += 1;

			fFadeoutThreshold = std::max(0.075f, 0.3f - bolts_dodged * 0.0011f);
			mode = eMode::IDLE;
		}

	}

	//Die state function.  The player has died and will
	//be given the option to try again.
	void DieFunction(float fElapsedTime) {
		int x;
		DrawString(20, 50, "You have died after dodging");
		if (bolts_dodged < 10) {
			x = 124;
		}
		else if (bolts_dodged < 100) {
			x = 120;
		}
		else {
			x = 116;
		}
		
		DrawString(x, 60, std::to_string(bolts_dodged));
		DrawString(56, 70, "bolts of lightning");

		for (const auto& s : bolt.segments) {
			DrawLine(s.start, s.end, s.color);
		}

		olc::Pixel border_color;
		olc::Pixel inner_color;

		olc::vi2d pos = { 97, 117 };
		olc::vi2d size = { 59, 12 };
		olc::vi2d mouse = GetMousePos();
		if (mouse.x >= pos.x && mouse.x <= pos.x + size.x && mouse.y >= pos.y && mouse.y <= pos.y + size.y) {
			border_color = olc::VERY_DARK_BLUE;
			inner_color = olc::DARK_BLUE;
			if (GetMouse(0).bPressed) {
				Reset();
			}
		}
		else {
			border_color = olc::DARK_BLUE;
			inner_color = olc::BLUE;
		}

		FillRect(pos, size, inner_color);
		DrawRect(pos, size, border_color);
		DrawString(100, 120, "Restart");

	}
	
	//basic movement handling clamped to the edges
	//of the screen
	void HandleMovement(float fElapsedTime) {
		if (GetKey(olc::W).bHeld) {
			hint_point.y -= fSpeed * fElapsedTime;
		}

		if (GetKey(olc::S).bHeld) {
			hint_point.y += fSpeed * fElapsedTime;
		}

		if (GetKey(olc::A).bHeld) {
			hint_point.x -= fSpeed * fElapsedTime;
		}

		if (GetKey(olc::D).bHeld) {
			hint_point.x += fSpeed * fElapsedTime;
		}

		hint_point.x = std::max(4.0f, std::min((float)ScreenWidth() - 5, hint_point.x));
		hint_point.y = std::max(4.0f, std::min((float)ScreenHeight() - 5, hint_point.y));
	}

	bool OnUserUpdate(float fElapsedTime) override
	{
		Clear(olc::BLACK);
		SetPixelMode(olc::Pixel::ALPHA);

		fStateTimer += fElapsedTime;
		fTotalTime += fElapsedTime;

		//Check the state machine
		switch (mode) {
		case eMode::START:
			StartFunction(fElapsedTime);
			break;
		case eMode::IDLE:
			IdleFunction(fElapsedTime);
			break;
		case eMode::HINT:
			HintFunction(fElapsedTime);
			break;
		case eMode::TRIGGER:
			TriggerFunction(fElapsedTime);
			break;
		case eMode::SHOW:
			ShowFunction(fElapsedTime);
			break;
		case eMode::FADEOUT:
			FadeoutFunction(fElapsedTime);
			break;
		case eMode::DIE:
			DieFunction(fElapsedTime);
			break;
		}

		//Determine if we're checking collision or drawing the player
		bool check_collision = (mode == eMode::TRIGGER) || (mode == eMode::SHOW) || (mode == eMode::FADEOUT);
		bool draw_player = !((mode == eMode::START) || (mode == eMode::DIE));
		bool dead = false;

		if (check_collision) {
			for (auto p : collision_points) {
				if (canvas->GetPixel(hint_point + p) != olc::BLACK) {
					dead = true;
				}
			}
		}

		if (draw_player) {
			DrawString(256 - 32, 2, std::to_string(bolts_dodged));
			for (auto p : collision_points) {
				Draw(hint_point + p, olc::GREEN);
			}
		}

		//DrawString(20, 40, std::to_string(adsr.mTotalTime));
		//DrawString(20, 50, std::to_string(times_called));
		//DrawString(20, 60, std::to_string(fTotalTime));
		//DrawString(20, 70, std::to_string(fTotalTime*44100));

		if (dead) {
			mode = eMode::DIE;
		}

		return true;
	}
};

int main()
{
	Example demo;
	if (demo.Construct(256, 240, 4, 4, false, true))
		demo.Start();
	return 0;
}
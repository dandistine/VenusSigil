#define OLC_PGE_APPLICATION
#include "olcPixelGameEngine.h"


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

		return true;
	}

	std::string title = "VENUS SIGIL";
	std::array<olc::Pixel, 11> title_colors;
	std::array<float, 11> title_phase;
	std::array<float, 11> title_fmod;

	eMode mode = eMode::START;

	Bolt bolt;

	float fTotalTime = 0.0f;
	float fStateTimer = 0.0f;
	float fIdleThreshold = 8.0f;
	float fHintThreshold = 1.0f;
	float fTriggerThreshold = .5f;
	float fShowThreshold = 2.0f;
	float fFadeoutThreshold = 0.3f;


	float fSpeed = 15.0f;

	int bolts_dodged = 0;

	olc::Sprite* canvas;

	olc::vf2d hint_point = { 128, 120 };

	void Reset() {
		fStateTimer = 0.0f;
		fIdleThreshold = 8.0f;
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

			bolt = Bolt({ x1, 0.0f }, { x2, (float)ScreenHeight() });

			float r = rand_float();
			float limit = r * 6 + 5;

			fTriggerThreshold = 0.1f + r / 10.0f;

			for (float i = 0; i < limit; i += 1) {
				bolt.Iterate();
			}

			fIdleThreshold = std::max(.50f, 8 - bolts_dodged * 0.3f);
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

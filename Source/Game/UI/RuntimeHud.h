#pragma once
/**
 * @file RuntimeHud.h
 * @brief Non-ImGui runtime HUD state (gauge fill + dialogue line + screen rect anchor)
 */

#include "Core/Common/Types.h"
#include <string>

namespace sw
{
	struct HudGauge
	{
		float32 _fill = 1.0f; // 0..1 material CB style
		float32 _x	  = 0.05f;
		float32 _y	  = 0.05f;
		float32 _w	  = 0.28f;
		float32 _h	  = 0.04f;
	};

	struct ScreenRect
	{
		float32 _x = 0.0f;
		float32 _y = 0.0f;
		float32 _w = 1.0f;
		float32 _h = 1.0f;
	};

	class RuntimeHud
	{
	public:
		RuntimeHud();

		void setScreenRect( float32 x, float32 y, float32 w, float32 h );
		void setDialogue( const std::string& line );
		void clearDialogue();
		void setBattleGauges( float32 playerHpFill, float32 foeHpFill, float32 expFill, float32 ppFill );
		void setFadeOverlay( float32 alpha );
		void setVisible( bool v ) { _bVisible = v ? 1 : 0; }

		bool			 isVisible() const { return _bVisible != 0; }
		const ScreenRect& getScreenRect() const { return _screen; }
		const std::string& getDialogue() const { return _dialogue; }
		const HudGauge&	 getPlayerHp() const { return _playerHp; }
		const HudGauge&	 getFoeHp() const { return _foeHp; }
		const HudGauge&	 getExp() const { return _exp; }
		const HudGauge&	 getPp() const { return _pp; }
		float32			 getFadeAlpha() const { return _fadeAlpha; }

		/** @brief Debug/log snapshot of HUD (render hook TBD — values ready for material CB). */
		void logSnapshot() const;

	private:
		ScreenRect	_screen{};
		HudGauge	_playerHp{ 1.0f, 0.05f, 0.82f, 0.30f, 0.04f };
		HudGauge	_foeHp{ 1.0f, 0.65f, 0.08f, 0.30f, 0.04f };
		HudGauge	_exp{ 0.0f, 0.05f, 0.88f, 0.30f, 0.02f };
		HudGauge	_pp{ 1.0f, 0.05f, 0.78f, 0.18f, 0.03f };
		std::string _dialogue;
		float32		_fadeAlpha = 0.0f;
		uint8		_bVisible : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw

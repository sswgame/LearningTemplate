#include "pch.h"

#include "GameFramework/UI/RuntimeHud.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	namespace
	{
		static hashed_string keyPlayerHp()
		{
			static const hashed_string k{ "hud.player_hp" };
			return k;
		}

		static hashed_string keyFoeHp()
		{
			static const hashed_string k{ "hud.foe_hp" };
			return k;
		}

		static hashed_string keyDash()
		{
			static const hashed_string k{ "hud.dash" };
			return k;
		}

		static hashed_string keyFade()
		{
			static const hashed_string k{ "hud.fade" };
			return k;
		}

		static hashed_string keyAction()
		{
			static const hashed_string k{ "hud.action_mode" };
			return k;
		}

		static hashed_string keyDialogue()
		{
			static const hashed_string k{ "hud.dialogue" };
			return k;
		}
	} // namespace

	RuntimeHud::RuntimeHud()
		: _bVisible{ 1 }
		, _reserved{ 0 } {}

	void RuntimeHud::setScreenRect( float32 x, float32 y, float32 w, float32 h )
	{
		_screen._x = x;
		_screen._y = y;
		_screen._w = w;
		_screen._h = h;
	}

	void RuntimeHud::setDialogue( string_view line )
	{
		if ( _dialogue == line )
			return;
		_dialogue.assign( line );
	}

	void RuntimeHud::clearDialogue()
	{
		_dialogue.clear();
	}

	void RuntimeHud::setBattleGauges( float32 playerHpFill, float32 foeHpFill, float32 expFill, float32 ppFill )
	{
		_playerHp._fill = playerHpFill;
		_foeHp._fill	= foeHpFill;
		_exp._fill		= expFill;
		_pp._fill		= ppFill;
	}

	void RuntimeHud::setActionGauges( float32 playerHpFill, float32 bossHpFill, float32 dashFill )
	{
		_playerHp._fill = playerHpFill;
		_foeHp._fill	= bossHpFill;
		_exp._fill		= 0.0f;
		_pp._fill		= dashFill;
	}

	void RuntimeHud::setFadeOverlay( float32 alpha )
	{
		_fadeAlpha = alpha < 0.0f ? 0.0f : ( alpha > 1.0f ? 1.0f : alpha );
	}

	void RuntimeHud::publishSnapshot( bool actionMode ) const
	{
		game::getService<DebugOverlayState>()->_bVisible = _bVisible != 0;
		game::getService<DebugOverlayState>()->setFloat( keyPlayerHp(), _playerHp._fill );
		game::getService<DebugOverlayState>()->setFloat( keyFoeHp(), _foeHp._fill );
		game::getService<DebugOverlayState>()->setFloat( keyDash(), _pp._fill );
		game::getService<DebugOverlayState>()->setFloat( keyFade(), _fadeAlpha );
		game::getService<DebugOverlayState>()->setFloat( keyAction(), actionMode ? 1.0f : 0.0f );
		if ( _dialogue.empty() )
			game::getService<DebugOverlayState>()->setString( keyDialogue(), {} );
		else
			game::getService<DebugOverlayState>()->setString( keyDialogue(), _dialogue );
	}

	void RuntimeHud::logSnapshot() const
	{
		if ( _dialogue.empty() == false || _fadeAlpha > 0.01f )
		{
			SW_LOG_INFO( "[RuntimeHud] rect=(%#,%# %#x%#) fade=%# hp=%#/%# dlg='%#'",
						 _screen._x, _screen._y, _screen._w, _screen._h,
						 _fadeAlpha, _playerHp._fill, _foeHp._fill, _dialogue );
		}
	}
} // namespace sw

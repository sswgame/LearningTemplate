#include "pch.h"

#include "GameFramework/UI/RuntimeHud.h"

#include "Engine/Utility/Debug/DebugOverlayState.h"

#include "GameFramework/Base/GameService.h"

namespace sw
{
	namespace
	{
		struct RuntimeHudInternal
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
		};
	} // namespace
} // namespace sw

namespace sw
{
	SW_LOG_CALLER( "RuntimeHud" );

	RuntimeHud::RuntimeHud()
		: _screen{}
		, _playerHp{ 1.0f, 0.05f, 0.82f, 0.30f, 0.04f }
		, _foeHp{ 1.0f, 0.65f, 0.08f, 0.30f, 0.04f }
		, _exp{ 0.0f, 0.05f, 0.88f, 0.30f, 0.02f }
		, _pp{ 1.0f, 0.05f, 0.78f, 0.18f, 0.03f }
		, _dialogue{}
		, _fadeAlpha{ 0.0f }
		, _bVisible{ SW_TRUE }
		, _reserved{ 0 }
	{
	}

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
		DebugOverlayState* pOverlay = game::getService<DebugOverlayState>();
		if ( pOverlay == nullptr )
			return;

		pOverlay->_bVisible = _bVisible;
		pOverlay->setFloat( RuntimeHudInternal::keyPlayerHp(), _playerHp._fill );
		pOverlay->setFloat( RuntimeHudInternal::keyFoeHp(), _foeHp._fill );
		pOverlay->setFloat( RuntimeHudInternal::keyDash(), _pp._fill );
		pOverlay->setFloat( RuntimeHudInternal::keyFade(), _fadeAlpha );
		pOverlay->setFloat( RuntimeHudInternal::keyAction(), actionMode ? 1.0f : 0.0f );
		if ( _dialogue.empty() )
			pOverlay->setString( RuntimeHudInternal::keyDialogue(), {} );
		else
			pOverlay->setString( RuntimeHudInternal::keyDialogue(), _dialogue );
	}

	void RuntimeHud::logSnapshot() const
	{
		if ( _dialogue.empty() == false || _fadeAlpha > 0.01f )
		{
			SW_LOG_TRACE( "rect=(%#,%# %#x%#) fade=%# hp=%#/%# dlg='%#'",
						  _screen._x, _screen._y, _screen._w, _screen._h,
						  _fadeAlpha, _playerHp._fill, _foeHp._fill, _dialogue );
		}
	}
} // namespace sw

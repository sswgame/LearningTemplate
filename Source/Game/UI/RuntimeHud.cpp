/**
 * @file RuntimeHud.cpp
 */
#include "RuntimeHud.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	RuntimeHud::RuntimeHud()
		: _bVisible{ 1 }
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

	void RuntimeHud::setDialogue( const std::string& line )
	{
		_dialogue = line;
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

	void RuntimeHud::setFadeOverlay( float32 alpha )
	{
		_fadeAlpha = alpha < 0.0f ? 0.0f : ( alpha > 1.0f ? 1.0f : alpha );
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

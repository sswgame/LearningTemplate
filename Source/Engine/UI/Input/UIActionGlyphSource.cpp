#include "pch.h"

#include "Engine/UI/Input/UIActionGlyphSource.h"

#include "Core/String/hashed_string.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    string UIActionGlyphSource::findGlyph( string_view action ) const
    {
        const hashed_string   name( action );
        const InputGlyphStyle style = getStyle();
        if ( _pInput != nullptr && _pInput->getInputMap().getActionHandle( name ).isValid() )
            return _pInput->getInputMap().getGlyphForAction( name, style );
        if ( _pUIInputMap != nullptr && _pUIInputMap->getActionHandle( name ).isValid() )
            return _pUIInputMap->getGlyphForAction( name, style );
        return "[ ? ]";
    }

    InputGlyphStyle UIActionGlyphSource::getStyle() const
    {
        return _pInput != nullptr ? _pInput->getActiveGlyphStyle() : InputGlyphStyle::KeyboardMouse;
    }
} // namespace sw

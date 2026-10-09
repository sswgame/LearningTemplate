#include "pch.h"

#include "Engine/UI/Input/UiActionGlyphSource.h"

#include "Core/String/hashed_string.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    string UiActionGlyphSource::findGlyph( string_view action ) const
    {
        const hashed_string   name( action );
        const InputGlyphStyle style = getStyle();
        if ( _pInput != nullptr && _pInput->getInputMap().getActionHandle( name ).isValid() )
            return _pInput->getInputMap().getGlyphForAction( name, style );
        if ( _pUiInputMap != nullptr && _pUiInputMap->getActionHandle( name ).isValid() )
            return _pUiInputMap->getGlyphForAction( name, style );
        return "[ ? ]";
    }

    InputGlyphStyle UiActionGlyphSource::getStyle() const
    {
        return _pInput != nullptr ? _pInput->getActiveGlyphStyle() : InputGlyphStyle::KeyboardMouse;
    }
} // namespace sw

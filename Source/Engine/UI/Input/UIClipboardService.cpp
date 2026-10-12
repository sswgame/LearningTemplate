#include "pch.h"

#include "Engine/UI/Input/UIClipboardService.h"

namespace sw
{
    UIClipboardService::UIClipboardService()
        : _api{}
        , _text{}
    {
    }

    void UIClipboardService::setAPI( const UIClipboardAPI& api )
    {
        _api = api;
    }

    void UIClipboardService::setText( string_view text )
    {
        _text = string{ text };
        if ( _api._pWrite != nullptr )
            _api._pWrite( text );
    }

    bool UIClipboardService::tryGetText( string& outText ) const
    {
        if ( _api._pRead != nullptr )
        {
            string fromSystem{};
            if ( _api._pRead( fromSystem ) && fromSystem.empty() == false )
            {
                outText = std::move( fromSystem );
                return true;
            }
        }
        if ( _text.empty() )
            return false;
        outText = _text;
        return true;
    }
} // namespace sw

#include "pch.h"

#include "Engine/UI/Screen/UiScreen.h"

#include "Engine/UI/Core/Widget.h"
#include "Engine/UI/UiSystem.h"

namespace sw
{
    UiScreen::UiScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : _tree{}
        , _desc{ desc }
        , _pUiSystem{ nullptr }
        , _handle{ kInvalidUiScreenHandle }
        , _lastFocused{ kInvalidWidgetId }
        , _pushOrder{ 0 }
        , _bClosing{ SW_FALSE }
    {
        _tree.setRoot( std::move( root ) );
    }

    UiScreen::~UiScreen() = default;

    bool UiScreen::takesFocus() const
    {
        return _desc._bTakesFocus && _desc._layer != UiLayer::Hud && _desc._layer != UiLayer::Overlay;
    }

    void UiScreen::close()
    {
        if ( _pUiSystem != nullptr )
            _pUiSystem->closeScreen( _handle );
        else
            _bClosing = SW_TRUE;
    }

    bool UiScreen::onBack()
    {
        close();
        return true;
    }
} // namespace sw

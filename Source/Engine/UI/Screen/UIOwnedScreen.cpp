#include "pch.h"

#include "Engine/UI/Screen/UIOwnedScreen.h"

#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/UISystem.h"

namespace sw
{
    UIOwnedScreen::UIOwnedScreen() noexcept
        : _closeSubscription{}
        , _pUISystem{ nullptr }
        , _handle{ kInvalidUIScreenHandle }
    {
    }

    UIOwnedScreen::UIOwnedScreen( UISystem& ui, UIScreenHandle handle )
        : UIOwnedScreen{}
    {
        if ( handle == kInvalidUIScreenHandle )
            return;
        _closeSubscription = ui.ownScreen( handle );
        _pUISystem         = &ui;
        _handle            = handle;
    }

    UIOwnedScreen::~UIOwnedScreen() = default;

    UIOwnedScreen::UIOwnedScreen( UIOwnedScreen&& other ) noexcept
        : _closeSubscription{ std::move( other._closeSubscription ) }
        , _pUISystem{ other._pUISystem }
        , _handle{ other._handle }
    {
        other._handle = kInvalidUIScreenHandle;
    }

    UIOwnedScreen& UIOwnedScreen::operator=( UIOwnedScreen&& other ) noexcept
    {
        if ( this != &other )
        {
            _closeSubscription = std::move( other._closeSubscription ); // 지금 화면을 닫는다
            _pUISystem         = other._pUISystem;
            _handle            = other._handle;
            other._handle      = kInvalidUIScreenHandle;
        }
        return *this;
    }

    void UIOwnedScreen::reset() noexcept
    {
        _closeSubscription.reset();
        _handle = kInvalidUIScreenHandle;
    }

    UIScreen* UIOwnedScreen::find() const
    {
        UIScreen* const pScreen = _closeSubscription.isActive() ? _pUISystem->findScreen( _handle ) : nullptr;
        return pScreen != nullptr && pScreen->isClosing() == false ? pScreen : nullptr;
    }
} // namespace sw

#include "pch.h"

#include "Engine/UI/Screens/SettingsConfirmScreen.h"

#include "Engine/UI/UiSystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

namespace sw
{
    SettingsConfirmViewModel::SettingsConfirmViewModel()
        : UiViewModel{}
        , _secondsLeft{ 0.0f }
    {
    }

    SettingsConfirmViewModel::~SettingsConfirmViewModel() = default;

    const TypeInfo* SettingsConfirmViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void SettingsConfirmViewModel::setSecondsLeft( float32 secondsLeft )
    {
        (void)setField( _secondsLeft, secondsLeft, "_secondsLeft" );
    }

    SettingsConfirmScreen::SettingsConfirmScreen( const UiScreenDesc& desc, unique_ptr<Widget> root )
        : UiScreen{ desc, std::move( root ) }
        , _viewModel{}
        , _pSettings{ nullptr }
    {
        setViewModel( &_viewModel );
    }

    SettingsConfirmScreen::~SettingsConfirmScreen()
    {
        setViewModel( nullptr );
    }

    UiScreenHandle SettingsConfirmScreen::open( UiSystem& ui, UserSettingsManager& settings, string_view documentPath )
    {
        const UiScreenHandle handle  = ui.openScreen<SettingsConfirmScreen>( documentPath );
        UiScreen*            pScreen = ui.findScreen( handle );
        if ( pScreen == nullptr )
            return kInvalidUiScreenHandle;
        SettingsConfirmScreen& screen = static_cast<SettingsConfirmScreen&>( *pScreen );
        screen._pSettings             = &settings;
        screen._viewModel.setSecondsLeft( settings.getConfirmSecondsLeft() );
        return handle;
    }

    bool SettingsConfirmScreen::onCommand( const hashed_string& command, Widget& source )
    {
        if ( command == hashed_string( "Keep" ) )
        {
            if ( _pSettings != nullptr )
                _pSettings->confirmChanges();
            close();
            return true;
        }
        if ( command == hashed_string( "Revert" ) )
        {
            revertNow();
            return true;
        }
        return UiScreen::onCommand( command, source );
    }

    bool SettingsConfirmScreen::onBack()
    {
        revertNow();
        return true;
    }

    void SettingsConfirmScreen::onTick( float32 deltaSeconds )
    {
        (void)deltaSeconds;
        // 시간은 매니저가 센다(호스트의 update) — 확인 대기가 끝나면(유지 · 시간 다 됨 · 다른 곳의 되돌림) 창도 끝이다.
        if ( _pSettings == nullptr || _pSettings->isAwaitingConfirm() == false )
        {
            close();
            return;
        }
        _viewModel.setSecondsLeft( _pSettings->getConfirmSecondsLeft() );
    }

    void SettingsConfirmScreen::revertNow()
    {
        if ( _pSettings != nullptr && _pSettings->isAwaitingConfirm() )
            _pSettings->update( _pSettings->getConfirmSecondsLeft() );
        close();
    }
} // namespace sw

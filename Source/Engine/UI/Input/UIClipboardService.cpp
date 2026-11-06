#include "pch.h"

#include "Engine/UI/Input/UIClipboardService.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

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

    bool UIClipboardService::setText( string_view text )
    {
        if ( _api._pWrite != nullptr )
            return _api._pWrite( text );
        _text = string{ text };
        return true;
    }

    bool UIClipboardService::tryGetText( string& outText ) const
    {
        // 운영체제 연결이 있으면 그쪽이 기준이다 — 비었으면(다른 프로그램이 비웠다) 엔진 안의 옛 글을 대신 내지 않는다.
        if ( _api._pRead != nullptr )
        {
            string fromSystem{};
            if ( _api._pRead( fromSystem ) == false || fromSystem.empty() )
                return false;
            outText = std::move( fromSystem );
            return true;
        }
        if ( _text.empty() )
            return false;
        outText = _text;
        return true;
    }
} // namespace sw

namespace sw
{
    // 화면 스택에 매이지 않는 UI 서비스는 따로 선 서브시스템이다 — UI 를 위한 범위를 따로 두지 않는다(언리얼 UEngineSubsystem · Godot DisplayServer 의 클립보드).
    SW_ENGINE_SERVICE_SUBSYSTEM( UIClipboardService, GameVisible, Startup, (), SW_ENGINE_DEFAULT_FACTORY( UIClipboardService ) );
} // namespace sw

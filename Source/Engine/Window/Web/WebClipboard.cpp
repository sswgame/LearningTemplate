/**
 * @file WebClipboard.cpp
 * @brief 웹(브라우저) 클립보드입니다. 브라우저의 읽기 API(`navigator.clipboard.readText`)는 비동기 · 권한 요청이라 프레임 안에서 동기로 받을 수 없다 —
 *        문서의 `paste` 이벤트가 준 글을 담아 두고 읽기가 그것을 돌려준다(Ctrl+V 의 keydown 과 paste 는 다음 프레임 전에 함께 온다 — WebWindow::onKey 는 글쇠의 기본 동작을 막지 않는다).
 * @details 쓰기는 `navigator.clipboard.writeText`(사용자 입력 직후에만 받는다 — 거절은 콘솔 경고)이고, 페이지 안 사본도 같은 글로 바꿔 게임 안 복사 → 붙여넣기는 OS 가 거절해도 된다.
 *          유니티 WebGL 의 클립보드 플러그인들이 같은 paste 이벤트 방식이다(기억). 메인(브라우저) 스레드에서만 부른다 — 웹은 렌더 스레드를 끈다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"

#include "Engine/Window/PlatformClipboard.h"

#if defined( SW_PLATFORM_WEB )

// 브라우저 쪽 본문은 WebClipboard.js(Emscripten JS 라이브러리)다.
extern "C"
{
    int   swWebClipboardInstall();
    char* swWebClipboardRead();
    int   swWebClipboardWrite( const char* pText );
}

namespace sw
{
    bool PlatformClipboard::isAvailable()
    {
        return swWebClipboardInstall() != 0;
    }

    bool PlatformClipboard::readText( string& outText )
    {
        outText.clear();
        utf8* const pText = swWebClipboardRead();
        if ( pText == nullptr )
            return false; // 아직 붙여넣은 글이 없다
        outText = pText;
        free( pText );
        return true;
    }

    bool PlatformClipboard::writeText( string_view text )
    {
        const string textCopy( text );
        if ( swWebClipboardWrite( textCopy.c_str() ) == 0 )
        {
            SW_LOG_WARNING_ID( "WebClipboard.WriteUnavailable", "The browser has no clipboard write API (navigator.clipboard) - the text stays only inside the page" );
            return false;
        }
        return true;
    }
} // namespace sw
#endif

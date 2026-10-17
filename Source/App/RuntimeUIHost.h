/**
 * @file RuntimeUIHost.h
 * @brief 런타임 UI 에 호스트(게임 창 실행 파일)만 아는 것을 끼웁니다 — 일시정지 메뉴의 "끝내기"(`QuitGame`)와 운영체제 클립보드입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Engine;
    class Widget;

    /**
     * @class RuntimeUIHost
     * @brief App 이 기동 뒤 한 번 붙이고 엔진을 내리기 전에 뗍니다.
     * @details **끝내기.** 에디터 없이 뜬 게임 창이면 UI 전역 명령 `QuitGame` 을 등록해 일시정지 메뉴에 "끝내기" 를 보인다. 누르면 `Engine::requestQuit( 0 )` 으로
     *          이번 프레임 뒤 루프를 끝낸다. 에디터 안에서는 등록하지 않는다(게임을 끝내는 것은 에디터의 플레이 중지다 — 언리얼 PIE 의 Quit 이 PIE 만 끝내는 것과 같은 자리).
     *          **클립보드.** 플랫폼이 운영체제 클립보드를 이으면(`PlatformClipboard::isAvailable`) 글 입력 칸의 복사 · 붙여넣기가 다른 프로그램과 오간다.
     */
    class RuntimeUIHost
    {
    public:
        RuntimeUIHost();

        /** @brief UI 서비스가 있으면 끝내기 명령과 클립보드를 끼웁니다. @p bEnableEditor 이면 끝내기는 끼우지 않습니다. */
        void initialize( Engine* pEngine, bool bEnableEditor );
        /** @brief 끼운 것을 뗍니다. 엔진 서비스가 내려가기 전에 부릅니다. */
        void shutdown();

    private:
        /** @brief UI 전역 명령 `QuitGame` 의 처리 함수입니다. */
        void onQuitGameCommand( const hashed_string& command, Widget& source );

    private:
        Engine* _pEngine;
    };
} // namespace sw

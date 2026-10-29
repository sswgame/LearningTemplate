/**
 * @file RuntimeUIHost.h
 * @brief 런타임 UI 에 호스트(게임 창 실행 파일)만 아는 것을 끼웁니다 — 운영체제 클립보드, 스크린 리더 연결, 에디터 없이 뜬 게임 창이면 일시정지 메뉴의 "끝내기".
 * @details App 의 호스트 서비스 `RuntimeUIHooks` 가 세우고 내리는 협력 객체다(언리얼 GameViewportClient 가 플랫폼 클립보드 · 접근성 · 종료 요청을 UI 에 잇는 자리).
 *          에디터 안에서는 끝내기 · 스크린 리더를 걸지 않는다(게임을 끝내는 것은 에디터의 플레이 중지다 — 언리얼 PIE 의 Quit 이 PIE 만 끝내는 것과 같은 자리).
 *          클립보드는 플랫폼이 운영체제 클립보드를 이으면(`PlatformClipboard::isAvailable`) 글 입력 칸의 복사 · 붙여넣기가 다른 프로그램과 오간다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class Engine;
    class IWindow;
    class Widget;

    /**
     * @class RuntimeUIHost
     * @brief 런타임 UI 의 호스트 훅입니다. 파일 머리말 참고.
     */
    class RuntimeUIHost
    {
    public:
        RuntimeUIHost();

        /**
         * @brief 훅을 끼웁니다. UI 서비스가 없으면(헤드리스) 아무 일도 하지 않습니다.
         * @param engine 끝내기 명령이 루프를 끝낼 엔진(`Engine::requestQuit`). 이 객체보다 오래 산다.
         * @param pWindow 스크린 리더가 접근성 요청을 받을 주 창. 없으면 연결하지 않는다.
         * @param bEditor 에디터 안이면 끝내기 · 스크린 리더를 걸지 않는다.
         */
        void install( Engine& engine, IWindow* pWindow, bool bEditor );
        /** @brief `install` 이 끼운 것을 뗍니다. 엔진 서비스와 창이 내려가기 전에 부릅니다. */
        void remove();

    private:
        /** @brief UI 전역 명령 `QuitGame` 의 처리 함수입니다. 이번 프레임 뒤 루프를 끝냅니다(`Engine::requestQuit( 0 )`). */
        void onQuitGameCommand( const hashed_string& command, Widget& source );

        Engine*      _pEngine;
        Subscription _quitCommandRegistration; ///< UI 전역 명령 `QuitGame` 의 구독(`remove` 가 놓는다)
    };
} // namespace sw

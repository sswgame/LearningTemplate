/**
 * @file CallbackInputWindow.h
 * @brief 입력을 콜백으로 받는 플랫폼 창(웹 · Android)의 공통 바탕입니다. 콜백이 받은 입력을 모았다가 `processMessages` 에서 창 처리기로 한 번에 넘깁니다.
 * @details 데스크톱 창은 메시지를 끌어 읽지만(`PeekMessage` · `XNextEvent`), 브라우저와 Android 는 입력을 밀어 준다(DOM 콜백 · `onInputEvent`). 받은 자리에서 바로 엔진에 넘기면
 *          프레임 중간(렌더 · 갱신 도중)에 입력이 끼어든다 — 그래서 모았다가 메시지 처리 시점에 넘긴다(SDL 의 이벤트 큐와 같은 모양, 기억).
 *          수명 이벤트(표면 · 앱 멈춤)도 같은 곳에서 낸다. 크기 변경은 `applyPlatformResize` 하나로 모은다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/PlatformInputEvent.h"

namespace sw
{
    /** @class CallbackInputWindow @brief 콜백 입력 창의 바탕입니다. 파생은 콜백에서 `queueInput` · `applyPlatformResize` · `notifyLifecycleEvent` 를 부른다. */
    class SW_API CallbackInputWindow : public IWindow
    {
    public:
        CallbackInputWindow();
        ~CallbackInputWindow() override;

        /** @brief 모은 입력을 창 처리기(`bindCustomMessageHandler` — 입력 관리자 · ImGui)로 넘기고 비웁니다. 닫기를 청했으면 false 입니다. */
        bool processMessages() override;
        /** @brief 콜백 창은 기다릴 것이 없다 — 브라우저 · ALooper 가 다음 프레임을 부른다. */
        void waitForMessages( uint32 timeoutMilliseconds ) override { (void)timeoutMilliseconds; }
        /** @brief 틀이 없다(전체 화면 표면) — 두께 0 입니다. */
        [[nodiscard]] bool tryGetFrameExtents( WindowFrameExtents& outExtents ) const override;
        /** @brief 놓을 자리가 없다(표면은 화면 · 캔버스를 채운다). */
        [[nodiscard]] bool canPlaceWindow() const override { return false; }
        [[nodiscard]] bool setFramePosition( int32 x, int32 y ) override;
        float4             getSafeAreaInsets() const override { return _safeInsets; }
        float32            getContentScale() const override { return _contentScale; }

    protected:
        /** @brief 콜백이 받은 입력 하나를 모읍니다(창 스레드 — 브라우저 메인 · ALooper 스레드). */
        void queueInput( const PlatformInputEvent& event ) { _listPendingInput.push_back( event ); }
        /** @brief 표면 크기 · 배율 · 안전 여백이 바뀌었습니다. 크기가 달라졌으면 크기 콜백(스왑체인 · UI)을 부릅니다 — 회전도 이 길이다. */
        void applyPlatformResize( uint32 width, uint32 height, float32 contentScale, const float4& safeInsets );

    private:
        vector<PlatformInputEvent> _listPendingInput;
        float4                     _safeInsets;
        float32                    _contentScale;
    };
} // namespace sw

/**
 * @file CallbackInputWindow.h
 * @brief 입력을 콜백으로 받는 플랫폼 창(웹 · Android)의 공통 바탕입니다. 콜백이 받은 입력을 모았다가 `processMessages` 에서 창 처리기로 한 번에 넘깁니다.
 * @details 데스크톱 창은 메시지를 끌어 읽지만(`PeekMessage` · `XNextEvent`), 브라우저와 Android 는 입력을 밀어 준다(DOM 콜백 · `onInputEvent`). 받은 자리에서 바로 엔진에 넘기면
 *          프레임 중간(렌더 · 갱신 도중)에 입력이 끼어든다 — 그래서 모았다가 메시지 처리 시점에 넘긴다(SDL 의 이벤트 큐와 같은 모양, 기억).
 *          수명 이벤트(표면 · 앱 멈춤)도 같은 곳에서 낸다. 크기 변경은 `applyPlatformResize` 하나로 모은다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Window/IWindow.h"
#include "Engine/Window/PlatformInputEvent.h"
#include "Engine/Window/WindowMonitor.h"
#include "Engine/Window/WindowTextInput.h"

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
        /** @brief 표면은 화면 · 캔버스를 채운다 — 숨은 상태는 앱 멈춤(`isRenderPaused`)이 말한다. */
        bool isVisible() const override { return true; }
        /** @brief 모니터를 물을 곳이 없다(브라우저 · Android 는 표면 하나다). */
        [[nodiscard]] bool tryGetMonitors( vector<WindowMonitor>& outListMonitor ) const override;
        /** @brief 청하지 않는다 — 브라우저 · 액티비티의 초점은 사용자 입력과 OS 가 정한다. */
        [[nodiscard]] bool requestInputFocus() override { return false; }
        /** @brief 창이 잇지 않는다 — 웹 · Android 클립보드는 `PlatformClipboard`(WebClipboard · AndroidClipboard)가 창 없이 연다. */
        [[nodiscard]] bool supportsClipboard() const override { return false; }
        [[nodiscard]] bool readClipboardText( string& outText ) override
        {
            outText.clear();
            return false;
        }
        [[nodiscard]] bool writeClipboardText( string_view text ) override
        {
            (void)text;
            return false;
        }
        /** @brief 바꾸지 못한다 — 표면 크기는 페이지(CSS) · 기기가 정한다. */
        bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height ) override;
        /** @brief 최소화는 앱 멈춤(탭 숨김 · onPause)이고 앱이 청하지 못한다. */
        [[nodiscard]] bool canMinimize() const override { return false; }
        bool               setMinimized( bool bMinimized ) override
        {
            (void)bMinimized;
            return false;
        }
        /** @brief 포커스 변화를 플랫폼 콜백과 같은 길(`applyPlatformFocus` → 포커스 입력)로 낸다 — 자동화가 웹 · Android 의 포커스 경로를 본다. */
        [[nodiscard]] bool sendFocusEvent( bool bFocused ) override
        {
            applyPlatformFocus( bFocused );
            return true;
        }

    protected:
        /** @brief 콜백이 받은 입력 하나를 모읍니다(창 스레드 — 브라우저 메인 · ALooper 스레드). */
        void queueInput( const PlatformInputEvent& event ) { _listPendingInput.push_back( event ); }
        /**
         * @brief 콜백이 받은 조합 · 확정 글 하나를 모읍니다(웹 숨은 글 칸의 `compositionupdate` · `input`, Android 키의 유니코드).
         * @details `processMessages` 가 앞뒤 입력과 받은 차례대로 데스크톱 창과 같은 글 메시지(`dispatchTextInput` → `kMessageTextInput`)로 넘긴다.
         *          글은 길이 제한 없이 담는다 — 플랫폼 입력 한 칸에 실으면 고정 크기에서 잘린다(감사 P-4).
         */
        void queueTextInput( WindowTextInputKind kind, string_view text );
        /**
         * @brief 창 포커스가 바뀌었습니다. 상태(`hasFocus` — 프레임 페이싱)를 적고, 값이 바뀌었으면 입력 관리자가 받을 포커스 입력을 모읍니다.
         * @details 상태만 적으면 입력 관리자가 몰라 포커스를 잃기 전에 눌린 키가 눌린 채 남는다(감사 P-3). 그래서 콜백 창에서는 `setFocusState` 를 숨기고 이것만 연다.
         */
        void applyPlatformFocus( bool bFocused );
        /** @brief 콜백 창의 포커스는 `applyPlatformFocus` 로만 바꾼다 — 상태만 바꾸고 입력을 빠뜨리는 길을 형식으로 막는다. */
        void setFocusState( bool bFocused ) = delete;
        /** @brief 표면 크기 · 배율 · 안전 여백이 바뀌었습니다. 크기가 달라졌으면 프레임 시작에 크기 콜백(스왑체인 · UI)이 불린다(`IWindow::notifyResized`) — 회전도 이 길이다. */
        void applyPlatformResize( uint32 width, uint32 height, float32 contentScale, const float4& safeInsets );

    private:
        /** @brief 모은 글 하나와 그 글이 온 자리(그때까지 모인 입력 수)입니다. 입력 큐를 넓히지 않고 차례만 기억한다. */
        struct PendingText
        {
            size_t              _inputPosition{ 0 };
            WindowTextInputKind _kind{ WindowTextInputKind::Commit };
            string              _text;
        };

        vector<PlatformInputEvent> _listPendingInput;
        vector<PendingText>        _listPendingText;
        float4                     _safeInsets;
        float32                    _contentScale;
    };
} // namespace sw

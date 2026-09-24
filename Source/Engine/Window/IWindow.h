/**
 * @file IWindow.h
 * @brief 플랫폼 독립적인 창(OS 디스플레이 창) 생성과 메시지 처리 인터페이스입니다.
 */
#pragma once
#include "Engine/Common/IRenderSurface.h"
#include "Engine/EngineMinimal.h"

namespace sw
{
    struct NativeWindowEvent;

    SW_DECLARE_DELEGATE( bool, WindowMessageHandlerDelegate, const NativeWindowEvent& event );
    SW_DECLARE_DELEGATE( void, WindowResizeDelegate, uint32 width, uint32 height );
    SW_DECLARE_DELEGATE( bool, WindowCloseQueryDelegate, void );

    /**
     * @class IWindow
     * @brief 애플리케이션의 주 화면이나 보조 화면을 추상화하는 기본 인터페이스입니다.
     * @details 플랫폼별(Windows, Linux, macOS 등) 구체 클래스가 이 인터페이스를 상속해 구현합니다.
     *          `IRenderSurface` 를 구현하므로 RHI 는 창을 **표면으로만** 봅니다. `Graphics` 가 `Window` 를
     *          include 하지 않는 이유입니다(Engine/Common/IRenderSurface.h).
     */
    class SW_API IWindow : public IRenderSurface
    {
    public:
        /** @brief 빈 창 인터페이스로 만듭니다. */
        IWindow();
        /** @brief 가상 소멸자입니다. 활성 창이면 전역 포인터도 끊습니다. */
        ~IWindow() override;
        /** @brief 복사를 금지합니다. */
        IWindow( const IWindow& ) = delete;
        /** @brief 대입을 금지합니다. */
        IWindow& operator=( const IWindow& ) = delete;
        /** @brief 이동을 금지합니다. */
        IWindow( IWindow&& ) = delete;
        /** @brief 이동 대입을 금지합니다. */
        IWindow& operator=( IWindow&& ) = delete;

        /**
         * @brief 창을 만듭니다. Win32 · X11 은 여기서 띄우지 않으므로 `showWindow( true )` 를 불러야 보입니다.
         * @param pTitle 창 캡션(제목 줄)
         * @param width 클라이언트 영역 가로 크기
         * @param height 클라이언트 영역 세로 크기
         * @return 만들었으면 true 입니다.
         */
        virtual bool initializeWindow( const utf8* pTitle, uint32 width, uint32 height ) = 0;

        /** @brief 창 리소스를 놓고 화면에서 없앱니다. */
        virtual void destroy() = 0;

        /**
         * @brief 같은 크기 · 제목 · 위치 · 표시 상태로 네이티브 창을 다시 만듭니다(OpenGL↔DXGI 핫스왑용).
         *
         * @details **절차는 여기 한 벌뿐입니다.** 예전에는 이 순서가 Win32 · X11 · 그리고 이 기반 클래스에
         *          **세 벌** 있었고, 기반의 것이 조용히 모자랐습니다. `_bRecreating` 을 세우지 않아 다시
         *          만드는 동안의 리사이즈 · 닫기 통보가 그대로 새어 나갔고, 무엇보다 **보이던 창을 다시
         *          보이게 하지 않았습니다.** 백엔드 교체(`RHI::recreateDevice` → `recreateSurface`)가 부르는
         *          자리라, 그 길로 들어온 창은 교체 뒤 사라졌습니다. 플랫폼이 정말로 다른 두 가지는 아래 훅으로 뺐습니다.
         *
         * @note 플랫폼이 이 기능을 지원하지 않으면(macOS) 이 함수를 재정의해 `false` 를 반환합니다.
         *       재정의해서 **절차를 다시 적지는 마십시오.** 그것이 세 벌이 생긴 경위입니다.
         */
        virtual bool recreate();

        /**
         * @brief 플랫폼 메시지 루프를 한 번 돕니다. 매 프레임 불러야 합니다.
         * @return 계속 실행해야 하면 true, 종료 요청(WM_CLOSE 등)이 있으면 false 입니다.
         */
        virtual bool processMessages() = 0;

        /** @brief 네이티브 윈도우 핸들을 반환합니다. */
        virtual void* getNativeHandle() const = 0;
        /** @brief 네이티브 디스플레이 연결을 반환합니다. 없으면 nullptr. */
        virtual void* getNativeDisplay() const { return nullptr; }
        /** @brief 클라이언트 너비를 반환합니다. */
        virtual uint32 getWidth() const { return _width; }
        /** @brief 클라이언트 높이를 반환합니다. */
        virtual uint32 getHeight() const { return _height; }

        // ------------------------------------------------------------------------------
        // IRenderSurface: RHI 가 창 시스템을 모르는 채로 묻는 다섯 가지
        // ------------------------------------------------------------------------------
        void*  getSurfaceHandle() const override { return getNativeHandle(); }
        void*  getSurfaceDisplay() const override { return getNativeDisplay(); }
        uint32 getSurfaceWidth() const override { return getWidth(); }
        uint32 getSurfaceHeight() const override { return getHeight(); }
        bool   recreateSurface() override { return recreate(); }
        /**
         * @brief 창을 화면에 띄우거나 숨깁니다. **"보이기로 했다" 는 사실을 기억합니다.**
         * @details 이것은 가상이 아닙니다. 의도를 기록하는 일을 플랫폼이 빠뜨릴 수 없게 합니다.
         *          실제로 창을 띄우고 내리는 일만 `applyWindowVisibility` 로 내려갑니다.
         */
        void showWindow( bool bShow )
        {
            _bVisibleIntent = bShow ? SW_TRUE : SW_FALSE;
            applyWindowVisibility( bShow );
        }

        /**
         * @brief **지금 화면에 보이는지** 플랫폼에 직접 묻습니다.
         * @warning 이것은 `isVisibleIntended()` 와 **다른 질문**입니다. X11 에서는 `XMapWindow` 가
         *          요청일 뿐이고 창 관리자가 실제로 매핑하기 전까지 `IsViewable` 이 아닙니다.
         *          즉 방금 보이라고 한 창도 여기서는 **false** 입니다. 최소화 · 다른 워크스페이스도 같습니다.
         *          "다시 만든 뒤 되살릴까" 같은 판단에는 쓰지 마십시오(그래서 `recreate` 는 의도를 봅니다).
         */
        virtual bool isVisible() const { return true; }

        /**
         * @brief **보이기로 정해져 있는지** 반환합니다. 마지막 `showWindow()` 의 인자입니다.
         * @details 창 관리자의 사정과 상관없이 바로 답합니다. 창을 다시 만들 때 되살릴지를 정하는
         *          것은 이 값입니다. 화면 상태가 아니라 **우리가 원한 상태**가 기준이어야 합니다.
         */
        bool isVisibleIntended() const { return _bVisibleIntent == SW_TRUE; }

        /** @brief 외부 이벤트 처리기(예: ImGui)를 연결합니다. */
        void setCustomMessageHandler( WindowMessageHandlerDelegate handler ) { _customHandler = std::move( handler ); }

        /** @brief 창 크기가 바뀔 때 부를 콜백을 설정합니다. */
        void setResizeCallback( WindowResizeDelegate callback ) { _onResize = std::move( callback ); }

        /** @brief 닫기 전에 불리는 처리기를 설정합니다. false 를 반환하면 닫기를 보류합니다. */
        void setCloseQueryHandler( WindowCloseQueryDelegate handler ) { _closeQuery = std::move( handler ); }
        /** @brief 확인 없이 종료 플래그를 켭니다. */
        void requestClose();
        /** @brief 닫기 쿼리를 거쳐 종료를 시도합니다. 허용되면 true 입니다. */
        bool tryBeginClose();
        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 처리기(메시지 · 크기 · 닫기)를 떼고, 뗀 수를 반환합니다.
         * @details 창은 App 소유라 모듈보다 오래 삽니다. 핫 리로드가 모듈 이미지를 내리기 전에 부릅니다 — 에디터는 닫기 처리기를 답니다.
         */
        uint32 releaseCodeWithin( const void* pBegin, const void* pEnd );

        /** @brief 현재 플랫폼에 맞는 IWindow 인스턴스를 만들어 반환합니다. */
        static unique_ptr<IWindow> createPlatformWindow();

        /** @brief App 이 소유하는 활성 창 포인터를 설정합니다(RHI 초기화 등이 씁니다). */
        static void setActiveWindow( IWindow* pWindow );
        /** @brief 활성 창을 반환합니다. */
        static IWindow* getActiveWindow();

    protected:
        /** @brief 실제로 창을 띄우거나 내립니다. 의도 기록은 기반이 이미 했습니다. */
        virtual void applyWindowVisibility( bool bShow ) { (void)bShow; }

        /**
         * @brief 다시 만들기 직전의 창 위치를 `_restoreX` · `_restoreY` 에 담습니다.
         * @details 플랫폼마다 묻는 API 가 다릅니다(`GetWindowRect` · `XGetWindowAttributes`).
         *          지원하지 않으면 아무것도 하지 않으면 됩니다. 그러면 새 창은 기본 위치에 뜹니다.
         */
        virtual void captureRestorePosition() {}
        /**
         * @brief 복원 위치를 플랫폼의 "알아서 정해라" 값으로 되돌립니다.
         * @details 그 값이 플랫폼마다 다릅니다. Win32 는 `CW_USEDEFAULT`, X11 은 좌표 하나입니다.
         */
        virtual void clearRestorePosition() {}

    protected:
        wstring                      _title;
        WindowMessageHandlerDelegate _customHandler;
        WindowResizeDelegate         _onResize;
        WindowCloseQueryDelegate     _closeQuery;
        uint32                       _width;
        uint32                       _height;
        uint8                        _bShouldClose : 1;
        /**
         * @brief 다시 만드는 중인지 여부입니다.
         * @details 플랫폼 메시지 처리기가 이 깃발을 보고 그 사이의 리사이즈 · 닫기 통보를 삼킵니다.
         *          다시 만드는 과정의 `destroy()` 가 앱 종료로 오해되면 안 되기 때문입니다.
         *          절차가 이 클래스로 올라왔으므로 깃발도 같이 올라왔습니다.
         */
        uint8                  _bRecreating    : 1;
        uint8                  _bVisibleIntent : 1; /**< 마지막 showWindow() 의 인자. 화면 상태가 아니라 **의도**. */
        [[maybe_unused]] uint8 _reserved       : 5;
        uint8                  _arrReserved[7];
        /** @brief 다시 만들 때 놓을 위치입니다. 플랫폼 생성자가 자기 "알아서" 값으로 채웁니다. */
        int32 _restoreX;
        int32 _restoreY;
    };
} // namespace sw

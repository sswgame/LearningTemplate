/**
 * @file IWindow.h
 * @brief 플랫폼 독립적인 창(OS 디스플레이 창) 생성과 메시지 처리 인터페이스입니다.
 */
#pragma once
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRenderSurface.h"

namespace sw
{
    struct NativeWindowEvent;

    SW_DECLARE_DELEGATE( bool, WindowMessageHandlerDelegate, const NativeWindowEvent& event );
    SW_DECLARE_DELEGATE( void, WindowResizeDelegate, uint32 width, uint32 height );
    SW_DECLARE_DELEGATE( bool, WindowCloseQueryDelegate, void );

    /**
     * @brief 창을 화면에 놓는 방식입니다.
     * @details 전용 전체 화면(디스플레이 모드 변경)은 두지 않습니다. 플립 모델 · 티어링 스왑체인과 함께 쓰면 Present 규칙이 달라지고 Alt+Tab 이
     *          모드를 되돌리므로, 전체 화면은 모니터를 덮는 테두리 없는 창 하나로 합니다(Unreal `WindowedFullscreen` · Unity `FullScreenWindow`).
     */
    enum class WindowDisplayMode : uint8
    {
        Windowed = 0,         ///< 테두리 · 제목 줄이 있는 창. 클라이언트 크기를 그대로 씁니다.
        BorderlessFullscreen, ///< 창이 놓인 모니터를 덮는 테두리 없는 창. 크기는 모니터 크기입니다.
    };

    /**
     * @class IWindow
     * @brief 애플리케이션의 주 화면이나 보조 화면을 추상화하는 기본 인터페이스입니다.
     * @details 플랫폼별(Windows, Linux) 구체 클래스가 이 인터페이스를 상속해 구현합니다.
     *          `IRenderSurface` 를 구현하므로 RHI 는 창을 **표면으로만** 봅니다. `Graphics` 가 `Window` 를
     *          include 하지 않는 이유입니다(Engine/Graphics/RHI/IRenderSurface.h).
     */
    class SW_API IWindow : public IRenderSurface, public IModuleUnloadListener
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
         * @details **절차는 여기 한 벌뿐입니다** — `_bRecreating` 을 세워 다시 만드는 동안의 리사이즈 · 닫기 통보를 막고,
         *          **보이던 창을 다시 보이게 합니다.** 백엔드 교체(`RHI::recreateDevice` → `recreateSurface`)가 부르는 자리입니다.
         *          플랫폼이 정말로 다른 두 가지는 아래 훅으로 뺐습니다.
         *
         * @note 플랫폼이 이 기능을 지원하지 않으면 이 함수를 재정의해 `false` 를 반환합니다.
         *       재정의해서 **절차를 다시 적지는 마십시오** — 벌마다 조용히 어긋납니다.
         */
        [[nodiscard]] virtual bool recreate();

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
        /**
         * @brief OS 가 이 창에 권하는 배율입니다(96 DPI = 1). Win32 `GetDpiForWindow`(PerMonitorV2 매니페스트 — 모니터를 옮기면 따른다) ·
         *        X11 `Xft.dpi` 리소스. 모르면 1 입니다.
         * @details 게임 UI 는 기본으로 곱하지 않는다(해상도 규칙이 이미 창 크기를 따른다 — `UiScaleSettings::_bApplyContentScale`).
         */
        virtual float32 getContentScale() const { return 1.0f; }

        /** @brief 배율 1 에 해당하는 DPI 입니다(Windows · X11 의 기준). */
        static constexpr float32 kReferenceDpi = 96.0f;

        /**
         * @brief 창 방식과 (창 모드일 때의) 클라이언트 크기를 바꿉니다.
         * @details 크기가 바뀌면 플랫폼의 크기 통보가 `setResizeCallback` 의 콜백을 부릅니다 — 스왑체인은 그 길(App::onResize 가 렌더 스레드를
         *          기다린 뒤 resize)로만 바뀝니다. Win32 는 이 호출 안에서 같은 스레드로 통보하고, X11 은 다음 `processMessages` 에서 통보합니다.
         *          **창 스레드(메인 스레드)에서만 부릅니다.** `BorderlessFullscreen` 이면 @p width · @p height 는 무시하고 모니터 크기를 씁니다.
         * @return 지원하지 않는 플랫폼이면 false 입니다.
         */
        virtual bool setDisplayMode( WindowDisplayMode mode, uint32 width, uint32 height )
        {
            (void)mode;
            (void)width;
            (void)height;
            return false;
        }
        /** @brief 마지막 `setDisplayMode` 가 고른 창 방식입니다. */
        WindowDisplayMode getDisplayMode() const { return _displayMode; }
        /**
         * @brief 사용자가 창을 줄일 수 있는 가장 작은 클라이언트 크기(물리 픽셀)를 정합니다. 0 이면 제한하지 않습니다.
         * @details 창 모드에만 적용합니다(Win32 `WM_GETMINMAXINFO` · X11 `WM_NORMAL_HINTS` 최소 크기). 지금 창이 이보다 작으면 이 크기로 키웁니다.
         *          다시 만든 창(`recreate`)에도 그대로 걸립니다. **창 스레드(메인 스레드)에서만 부릅니다.**
         */
        void setMinimumClientSize( uint32 width, uint32 height );
        /** @brief `setMinimumClientSize` 의 가로 크기입니다. 0 이면 제한이 없습니다. */
        uint32 getMinimumClientWidth() const { return _minClientWidth; }
        /** @brief `setMinimumClientSize` 의 세로 크기입니다. 0 이면 제한이 없습니다. */
        uint32 getMinimumClientHeight() const { return _minClientHeight; }
        /**
         * @brief 창 제목을 바꿉니다(UTF-8). 같은 제목이거나 빈 제목이면 아무것도 하지 않습니다. **창 스레드(메인 스레드)에서만 부릅니다.**
         * @details 제목은 `_title` 에 남아 `recreate` 가 같은 제목으로 다시 만듭니다(빈 제목은 `recreate` 가 거절하므로 받지 않습니다).
         */
        void setTitle( const utf8* pTitle );
        /** @brief 지금 제목입니다(UTF-16 — 플랫폼 창이 쓰는 그대로). */
        const wstring& getTitle() const { return _title; }

        // ------------------------------------------------------------------------------
        // IRenderSurface: RHI 가 창 시스템을 모르는 채로 묻는 다섯 가지
        // ------------------------------------------------------------------------------
        void*              getSurfaceHandle() const override { return getNativeHandle(); }
        void*              getSurfaceDisplay() const override { return getNativeDisplay(); }
        uint32             getSurfaceWidth() const override { return getWidth(); }
        uint32             getSurfaceHeight() const override { return getHeight(); }
        [[nodiscard]] bool recreateSurface() override { return recreate(); }
        /**
         * @brief 창을 화면에 띄우거나 숨깁니다. **"보이기로 했다" 는 사실을 기억합니다.**
         * @details 이것은 가상이 아닙니다. 의도를 기록하는 일을 플랫폼이 빠뜨릴 수 없게 합니다.
         *          실제로 창을 띄우고 내리는 일만 `applyWindowVisibility` 로 내려갑니다.
         */
        void showWindow( bool bShow )
        {
            _bVisibleRequested = bShow ? SW_TRUE : SW_FALSE;
            applyWindowVisibility( bShow );
        }

        /**
         * @brief **지금 화면에 보이는지** 플랫폼에 직접 묻습니다.
         * @warning 이것은 `isVisibleRequested()` 와 **다른 질문**입니다. X11 에서는 `XMapWindow` 가
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
        bool isVisibleRequested() const { return _bVisibleRequested == SW_TRUE; }

        /** @brief 외부 이벤트 처리기(예: ImGui)를 연결합니다. */
        void setCustomMessageHandler( WindowMessageHandlerDelegate handler ) { _customHandler = std::move( handler ); }

        /** @brief 창 크기가 바뀔 때 부를 콜백을 설정합니다. */
        void setResizeCallback( WindowResizeDelegate callback ) { _onResize = std::move( callback ); }

        /** @brief 닫기 전에 불리는 처리기를 설정합니다. false 를 반환하면 닫기를 보류합니다. */
        void setCloseQueryHandler( WindowCloseQueryDelegate handler ) { _closeQuery = std::move( handler ); }
        /** @brief 확인 없이 종료 플래그를 켭니다. */
        void requestClose();
        /** @brief 닫기 쿼리를 거쳐 종료를 시도합니다. 허용되면 true 입니다. */
        [[nodiscard]] bool tryBeginClose();
        /**
         * @brief 호출 스텁이 [@p pBegin, @p pEnd) 안에 있는 처리기(메시지 · 크기 · 닫기)를 떼고, 뗀 수를 반환합니다.
         * @details 창은 App 소유라 모듈보다 오래 삽니다. 핫 리로드가 모듈 이미지를 내리기 전에 부릅니다 — 에디터는 닫기 처리기를 답니다.
         */
        uint32 releaseCodeWithin( const void* pBegin, const void* pEnd );

        /** @brief 언로드 리스너 목록의 이름입니다. */
        const utf8* getModuleUnloadListenerName() const override { return "window handlers"; }
        /** @brief `releaseCodeWithin` 입니다. 활성 창만이 아니라 살아 있는 모든 창이 훑깁니다. */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

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
        /**
         * @brief 최소 크기를 플랫폼에 알립니다. 값은 `_minClientWidth` · `_minClientHeight` 에 이미 있습니다.
         * @details 메시지를 받을 때마다 값을 읽는 플랫폼(Win32 `WM_GETMINMAXINFO`)은 할 일이 없습니다.
         */
        virtual void applyMinimumClientSize() {}
        /** @brief `_title` 을 플랫폼 창의 제목 줄에 겁니다. 창이 아직 없으면 아무것도 하지 않습니다(만들 때 `_title` 로 겁니다). */
        virtual void applyTitle() {}

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
        uint8                  _bRecreating       : 1;
        uint8                  _bVisibleRequested : 1; /**< 마지막 showWindow() 의 인자. 화면 상태가 아니라 **의도**. */
        [[maybe_unused]] uint8 _reserved          : 5;
        WindowDisplayMode      _displayMode; /**< 마지막 `setDisplayMode` 가 고른 방식. */
        uint8                  _arrReserved[6];
        /** @brief 다시 만들 때 놓을 위치입니다. 플랫폼 생성자가 자기 "알아서" 값으로 채웁니다. */
        int32  _restoreX;
        int32  _restoreY;
        uint32 _minClientWidth;  /**< 사용자가 줄일 수 있는 가장 작은 클라이언트 가로(물리 픽셀). 0 = 제한 없음 */
        uint32 _minClientHeight; /**< 위와 같습니다(세로). */
    };
} // namespace sw

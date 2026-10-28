/**
 * @file IImGuiPlatformBackend.h
 * @brief ImGui 플랫폼(창 · 입력) 백엔드의 추상 인터페이스입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

struct ImGuiViewport;

namespace sw
{
    enum class RHIBackend : uint32;

    struct NativeWindowEvent;
    struct X11SurfaceHandles;

    class IWindow;
} // namespace sw

namespace sw::editor
{
    /** @brief ImGui 창 · 입력 백엔드입니다(Win32 / X11 / Wayland). */
    class IImGuiPlatformBackend
    {
    public:
        // ------------------------------------------------------------------------------
        // 1) 생명주기 · 프레임 · 이벤트
        // ------------------------------------------------------------------------------
        /** @brief 파생 백엔드가 네이티브 리소스를 해제할 수 있게 합니다. */
        virtual ~IImGuiPlatformBackend() = default;

        /** @brief 네이티브 창과 RHI 백엔드에 맞춰 ImGui 플랫폼 레이어를 초기화합니다. */
        [[nodiscard]] virtual bool initialize( IWindow* pWindow, RHIBackend backendType ) = 0;
        /** @brief ImGui 플랫폼 리소스를 해제합니다. */
        virtual void shutdown() = 0;
        /** @brief 프레임 시작 시 디스플레이 크기·입력 상태를 갱신합니다. */
        virtual void newFrame() = 0;

        /** @brief 플랫폼 네이티브 이벤트를 ImGui로 전달합니다. */
        virtual bool processEvent( const NativeWindowEvent& event ) = 0;

        /** @brief 메인 창이 놓인 모니터의 DPI 배율(96 DPI = 1)입니다. 모르는 플랫폼은 1 입니다. `initialize` 뒤에 부릅니다. */
        virtual float32 getDpiScale() const { return 1.0f; }

        // ------------------------------------------------------------------------------
        // 2) 팩토리 (현재 플랫폼 구현)
        // ------------------------------------------------------------------------------
        /** @brief 현재 플랫폼용 ImGui 플랫폼 백엔드를 생성합니다. */
        static unique_ptr<IImGuiPlatformBackend> createPlatformBackend();
        /**
         * @brief @p window 의 창 시스템에 맞는 플랫폼 백엔드를 생성합니다. 리눅스는 창이 Wayland 면 Wayland 백엔드, 아니면 X11 백엔드이고 Windows 는 늘 Win32 백엔드입니다.
         * @details 에디터는 이것을 쓴다(창 종류를 모르는 `createPlatformBackend()` 는 현재 플랫폼의 기본 창 시스템용이다 — X11 · Win32).
         */
        static unique_ptr<IImGuiPlatformBackend> createPlatformBackendFor( const IWindow& window );

        /**
         * @brief 뷰포트 창이 X11 창이면 서버 연결(`Display*`)과 창 ID 를 @p outHandles 에 담고 true 입니다. 정의는 리눅스 X11 백엔드에 있습니다.
         * @details 뷰포트의 `PlatformHandle` 은 플랫폼 백엔드마다 뜻이 다른 `void*` 라서(Wayland 는 `wl_display*`) 렌더러가 직접 `Display*` 로 읽으면 안 된다.
         *          활성 플랫폼 백엔드가 X11 인지(`ImGuiIO::BackendPlatformName`)와 값이 있는지를 한 곳에서 확인한다 — `IWindow::tryGetX11Surface` 의 뷰포트판이다.
         * @return X11 백엔드가 아니거나(Wayland 등) 연결 · 창 ID 가 비어 있으면 false 이며 @p outHandles 는 비어 있다.
         */
        [[nodiscard]] static bool tryGetViewportX11Surface( const ImGuiViewport* pViewport, X11SurfaceHandles& outHandles );

        /**
         * @brief 뷰포트 창이 Win32 창이면 `HWND` 를 `void*` 로 반환하고, 아니면 nullptr 입니다. 호출처가 `static_cast<HWND>` 로 읽습니다. 정의는 Win32 백엔드에 있습니다.
         * @details 뷰포트의 `PlatformHandle` · `PlatformHandleRaw` 는 플랫폼 백엔드마다 뜻이 다른 `void*` 라서 렌더러 · 테스트가 직접 `HWND` 로 읽으면 안 된다.
         *          활성 플랫폼 백엔드가 Win32 인지(`ImGuiIO::BackendPlatformName`)와 값이 있는지를 한 곳에서 확인한다 — `IWindow::tryGetWin32Handle` 의 뷰포트판이다.
         *          반환값을 버리면 안 되고, nullptr 이면 호출처가 건너뛰거나 오류로 멈춘다(다른 핸들로 대신하지 않는다).
         */
        [[nodiscard]] static void* tryGetViewportWin32Handle( const ImGuiViewport* pViewport );
    };
} // namespace sw::editor

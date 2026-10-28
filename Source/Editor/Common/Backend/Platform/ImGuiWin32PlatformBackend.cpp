#include "pch.h"

#include "Core/Common/PlatformOSHeaders.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Container/StringUtil.h"

    #include "Engine/Config/RHIBackendType.h"
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <imgui.h>
    #include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace sw::editor
{
    SW_LOG_CALLER( "ImGuiWin32" );

    class ImGuiWin32PlatformBackend : public IImGuiPlatformBackend
    {
    public:
        /** @brief `imgui_impl_win32` 가 `ImGuiIO::BackendPlatformName` 에 거는 이름입니다. `tryGetViewportWin32Handle` 이 활성 플랫폼 백엔드의 종류를 이 이름으로 알아본다. */
        static constexpr const utf8* kPlatformName = "imgui_impl_win32";

        bool initialize( IWindow* pWindow, RHIBackend backendType ) override
        {
            if ( pWindow == nullptr )
                return false;

            // 이 백엔드는 Win32 전용이다 — Win32 창이 아니거나 핸들이 없으면 다른 창 시스템의 핸들을 넘기지 않고 오류로 멈춘다.
            HWND hWnd = static_cast<HWND>( pWindow->tryGetWin32Handle() );
            if ( hWnd == nullptr )
            {
                SW_LOG_ERROR( "The ImGui Win32 platform backend needs a Win32 window with a handle" );
                return false;
            }
            _hWnd = hWnd;
            if ( backendType == RHIBackend::OpenGL )
                return ImGui_ImplWin32_InitForOpenGL( hWnd );
            return ImGui_ImplWin32_Init( hWnd );
        }

        float32 getDPIScale() const override
        {
            if ( _hWnd == nullptr )
                return 1.0f;
            const float32 scale = ImGui_ImplWin32_GetDpiScaleForHwnd( _hWnd );
            return scale > 0.0f ? scale : 1.0f;
        }

        void shutdown() override
        {
            // **초기화가 실패한 뒤에도 여기로 온다.** `ImGuiEditor::shutdownPartialInitialization` 은 `initialize()` 가 false 를
            // 반환한 직후 이 함수를 부르는데, 그때 ImGui 쪽에는 짝이 되는 Init 이 없어서 `ImGui_ImplWin32_Shutdown` 첫 줄의
            // assert 에 걸린다. **실패를 수습하려고 있는 경로가 실패하는 것이다.** 렌더러 백엔드 넷도 모두
            // `BackendRendererUserData` 로 같은 상황을 막는다.
            if ( ImGui::GetIO().BackendPlatformUserData != nullptr )
                ImGui_ImplWin32_Shutdown();
        }

        void newFrame() override
        {
            ImGui_ImplWin32_NewFrame();
        }

        bool processEvent( const NativeWindowEvent& event ) override
        {
            // Win32 창이 낸 이벤트가 아니면 `HWND` 가 없다 — 다른 창 시스템의 핸들을 ImGui 에 넘기지 않고 처리하지 않은 것으로 돌려준다.
            HWND hWnd = static_cast<HWND>( event.tryGetWin32Window() );
            if ( hWnd == nullptr )
                return false;
            UINT   uMsg = event._message;
            WPARAM wp   = event._wParam;
            LPARAM lp   = event._lParam;
            return ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wp, lp ) != 0;
        }

    private:
        HWND _hWnd{ nullptr }; ///< 메인 창(DPI 를 물을 때 쓴다)
    };

    unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
    {
        return make_unique<ImGuiWin32PlatformBackend>();
    }

    unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackendFor( const IWindow& window )
    {
        (void)window; // Windows 는 창 시스템이 Win32 하나다 — 창이 Win32 가 아니면 `initialize` 가 오류로 멈춘다
        return createPlatformBackend();
    }

    void* IImGuiPlatformBackend::tryGetViewportWin32Handle( const ImGuiViewport* pViewport )
    {
        if ( pViewport == nullptr || ImGui::GetCurrentContext() == nullptr )
            return nullptr;
        // 뷰포트 핸들은 활성 플랫폼 백엔드가 채운 것이다 — Win32 백엔드가 아니면 `PlatformHandle` 이 HWND 가 아니다.
        const utf8* pPlatformName = ImGui::GetIO().BackendPlatformName;
        if ( pPlatformName == nullptr || StringUtil::equals( pPlatformName, ImGuiWin32PlatformBackend::kPlatformName ) == false )
            return nullptr;
        // imgui_impl_win32 는 두 칸에 같은 HWND 를 건다. Raw 가 비면 생성 도중이라 값이 없는 것이다.
        return pViewport->PlatformHandleRaw;
    }
} // namespace sw::editor
#endif

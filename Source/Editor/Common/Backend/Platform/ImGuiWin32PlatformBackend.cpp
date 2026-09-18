#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Window/IWindow.h"
    #include "Engine/Window/NativeWindowEvent.h"

    #include <imgui.h>
    #include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace sw::editor
{
    class ImGuiWin32PlatformBackend : public IImGuiPlatformBackend
    {
    public:
        bool initialize( IWindow* pWindow, RHIBackend backendType ) override
        {
            if ( pWindow == nullptr )
                return false;

            HWND hWnd = static_cast<HWND>( pWindow->getNativeHandle() );
            if ( backendType == RHIBackend::OpenGL )
                return ImGui_ImplWin32_InitForOpenGL( hWnd );
            return ImGui_ImplWin32_Init( hWnd );
        }

        void shutdown() override
        {
            // **초기화가 실패한 뒤에도 여기로 온다.** `ImGuiEditor::shutdownPartialInitialization`
            // 은 `initialize()` 가 false 를 돌려준 직후 이 함수를 부르는데, 그때 ImGui 쪽에는
            // 짝이 되는 Init 이 없어서 `ImGui_ImplWin32_Shutdown` 의 첫 줄 단정에 걸린다 —
            // **실패를 수습하라고 있는 경로가 실패한다.** 렌더러 백엔드 넷은 모두
            // `BackendRendererUserData` 로 같은 것을 막고 있었다; 플랫폼 쪽만 빠져 있었다.
            if ( ImGui::GetIO().BackendPlatformUserData != nullptr )
                ImGui_ImplWin32_Shutdown();
        }

        void newFrame() override
        {
            ImGui_ImplWin32_NewFrame();
        }

        bool processEvent( const NativeWindowEvent& event ) override
        {
            HWND   hWnd = static_cast<HWND>( event._pNativeWindow );
            UINT   uMsg = event._message;
            WPARAM wp   = event._wParam;
            LPARAM lp   = event._lParam;
            return ImGui_ImplWin32_WndProcHandler( hWnd, uMsg, wp, lp ) != 0;
        }
    };

    unique_ptr<IImGuiPlatformBackend> IImGuiPlatformBackend::createPlatformBackend()
    {
        return make_unique<ImGuiWin32PlatformBackend>();
    }
} // namespace sw::editor
#endif

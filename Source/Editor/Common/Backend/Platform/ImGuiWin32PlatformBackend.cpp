#include "pch.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Engine/Config/RHIBackendType.h"
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
            _hWnd     = hWnd;
            if ( backendType == RHIBackend::OpenGL )
                return ImGui_ImplWin32_InitForOpenGL( hWnd );
            return ImGui_ImplWin32_Init( hWnd );
        }

        float32 getDpiScale() const override
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
            HWND   hWnd = static_cast<HWND>( event._pNativeWindow );
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
} // namespace sw::editor
#endif

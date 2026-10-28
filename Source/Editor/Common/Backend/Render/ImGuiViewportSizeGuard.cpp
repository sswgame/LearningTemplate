#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#include "Core/Common/PlatformOSHeaders.h"

#include "Editor/Common/Backend/IImGuiPlatformBackend.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 뷰포트 크기를 HWND 클라이언트 크기로 맞춥니다. DXGI_SCALING_NONE 은 두 크기가 같아야 합니다. */
        void syncViewportSizeFromHwnd( ImGuiViewport* pViewport )
        {
            if ( pViewport == nullptr )
                return;

            // Win32 뷰포트 창이 아니거나 핸들이 비어 있으면 잴 창이 없으니 보정을 건너뛴다(다른 핸들로 대신하지 않는다).
            HWND hwnd = static_cast<HWND>( IImGuiPlatformBackend::tryGetViewportWin32Handle( pViewport ) );
            if ( hwnd == nullptr )
                return;

            RECT rc{};
            if ( GetClientRect( hwnd, &rc ) == FALSE )
                return;

            const float32 width  = static_cast<float32>( rc.right - rc.left );
            const float32 height = static_cast<float32>( rc.bottom - rc.top );
            if ( width >= 1.0f && height >= 1.0f )
            {
                pViewport->Size.x = width;
                pViewport->Size.y = height;
            }
        }
    } // namespace

    void ImGuiViewportSizeGuard::guardedCreateWindow( ImGuiViewport* pViewport )
    {
        const ImGuiViewportSizeGuard* pGuard      = static_cast<const ImGuiViewportSizeGuard*>( ImGui::GetIO().UserData );
        const auto                    pfnOriginal = pGuard != nullptr ? pGuard->_pOriginalCreateWindow : nullptr;
        if ( pViewport == nullptr || pfnOriginal == nullptr )
            return;

        syncViewportSizeFromHwnd( pViewport );
        if ( pViewport->Size.x < 1.0f )
            pViewport->Size.x = 1.0f;
        if ( pViewport->Size.y < 1.0f )
            pViewport->Size.y = 1.0f;

        pfnOriginal( pViewport );
    }

    void ImGuiViewportSizeGuard::guardedSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
    {
        const ImGuiViewportSizeGuard* pGuard      = static_cast<const ImGuiViewportSizeGuard*>( ImGui::GetIO().UserData );
        const auto                    pfnOriginal = pGuard != nullptr ? pGuard->_pOriginalSetWindowSize : nullptr;
        if ( pViewport == nullptr || pfnOriginal == nullptr )
            return;

        syncViewportSizeFromHwnd( pViewport );
        if ( pViewport->Size.x >= 1.0f && pViewport->Size.y >= 1.0f )
            size = pViewport->Size;

        pfnOriginal( pViewport, size );
    }

    void ImGuiViewportSizeGuard::install()
    {
        ImGuiIO& io = ImGui::GetIO();
        SW_ASSERT( io.UserData == nullptr || io.UserData == this ); // 컨텍스트의 사용자 칸은 이 가드 하나가 쓴다
        io.UserData = this;

        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        if ( platformIO.Renderer_CreateWindow != nullptr && platformIO.Renderer_CreateWindow != &guardedCreateWindow )
        {
            _pOriginalCreateWindow           = platformIO.Renderer_CreateWindow;
            platformIO.Renderer_CreateWindow = &guardedCreateWindow;
        }

        if ( platformIO.Renderer_SetWindowSize != nullptr && platformIO.Renderer_SetWindowSize != &guardedSetWindowSize )
        {
            _pOriginalSetWindowSize           = platformIO.Renderer_SetWindowSize;
            platformIO.Renderer_SetWindowSize = &guardedSetWindowSize;
        }
    }

    void ImGuiViewportSizeGuard::clear()
    {
        _pOriginalCreateWindow  = nullptr;
        _pOriginalSetWindowSize = nullptr;
        if ( ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().UserData == this )
            ImGui::GetIO().UserData = nullptr;
    }
} // namespace sw::editor

#endif // SW_PLATFORM_WINDOWS

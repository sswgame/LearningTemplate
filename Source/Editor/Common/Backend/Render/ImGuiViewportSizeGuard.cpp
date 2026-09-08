#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiViewportSizeGuardInternal
        {
            inline static void ( *s_pOrigCreateWindow )( ImGuiViewport* )          = nullptr;
            inline static void ( *s_pOrigSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;

            /** @brief DXGI_SCALING_NONE 은 HWND 클라이언트 크기와 스왑체인 크기가 일치해야 한다. */
            static void syncViewportSizeFromHwnd( ImGuiViewport* pViewport )
            {
                if ( pViewport == nullptr )
                    return;

                HWND hwnd = static_cast<HWND>( pViewport->PlatformHandleRaw ? pViewport->PlatformHandleRaw : pViewport->PlatformHandle );
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

            static void guardedCreateWindow( ImGuiViewport* pViewport )
            {
                if ( pViewport == nullptr || s_pOrigCreateWindow == nullptr )
                    return;

                syncViewportSizeFromHwnd( pViewport );
                if ( pViewport->Size.x < 1.0f )
                    pViewport->Size.x = 1.0f;
                if ( pViewport->Size.y < 1.0f )
                    pViewport->Size.y = 1.0f;

                s_pOrigCreateWindow( pViewport );
            }

            static void guardedSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
            {
                if ( pViewport == nullptr || s_pOrigSetWindowSize == nullptr )
                    return;

                syncViewportSizeFromHwnd( pViewport );
                if ( pViewport->Size.x >= 1.0f && pViewport->Size.y >= 1.0f )
                    size = pViewport->Size;

                s_pOrigSetWindowSize( pViewport, size );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void ImGuiViewportSizeGuard::install()
    {
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

        if ( platformIO.Renderer_CreateWindow != nullptr &&
             platformIO.Renderer_CreateWindow != &ImGuiViewportSizeGuardInternal::guardedCreateWindow )
        {
            ImGuiViewportSizeGuardInternal::s_pOrigCreateWindow = platformIO.Renderer_CreateWindow;
            platformIO.Renderer_CreateWindow                    = &ImGuiViewportSizeGuardInternal::guardedCreateWindow;
        }

        if ( platformIO.Renderer_SetWindowSize != nullptr &&
             platformIO.Renderer_SetWindowSize != &ImGuiViewportSizeGuardInternal::guardedSetWindowSize )
        {
            ImGuiViewportSizeGuardInternal::s_pOrigSetWindowSize = platformIO.Renderer_SetWindowSize;
            platformIO.Renderer_SetWindowSize                    = &ImGuiViewportSizeGuardInternal::guardedSetWindowSize;
        }
    }

    void ImGuiViewportSizeGuard::clear()
    {
        ImGuiViewportSizeGuardInternal::s_pOrigCreateWindow  = nullptr;
        ImGuiViewportSizeGuardInternal::s_pOrigSetWindowSize = nullptr;
    }
} // namespace sw::editor

#endif // SW_PLATFORM_WINDOWS

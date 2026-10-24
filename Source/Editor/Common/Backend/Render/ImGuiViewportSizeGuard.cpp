#include "pch.h"

#include "Editor/Common/Backend/Render/ImGuiViewportSizeGuard.h"

#include "Core/Common/PlatformOsHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )

    #include "Editor/Common/Workspace/EditorService.h"
    #include "Editor/Common/Workspace/EditorStateStore.h"

    #include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ImGuiViewportSizeGuardInternal
        {
            /**
             * @brief 가로챈 ImGui 렌더러 콜백의 원래 함수입니다. 콜백에는 사용자 포인터가 없어 thunk 가 이 값을 찾아야 하므로 에디터 범위의 상태 저장소가 소유한다
             *        (thunk 는 에디터 프레임 안에서만 불린다). 전에는 프로세스 정적 변수였다.
             */
            struct State
            {
                void ( *_pOriginalCreateWindow )( ImGuiViewport* )          = nullptr;
                void ( *_pOriginalSetWindowSize )( ImGuiViewport*, ImVec2 ) = nullptr;
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief 뷰포트 크기를 HWND 클라이언트 크기로 맞춥니다. DXGI_SCALING_NONE 은 두 크기가 같아야 합니다. */
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
                if ( pViewport == nullptr || getState()._pOriginalCreateWindow == nullptr )
                    return;

                syncViewportSizeFromHwnd( pViewport );
                if ( pViewport->Size.x < 1.0f )
                    pViewport->Size.x = 1.0f;
                if ( pViewport->Size.y < 1.0f )
                    pViewport->Size.y = 1.0f;

                getState()._pOriginalCreateWindow( pViewport );
            }

            static void guardedSetWindowSize( ImGuiViewport* pViewport, ImVec2 size )
            {
                if ( pViewport == nullptr || getState()._pOriginalSetWindowSize == nullptr )
                    return;

                syncViewportSizeFromHwnd( pViewport );
                if ( pViewport->Size.x >= 1.0f && pViewport->Size.y >= 1.0f )
                    size = pViewport->Size;

                getState()._pOriginalSetWindowSize( pViewport, size );
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
            ImGuiViewportSizeGuardInternal::getState()._pOriginalCreateWindow = platformIO.Renderer_CreateWindow;
            platformIO.Renderer_CreateWindow                                  = &ImGuiViewportSizeGuardInternal::guardedCreateWindow;
        }

        if ( platformIO.Renderer_SetWindowSize != nullptr &&
             platformIO.Renderer_SetWindowSize != &ImGuiViewportSizeGuardInternal::guardedSetWindowSize )
        {
            ImGuiViewportSizeGuardInternal::getState()._pOriginalSetWindowSize = platformIO.Renderer_SetWindowSize;
            platformIO.Renderer_SetWindowSize                                  = &ImGuiViewportSizeGuardInternal::guardedSetWindowSize;
        }
    }

    void ImGuiViewportSizeGuard::clear()
    {
        // 상태 저장소가 이미 사라졌으면(에디터 범위가 먼저 닫힘) 지울 값도 함께 사라진 것이다.
        EditorStateStore* pStore = findSubsystem<EditorStateStore>();
        if ( pStore != nullptr )
            pStore->get<ImGuiViewportSizeGuardInternal::State>() = ImGuiViewportSizeGuardInternal::State{};
    }
} // namespace sw::editor

#endif // SW_PLATFORM_WINDOWS

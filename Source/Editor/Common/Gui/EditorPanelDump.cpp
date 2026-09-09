#include "pch.h"

#include "Editor/Common/Gui/EditorPanelDump.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw
{
    /**
     * @brief `-gv_editorPanelDump=N` — 선언은 `Engine/EngineLoop.cpp` 에 있다.
     * @details 커맨드라인은 모듈 로드 전에 파싱되므로 EditorModule 이 선언한 전역 변수는
     *          `-gv_...` 로 설정할 수 없다(파서가 "해당 Argument 없음" 으로 무시한다). 그래서 선언은
     *          Engine 의 진단 스위치 블록에 두고 여기서는 읽기만 한다. 모듈 경계를 넘어 읽으므로
     *          `SW_API` 다 — `gv_rhiBackend` · `gv_useRenderThread` 와 같은 형태다.
     */
    extern SW_API int32 gv_editorPanelDump;
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 이 TU 로컬 헬퍼 모음 (유니티 빌드 이름 충돌을 피하려 TU 이름을 붙인다). */
        struct EditorPanelDumpInternal
        {
            /** @brief 한 번만 찍는다. 이후 프레임에서 다시 찍지 않게 기억한다. */
            static inline bool s_bDumped{ false };

            /**
             * @brief 셀 의미가 있는 창인지 판별합니다.
             * @details ImGui 는 툴팁·드래그 페이로드·디버그 창도 같은 목록에 담는다. `##` 로 시작하는
             *          내부 창은 뺀다. 자식 창(`부모/##자식`)은 남긴다 — 내용이 자식에 들어 있는
             *          패널이 많아서, 빼면 오히려 중요한 것이 안 보인다.
             */
            static bool isReportableWindow( const ImGuiWindow* pWindow )
            {
                if ( pWindow == nullptr || pWindow->Name == nullptr )
                    return false;
                if ( pWindow->Name[0] == '#' && pWindow->Name[1] == '#' )
                    return false;
                return true;
            }

            /**
             * @brief 정점이 0 인 것이 **정상인** 창인지 판별합니다.
             * @details 세 종류가 그렇다.
             *          (1) 도킹 호스트처럼 **자식이 내용을 들고 있는 컨테이너**,
             *          (2) 배경을 그리지 않는 창,
             *          (3) **입력을 전혀 받지 않는 순수 오버레이** — ImGuizmo 가 만드는 `gizmo` 창이
             *              그렇다. 선택이 없으면 그릴 것이 없다. 사용자가 조작하는 패널은 `NoInputs`
             *              를 갖지 않으므로, 이 조건이 진짜 고장을 가릴 일은 없다.
             *          이것까지 경고하면 매 실행마다 거짓 경보가 나서 아무도 이 도구를 믿지 않는다.
             */
            static bool isEmptyByDesign( const ImGuiWindow* pWindow )
            {
                if ( pWindow->DC.ChildWindows.Size > 0 )
                    return true;
                if ( ( pWindow->Flags & ImGuiWindowFlags_NoBackground ) != 0 )
                    return true;
                return ( pWindow->Flags & ImGuiWindowFlags_NoInputs ) == ImGuiWindowFlags_NoInputs;
            }
        };
    } // namespace
} // namespace sw

namespace sw::editor
{
    SW_LOG_CALLER( "PanelDump" );

    void EditorPanelDump::dumpIfRequested()
    {
        const int32 targetFrame = gv_editorPanelDump;
        if ( targetFrame <= 0 || EditorPanelDumpInternal::s_bDumped )
            return;

        ImGuiContext* pContext = ImGui::GetCurrentContext();
        if ( pContext == nullptr )
            return;

        const int32 frameCount = static_cast<int32>( pContext->FrameCount );
        if ( frameCount < targetFrame )
            return;
        EditorPanelDumpInternal::s_bDumped = true;

        uint32 windowCount = 0;
        uint32 blankCount  = 0;

        SW_LOG_INFO( "===== ImGui 창 덤프 (frame %#) — 패널 변경 전후로 이 블록을 비교한다 =====", frameCount );
        for ( const ImGuiWindow* pWindow : pContext->Windows )
        {
            if ( EditorPanelDumpInternal::isReportableWindow( pWindow ) == false )
                continue;

            const bool   bActive    = pWindow->WasActive;
            const bool   bCollapsed = pWindow->Collapsed;
            const bool   bHidden    = pWindow->Hidden;
            const uint32 vertexCount =
                ( pWindow->DrawList != nullptr ) ? static_cast<uint32>( pWindow->DrawList->VtxBuffer.Size ) : 0u;

            ++windowCount;

            // 보이는데 정점이 0 이고 그게 설계상 정상도 아니면 **빈 패널**이다. 이것이 "컴파일은
            // 통과했는데 화면이 비었다" 의, 기계가 읽을 수 있는 형태다.
            const bool bVisible = bActive && bCollapsed == false && bHidden == false;
            const bool bBlank   = bVisible && vertexCount == 0 && EditorPanelDumpInternal::isEmptyByDesign( pWindow ) == false;
            if ( bBlank )
                ++blankCount;

            SW_LOG_INFO( "  %# size=%#×%# vtx=%# active=%# collapsed=%# hidden=%# %#",
                         pWindow->Name,
                         static_cast<int32>( pWindow->Size.x ),
                         static_cast<int32>( pWindow->Size.y ),
                         vertexCount,
                         bActive ? 1 : 0,
                         bCollapsed ? 1 : 0,
                         bHidden ? 1 : 0,
                         bBlank ? "<== BLANK" : "" );
        }

        SW_LOG_INFO( "===== 창 %#개, 내용 없는 패널 %#개 =====", windowCount, blankCount );
        if ( blankCount > 0 )
        {
            SW_LOG_WARNING( "보이는데 아무것도 그리지 않은 패널이 %#개 있습니다. 컨테이너·오버레이는 빼고 센 숫자이므로, "
                            "패널 변경이 화면을 비웠을 가능성이 높습니다.",
                            blankCount );
        }
    }
} // namespace sw::editor

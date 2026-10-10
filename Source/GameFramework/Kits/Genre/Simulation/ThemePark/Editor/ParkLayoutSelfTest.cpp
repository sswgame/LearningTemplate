/**
 * @file ParkLayoutSelfTest.cpp
 * @brief ThemePark 확장의 에디터 자체 시험 — 확장 패널이 에디터의 ImGui 컨텍스트에 그려지고(결속기), 배치 파일이 키트 로더로 읽힌다.
 */
#include "pch.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/Kits/Genre/Simulation/ThemePark/Editor/ParkLayoutPreview.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutSelfTestInternal
        {
            /** @brief 확장 패널이 등록 목록에 올라 그려진다 — 결속기가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가 이 창이 에디터에 없다. */
            static EditorSelfTestStep runExtensionPanelDraws( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( pContext->getPanelManager().setPanelOpen( "themepark.layout", true ), "extension panel is not registered" );
                    return EditorSelfTestStep::Continue; // 한 프레임 그린다
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( "Park Layout" );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "extension panel drew nothing in the editor's ImGui context" );
                (void)pContext->getPanelManager().setPanelOpen( "themepark.layout", false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 배치 파일을 읽어 놀이기구가 하나 이상이고 선분이 나온다(키트 로더 · 트랙 빌더가 확장 안에서 돈다). */
            static EditorSelfTestStep runLayoutPreviewLoads( EditorSelfTestContext& context )
            {
                ParkLayoutPreview preview;
                (void)preview.refresh(); // 결과는 아래 isLoaded 로 본다
                context.expect( preview.isLoaded() && preview.getRides().empty() == false, "rides.xml did not load through the ThemePark kit" );
                vector<EditorWorldSegment> listSegment;
                preview.appendSegments( listSegment );
                context.expect( listSegment.size() >= preview.getRides().size() * 4u, "every ride should give at least its four footprint edges" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( ParkLayoutPanel, "themepark.extensionPanelDraws", 9000, &ParkLayoutSelfTestInternal::runExtensionPanelDraws );
    SW_EDITOR_SELF_TEST( ParkLayoutPreview, "themepark.layoutPreviewLoads", 9010, &ParkLayoutSelfTestInternal::runLayoutPreviewLoads );
} // namespace sw::editor

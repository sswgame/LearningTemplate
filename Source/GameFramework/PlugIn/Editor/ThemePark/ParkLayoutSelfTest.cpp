/**
 * @file ParkLayoutSelfTest.cpp
 * @brief ThemePark 확장의 에디터 셀프 테스트입니다. 확장 패널이 에디터의 ImGui 컨텍스트에 그려지는지(바인더), 배치 파일이 키트 로더로 읽히는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPanel.h"
#include "GameFramework/PlugIn/Editor/ThemePark/ParkLayoutPreview.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct ParkLayoutSelfTestInternal
        {
            /** @brief 확장 패널이 등록 목록에 올라 그려지는지 확인합니다. 바인더가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가서 이 창이 에디터에 없습니다. */
            static EditorSelfTestStep runExtensionPanelDraws( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( findSubsystem<ParkLayoutPreview>() != nullptr, "the extension subsystem ParkLayoutPreview is not bound in the editor scope" );
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen<ParkLayoutPanel>( true ), "extension panel is not registered" );
                    return EditorSelfTestStep::Continue; // 한 프레임 그린다
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( "Park Layout" );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "extension panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<ParkLayoutPanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 배치 파일을 읽어 놀이기구가 하나 이상이고 선분이 나오는지 확인합니다. 키트 로더와 트랙 빌더가 확장 안에서 동작해야 합니다. */
            static EditorSelfTestStep runLayoutPreviewLoads( EditorSelfTestContext& context )
            {
                ParkLayoutPreview preview;
                (void)preview.refresh(); // 결과는 아래 isLoaded 로 확인한다
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

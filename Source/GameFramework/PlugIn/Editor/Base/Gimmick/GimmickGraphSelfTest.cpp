/**
 * @file GimmickGraphSelfTest.cpp
 * @brief 기믹 회로 그래프 확장의 에디터 자체 테스트입니다. 확장 패널이 에디터의 ImGui 컨텍스트에 그려지는지(바인더)와, 회로 편집 모델이 확장 안에서 동작하는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphModel.h"
#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct GimmickGraphSelfTestInternal
        {
            /** @brief 확장 패널이 등록 목록에 올라 그려지는지 확인합니다. 바인더가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가서 이 창이 에디터에 없습니다. */
            static EditorSelfTestStep runPanelDraws( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( true ), "gimmick graph panel is not registered" );
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( GimmickGraphPanel::kPanelTitle );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "gimmick graph panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 모델이 확장 이미지 안에서 노드 · 배선을 만들고 내보내는지 확인합니다. 기본 노드 종류 레지스트리가 확장에서도 보여야 합니다. */
            static EditorSelfTestStep runModelBuildsACircuit( EditorSelfTestContext& context )
            {
                GimmickGraphModel model( GimmickNodeRegistry::getDefault() );
                const int32       plate = model.addNode( hashed_string( "Signal" ) );
                const int32       door  = model.addNode( hashed_string( "Door" ) );
                context.expect( plate != GimmickGraphModel::kNoGraphID && door != GimmickGraphModel::kNoGraphID, "the default node registry has no Signal or Door" );
                int32  wireID = 0;
                string error;
                context.expect( model.addWire( plate, "Active", door, "Open", wireID, error ), "a Signal -> Door wire was rejected" );
                vector<GimmickGraphIssue> listIssue;
                model.collectIssues( listIssue );
                context.expect( listIssue.empty(), "a valid two-node circuit reported problems" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( GimmickGraphPanel, "gimmick.panelDraws", 9200, &GimmickGraphSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( GimmickGraphModel, "gimmick.modelBuildsACircuit", 9210, &GimmickGraphSelfTestInternal::runModelBuildsACircuit );
} // namespace sw::editor

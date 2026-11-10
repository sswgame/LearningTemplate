/**
 * @file GimmickGraphSelfTest.cpp
 * @brief 기믹 회로 그래프 확장의 에디터 자체 테스트입니다. 확장 패널이 에디터의 ImGui 컨텍스트에 그려지는지(바인더)와, 회로 편집 모델이 확장 안에서 동작하는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphModel.h"
#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphPanel.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

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
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( true ), "gimmick graph panel is not registered" );
                    return EditorSelfTestStep::WaitSettled; // 열린 패널이 도킹 · 그리기를 마칠 때까지
                }
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( GimmickGraphPanel::kPanelTitle );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "gimmick graph panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<GimmickGraphPanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 모델이 확장 이미지 안에서 노드 · 배선을 만들고 내보내는지 확인합니다. 기본 노드 종류 레지스트리가 확장에서도 보여야 합니다. */
            static EditorSelfTestStep runModelBuildsACircuit( EditorSelfTestContext& context )
            {
                GimmickGraphModel     model( game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry() );
                const expected<int32> plateID = model.addNode( hashed_string( "Signal" ) );
                const expected<int32> doorID  = model.addNode( hashed_string( "Door" ) );
                context.expect( plateID.has_value() && doorID.has_value(), "the default node registry has no Signal or Door" );
                if ( plateID.has_value() == false || doorID.has_value() == false )
                    return EditorSelfTestStep::Done;
                const int32 plate = *plateID;
                const int32 door  = *doorID;
                context.expect( model.addWire( plate, "Active", door, "Open" ).has_value(), "a Signal -> Door wire was rejected" );
                vector<GimmickGraphIssue> listIssue;
                model.collectIssues( listIssue );
                context.expect( listIssue.empty(), "a valid two-node circuit reported problems" );
                // 복제는 노드 둘과 그 사이 배선 하나를 새 번호로 만든다(원본은 그대로).
                const vector<int32> listCopy = model.duplicateNodes( vector<int32>{ plate, door }, float2{ 40.0f, 40.0f } );
                context.expect( listCopy.size() == 2 && model.getNodes().size() == 4 && model.getWires().size() == 2, "duplicating two wired nodes did not make two nodes and their wire" );

                // 노드 자리와 주석 상자는 컴포넌트 목록을 왕복한다 — 다시 읽어도 자동 배치로 흩어지지 않는다.
                model.getMutableNodes()[0]._position = float2{ 123.0f, -45.0f };
                GraphCommentBox comment{};
                comment._id   = 1;
                comment._text = "Door logic";
                model.getMutableComments().push_back( comment );
                vector<GimmickNodeDesc>  listNode;
                vector<GameObjectHandle> listTarget;
                vector<GimmickWireDesc>  listWire;
                vector<GraphCommentBox>  listComment;
                model.exportTo( listNode, listTarget, listWire, listComment );
                const uint64      signature = model.computeSignature();
                GimmickGraphModel reloaded( game::getSubsystem<GimmickNodeRegistrySubsystem>().getRegistry() );
                reloaded.populate( listNode, listTarget, listWire, listComment );
                const bool bKept = reloaded.getNodes()[0]._position == float2{ 123.0f, -45.0f } && reloaded.getMutableComments().size() == 1 && reloaded.computeSignature() == signature;
                context.expect( bKept, "node positions or comment boxes did not survive a component round trip" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( GimmickGraphPanel, "gimmick.panelDraws", 9200, &GimmickGraphSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( GimmickGraphModel, "gimmick.modelBuildsACircuit", 9210, &GimmickGraphSelfTestInternal::runModelBuildsACircuit );
} // namespace sw::editor

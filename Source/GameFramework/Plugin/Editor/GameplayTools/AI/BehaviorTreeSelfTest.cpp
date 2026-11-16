/**
 * @file BehaviorTreeSelfTest.cpp
 * @brief 행동 트리 편집 창 확장의 에디터 자체 테스트입니다. 확장 패널이 에디터의 ImGui 컨텍스트에 그려지는지(바인더)와, 편집 모델이 확장 이미지 안에서 읽고 쓰는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreeEditModel.h"
#include "GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreePanel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct BehaviorTreeSelfTestInternal
        {
            /** @brief 확장 패널이 등록 목록에 올라 그려지는지 확인합니다. 바인더가 틀리면 확장의 ImGui::Begin 이 다른 컨텍스트로 가서 이 창이 에디터에 없습니다. */
            static EditorSelfTestStep runPanelDraws( EditorSelfTestContext& context )
            {
                EditorContext* pContext = editor::findSubsystem<EditorContext>();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return EditorSelfTestStep::Done;
                if ( context.getStepIndex() == 0 )
                {
                    context.expect( pContext->getSubsystem<EditorPanelManager>().setPanelOpen<BehaviorTreePanel>( true ), "behavior tree panel is not registered" );
                    return EditorSelfTestStep::WaitStable; // 열린 패널이 도킹 · 그리기를 마칠 때까지
                }
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( BehaviorTreePanel::kPanelTitle );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "behavior tree panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<BehaviorTreePanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 모델이 확장 이미지 안에서 트리를 읽고 같은 글로 되쓰는지 확인합니다(노드 이름표 표가 확장에서도 보여야 한다). */
            static EditorSelfTestStep runModelRoundTrips( EditorSelfTestContext& context )
            {
                BehaviorTreeEditModel model;
                const expected<int32> root = model.addNode( -1, BehaviorNodeKind::Selector );
                const expected<int32> wait = root.has_value() ? model.addNode( *root, BehaviorNodeKind::Wait ) : root;
                context.expect( wait.has_value(), "the model rejected a Selector with a Wait child" );
                if ( root.has_value() == false || wait.has_value() == false )
                    return EditorSelfTestStep::Done; // 위 expect 가 이미 알렸다
                string                text = model.write();
                BehaviorTreeEditModel reread;
                context.expect( reread.parse( text ).has_value(), "the model could not read what it wrote" );
                context.expect( reread.write() == text, "writing the re-read tree gave a different text" );

                // 칸은 종류마다의 스키마(`BehaviorTreeNodeParams.h`)로 읽고 쓴다 — 열거 이름 · 불리언 · 기본값 생략이 같은 글로 돌아온다.
                const string_view     kAuthored = "<BehaviorTree><Selector name=\"Root\" reactive=\"true\"><BlackboardCondition test=\"blackboard(Threat) &gt; 2\" abort=\"LowerPriority\">"
                                                  "<MoveTo key=\"Goal\"/></BlackboardCondition></Selector></BehaviorTree>";
                BehaviorTreeEditModel authored;
                context.expect( authored.parse( kAuthored ).has_value(), "the model could not read a hand-written tree" );
                const BehaviorTreeEditNode*             pRoot       = authored.find( authored.getRootID() );
                const BehaviorTreeEditNode*             pCondition  = pRoot != nullptr && pRoot->_listChild.size() == 1 ? authored.find( pRoot->_listChild[0] ) : nullptr;
                const BehaviorTreeEditNode*             pMove       = pCondition != nullptr && pCondition->_listChild.size() == 1 ? authored.find( pCondition->_listChild[0] ) : nullptr;
                const BehaviorSelectorParams*           pSelector   = pRoot != nullptr ? pRoot->_params.find<BehaviorSelectorParams>() : nullptr;
                const BehaviorConditionDecoratorParams* pDecorator  = pCondition != nullptr ? pCondition->_params.find<BehaviorConditionDecoratorParams>() : nullptr;
                const BehaviorMoveToParams*             pMoveTo     = pMove != nullptr ? pMove->_params.find<BehaviorMoveToParams>() : nullptr;
                const bool                              bProperties = pSelector != nullptr && pSelector->_bReactive && pRoot->_name == "Root" && pDecorator != nullptr && pDecorator->_test == "blackboard(Threat) > 2" &&
                                         pDecorator->_abort == BehaviorAbortMode::LowerPriority && pMoveTo != nullptr && pMoveTo->_key == "Goal" && pMoveTo->_repathDistance == 0.5f;
                context.expect( bProperties, "the node fields were not read through the schema" );
                BehaviorTreeEditModel rewritten;
                context.expect( rewritten.parse( authored.write() ).has_value() && rewritten.write() == authored.write(), "a hand-written tree did not survive write and read" );
                context.expect( authored.write().find( "repathDistance" ) == string::npos, "a default value was written out" );

                // 하위 트리 복제는 바로 뒤 형제로 들어가고, 붙여 넣기는 고른 부모의 맨 뒤 자식이다. 루트 복제 · 잎 아래 붙이기는 모델을 그대로 둔다.
                const expected<int32> copy = model.duplicateSubtree( *wait );
                context.expect( copy.has_value() && model.find( *root )->_listChild.size() == 2 && model.find( *root )->_listChild[1] == *copy, "duplicating a Wait did not add it right after the original" );
                const expected<int32> pasted = model.pasteSubtree( *root, model.writeSubtree( *wait ), -1 );
                context.expect( pasted.has_value() && model.find( *root )->_listChild.back() == *pasted, "pasting a subtree did not append it under the parent" );
                const uint32 nodeCount = model.getNodeCount();
                context.expect( model.duplicateSubtree( *root ).has_value() == false && model.pasteSubtree( *wait, model.writeSubtree( *wait ), -1 ).has_value() == false && model.getNodeCount() == nodeCount,
                                "duplicating the root or pasting under a leaf changed the tree" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( BehaviorTreePanel, "behaviorTree.panelDraws", 9220, &BehaviorTreeSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( BehaviorTreeEditModel, "behaviorTree.modelRoundTrips", 9230, &BehaviorTreeSelfTestInternal::runModelRoundTrips );
} // namespace sw::editor

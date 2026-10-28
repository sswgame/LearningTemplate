/**
 * @file BehaviorTreeSelfTest.cpp
 * @brief 행동 트리 편집 창 확장의 에디터 자체 테스트입니다. 확장 패널이 에디터의 ImGui 컨텍스트에 그려지는지(바인더)와, 편집 모델이 확장 이미지 안에서 읽고 쓰는지 확인합니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "GameFramework/Plugin/Editor/Base/AI/BehaviorTreeEditModel.h"
#include "GameFramework/Plugin/Editor/Base/AI/BehaviorTreePanel.h"

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
                    return EditorSelfTestStep::Continue;
                }
                if ( context.getStepIndex() < 3 )
                    return EditorSelfTestStep::Continue;
                const ImGuiWindow* pWindow = ImGui::FindWindowByName( BehaviorTreePanel::kPanelTitle );
                context.expect( pWindow != nullptr && pWindow->DrawList->VtxBuffer.Size > 0, "behavior tree panel drew nothing in the editor's ImGui context" );
                (void)pContext->getSubsystem<EditorPanelManager>().setPanelOpen<BehaviorTreePanel>( false ); // 닫기 실패는 위의 expect 가 이미 알렸다
                return EditorSelfTestStep::Done;
            }

            /** @brief 모델이 확장 이미지 안에서 트리를 읽고 같은 글로 되쓰는지 확인합니다(노드 이름표 표가 확장에서도 보여야 한다). */
            static EditorSelfTestStep runModelRoundTrips( EditorSelfTestContext& context )
            {
                BehaviorTreeEditModel model;
                const int32           root = model.addNode( -1, BehaviorNodeType::Selector );
                const int32           wait = model.addNode( root, BehaviorNodeType::Wait );
                context.expect( root >= 0 && wait >= 0, "the model rejected a Selector with a Wait child" );
                string                text = model.write();
                BehaviorTreeEditModel reread;
                string                error;
                context.expect( reread.parse( text, error ), "the model could not read what it wrote" );
                context.expect( reread.write() == text, "writing the re-read tree gave a different text" );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( BehaviorTreePanel, "behaviorTree.panelDraws", 9220, &BehaviorTreeSelfTestInternal::runPanelDraws );
    SW_EDITOR_SELF_TEST( BehaviorTreeEditModel, "behaviorTree.modelRoundTrips", 9230, &BehaviorTreeSelfTestInternal::runModelRoundTrips );
} // namespace sw::editor

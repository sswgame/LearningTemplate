/**
 * @file EditorUndoCoverage.cpp
 * @brief 되돌리기 전수 검사(`editor.undoCoverage`)입니다 — 거래를 받는 명령을 모두 실행하고 되돌린 뒤 씬 해시가 처음과 같은지 봅니다.
 * @details 씬 명령은 형식으로 거래를 받는다(`EditorSceneCommandActionFn`). 형식이 막지 못하는 틈 — 거래 안에서 기록 없이 바꾸는 것, 기록이 덜 되돌리는 것 — 을
 *          이 검사가 잡는다(언리얼 에디터의 트랜잭션 테스트 · 유니티 Undo 테스트처럼 실행 → 되돌리기 → 상태 비교). 시나리오 `editor/undocoverage` 가 부른다.
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUndoCoverageInternal
        {
            /** @brief 명령 하나를 부를 대상입니다 — 맥락 메뉴 종류에 맞춰 지금 선택(오브젝트) · 원점(뷰포트) · 없음(전역)을 준다. */
            static EditorCommandTarget makeTarget( const EditorCommandDesc& desc, const vector<uint64>& listSelectedID )
            {
                EditorCommandTarget target{};
                target._kind = desc._contextMenuKind;
                if ( desc._contextMenuKind == EditorCommandTargetKind::GameObject )
                    target._listSelectedID = listSelectedID;
                if ( desc._contextMenuKind == EditorCommandTargetKind::Viewport )
                    target._bHasWorldPosition = true; // 원점 — 뷰포트 빈 곳 메뉴가 바닥 평면과 만난 점을 주는 자리
                return target;
            }

            /** @brief 처음 선택으로 되돌립니다. 지우기 · 복제가 선택을 바꾸므로 명령마다 같은 대상에서 시작한다(선택은 씬 해시 밖이다). */
            static void restoreSelection( GameObjectManager& manager, const vector<uint64>& listSelectedID )
            {
                vector<GameObject*> listObject;
                for ( const uint64 objectID : listSelectedID )
                {
                    GameObject* pObject = manager.findGameObjectByID( objectID );
                    if ( pObject != nullptr )
                        listObject.push_back( pObject );
                }
                (void)EditorSceneCommands::selectObjects( listObject ); // 고른 수는 쓰지 않는다 — 사라진 오브젝트는 위에서 걸렀다
            }

            static bool isSkipped( const vector<string>& listSkip, const string& commandID )
            {
                for ( const string& skip : listSkip )
                {
                    if ( skip == commandID )
                        return true;
                }
                return false;
            }

            /**
             * @brief 명령 하나를 실행하고 되돌린 뒤 해시를 맞댑니다. 맞으면 true 입니다(씬을 바꾸지 않은 실행도 true).
             * @details 실패 셋을 가른다: 씬을 바꾸고 기록이 없음(`Unrecorded`), 한 번 실행이 두 단계 이상(`Steps` — 거래가 하나로 묶지 못했다), 되돌려도 해시가 다름(`Mismatch`).
             */
            static bool checkCommand( const EditorCommandDesc& desc, const EditorCommandTarget& target, CommandStack& stack )
            {
                const string commandID = desc._id; // 실행이 레지스트리를 다시 지으면 desc 가 사라진다 — 알림에 쓸 id 는 미리 둔다
                uint64       before    = 0;
                if ( computeEditedSceneHash( before ) == false )
                    return false;
                const size_t indexBefore = stack.getCurrentIndex();
                (void)EditorCommandRegistry::executeDesc( desc, target ); // 켜짐은 부른 쪽이 이미 봤다
                uint64 after = 0;
                if ( computeEditedSceneHash( after ) == false )
                    return false;
                const size_t indexAfter = stack.getCurrentIndex();
                if ( after != before && indexAfter == indexBefore )
                {
                    SW_LOG_ERROR_ID( "Editor.UndoCoverageUnrecorded", "Undo coverage: '%#' changed the scene without an undo step", commandID.c_str() );
                    return false;
                }
                if ( indexAfter > indexBefore + 1 )
                    SW_LOG_ERROR_ID( "Editor.UndoCoverageSteps", "Undo coverage: '%#' pushed %# undo steps for one run (its transaction did not group them)", commandID.c_str(),
                                     static_cast<uint64>( indexAfter - indexBefore ) );
                while ( stack.getCurrentIndex() > indexBefore )
                {
                    stack.undo();
                }
                uint64 restored = 0;
                if ( computeEditedSceneHash( restored ) == false )
                    return false;
                if ( restored != before )
                {
                    SW_LOG_ERROR_ID( "Editor.UndoCoverageMismatch", "Undo coverage: undoing '%#' did not bring the scene back (scene hash differs - run with -gv_automationHashDump=1 to see which object)",
                                     commandID.c_str() );
                    return false;
                }
                return indexAfter <= indexBefore + 1;
            }

            /** @brief `editor.undoCoverage [건너뛸 명령 id…]` — 결과 줄을 답하고, 실패가 하나라도 있으면 false 입니다. */
            static bool run( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                if ( pManager == nullptr )
                {
                    outReply = "no edited scene";
                    return false;
                }
                CommandStack&          stack    = editor::getSubsystem<CommandStack>(); // 필수 - 에디터 서브시스템이다
                EditorCommandRegistry& registry = EditorCommandRegistry::getInEditorContext();

                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                vector<uint64> listSelectedID;
                for ( const GameObject* pObject : listSelected )
                {
                    if ( pObject != nullptr )
                        listSelectedID.push_back( pObject->getObjectID() );
                }

                // 실행이 레지스트리를 다시 지을 수 있으므로(모듈 적재) id 로 돌고 명령마다 다시 찾는다.
                vector<string> listCommandID;
                for ( const EditorCommandDesc& desc : registry.getCommands() )
                {
                    if ( EditorCommandTargetUtil::isTransacted( desc._edit ) )
                        listCommandID.push_back( desc._id );
                }
                uint32 checked = 0;
                uint32 skipped = 0;
                uint32 failed  = 0;
                for ( const string& commandID : listCommandID )
                {
                    const EditorCommandDesc* pDesc = registry.find( commandID );
                    if ( pDesc == nullptr )
                        continue; // 앞 명령이 레지스트리를 다시 지어 빠졌다
                    const EditorCommandDesc& desc = *pDesc;
                    restoreSelection( *pManager, listSelectedID );
                    const EditorCommandTarget target = makeTarget( desc, listSelectedID );
                    if ( isSkipped( listArgument, desc._id ) || EditorCommandRegistry::isEnabled( desc, target ) == false )
                    {
                        ++skipped;
                        SW_LOG_INFO_ID( "Editor.UndoCoverageSkipped", "Undo coverage: '%#' skipped (listed or disabled for this selection)", desc._id.c_str() );
                        continue;
                    }
                    ++checked;
                    if ( checkCommand( desc, target, stack ) == false )
                        ++failed;
                }
                restoreSelection( *pManager, listSelectedID );

                StringBuilder<constant::buffer::kMax128> replyBuilder;
                replyBuilder.appendFormat( "undo coverage: checked %# skipped %# failed %#", checked, skipped, failed );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "Editor.UndoCoverageResult", "%#", outReply.c_str() );
                return failed == 0 && checked > 0;
            }
        };
    } // namespace

    SW_DEV_COMMAND( UndoCoverage, "editor.undoCoverage", "editor.undoCoverage [skipCommandID...]",
                    "Run every scene command (transaction-taking), undo it and check the scene hash is back (scenario editor/undocoverage)", &EditorUndoCoverageInternal::run );
} // namespace sw::editor

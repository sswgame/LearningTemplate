/**
 * @file EditorUndoCoverage.cpp
 * @brief 되돌리기 전수 검사(`editor.undoCoverage`)입니다 — 거래를 받는 명령을 모두 실행하고 되돌린 뒤 씬 해시가 처음과 같은지 봅니다.
 * @details 씬 명령은 형식으로 거래를 받는다(`EditorSceneCommandActionFn`). 형식이 막지 못하는 틈(거래 안에서 기록 없이 바꾸는 것, 기록이 덜 되돌리는 것)을
 *          이 검사가 잡는다(언리얼 에디터의 트랜잭션 테스트 · 유니티 Undo 테스트처럼 실행 → 되돌리기 → 상태 비교). 시나리오 `editor/undocoverage` 가 부른다.
 *          둘째 검사(`editor.undoCoverageProperties`)는 인스펙터 프로퍼티 전수다 — 프로퍼티 단위 기록(`PropertyEditStep.h`)은 값 글 하나만 되돌리므로, 한 쓰기가
 *          다른 칸을 고치는 타입(`onPropertyChanged`)이 되돌리기에서 어긋나는지 추측하지 않고 프로퍼티마다 편집 → 되돌리기 → 씬 해시로 가린다(설계 E-7).
 */
#include "pch.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskTypes.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Scene/EditorSceneCommands.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/InspectorBuiltinValue.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridViewModel.h"
#include "Editor/Common/Widgets/PropertyGrid/Model/PropertyGridViewState.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/SelfTest/EditorWatchSupport.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeInfo.h"

namespace sw::editor
{
    namespace
    {
        struct EditorUndoCoverageInternal
        {
            /**
             * @brief 명령 하나를 부를 대상 후보입니다 — 앞에서부터 켜진 첫 후보로 돈다.
             * @details 선택 하나로만 돌면 하나에만 뜻이 있는 명령(자식 만들기 · 위아래 옮기기)과 컴포넌트 명령이 "이 선택에서 꺼짐" 으로 빠져 전수가 비었다(16 검사 · 15 건너뜀).
             *          그래서 종류마다 맥락 메뉴가 줄 수 있는 대상을 모두 낸다: 오브젝트는 선택 전체 → 하나씩(누른 항목), 컴포넌트는 고른 오브젝트의 컴포넌트마다, 뷰포트는 원점.
             */
            static void makeTargets( const EditorCommandDesc& desc, GameObjectManager& manager, const vector<uint64>& listSelectedID, vector<EditorCommandTarget>& outListTarget )
            {
                outListTarget.clear();
                // 명령 자신의 맥락 종류(맥락 메뉴 명령은 등록이 종류를 정하지 않아 None 일 수 있다 — 그래서 아래 후보를 종류와 상관없이 더한다).
                EditorCommandTarget base{};
                base._kind = desc._contextMenuKind;
                if ( desc._contextMenuKind == EditorCommandTargetKind::GameObject )
                    base._listSelectedID = listSelectedID;
                if ( desc._contextMenuKind == EditorCommandTargetKind::Viewport )
                    base._bHasWorldPosition = true; // 원점 — 뷰포트 빈 곳 메뉴가 바닥 평면과 만난 점을 주는 자리
                outListTarget.push_back( base );
                if ( desc._contextMenuKind == EditorCommandTargetKind::Viewport )
                    return;
                // 오브젝트: 선택 전체 → 하나씩(누른 항목).
                EditorCommandTarget all{};
                all._kind           = EditorCommandTargetKind::GameObject;
                all._listSelectedID = listSelectedID;
                outListTarget.push_back( all );
                for ( const uint64 objectID : listSelectedID )
                {
                    EditorCommandTarget single{};
                    single._kind           = EditorCommandTargetKind::GameObject;
                    single._listSelectedID = { objectID };
                    single._hoveredID      = objectID;
                    outListTarget.push_back( single );
                }
                // 컴포넌트: 고른 오브젝트의 컴포넌트마다(누른 컴포넌트).
                for ( const uint64 objectID : listSelectedID )
                {
                    const GameObject* pObject = manager.findGameObjectByID( objectID );
                    if ( pObject == nullptr )
                        continue;
                    for ( const Component* pComponent : pObject->getComponents() )
                    {
                        EditorCommandTarget component{};
                        component._kind           = EditorCommandTargetKind::Component;
                        component._listSelectedID = { pComponent->getComponentID() };
                        component._hoveredID      = pComponent->getComponentID();
                        outListTarget.push_back( component );
                    }
                }
            }

            /** @brief 거래를 받지 않는 복사 명령을 먼저 돌려 붙여넣기 명령이 켜지게 합니다(오브젝트 · 컴포넌트 클립보드). 씬을 바꾸지 않는다. */
            static void fillClipboards( EditorCommandRegistry& registry, GameObjectManager& manager, const vector<uint64>& listSelectedID )
            {
                static constexpr const utf8* kArrCopyCommand[] = { "scene.copy", "scene.component.copy" };
                vector<EditorCommandTarget>  listTarget;
                for ( const utf8* pCommandID : kArrCopyCommand )
                {
                    const EditorCommandDesc* pDesc = registry.find( pCommandID );
                    if ( pDesc == nullptr )
                        continue;
                    makeTargets( *pDesc, manager, listSelectedID, listTarget );
                    for ( const EditorCommandTarget& target : listTarget )
                    {
                        if ( EditorCommandRegistry::isEnabled( *pDesc, target ) )
                        {
                            (void)EditorCommandRegistry::executeDesc( *pDesc, target );
                            break;
                        }
                    }
                }
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
            static bool validateCommand( const EditorCommandDesc& desc, const EditorCommandTarget& target, CommandStack& stack )
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
                while ( stack.getCurrentIndex() > indexBefore && stack.undo() ) // 거절되면(열린 편집) 멈춘다 — 아래 해시 비교가 실패로 알린다
                {
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
                fillClipboards( registry, *pManager, listSelectedID );
                uint32                      checked = 0;
                uint32                      skipped = 0;
                uint32                      failed  = 0;
                vector<EditorCommandTarget> listTarget;
                for ( const string& commandID : listCommandID )
                {
                    const EditorCommandDesc* pDesc = registry.find( commandID );
                    if ( pDesc == nullptr )
                        continue; // 앞 명령이 레지스트리를 다시 지어 빠졌다
                    const EditorCommandDesc& desc = *pDesc;
                    if ( isSkipped( listArgument, desc._id ) )
                    {
                        ++skipped;
                        SW_LOG_INFO_ID( "Editor.UndoCoverageSkipped", "Undo coverage: '%#' skipped (listed)", desc._id.c_str() );
                        continue;
                    }
                    restoreSelection( *pManager, listSelectedID );
                    makeTargets( desc, *pManager, listSelectedID, listTarget );
                    const EditorCommandTarget* pTarget = nullptr;
                    for ( const EditorCommandTarget& target : listTarget )
                    {
                        if ( EditorCommandRegistry::isEnabled( desc, target ) )
                        {
                            pTarget = &target;
                            break;
                        }
                    }
                    // 켜지는 대상이 하나도 없으면 전수가 빈 것이다 — 조용히 건너뛰지 않고 실패로 센다(장면에 그 대상을 두거나 건너뛸 목록에 이유와 함께 적는다).
                    if ( pTarget == nullptr )
                    {
                        ++failed;
                        SW_LOG_ERROR_ID( "Editor.UndoCoverageNoTarget", "Undo coverage: '%#' is disabled for every target the selection gives - put that target in the scene or list it with a reason",
                                         desc._id.c_str() );
                        continue;
                    }
                    ++checked;
                    if ( validateCommand( desc, *pTarget, stack ) == false )
                        ++failed;
                }
                restoreSelection( *pManager, listSelectedID );

                StringBuilder<constant::buffer::kMax128> replyBuilder;
                replyBuilder.appendFormat( "undo coverage: checked %# skipped %# failed %#", checked, skipped, failed );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "Editor.UndoCoverageResult", "%#", outReply.c_str() );
                return failed == 0 && checked > 0;
            }

            // ------------------------------------------------------------------------------
            // 프로퍼티 전수
            // ------------------------------------------------------------------------------
            /**
             * @brief 행 @p row 를 인스펙터와 같은 쓰기 명령으로 한 번 고칩니다 — 숫자 · 벡터는 @p delta 만큼, 체크는 뒤집는다.
             * @return 일반으로 고칠 값을 지을 수 없는 행(글 · 열거형 · 컨테이너 · 확장 타입)이면 false 입니다.
             */
            static bool editRow( PropertyGridViewModel& model, const PropertyValueRow& row, float64 delta )
            {
                const bool bCheckbox = row._kind == PropertyRowKind::BitField || ( row._kind == PropertyRowKind::Builtin && row._widget == InspectorValueWidget::Checkbox );
                if ( bCheckbox )
                {
                    const bool* pValue = row._value.getPtr<bool>();
                    if ( pValue == nullptr )
                        return false;
                    model.setRowValue( row._rowID, TaskValue{ *pValue == false } );
                    return true;
                }
                if ( row._kind != PropertyRowKind::Builtin )
                    return false;
                if ( row._widget == InspectorValueWidget::Number )
                {
                    MutableObjectAddress instance{ nullptr };
                    const PropertyInfo*  pProp = nullptr;
                    if ( model.findRowValue( row._rowID, instance, pProp ) == false )
                        return false;
                    const InspectorBuiltinValue* pBuiltin = InspectorBuiltinValue::findBuiltin( pProp->_typeName.c_str() );
                    float64                      value    = 0.0;
                    if ( pBuiltin == nullptr || ReflectedValueUtil::readNumber( static_cast<ReflectBuiltinIndex>( pBuiltin->_index ), pProp->getValuePtr<void>( instance.get() ), value ) == false )
                        return false;
                    model.setRowNumber( row._rowID, value + delta );
                    return true;
                }
                if ( row._widget == InspectorValueWidget::Vector3 )
                {
                    const float3* pValue = row._value.getPtr<float3>();
                    if ( pValue == nullptr )
                        return false;
                    model.setRowValue( row._rowID, TaskValue{ *pValue + float3{ static_cast<float32>( delta ) } } );
                    return true;
                }
                return false;
            }

            /** @brief 프로퍼티 전수의 셈입니다. */
            struct PropertyCoverageCount
            {
                uint32 _checked{ 0 };   ///< 고쳐서 되돌려 본 프로퍼티
                uint32 _unchanged{ 0 }; ///< 고칠 수 없거나(일반 값이 없다) 써도 바뀌지 않은(범위 끝 · 막힌) 프로퍼티
                uint32 _failed{ 0 };
            };

            /**
             * @brief 행 @p row 하나: 고치고 마감(`endFrame` — 인스펙터 프레임 끝과 같다) → 되돌리기 → 해시를 맞댑니다.
             * @details 실패 셋은 명령 전수와 같다: 씬을 바꾸고 기록이 없음(`PropertyUnrecorded`), 한 쓰기가 두 단계 이상(`PropertySteps`), 되돌려도 해시가 다름(`PropertyMismatch` —
             *          그 프로퍼티는 값 글 하나로 되돌릴 수 없는 결합이 있다: 거래에 오브젝트 스냅샷을 함께 싣는다).
             */
            static void validatePropertyRow( PropertyGridViewModel& model, const PropertyValueRow& row, CommandStack& stack, PropertyCoverageCount& inoutCount )
            {
                uint64 before = 0;
                if ( computeEditedSceneHash( before ) == false )
                {
                    ++inoutCount._failed;
                    return;
                }
                const size_t indexBefore = stack.getCurrentIndex();
                uint64       after       = before;
                // 범위 끝이라 +1 이 잘려 그대로면 -1 로 한 번 더 고친다(체크는 뒤집기 하나).
                for ( const float64 delta : { 1.0, -1.0 } )
                {
                    if ( editRow( model, row, delta ) == false )
                        break;
                    model.endFrame();
                    // 씬 대상의 쓰기와 마감은 게임 단계 요청이다 — 콘솔은 게임 스레드라 그 단계를 지금 돌린다(호스트 프레임의 `applyUICommands` 자리와 같다).
                    EditorCommandRegistry::getInEditorContext().runRequestedSceneCommands();
                    if ( computeEditedSceneHash( after ) == false )
                    {
                        ++inoutCount._failed;
                        return;
                    }
                    if ( after != before || stack.getCurrentIndex() != indexBefore || row._widget == InspectorValueWidget::Checkbox || row._kind == PropertyRowKind::BitField )
                        break;
                }
                const size_t indexAfter = stack.getCurrentIndex();
                if ( after == before && indexAfter == indexBefore )
                {
                    ++inoutCount._unchanged;
                    return;
                }
                ++inoutCount._checked;
                if ( after != before && indexAfter == indexBefore )
                {
                    ++inoutCount._failed;
                    SW_LOG_ERROR_ID( "Editor.UndoCoveragePropertyUnrecorded", "Undo coverage: editing '%#' changed the scene without an undo step", row._markName.c_str() );
                    return;
                }
                if ( indexAfter > indexBefore + 1 )
                {
                    ++inoutCount._failed;
                    SW_LOG_ERROR_ID( "Editor.UndoCoveragePropertySteps", "Undo coverage: one edit of '%#' pushed %# undo steps", row._markName.c_str(), static_cast<uint64>( indexAfter - indexBefore ) );
                }
                while ( stack.getCurrentIndex() > indexBefore && stack.undo() )
                {
                }
                uint64 restored = 0;
                if ( computeEditedSceneHash( restored ) == false || restored != before )
                {
                    ++inoutCount._failed;
                    SW_LOG_ERROR_ID( "Editor.UndoCoveragePropertyMismatch",
                                     "Undo coverage: undoing an edit of '%#' did not bring the scene back - the property write changes other state (record an object snapshot with it)",
                                     row._markName.c_str() );
                }
            }

            /** @brief `editor.undoCoverageProperties` — 고른 오브젝트와 그 컴포넌트의 깊이 0 프로퍼티마다 `validatePropertyRow`. 실패가 하나라도 있거나 본 것이 없으면 false 입니다. */
            static bool runProperties( [[maybe_unused]] const vector<string>& listArgument, string& outReply )
            {
                if ( editor::getActiveObjectManager() == nullptr )
                {
                    outReply = "no edited scene";
                    return false;
                }
                CommandStack&       stack = editor::getSubsystem<CommandStack>(); // 필수 - 에디터 서브시스템이다
                vector<GameObject*> listSelected;
                editor::getMainSceneSelection().getSelectedObjects( listSelected );
                vector<EditorPropertyGridTarget> listTarget;
                for ( const GameObject* pObject : listSelected )
                {
                    if ( pObject == nullptr )
                        continue;
                    listTarget.push_back( EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, pObject->getObjectID(), 0 ) );
                    for ( const Component* pComponent : pObject->getComponents() )
                    {
                        if ( pComponent != nullptr )
                            listTarget.push_back( EditorPropertyGridTarget::makeSceneTarget( EditorPropertyGridScope::MainScene, pObject->getObjectID(), pComponent->getComponentID() ) );
                    }
                }

                PropertyGridViewModel    model; // 인스펙터와 같은 편집 모델 — 쓰기 · 편집 구간 · 마감이 같은 길이다
                PropertyGridViewState    state;
                PropertyGridGatherParams params{};
                params._window._bSkipOffscreen = SW_FALSE;
                PropertyCoverageCount count;
                for ( const EditorPropertyGridTarget& target : listTarget )
                {
                    if ( model.beginTarget( target ) == false )
                        continue; // 앞 프로퍼티의 되돌리기가 그 컴포넌트를 없앴다면 여기서 빠진다(해시 비교가 이미 알렸다)
                    model.gather( params, state );
                    for ( uint32 index = 0; index < state._rowCount; ++index )
                    {
                        const PropertyValueRow& row = state._listRow[index];
                        if ( row._depth != 0 || row._bReadOnly != SW_FALSE || row._bDisabled != SW_FALSE )
                            continue;
                        validatePropertyRow( model, row, stack, count );
                    }
                    model.endTarget();
                }

                StringBuilder<constant::buffer::kMax128> replyBuilder;
                replyBuilder.appendFormat( "property undo coverage: checked %# unchanged %# failed %#", count._checked, count._unchanged, count._failed );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "Editor.UndoCoveragePropertyResult", "%#", outReply.c_str() );
                return count._failed == 0 && count._checked > 0;
            }

            /**
             * @brief `editor.undoScaleScene <수>` — 활성 씬 루트에 씬 컴포넌트 오브젝트를 그 수만큼 만들고 모두 고릅니다(기록 없음). 규모 판정(8000 선택의 칸 누르기 · 끌기)의 무대다.
             * @details 명령이 아니라 매니저에 바로 만든다 — 되돌리기 스택이 비어 있어야 뒤의 판정(기록 수 · 거래 바이트)이 그 편집 하나만 본다.
             */
            static bool runScaleScene( const vector<string>& listArgument, string& outReply )
            {
                GameObjectManager* pManager = editor::getActiveObjectManager();
                uint64             count    = 0;
                if ( pManager == nullptr || listArgument.size() != 1 || StringUtil::parseUint64( listArgument[0], count ) == false || count == 0 )
                {
                    outReply = "usage: editor.undoScaleScene <count> (needs an edited scene)";
                    return false;
                }
                const hashed_string name( "UndoScale" );
                vector<GameObject*> listObject;
                listObject.reserve( static_cast<size_t>( count ) );
                for ( uint64 index = 0; index < count; ++index )
                {
                    GameObject* pObject = pManager->createGameObject( name );
                    if ( pObject != nullptr && pObject->addComponent<SceneComponent>() != nullptr )
                        listObject.push_back( pObject );
                }
                const uint32                             selected = EditorSceneCommands::selectObjects( listObject );
                StringBuilder<constant::buffer::kMax128> replyBuilder;
                replyBuilder.appendFormat( "undo scale scene: created %# selected %#", static_cast<uint64>( listObject.size() ), selected );
                outReply = string( replyBuilder.view() );
                return listObject.size() == count;
            }
        };
    } // namespace

    SW_DEV_COMMAND( UndoCoverage, "editor.undoCoverage", "editor.undoCoverage [skipCommandID...]",
                    "Run every scene command (transaction-taking), undo it and check the scene hash is back (scenario editor/undocoverage)", &EditorUndoCoverageInternal::run );
    SW_DEV_COMMAND( UndoCoverageProperties, "editor.undoCoverageProperties", "editor.undoCoverageProperties",
                    "Edit every inspector property of the selection once through the property grid model, undo it and check the scene hash is back (scenario editor/undocoverage)",
                    &EditorUndoCoverageInternal::runProperties );
    SW_DEV_COMMAND( UndoScaleScene, "editor.undoScaleScene", "editor.undoScaleScene <count>",
                    "Create <count> scene-component objects at the scene root without an undo step and select them all (scenario editor/inspectorundoscale)",
                    &EditorUndoCoverageInternal::runScaleScene );
} // namespace sw::editor

#include "pch.h"

#include "Editor/Common/Workspace/EditorTransaction.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/CommandStack.h"

namespace sw::editor
{
    namespace
    {
        struct EditorTransactionInternal
        {
            /**
             * @brief Undo 스택입니다. 없으면 nullptr 이므로 **부르는 쪽이 반드시 확인해야 합니다.**
             * @details `editor::getService<T>()` 는 문서대로 **nullptr 을 반환할 수 있습니다**(로컬 등록도 없고 모듈 서비스 표에도
             *          없을 때). 그런데 이 파일의 일곱 곳이 그 값을 그대로 `->` 로 따라가고 있었습니다. 커맨드 스택은 `EngineLoop`
             *          소유라 EditorModule 보다 오래 살고, 종료할 때는 서비스 연결이 먼저 풀립니다. 그 틈에 트랜잭션이 하나라도
             *          돌면 널 역참조입니다. 바로 아래의 씬 접근이 `getActiveScene()` 로 같은 검사를 한곳에 모아 둔 것과 같은
             *          모양입니다.
             */
            static CommandStack* getCommandStack()
            {
                return editor::getService<CommandStack>();
            }

            static GameObjectManager* getActiveGameObjectManager()
            {
                Scene* pActiveScene = editor::getActiveScene();
                if ( pActiveScene == nullptr )
                    return nullptr;

                return pActiveScene->getObjectManager();
            }

            /**
             * @brief 활성 씬을 dirty 로 표시합니다. 기록할 때와 **되돌리기 · 다시 하기가 씬을 바꿀 때** 부릅니다.
             * @details 예전에는 기록할 때만 표시해서, 편집 → 저장 → Ctrl+Z 하면 씬이 바뀌었는데도 깨끗하다고 했다 — 그대로 끄거나
             *          다른 씬을 열면 묻지도 않고 되돌린 상태를 잃었다. 컨텍스트가 없으면(테스트 · 도구) 지역 서비스로 건 워크스페이스에.
             */
            static void markActiveSceneDirty()
            {
                EditorContext*   pContext   = EditorContext::get();
                EditorWorkspace* pWorkspace = ( pContext != nullptr ) ? &pContext->getWorkspace() : editor::getService<EditorWorkspace>();
                if ( pWorkspace != nullptr )
                    pWorkspace->markSceneDirty();
            }

            /** @brief 되돌리기 · 다시 하기가 대상 오브젝트를 다시 찾는 열쇠입니다. 기록할 때 적고, 되돌릴 때 `findTargetGameObject` 로 찾습니다. */
            struct TargetKey
            {
                Uuid   _guid;       /**< 워크스페이스 guid 입니다. 다시 만든 오브젝트도 이것으로 찾습니다 */
                uint64 _objId{ 0 }; /**< 기록할 때의 오브젝트 id 입니다 */
                string _objName;    /**< 기록할 때의 이름입니다. guid · id 로 못 찾을 때 씁니다 */
            };

            /** @brief 오브젝트의 열쇠를 적습니다. guid 가 아직 없으면 이때 줍니다. */
            static TargetKey makeTargetKey( const GameObject* pObj )
            {
                EditorContext* pContext = EditorContext::get();
                TargetKey      key;
                key._objId   = pObj->getObjectId();
                key._guid    = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( key._objId ) : Uuid{};
                key._objName = string{ pObj->getName().c_str() };
                return key;
            }

            /** @brief 열쇠로 대상을 찾습니다. guid(삭제 대기가 아닌 것) → id → 이름 순입니다. 매니저가 없으면 nullptr 입니다. */
            static GameObject* findTargetGameObject( GameObjectManager* pManager, const TargetKey& key )
            {
                if ( pManager == nullptr )
                    return nullptr;

                if ( key._guid.isNull() == false )
                {
                    EditorContext* pContext = EditorContext::get();
                    if ( pContext != nullptr )
                    {
                        GameObject* pByGuid = pContext->getWorkspace().findGameObjectByGuid( key._guid );
                        if ( pByGuid != nullptr && pByGuid->isPendingDestroy() == false )
                            return pByGuid;
                    }
                }

                GameObject* pTarget = pManager->findGameObjectById( key._objId );
                if ( pTarget == nullptr && key._objName.empty() == false )
                    pTarget = pManager->findGameObjectByName( hashed_string( key._objName.c_str() ) );
                return pTarget;
            }

            /**
             * @brief XML 스냅샷을 오브젝트에 되읽고 씬 계층을 다시 잇습니다. 수정 되돌리기와 삭제 되돌리기(다시 만들기)가 함께 씁니다.
             * @details 부모는 스냅샷에 적힌 **런타임 id** 로 찾습니다(같은 실행의 스냅샷이고, 지운 오브젝트는 원래 id 로 되살아난다). 예전에는
             *          이름으로 찾아, 지운 사이 같은 이름의 오브젝트가 생기면(엔진이 프레임마다 만드는 "GameCamera") 자식이 그쪽에 붙었다.
             */
            static void loadXmlSnapshot( GameObject* pTarget, const EditorObjectSnapshot& snapshot )
            {
                ObjectLoadContext context{};
                context._pIdentity = &snapshot._identity;
                ObjectStateSerializer::loadFromXmlString( pTarget, snapshot._xml, context );
            }

            /** @brief 활성 씬에서 대상을 다시 찾아 XML 스냅샷을 되읽습니다. 대상이 없으면 아무것도 하지 않습니다. */
            static void restoreXmlSnapshot( const TargetKey& key, const EditorObjectSnapshot& snapshot )
            {
                GameObject* pTarget = findTargetGameObject( getActiveGameObjectManager(), key );
                if ( pTarget == nullptr )
                    return;
                loadXmlSnapshot( pTarget, snapshot );
                markActiveSceneDirty();
            }

            /**
             * @brief 기록을 스택에 넣고 활성 씬을 dirty 로 표시합니다. 오브젝트 기록 둘(수정 · 생성/삭제)이 함께 씁니다.
             * @details 스택이 없어도 **씬은 이미 바뀌었습니다.** 되돌리기 기록만 남기지 못할 뿐이므로 dirty 는 표시합니다.
             */
            static void pushCommand( CommandStack::Command&& cmd )
            {
                CommandStack* pStack = getCommandStack();
                if ( pStack != nullptr )
                    pStack->push( std::move( cmd ) );
                markActiveSceneDirty();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorTransaction::beginTransaction( string_view label )
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->beginTransaction( label );
    }

    void EditorTransaction::endTransaction()
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->endTransaction();
    }

    void EditorTransaction::cancelTransaction()
    {
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack == nullptr )
            return;
        pStack->cancelTransaction();
    }

    EditorObjectSnapshot EditorTransaction::captureSnapshot( const GameObject* pObj )
    {
        EditorObjectSnapshot snapshot;
        if ( pObj == nullptr )
            return snapshot;
        snapshot._xml      = ObjectStateSerializer::saveToXmlString( pObj );
        snapshot._identity = ObjectStateSerializer::captureIdentity( pObj );
        return snapshot;
    }

    void EditorTransaction::recordModify( GameObject* pObj, const EditorObjectSnapshot& before, const EditorObjectSnapshot& after,
                                          string_view label )
    {
        if ( pObj == nullptr || before._xml == after._xml )
            return;

        const EditorTransactionInternal::TargetKey key = EditorTransactionInternal::makeTargetKey( pObj );
        CommandStack::Command                      cmd{};
        cmd._label = string{ label };
        cmd._undo  = [key, beforeSnapshot = before]()
        {
            EditorTransactionInternal::restoreXmlSnapshot( key, beforeSnapshot );
        };
        cmd._redo = [key, afterSnapshot = after]()
        {
            EditorTransactionInternal::restoreXmlSnapshot( key, afterSnapshot );
        };
        EditorTransactionInternal::pushCommand( std::move( cmd ) );
    }

    void EditorTransaction::recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit )
    {
        if ( pObj == nullptr )
            return;

        EditorContext*                             pContext   = EditorContext::get();
        const EditorTransactionInternal::TargetKey key        = EditorTransactionInternal::makeTargetKey( pObj );
        const EditorObjectSnapshot                 snapshot   = captureSnapshot( pObj );
        const string                               prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( key._objId ) : string{};

        // 오브젝트를 없애는 절차. 선택에서 먼저 빼는 것이 중요하다. 파괴는 지연 큐를 거치므로, 선택에
        // 남겨 두면 실제로 사라질 때까지 인스펙터 · 기즈모가 그 오브젝트를 계속 대상으로 삼는다.
        Delegate<void()> destroyStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [key]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, key );
            if ( pTarget != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && pCurrentContext->getSelectionManager().hasObject( pTarget ) )
                    pCurrentContext->getSelectionManager().selectObject( pTarget, SelectionMode::Remove );
                pManager->destroyObject( pTarget );
                EditorTransactionInternal::markActiveSceneDirty();
            }
        } );

        // 저장해 둔 XML 로 오브젝트를 되살리는 절차. **원래 id 로** 되살린다. 그래야 이 오브젝트와 그 컴포넌트를
        // 가리키던 핸들(선택 · 다른 기록 · 씬의 활성 카메라)이 그대로 이어진다. guid 도 되돌려 놓아 다음 되돌리기가
        // 같은 오브젝트를 다시 찾게 한다.
        Delegate<void()> recreateStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [key, snapshot, prefabPath]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pCreated = pManager->createGameObjectWithId( hashed_string( key._objName.c_str() ), snapshot._identity._objectId );
            if ( pCreated != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && key._guid.isNull() == false )
                    pCurrentContext->getWorkspace().setGuid( pCreated->getObjectId(), key._guid );
                EditorTransactionInternal::loadXmlSnapshot( pCreated, snapshot );
                if ( pCurrentContext != nullptr )
                {
                    pCurrentContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
                    pCurrentContext->getSelectionManager().selectObject( pCreated, SelectionMode::Replace );
                }
                EditorTransactionInternal::markActiveSceneDirty();
            }
        } );

        // **여기가 이 함수의 전부다.** 생성과 삭제는 같은 두 절차를 반대 순서로 잇는 것이다.
        CommandStack::Command cmd{};
        cmd._label                         = string{ label };
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;
        EditorTransactionInternal::pushCommand( std::move( cmd ) );
    }

    void EditorTransaction::recordCreation( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Created );
    }

    void EditorTransaction::recordDestruction( GameObject* pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Destroyed );
    }

    void EditorTransaction::push( Delegate<void()> undo, Delegate<void()> redo, string_view label,
                                  string_view coalesceKey )
    {
        CommandStack* pStack = editor::getService<CommandStack>();
        if ( pStack == nullptr )
            return;

        CommandStack::Command cmd;
        cmd._label = string{ label };
        cmd._undo  = std::move( undo );
        cmd._redo  = std::move( redo );
        if ( coalesceKey.empty() == false )
            pStack->pushCoalesce( coalesceKey, std::move( cmd ) );
        else
            pStack->push( std::move( cmd ) );
    }

    void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                                const EditorDocumentRestoreDelegate& restore, string_view coalesceKey )
    {
        recordDocumentText( beforeText, afterText, label, restore, {}, coalesceKey );
    }

    void EditorTransaction::recordDocumentText( string_view beforeText, string_view afterText, string_view label,
                                                const EditorDocumentRestoreDelegate& restore, const EditorDocumentCaptureDelegate& capture,
                                                string_view coalesceKey )
    {
        if ( beforeText == afterText || restore.isBound() == false )
            return;

        const StringChangeSpan span = StringUtil::makeChangeSpan( beforeText, afterText );
        if ( capture.isBound() )
        {
            push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( StringUtil::reconstructBefore( span, capture() ) );
            } ),
                  SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, capture, span]()
            {
                restore( StringUtil::reconstructAfter( span, capture() ) );
            } ),
                  label, coalesceKey );
            return;
        }

        const string beforeStr{ beforeText };
        const string afterStr{ afterText };
        push( SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, beforeStr]()
        {
            restore( beforeStr );
        } ),
              SW_DELEGATE_LAMBDA( Delegate<void()>, [restore, afterStr]()
        {
            restore( afterStr );
        } ),
              label, coalesceKey );
    }
} // namespace sw::editor

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

            static void markActiveSceneDirty()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                pContext->getWorkspace().markSceneDirty();
            }

            static GameObject* findTargetGameObject( GameObjectManager* pManager, const Uuid& guid, uint64 objId, string_view objName )
            {
                if ( pManager == nullptr )
                    return nullptr;

                if ( guid.isNull() == false )
                {
                    EditorContext* pContext = EditorContext::get();
                    if ( pContext != nullptr )
                    {
                        GameObject* pByGuid = pContext->getWorkspace().findGameObjectByGuid( guid );
                        if ( pByGuid != nullptr && pByGuid->isPendingKill() == false )
                            return pByGuid;
                    }
                }

                GameObject* pTarget = pManager->findGameObjectById( objId );
                if ( pTarget == nullptr && objName.empty() == false )
                    pTarget = pManager->findGameObjectByName( hashed_string( objName.data(), static_cast<uint32>( objName.size() ) ) );
                return pTarget;
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

    bool EditorTransaction::captureBinarySnapshot( const GameObject* pObj, EditorObjectBinarySnapshot& outSnapshot )
    {
        outSnapshot = EditorObjectBinarySnapshot{};
        if ( pObj == nullptr )
            return false;
        outSnapshot._identity = ObjectStateSerializer::captureIdentity( pObj );
        return ObjectStateSerializer::saveToBinaryBuffer( pObj, outSnapshot._bytes );
    }

    void EditorTransaction::recordBinaryModify( GameObject* pObj, const EditorObjectBinarySnapshot& before, const EditorObjectBinarySnapshot& after,
                                                string_view label )
    {
        if ( pObj == nullptr || before._bytes == after._bytes )
            return;

        EditorContext* pContext = EditorContext::get();
        const uint64   objId    = pObj->getObjectId();
        const Uuid     guid     = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string   objName  = string{ pObj->getName().c_str() };

        // **람다가 직접 캡처한다.** 예전에는 여기서 `beforeBuf`/`afterBuf` 지역 사본을 하나씩
        // 만들고 그것을 다시 람다가 값으로 캡처해서, 스냅샷마다 **바이트를 두 번** 복사했다.
        // 오브젝트 하나의 바이너리 스냅샷은 수 KB 가 될 수 있고 편집마다 기록된다.
        // 되돌릴 때는 찍을 때의 컴포넌트 id 도 함께 되살린다. 그래야 그 컴포넌트를 가리키던 핸들이 끊기지 않는다.
        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = [guid, objId, objName, beforeSnapshot = before]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr || beforeSnapshot._bytes.empty() )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                string parentName;
                ObjectStateSerializer::loadFromBinaryBuffer( pTarget, beforeSnapshot._bytes.data(), beforeSnapshot._bytes.size(), parentName,
                                                             &beforeSnapshot._identity );
            }
        };

        cmd._redo = [guid, objId, objName, afterSnapshot = after]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr || afterSnapshot._bytes.empty() )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                string parentName;
                ObjectStateSerializer::loadFromBinaryBuffer( pTarget, afterSnapshot._bytes.data(), afterSnapshot._bytes.size(), parentName,
                                                             &afterSnapshot._identity );
            }
        };

        // 스택이 없어도 **씬은 이미 바뀌었다.** 되돌리기 기록만 남기지 못할 뿐이므로 dirty 는 표시한다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
    }

    void EditorTransaction::recordModify( GameObject* pObj, const EditorObjectSnapshot& before, const EditorObjectSnapshot& after,
                                          string_view label )
    {
        if ( pObj == nullptr || before._xml == after._xml )
            return;

        EditorContext* pContext = EditorContext::get();
        const uint64   objId    = pObj->getObjectId();
        const Uuid     guid     = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string   objName  = string{ pObj->getName().c_str() };

        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = [guid, objId, objName, beforeSnapshot = before]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                ObjectStateSerializer::loadFromXmlString( pTarget, beforeSnapshot._xml, &beforeSnapshot._identity );
                ObjectStateSerializer::rebindSceneHierarchy( pTarget, beforeSnapshot._xml );
            }
        };

        cmd._redo = [guid, objId, objName, afterSnapshot = after]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                ObjectStateSerializer::loadFromXmlString( pTarget, afterSnapshot._xml, &afterSnapshot._identity );
                ObjectStateSerializer::rebindSceneHierarchy( pTarget, afterSnapshot._xml );
            }
        };

        // 스택이 없어도 **씬은 이미 바뀌었다.** 되돌리기 기록만 남기지 못할 뿐이므로 dirty 는 표시한다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
    }

    void EditorTransaction::recordObjectLifetime( GameObject* pObj, string_view label, ObjectLifetimeEdit edit )
    {
        if ( pObj == nullptr )
            return;

        EditorContext*             pContext   = EditorContext::get();
        const uint64               objId      = pObj->getObjectId();
        const Uuid                 guid       = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string               objName    = string{ pObj->getName().c_str() };
        const EditorObjectSnapshot snapshot   = captureSnapshot( pObj );
        const string               prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( objId ) : string{};

        // 오브젝트를 없애는 절차. 선택에서 먼저 빼는 것이 중요하다. 파괴는 지연 큐를 거치므로, 선택에
        // 남겨 두면 실제로 사라질 때까지 인스펙터 · 기즈모가 그 오브젝트를 계속 대상으로 삼는다.
        Delegate<void()> destroyStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [guid, objId, objName]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && pCurrentContext->getSelectionManager().hasObject( pTarget ) )
                    pCurrentContext->getSelectionManager().selectObject( pTarget, SelectionMode::Remove );
                pManager->destroyObject( pTarget );
            }
        } );

        // 저장해 둔 XML 로 오브젝트를 되살리는 절차. **원래 id 로** 되살린다. 그래야 이 오브젝트와 그 컴포넌트를
        // 가리키던 핸들(선택 · 다른 기록 · 씬의 활성 카메라)이 그대로 이어진다. guid 도 되돌려 놓아 다음 되돌리기가
        // 같은 오브젝트를 다시 찾게 한다.
        Delegate<void()> recreateStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [guid, objName, snapshot, prefabPath]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pCreated = pManager->createGameObjectWithId( hashed_string( objName.c_str() ), snapshot._identity._objectId );
            if ( pCreated != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && guid.isNull() == false )
                    pCurrentContext->getWorkspace().setGuid( pCreated->getObjectId(), guid );
                ObjectStateSerializer::loadFromXmlString( pCreated, snapshot._xml, &snapshot._identity );
                ObjectStateSerializer::rebindSceneHierarchy( pCreated, snapshot._xml );
                if ( pCurrentContext != nullptr )
                {
                    pCurrentContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
                    pCurrentContext->getSelectionManager().selectObject( pCreated, SelectionMode::Replace );
                }
            }
        } );

        // **여기가 이 함수의 전부다.** 생성과 삭제는 같은 두 절차를 반대 순서로 잇는 것이다.
        CommandStack::Command cmd{};
        cmd._label                         = string{ label };
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;

        // 스택이 없어도 **씬은 이미 바뀌었다.** 되돌리기 기록만 남기지 못할 뿐이므로 dirty 는 표시한다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
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

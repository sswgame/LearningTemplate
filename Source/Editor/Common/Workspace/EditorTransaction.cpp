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
             * @brief Undo 스택. 없으면 nullptr — **부르는 쪽은 반드시 확인한다.**
             * @details `editor::getService<T>()` 는 문서대로 **nullptr 을 돌려줄 수 있다**
             *          (지역 등록도 없고 모듈 서비스 표에도 없을 때). 그런데 이 파일의 일곱
             *          자리가 그 값을 그대로 `->` 로 따라가고 있었다. 커맨드 스택은 `EngineLoop`
             *          소유라 EditorModule 보다 오래 살고, 종료할 때 서비스 결합이 먼저 풀린다 —
             *          그 창에서 트랜잭션이 하나라도 돌면 널 역참조다. 바로 아래 씬 접근이
             *          `getActiveScene()` 로 같은 검사를 한 자리에 모아 둔 것과 같은 모양이다.
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

    string EditorTransaction::captureSnapshot( const GameObjectPtr& pObj )
    {
        GameObject* pRaw = pObj.get();
        if ( pRaw == nullptr )
            return {};
        return ObjectStateSerializer::saveToXmlString( pRaw );
    }

    bool EditorTransaction::captureBinarySnapshot( const GameObjectPtr& pObj, vector<uint8>& outBytes )
    {
        GameObject* pRaw = pObj.get();
        if ( pRaw == nullptr )
            return false;
        return ObjectStateSerializer::saveToBinaryBuffer( pRaw, outBytes );
    }

    void EditorTransaction::recordBinaryModify( const GameObjectPtr& pObj, const vector<uint8>& beforeBytes, const vector<uint8>& afterBytes,
                                                string_view label )
    {
        GameObject* pRaw = pObj.get();
        if ( pRaw == nullptr || beforeBytes == afterBytes )
            return;

        EditorContext* pContext = EditorContext::get();
        const uint64   objId    = pRaw->getObjectId();
        const Uuid     guid     = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string   objName  = string{ pRaw->getName().c_str() };

        // **람다가 직접 캡처한다.** 예전에는 여기서 `beforeBuf`/`afterBuf` 지역 사본을 하나씩
        // 만들고 그것을 다시 람다가 값으로 캡처해서, 스냅샷마다 **바이트를 두 번** 복사했다.
        // 오브젝트 하나의 바이너리 스냅샷은 수 KB 가 될 수 있고 편집마다 기록된다.
        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = [guid, objId, objName, beforeBuf = beforeBytes]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr || beforeBuf.empty() )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                string parentName;
                ObjectStateSerializer::loadFromBinaryBuffer( pTarget, beforeBuf.data(), beforeBuf.size(), parentName );
            }
        };

        cmd._redo = [guid, objId, objName, afterBuf = afterBytes]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr || afterBuf.empty() )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                string parentName;
                ObjectStateSerializer::loadFromBinaryBuffer( pTarget, afterBuf.data(), afterBuf.size(), parentName );
            }
        };

        // 스택이 없어도 **씬은 이미 바뀌었다** — 되돌리기 기록만 못 남길 뿐이므로 dirty 는 찍는다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
    }

    void EditorTransaction::recordModify( const GameObjectPtr& pObj, string_view beforeXml, string_view afterXml,
                                          string_view label )
    {
        GameObject* pRaw = pObj.get();
        if ( pRaw == nullptr || beforeXml == afterXml )
            return;

        EditorContext* pContext  = EditorContext::get();
        const uint64   objId     = pRaw->getObjectId();
        const Uuid     guid      = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string   objName   = string{ pRaw->getName().c_str() };
        const string   beforeStr = string{ beforeXml };
        const string   afterStr  = string{ afterXml };

        CommandStack::Command cmd{};
        cmd._label = string{ label };
        cmd._undo  = [guid, objId, objName, beforeStr]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                ObjectStateSerializer::loadFromXmlString( pTarget, beforeStr );
                ObjectStateSerializer::rebindSceneHierarchy( pTarget, beforeStr );
            }
        };

        cmd._redo = [guid, objId, objName, afterStr]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                ObjectStateSerializer::loadFromXmlString( pTarget, afterStr );
                ObjectStateSerializer::rebindSceneHierarchy( pTarget, afterStr );
            }
        };

        // 스택이 없어도 **씬은 이미 바뀌었다** — 되돌리기 기록만 못 남길 뿐이므로 dirty 는 찍는다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
    }

    void EditorTransaction::recordObjectLifetime( const GameObjectPtr& pObj, string_view label, ObjectLifetimeEdit edit )
    {
        GameObject* pRaw = pObj.get();
        if ( pRaw == nullptr )
            return;

        EditorContext* pContext   = EditorContext::get();
        const uint64   objId      = pRaw->getObjectId();
        const Uuid     guid       = ( pContext != nullptr ) ? pContext->getWorkspace().getOrAssignGuid( objId ) : Uuid{};
        const string   objName    = string{ pRaw->getName().c_str() };
        const string   stateXml   = ObjectStateSerializer::saveToXmlString( pRaw );
        const string   prefabPath = ( pContext != nullptr ) ? pContext->getWorkspace().getGameObjectPrefabPath( objId ) : string{};

        // 오브젝트를 없애는 절차. 선택에서 먼저 빼는 것이 중요하다 — 선택 목록이 죽은 오브젝트를
        // 들고 있으면 다음 프레임의 인스펙터·기즈모가 그것을 따라간다.
        Delegate<void()> destroyStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [guid, objId, objName]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pTarget = EditorTransactionInternal::findTargetGameObject( pManager, guid, objId, objName );
            if ( pTarget != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && pCurrentContext->getSelectionManager().hasObject( GameObjectPtr{ pTarget } ) )
                    pCurrentContext->getSelectionManager().selectObject( GameObjectPtr{ pTarget }, SelectionMode::Remove );
                pManager->destroyObject( pTarget );
            }
        } );

        // 저장해 둔 XML 로 오브젝트를 되살리는 절차. guid 를 먼저 되돌려 놓아야 다음 되돌리기가
        // 같은 오브젝트를 다시 찾을 수 있다.
        Delegate<void()> recreateStep = SW_DELEGATE_LAMBDA( Delegate<void()>, [guid, objName, stateXml, prefabPath]()
        {
            GameObjectManager* pManager = EditorTransactionInternal::getActiveGameObjectManager();
            if ( pManager == nullptr )
                return;

            GameObject* pCreated = pManager->createGameObject( hashed_string( objName.c_str() ) );
            if ( pCreated != nullptr )
            {
                EditorContext* pCurrentContext = EditorContext::get();
                if ( pCurrentContext != nullptr && guid.isNull() == false )
                    pCurrentContext->getWorkspace().setGuid( pCreated->getObjectId(), guid );
                ObjectStateSerializer::loadFromXmlString( pCreated, stateXml );
                ObjectStateSerializer::rebindSceneHierarchy( pCreated, stateXml );
                if ( pCurrentContext != nullptr )
                {
                    pCurrentContext->getWorkspace().setGameObjectPrefabPath( pCreated->getObjectId(), prefabPath );
                    pCurrentContext->getSelectionManager().selectObject( GameObjectPtr{ pCreated }, SelectionMode::Replace );
                }
            }
        } );

        // **여기가 이 함수의 전부다** — 생성과 삭제는 같은 두 절차를 반대로 잇는 것이다.
        CommandStack::Command cmd{};
        cmd._label                         = string{ label };
        const bool bUndoRecreatesTheObject = ( edit == ObjectLifetimeEdit::Destroyed );
        cmd._undo                          = bUndoRecreatesTheObject ? recreateStep : destroyStep;
        cmd._redo                          = bUndoRecreatesTheObject ? destroyStep : recreateStep;

        // 스택이 없어도 **씬은 이미 바뀌었다** — 되돌리기 기록만 못 남길 뿐이므로 dirty 는 찍는다.
        CommandStack* pStack = EditorTransactionInternal::getCommandStack();
        if ( pStack != nullptr )
            pStack->push( std::move( cmd ) );
        EditorTransactionInternal::markActiveSceneDirty();
    }

    void EditorTransaction::recordCreation( const GameObjectPtr& pObj, string_view label )
    {
        recordObjectLifetime( pObj, label, ObjectLifetimeEdit::Created );
    }

    void EditorTransaction::recordDestruction( const GameObjectPtr& pObj, string_view label )
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

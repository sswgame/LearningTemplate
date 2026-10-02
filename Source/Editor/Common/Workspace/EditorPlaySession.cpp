#include "pch.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"

#include "Core/Log/Logger.h"
#include "Core/Uuid/Uuid.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

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
        struct EditorPlaySessionInternal
        {
            /**
             * @brief 월드의 플레이 상태를 바꿉니다. 활성 씬의 시작 · 끝은 `SceneManager` 가 합니다 — 플레이 중에 씬을 바꾸면 새 씬도 시작합니다.
             * @details 예전에는 여기서 활성 씬의 매니저에 직접 `beginPlay` · `endPlay` 를 불렀고, 플레이 중에 연 씬은 시작하지 않았습니다.
             */
            static void setWorldPlaying( bool bPlaying )
            {
                SceneManager* pSceneManager = editor::getService<SceneManager>();
                if ( pSceneManager != nullptr )
                    pSceneManager->setWorldPlaying( bPlaying );
            }

            static void capturePlaySnapshot( PlaySessionData& data )
            {
                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;

                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return;

                // Stop 때 활성 씬이 이 씬인지 알아보려고 세대를, 아니면 다시 세우려고 이름 · 소스 경로를 적는다.
                SceneManager* pSceneManager = editor::getService<SceneManager>();
                data._sceneGeneration       = pSceneManager != nullptr ? pSceneManager->getSceneGeneration() : 0;
                data._sceneName             = pScene->getName();
                data._sceneSourcePath       = pScene->getSourcePath();

                GameObjectManager* pObjects = pScene->getObjectManager();
                EditorContext*     pContext = EditorContext::get();
                // 예전에는 여기서 getAllGameObjects() 를 두 번 불렀다. 개수를 세려고 한 번, 순회하려고 한 번이다.
                // 값으로 반환하는 함수라 씬 전체를 두 번 할당하고 복사했다.
                vector<GameObject*> listAllObject;
                pObjects->getAllGameObjects( listAllObject );
                data._listSnapshot.reserve( listAllObject.size() );

                for ( GameObject* pObj : listAllObject )
                {
                    if ( pObj == nullptr )
                        continue;

                    PlaySessionData::ObjectSnapshot entry;
                    entry._identity   = ObjectStateSerializer::captureIdentity( pObj );
                    entry._name       = pObj->getName().c_str();
                    entry._prefabPath = pScene->getEntityPrefabPath( entry._identity._objectId );
                    if ( pContext != nullptr )
                        entry._guid = pContext->getWorkspace().getOrAssignGuid( entry._identity._objectId );

                    if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, entry._bytes ) )
                        data._listSnapshot.push_back( std::move( entry ) );
                }

                data._bHasSnapshot = SW_TRUE;
                SW_LOG_TRACE( "Play snapshot captured (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );
            }

            static void restorePlaySnapshot( PlaySessionData& data )
            {
                if ( data._bHasSnapshot == SW_FALSE )
                    return;

                // 플레이 중에 활성 씬이 바뀌었으면(게임 코드가 다음 레벨을 열었다 · 씬 로드가 끝났다) 스냅샷은 지금 씬의 것이 아니다. 예전에는
                // 그 씬에 그대로 되돌려 두 씬의 오브젝트가 섞였고, 활성 씬은 플레이 중에 연 씬의 소스 경로를 든 채라 저장하면 **그 씬 파일**을
                // 덮어썼다. 편집하던 씬을 빈 씬으로 다시 세우고(이름 · 소스 경로) 거기에 되돌린다. 로드가 아직 돌고 있으면 끝나며 씬을 다시
                // 바꿔 놓으므로 먼저 거둔다. 오브젝트는 원래 id 로 되살아나므로 프리팹 연결도 id 로 다시 맨다(아래 2).
                SceneManager* pSceneManager = editor::getService<SceneManager>();
                if ( pSceneManager != nullptr && pSceneManager->getSceneGeneration() != data._sceneGeneration )
                {
                    pSceneManager->cancelPendingAsyncLoads();
                    Scene* pRebuilt = pSceneManager->createEmptyActiveScene( data._sceneName );
                    if ( pRebuilt != nullptr )
                        pRebuilt->setSourcePath( data._sceneSourcePath );
                    SW_LOG_INFO( "플레이 중에 활성 씬이 바뀌었습니다 — 편집하던 씬 '%#' 을 다시 세워 되돌립니다.", data._sceneName.c_str() );
                }

                Scene*             pScene   = editor::getActiveScene();
                GameObjectManager* pObjects = editor::getActiveObjectManager();
                if ( pObjects == nullptr )
                {
                    data._listSnapshot.clear();
                    data._bHasSnapshot = SW_FALSE;
                    return;
                }

                EditorContext* pContext = EditorContext::get();

                // 1. 플레이 도중 생성된 오브젝트 파괴
                {
                    unordered_set<uint64> uniqueSnapIds;
                    uniqueSnapIds.reserve( data._listSnapshot.size() );
                    for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                    {
                        uniqueSnapIds.insert( snap._identity._objectId );
                    }

                    vector<GameObject*> listAllObject;
                    pObjects->getAllGameObjects( listAllObject );

                    vector<GameObject*> listToDestroy;
                    for ( GameObject* pObj : listAllObject )
                    {
                        if ( pObj != nullptr && uniqueSnapIds.find( pObj->getObjectId() ) == uniqueSnapIds.end() )
                            listToDestroy.push_back( pObj );
                    }
                    for ( GameObject* pObj : listToDestroy )
                    {
                        pObjects->destroyObject( pObj );
                    }
                }

                // 2. 기존 오브젝트 상태 복구 및 삭제된 오브젝트 재생성
                unordered_map<uint64, GameObject*> mapRestored;
                for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    GameObject* pObj = pObjects->findGameObjectById( snap._identity._objectId );
                    if ( pObj == nullptr && snap._guid.isNull() == false && pContext != nullptr )
                        pObj = pContext->getWorkspace().findGameObjectByGuid( snap._guid );

                    // 플레이 중에 사라진 오브젝트는 원래 id 로 되살린다. 플레이 전에 들고 있던 핸들이 이어지게 하기 위해서다.
                    if ( pObj == nullptr )
                    {
                        pObj = pObjects->createGameObjectWithId( hashed_string( snap._name.c_str() ), snap._identity._objectId );
                        if ( pObj == nullptr )
                        {
                            SW_LOG_WARNING( "Failed to recreate '%#' from play snapshot.", snap._name.c_str() );
                            continue;
                        }
                    }

                    if ( snap._guid.isNull() == false && pContext != nullptr )
                        pContext->getWorkspace().setGuid( pObj->getObjectId(), snap._guid );
                    if ( snap._prefabPath.empty() == false && pScene != nullptr )
                        pScene->setEntityPrefabPath( pObj->getObjectId(), snap._prefabPath );

                    mapRestored[snap._identity._objectId] = pObj;

                    string parentName;
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pObj, snap._bytes.data(), snap._bytes.size(), parentName, &snap._identity ) == 0 )
                        SW_LOG_WARNING( "Failed to restore '%#' from binary play snapshot.", snap._name.c_str() );
                }

                // 3. 계층을 다시 잇는다 — **모두 읽은 뒤에.** 읽는 동안에는 부모를 이름으로 찾는데, 플레이 중에 부모도 지워졌고 자식이 스냅샷에서
                // 먼저 나오면(먼저 만들었으면) 그 순간 부모가 아직 없어 루트로 남았다. 씬 로드(`Scene::instantiate`)와 같은 두 단계다. 예전에는
                // 이 단계가 XML 폴백에만 있었는데, 그 폴백은 바이너리 저장이 실패할 때만(오브젝트가 null) 타서 실제로는 돌지 않았다.
                for ( const auto& [objectId, pObj] : mapRestored )
                {
                    (void)objectId;
                    ObjectStateSerializer::rebindSceneHierarchy( pObj );
                }

                SW_LOG_TRACE( "Play snapshot restored (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );

                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;
            }

            /**
             * @brief 컨텍스트가 들고 있는 플레이 상태입니다. 컨텍스트가 없으면 nullptr 입니다.
             * @details 에디터 셸이 아직 서지 않았거나 이미 내려간 시점에도 이 파사드가 불릴 수 있습니다(패널 정리 경로). 그때는
             *          "정지" 로 답해야 합니다.
             */
            static PlaySessionData* data()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? &pContext->getPlaySessionData() : nullptr;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPlaySession" );

    PlaySessionState EditorPlaySession::getState()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr ? pData->_state : PlaySessionState::Stopped;
    }

    bool EditorPlaySession::isPlaying()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData == nullptr )
            return false;
        if ( pData->_bStepPending == SW_TRUE )
            return true;
        return pData->_state == PlaySessionState::Playing;
    }

    bool EditorPlaySession::isPaused()
    {
        return getState() == PlaySessionState::Paused;
    }

    bool EditorPlaySession::isStopped()
    {
        return getState() == PlaySessionState::Stopped;
    }

    bool EditorPlaySession::hasPendingStep()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr && pData->_bStepPending == SW_TRUE;
    }

    bool EditorPlaySession::isPlayQueued()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr && pData->_bStartQueued != SW_FALSE;
    }

    void EditorPlaySession::stepOnce()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData == nullptr )
            return;
        if ( pData->_state == PlaySessionState::Stopped )
        {
            setState( PlaySessionState::Playing );
            if ( pData->_state == PlaySessionState::Stopped ) // 시작을 미뤘다(씬을 여는 중) — 로드가 끝나면 플레이로 시작한다
                return;
        }
        pData->_bStepPending = SW_TRUE;
    }

    void EditorPlaySession::consumePendingStep()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData == nullptr || pData->_bStepPending == SW_FALSE )
            return;
        pData->_bStepPending = SW_FALSE;
        if ( pData->_state == PlaySessionState::Playing )
            pData->_state = PlaySessionState::Paused;
    }

    void EditorPlaySession::setState( PlaySessionState state )
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            setState( *pData, state );
    }

    void EditorPlaySession::update()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            update( *pData );
    }

    void EditorPlaySession::update( PlaySessionData& data )
    {
        if ( data._bStartQueued == SW_FALSE )
            return;
        const SceneManager* pSceneManager = editor::getService<SceneManager>();
        if ( pSceneManager != nullptr && pSceneManager->isTransitioning() )
            return;
        data._bStartQueued = SW_FALSE;
        setState( data, data._queuedState );
    }

    void EditorPlaySession::setState( PlaySessionData& data, PlaySessionState state )
    {
        // Stop 은 미룬 시작도 거둔다 — 로드가 끝난 뒤 사용자가 멈춘 플레이가 저절로 시작하면 안 된다.
        if ( state == PlaySessionState::Stopped )
            data._bStartQueued = SW_FALSE;
        if ( data._state == state )
            return;

        // 씬을 여는 중이면 시작을 미룬다(언리얼 RequestPlaySession 처럼 요청을 걸어 두고 로드가 끝난 프레임에 update 가 시작한다). 지금 시작하면
        // 스냅샷은 곧 내려갈 씬을 찍고, 플레이 중에 로드가 끝나 씬이 바뀐다 — Stop 이 되돌릴 씬이 사용자가 막 연 씬이 아니게 된다.
        if ( data._state == PlaySessionState::Stopped )
        {
            const SceneManager* pSceneManager = editor::getService<SceneManager>();
            if ( pSceneManager != nullptr && pSceneManager->isTransitioning() )
            {
                if ( data._bStartQueued == SW_FALSE )
                    SW_LOG_INFO( "씬을 여는 중입니다 — 로드가 끝나면 플레이를 시작합니다." );
                data._bStartQueued = SW_TRUE;
                data._queuedState  = state;
                return;
            }
        }
        data._bStartQueued = SW_FALSE;

        PlaySessionData* pData          = &data;
        pData->_bStepPending            = SW_FALSE;
        const PlaySessionState previous = pData->_state;
        pData->_state                   = state;

        CommandStack* pCommandStack = editor::getService<CommandStack>();
        if ( pCommandStack != nullptr )
            pCommandStack->clear();

        // **멈춤을 떠날 때 · 멈춤으로 돌아올 때로 가른다(목표 상태가 무엇이든).** 예전에는 Stopped → Playing 만 월드를 켜서, 멈춤에서
        // 일시정지를 누른 뒤 Play 하면 스냅샷도 onBeginPlay 도 없이 플레이가 돌았고 Stop 이 편집 씬을 되돌리지 못했다.
        // 멈춤 → 일시정지는 일시정지 상태로 플레이를 시작한다(스냅샷 · 시작은 하고 씬은 틱하지 않는다).
        if ( previous == PlaySessionState::Stopped )
        {
            captureSnapshot( *pData );
            EditorPlaySessionInternal::setWorldPlaying( true );
        }
        else if ( state == PlaySessionState::Stopped )
        {
            EditorPlaySessionInternal::setWorldPlaying( false );
            restoreSnapshot( *pData );
        }
    }

    void EditorPlaySession::captureSnapshot( PlaySessionData& data )
    {
        EditorPlaySessionInternal::capturePlaySnapshot( data );
    }

    void EditorPlaySession::restoreSnapshot( PlaySessionData& data )
    {
        EditorPlaySessionInternal::restorePlaySnapshot( data );
    }

} // namespace sw::editor

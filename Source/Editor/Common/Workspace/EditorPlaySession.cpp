#include "pch.h"

#include "Editor/Common/Workspace/EditorPlaySession.h"

#include "Core/Log/Logger.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
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
             * @details 활성 씬의 매니저에 직접 `beginPlay` · `endPlay` 를 부르지 않습니다 — 그러면 플레이 중에 연 씬이 시작하지 않습니다.
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

                GameObjectManager*  pObjects = pScene->getObjectManager();
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

                // 플레이 중에 활성 씬이 바뀌었으면(게임 코드가 다음 레벨을 열었다 · 씬 로드가 끝났다) 스냅샷은 지금 씬의 것이 아니다. 그 씬에
                // 그대로 되돌리면 두 씬의 오브젝트가 섞이고, 활성 씬은 플레이 중에 연 씬의 소스 경로를 든 채라 저장하면 **그 씬 파일**을
                // 덮어쓴다. 편집하던 씬을 빈 씬으로 다시 세우고(이름 · 소스 경로) 거기에 되돌린다. 로드가 아직 돌고 있으면 끝나며 씬을 다시
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

                // 2. 기존 오브젝트 상태 복구 및 삭제된 오브젝트 재생성. 계층은 모두 읽은 뒤 묶음이 잇는다(아래 3).
                ObjectStateBatch batch( ObjectIdSpace::Live );
                for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    GameObject* pObj = pObjects->findGameObjectById( snap._identity._objectId );

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

                    if ( snap._prefabPath.empty() == false && pScene != nullptr )
                        pScene->setEntityPrefabPath( pObj->getObjectId(), snap._prefabPath );

                    ObjectLoadContext context{};
                    context._pIdentity = &snap._identity;
                    context._pBatch    = &batch;
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pObj, snap._bytes.data(), snap._bytes.size(), context ) == 0 )
                        SW_LOG_WARNING( "Failed to restore '%#' from binary play snapshot.", snap._name.c_str() );
                }

                // 3. 계층을 다시 잇는다 — **모두 읽은 뒤에**, 부모의 **원래 id** 로. 읽는 자리에서 이으면 플레이 중에 부모도 지워졌고 자식이
                // 스냅샷에서 먼저 나올 때 부모가 아직 없어 루트로 남는다. 이름으로 찾으면 플레이 중 이름을 바꾼 오브젝트(A 가 B 의
                // 이름을 가져갔다)의 자식이 엉뚱한 쪽에 붙는다. 씬 로드(`Scene::instantiate`)와 같은 묶음이다.
                batch.finish();

                SW_LOG_TRACE( "Play snapshot restored (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );

                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;
            }

            /**
             * @brief 시작 위치로 고른 오브젝트를 순간이동합니다. 옮길 것이 없으면 경고만 남깁니다.
             * @return 옮겼으면 true
             */
            [[nodiscard]] static bool moveToStartPosition( const PlaySessionData& data )
            {
                Scene*             pScene   = editor::getActiveScene();
                GameObjectManager* pObjects = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                if ( pObjects == nullptr )
                    return false;
                GameObject*     pTarget = EditorPlaySession::findStartObject( *pObjects, pScene->getActiveGameCamera() );
                SceneComponent* pRoot   = pTarget != nullptr ? pTarget->getComponent<SceneComponent>() : nullptr;
                if ( pRoot == nullptr )
                {
                    SW_LOG_WARNING( "Start at camera: no object tagged '%#' and no game camera to move.", EditorPlaySession::kPlayerStartTag );
                    return false;
                }
                pRoot->teleportTo( data._startPosition );
                return true;
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
        if ( pData->_pendingStepCount > 0 )
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
        return pData != nullptr && pData->_pendingStepCount > 0;
    }

    bool EditorPlaySession::isSimulating()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr && pData->_state != PlaySessionState::Stopped && pData->_bSimulate == SW_TRUE;
    }

    bool EditorPlaySession::isPlayerActive()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr && isPlayerActive( *pData );
    }

    bool EditorPlaySession::isPlayerActive( const PlaySessionData& data )
    {
        if ( data._bSimulate == SW_TRUE )
            return false;
        return data._pendingStepCount > 0 || data._state == PlaySessionState::Playing;
    }

    void EditorPlaySession::play()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            startSession( *pData, false );
    }

    void EditorPlaySession::simulate()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            startSession( *pData, true );
    }

    void EditorPlaySession::startSession( PlaySessionData& data, bool bSimulate )
    {
        // 세션 종류는 도는 중에도 바꿀 수 있다(Simulate 로 지켜보다 Play 로 조종) — 상태 전환과 따로 둔다.
        data._bSimulate = bSimulate ? SW_TRUE : SW_FALSE;
        setState( data, PlaySessionState::Playing );
    }

    PlaySessionData* EditorPlaySession::findData()
    {
        return EditorPlaySessionInternal::data();
    }

    void EditorPlaySession::setStartPosition( PlaySessionData& data, const float3& position )
    {
        data._startPosition    = position;
        data._bStartAtPosition = SW_TRUE;
    }

    void EditorPlaySession::clearStartPosition( PlaySessionData& data )
    {
        data._bStartAtPosition  = SW_FALSE;
        data._bStartMovePending = SW_FALSE;
    }

    GameObject* EditorPlaySession::findStartObject( GameObjectManager& manager, CameraComponent* pGameCamera )
    {
        const TagID playerTag = TagID::request( kPlayerStartTag );
        GameObject* pTagged   = nullptr;
        manager.forEachGameObject( [&pTagged, playerTag]( GameObject* pObj )
        {
            if ( pTagged == nullptr && pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( playerTag, true ) )
                pTagged = pObj;
        } );
        if ( pTagged != nullptr )
            return pTagged;

        // 태그가 없으면 게임 카메라를 든 오브젝트 — 1 인칭은 그것이 플레이어다. 카메라가 자식이면 맨 위 조상을 옮긴다(몸째 간다).
        GameObject* pOwner = pGameCamera != nullptr ? pGameCamera->getOwner() : nullptr;
        while ( pOwner != nullptr && pOwner->getParent() != nullptr )
        {
            pOwner = pOwner->getParent();
        }
        return pOwner;
    }

    bool EditorPlaySession::isPlayQueued()
    {
        const PlaySessionData* pData = EditorPlaySessionInternal::data();
        return pData != nullptr && pData->_bStartQueued != SW_FALSE;
    }

    void EditorPlaySession::stepFrames( uint32 frameCount )
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            stepFrames( *pData, frameCount );
    }

    void EditorPlaySession::stepFrames( PlaySessionData& data, uint32 frameCount )
    {
        if ( frameCount == 0 )
            return;
        if ( data._state == PlaySessionState::Stopped )
        {
            setState( data, PlaySessionState::Playing );
            if ( data._state == PlaySessionState::Stopped ) // 시작을 미뤘다(씬을 여는 중) — 로드가 끝나면 플레이로 시작한다
                return;
        }
        data._pendingStepCount = frameCount < kMaxStepFrameCount ? frameCount : kMaxStepFrameCount;
    }

    void EditorPlaySession::consumePendingStep()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData != nullptr )
            consumePendingStep( *pData );
    }

    void EditorPlaySession::consumePendingStep( PlaySessionData& data )
    {
        if ( data._pendingStepCount == 0 )
            return;
        --data._pendingStepCount;
        if ( data._pendingStepCount == 0 && data._state == PlaySessionState::Playing )
            data._state = PlaySessionState::Paused;
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
        // 첫 프레임이 지났다 — 첫 틱에 스폰 자리로 되돌린 게임이 있으니 한 번 더 옮긴다.
        if ( data._bStartMovePending == SW_TRUE && data._state != PlaySessionState::Stopped )
        {
            data._bStartMovePending = SW_FALSE;
            (void)EditorPlaySessionInternal::moveToStartPosition( data ); // 옮길 것이 없으면 시작할 때 이미 경고했다
        }
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
        pData->_pendingStepCount        = 0;
        const PlaySessionState previous = pData->_state;
        pData->_state                   = state;

        // 편집 내역은 Play 동안 맡겨 두고 Stop 뒤 되찾는다(아래 갈래). 플레이 중 편집은 Stop 이 스냅샷으로 되돌리므로 그 동안의 기록은 버린다 —
        // 플레이 ↔ 일시정지 사이는 비우기만 한다.
        CommandStack* pCommandStack = editor::getService<CommandStack>();
        if ( pCommandStack != nullptr && previous != PlaySessionState::Stopped && state != PlaySessionState::Stopped )
            pCommandStack->clear();

        // **멈춤을 떠날 때 · 멈춤으로 돌아올 때로 가른다(목표 상태가 무엇이든).** Stopped → Playing 만 월드를 켜면, 멈춤에서
        // 일시정지를 누른 뒤 Play 할 때 스냅샷도 onBeginPlay 도 없이 플레이가 돌고 Stop 이 편집 씬을 되돌리지 못한다.
        // 멈춤 → 일시정지는 일시정지 상태로 플레이를 시작한다(스냅샷 · 시작은 하고 씬은 틱하지 않는다).
        if ( previous == PlaySessionState::Stopped )
        {
            if ( pCommandStack != nullptr )
                pCommandStack->parkHistory();
            captureSnapshot( *pData );
            EditorPlaySessionInternal::setWorldPlaying( true );
            // 카메라 위치에서 시작 — 플레이어가 조종하는 세션만. 월드가 시작한 뒤(onBeginPlay 가 스폰 자리를 정한 뒤)에 옮긴다.
            if ( pData->_bStartAtPosition == SW_TRUE && pData->_bSimulate == SW_FALSE )
                pData->_bStartMovePending = EditorPlaySessionInternal::moveToStartPosition( *pData ) ? SW_TRUE : SW_FALSE;
        }
        else if ( state == PlaySessionState::Stopped )
        {
            pData->_bStartMovePending = SW_FALSE;
            EditorPlaySessionInternal::setWorldPlaying( false );
            restoreSnapshot( *pData );
            // 스냅샷이 오브젝트를 원래 id 로 되돌린 뒤라 맡긴 명령의 대상(id)이 다시 맞는다.
            if ( pCommandStack != nullptr )
                pCommandStack->unparkHistory();
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

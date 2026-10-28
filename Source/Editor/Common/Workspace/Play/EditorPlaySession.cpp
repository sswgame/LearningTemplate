#include "pch.h"

#include "Editor/Common/Workspace/Play/EditorPlaySession.h"

#include "Core/Log/Logger.h"
#include "Core/String/TagID.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorSessionPolicy.h"

#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Scene/CommandStack.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw::editor
{
    namespace
    {
        struct EditorPlaySessionInternal
        {
            /**
             * @brief 월드의 플레이 상태를 바꿉니다. 활성 씬의 시작과 끝은 `SceneManager` 가 처리하며, 플레이 중에 씬을 바꾸면 새 씬도 시작합니다.
             * @details 활성 씬의 매니저에 직접 `beginPlay` 와 `endPlay` 를 호출하지 않는다. 그러면 플레이 중에 연 씬이 시작하지 않는다.
             */
            static void setWorldPlaying( bool bPlaying )
            {
                editor::getSubsystem<SceneManager>().setWorldPlaying( bPlaying ); // 플레이는 에디터가 떠 있을 때만 시작한다 - SceneManager 는 필수
            }

            static void capturePlaySnapshot( PlaySessionData& data )
            {
                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;

                // 선택: 씬이 없는 상태에서도 플레이를 누를 수 있다. 되돌릴 것이 없으므로 스냅샷 없이 시작한다(`_bHasSnapshot` 이 false).
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return;

                // Stop 할 때 활성 씬이 이 씬인지 알아보려고 세대를 기록하고, 아니면 다시 만들려고 이름과 소스 경로를 기록한다.
                data._sceneGeneration = editor::getSubsystem<SceneManager>().getSceneGeneration();
                data._sceneName       = pScene->getName();
                data._sceneSourcePath = pScene->getSourcePath();

                GameObjectManager*  pObjects = pScene->getObjectManager();
                vector<GameObject*> listAllObject;
                pObjects->getAllGameObjects( listAllObject );
                data._listSnapshot.reserve( listAllObject.size() );

                for ( GameObject* pObj : listAllObject )
                {
                    if ( pObj == nullptr )
                        continue;

                    PlaySessionData::ObjectSnapshot entry;
                    entry._identity               = ObjectStateSerializer::captureIdentity( pObj );
                    entry._name                   = pObj->getName().c_str();
                    entry._prefabPath             = pScene->getEntityPrefabPath( entry._identity._objectID );
                    const PrefabNestedPart* pPart = pObj->getManager()->getSubsystem<PrefabInstanceRegistry>().findNestedPart( entry._identity._objectID );
                    if ( pPart != nullptr )
                    {
                        entry._nestedPart  = *pPart;
                        entry._bNestedPart = true;
                    }
                    const TerrainComponent* pTerrain = pObj->getComponent<TerrainComponent>();
                    if ( pTerrain != nullptr && pTerrain->hasWrittenEdits() )
                    {
                        Archive archive;
                        pTerrain->writeState( archive );
                        entry._listTerrainEdit.assign( archive.getData(), archive.getData() + archive.getSize() );
                    }

                    if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, entry._bytes, ObjectSaveOptions{ SerializePurpose::Snapshot } ) )
                        data._listSnapshot.push_back( std::move( entry ) );
                }

                data._bHasSnapshot = SW_TRUE;
                SW_LOG_TRACE( "Play snapshot captured (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );
            }

            /** @brief 되돌리기 1 단계: 스냅샷에 없는 오브젝트(플레이 도중 생성된 것)를 파괴합니다. */
            static void destroySpawnedDuringPlay( const PlaySessionData& data, GameObjectManager& objects )
            {
                unordered_set<uint64> uniqueSnapIDs;
                uniqueSnapIDs.reserve( data._listSnapshot.size() );
                for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    uniqueSnapIDs.insert( snap._identity._objectID );
                }

                vector<GameObject*> listAllObject;
                objects.getAllGameObjects( listAllObject );

                vector<GameObject*> listToDestroy;
                for ( GameObject* pObj : listAllObject )
                {
                    if ( pObj != nullptr && uniqueSnapIDs.find( pObj->getObjectID() ) == uniqueSnapIDs.end() )
                        listToDestroy.push_back( pObj );
                }
                for ( GameObject* pObj : listToDestroy )
                {
                    objects.destroyObject( pObj );
                }
            }

            /**
             * @brief 되돌리기 2 단계: 기존 오브젝트의 상태를 복구하고 삭제된 오브젝트를 원래 id 로 다시 만듭니다.
             * @details 계층은 @p inoutBatch 에 모아 두고 부르는 쪽이 모두 읽은 뒤 한꺼번에 연결합니다.
             */
            static void restoreSnapshotObjects( const PlaySessionData& data, Scene* pScene, GameObjectManager& objects, ObjectStateBatch& inoutBatch )
            {
                for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    GameObject* pObj = objects.findGameObjectByID( snap._identity._objectID );

                    // 플레이 중에 사라진 오브젝트는 원래 id 로 복원한다. 플레이 전에 보관하던 핸들이 이어지게 하기 위해서다.
                    if ( pObj == nullptr )
                    {
                        pObj = objects.createGameObjectWithID( hashed_string( snap._name.c_str() ), snap._identity._objectID );
                        if ( pObj == nullptr )
                        {
                            SW_LOG_WARNING( "Failed to recreate '%#' from play snapshot.", snap._name.c_str() );
                            continue;
                        }
                    }

                    if ( snap._prefabPath.empty() == false && pScene != nullptr )
                        pScene->setEntityPrefabPath( pObj->getObjectID(), snap._prefabPath );
                    if ( snap._bNestedPart )
                        objects.getSubsystem<PrefabInstanceRegistry>().restoreNestedPart( pObj->getObjectID(), snap._nestedPart );

                    ObjectLoadContext context{ SerializePurpose::Snapshot };
                    context._pIdentity = &snap._identity;
                    context._pBatch    = &inoutBatch;
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pObj, snap._bytes.data(), snap._bytes.size(), context ) == 0 )
                        SW_LOG_WARNING( "Failed to restore '%#' from binary play snapshot.", snap._name.c_str() );
                }
            }

            static void restorePlaySnapshot( PlaySessionData& data )
            {
                if ( data._bHasSnapshot == SW_FALSE )
                    return;

                // 플레이 중에 활성 씬이 바뀌었으면(게임 코드가 다음 레벨을 열었거나 씬 로드가 끝났다) 스냅샷은 현재 씬의 것이 아니다. 그 씬에
                // 그대로 되돌리면 두 씬의 오브젝트가 섞이고, 활성 씬이 플레이 중에 연 씬의 소스 경로를 가진 채라 저장하면 그 씬 파일을
                // 덮어쓴다. 편집하던 씬을 빈 씬으로 다시 만들고(이름과 소스 경로 복원) 거기에 되돌린다. 로드가 아직 진행 중이면 끝나면서 씬을 다시
                // 바꾸므로 먼저 취소한다. 오브젝트는 원래 id 로 복원되므로 프리팹 연결도 id 로 다시 연결한다(아래 2).
                SceneManager& sceneManager = editor::getSubsystem<SceneManager>();
                if ( sceneManager.getSceneGeneration() != data._sceneGeneration )
                {
                    sceneManager.cancelPendingAsyncLoads();
                    Scene* pRebuilt = sceneManager.createEmptyActiveScene( data._sceneName );
                    if ( pRebuilt != nullptr )
                        pRebuilt->setSourcePath( data._sceneSourcePath );
                    SW_LOG_INFO( "The active scene changed during play - restoring the edited scene '%#'.", data._sceneName.c_str() );
                }

                Scene*             pScene   = editor::getActiveScene();
                GameObjectManager* pObjects = editor::getActiveObjectManager();
                if ( pObjects == nullptr )
                {
                    data._listSnapshot.clear();
                    data._bHasSnapshot = SW_FALSE;
                    return;
                }

                // 1. 플레이 도중 생성된 오브젝트를 파괴한다
                destroySpawnedDuringPlay( data, *pObjects );

                // 2. 기존 오브젝트의 상태를 복구하고 삭제된 오브젝트를 다시 만든다. 계층은 모두 읽은 뒤 한꺼번에 연결한다(아래 3).
                ObjectStateBatch batch( ObjectIDSpace::Live );
                restoreSnapshotObjects( data, pScene, *pObjects, batch );

                // 3. 계층을 다시 연결한다. 모두 읽은 뒤에 부모의 원래 id 로 연결해야 한다. 읽으면서 바로 연결하면, 플레이 중에 부모도 지워졌고 자식이
                // 스냅샷에서 먼저 나올 때 부모가 아직 없어 루트로 남는다. 이름으로 찾으면 플레이 중에 이름을 바꾼 오브젝트(A 가 B 의
                // 이름을 가져간 경우)의 자식이 엉뚱한 쪽에 붙는다. 씬 로드(`Scene::instantiate`)와 같은 방식이다.
                // `Live` 배치는 런타임 id 가 맞아 옮기지 못한 핸들을 실패로 치지 않는다. 늘 true 다.
                [[maybe_unused]] const bool bLinked = batch.finish();
                SW_ASSERT( bLinked );

                // 4. 플레이 전에 칠한 지형 편집을 되돌려 쓴다. 다시 만든 지형 컴포넌트는 디스크 에셋을 읽은 상태다.
                for ( PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    if ( snap._listTerrainEdit.empty() )
                        continue;
                    GameObject*       pObj     = pObjects->findGameObjectByID( snap._identity._objectID );
                    TerrainComponent* pTerrain = pObj != nullptr ? pObj->getComponent<TerrainComponent>() : nullptr;
                    if ( pTerrain != nullptr )
                        pTerrain->restoreState( std::move( snap._listTerrainEdit ) );
                }

                SW_LOG_TRACE( "Play snapshot restored (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );

                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;
            }

            /**
             * @brief Keep Simulation Changes 로 저장한 상태를 편집 씬의 같은 id 오브젝트에 적용하고 되돌리기 한 번으로 기록합니다.
             * @details 스냅샷을 되돌리고 편집 내역을 되찾은 뒤에 호출한다. 그래야 기록이 편집 내역에 들어가고, Ctrl+Z 가 플레이 전 값으로 돌린다.
             */
            static void applyKeptStates( PlaySessionData& data )
            {
                GameObjectManager* pObjects = editor::getActiveObjectManager();
                if ( data._listKept.empty() || pObjects == nullptr )
                {
                    data._listKept.clear();
                    return;
                }
                const bool bCompound = data._listKept.size() > 1;
                if ( bCompound )
                    EditorTransaction::beginTransaction( "Keep Simulation Changes" );
                for ( const PlaySessionData::ObjectSnapshot& kept : data._listKept )
                {
                    GameObject* pObj = pObjects->findGameObjectByID( kept._identity._objectID );
                    if ( pObj == nullptr )
                    {
                        SW_LOG_WARNING( "Keep Simulation Changes: '%#' does not exist after Stop (created during play?)", kept._name.c_str() );
                        continue;
                    }
                    const ObjectSnapshot before = EditorTransaction::captureSnapshot( pObj );
                    ObjectStateBatch     batch( ObjectIDSpace::Live );
                    ObjectLoadContext    context{ SerializePurpose::Authoring };
                    context._pIdentity = &kept._identity;
                    context._pBatch    = &batch;
                    if ( ObjectStateSerializer::loadFromBinaryBuffer( pObj, kept._bytes.data(), kept._bytes.size(), context ) == 0 )
                    {
                        SW_LOG_WARNING( "Keep Simulation Changes: failed to apply the play state of '%#'", kept._name.c_str() );
                        continue;
                    }
                    // `Live` 배치는 런타임 id 가 맞아 옮기지 못한 핸들을 실패로 치지 않는다. 늘 true 다.
                    [[maybe_unused]] const bool bLinked = batch.finish();
                    SW_ASSERT( bLinked );
                    EditorTransaction::recordModify( pObj, before, EditorTransaction::captureSnapshot( pObj ), "Keep Simulation Changes" );
                }
                if ( bCompound )
                    EditorTransaction::endTransaction();
                SW_LOG_INFO( "Kept the play state of %# object(s).", static_cast<uint32>( data._listKept.size() ) );
                data._listKept.clear();
            }

            /**
             * @brief 시작 위치로 선택한 오브젝트를 순간이동합니다. 옮길 것이 없으면 경고만 남깁니다.
             * @return 옮겼으면 true 입니다.
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
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorPlaySession" );

    PlaySessionState EditorPlaySession::getState()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr ? pData->_state : PlaySessionState::Stopped;
    }

    bool EditorPlaySession::isPlaying()
    {
        const PlaySessionData* pData = findData();
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

    bool EditorPlaySession::areSceneEditsAllowed()
    {
        return EditorSessionPolicy::areSceneEditsAllowed( isStopped(), areSceneEditsHeld() );
    }

    bool EditorPlaySession::refuseSceneEdit( const utf8* pAction )
    {
        PlaySessionData* pData = findData();
        if ( areSceneEditsAllowed() )
        {
            if ( pData != nullptr )
                pData->_lastRefusedAction.clear(); // 다시 허용되면 다음 거절은 새로 남긴다
            return false;
        }
        // 서비스가 없으면 "정지" 이고 막는 작업도 없어 위에서 허용으로 반환했다. 여기에 오면 거절 상태(플레이 중이거나 막는 작업)가 있으니 서비스가 있다.
        SW_ASSERT( pData != nullptr );
        if ( pData == nullptr )
            return true;
        const bool bHeld = areSceneEditsHeld();
        // 매 프레임 부르는 경로(기즈모 드래그 등)가 줄마다 경고를 쏟지 않게, 같은 편집이 같은 이유로 연속 거절되면 첫 한 번만 남긴다.
        const bool bSameAsLast = pData->_lastRefusedAction == pAction && ( pData->_bLastRefusedByHold == SW_TRUE ) == bHeld;
        if ( bSameAsLast )
            return true;
        pData->_lastRefusedAction  = pAction;
        pData->_bLastRefusedByHold = bHeld ? SW_TRUE : SW_FALSE;
        const utf8* pReason        = bHeld ? "a long-running task (light probe bake) holds scene edits" : "the play session is not stopped";
        SW_LOG_WARNING_ID( "Editor.SceneEditRefused", "%# refused: scene edits are not allowed - %#", pAction, pReason );
        return true;
    }

    void EditorPlaySession::holdSceneEdits()
    {
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            ++pData->_sceneEditHoldCount;
    }

    void EditorPlaySession::releaseSceneEdits()
    {
        PlaySessionData* pData = findData();
        SW_ASSERT( pData == nullptr || pData->_sceneEditHoldCount > 0 );
        if ( pData != nullptr && pData->_sceneEditHoldCount > 0 )
            --pData->_sceneEditHoldCount;
    }

    bool EditorPlaySession::areSceneEditsHeld()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr && pData->_sceneEditHoldCount > 0;
    }

    void EditorPlaySession::noteSceneEditWhileHeld()
    {
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            ++pData->_heldSceneEditCount;
    }

    uint32 EditorPlaySession::getHeldSceneEditCount()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr ? pData->_heldSceneEditCount : 0u;
    }

    bool EditorPlaySession::hasPendingStep()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr && pData->_pendingStepCount > 0;
    }

    bool EditorPlaySession::isSimulating()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr && pData->_state != PlaySessionState::Stopped && pData->_bSimulate == SW_TRUE;
    }

    bool EditorPlaySession::isPlayerActive()
    {
        const PlaySessionData* pData = findData();
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
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            startSession( *pData, false );
    }

    void EditorPlaySession::simulate()
    {
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            startSession( *pData, true );
    }

    void EditorPlaySession::startSession( PlaySessionData& data, bool bSimulate )
    {
        // 세션 종류는 실행 중에도 바꿀 수 있다. Simulate 로 지켜보다가 Play 로 조종할 수 있다. 그래서 상태 전환과 따로 둔다.
        data._bSimulate = bSimulate ? SW_TRUE : SW_FALSE;
        setState( data, PlaySessionState::Playing );
    }

    PlaySessionData* EditorPlaySession::findData()
    {
        // 선택 조회(필수 아님): 에디터 셸이 아직 만들어지지 않았거나 이미 종료된 시점에도 정적 함수가 불릴 수 있다(패널 정리 경로). 그때는 "정지" 로 응답한다.
        EditorPlaySession* pSession = findSubsystem<EditorPlaySession>();
        return pSession != nullptr ? &pSession->_data : nullptr;
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
            const bool bFirstTaggedPlayer = pTagged == nullptr && pObj != nullptr && pObj->isPendingDestroy() == false && pObj->hasTag( playerTag, true );
            if ( bFirstTaggedPlayer )
                pTagged = pObj;
        } );
        if ( pTagged != nullptr )
            return pTagged;

        // 태그가 없으면 게임 카메라를 가진 오브젝트를 쓴다. 1인칭에서는 그것이 플레이어다. 카메라가 자식이면 최상위 조상을 옮겨서 몸째 이동한다.
        GameObject* pOwner = pGameCamera != nullptr ? pGameCamera->getOwner() : nullptr;
        while ( pOwner != nullptr && pOwner->getParent() != nullptr )
        {
            pOwner = pOwner->getParent();
        }
        return pOwner;
    }

    bool EditorPlaySession::isPlayQueued()
    {
        const PlaySessionData* pData = findData();
        return pData != nullptr && pData->_bStartQueued != SW_FALSE;
    }

    void EditorPlaySession::stepFrames( uint32 frameCount )
    {
        PlaySessionData* pData = findData();
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
            if ( data._state == PlaySessionState::Stopped ) // 시작을 미뤘다. 씬을 여는 중이며, 로드가 끝나면 플레이로 시작한다
                return;
        }
        data._pendingStepCount = frameCount < kMaxStepFrameCount ? frameCount : kMaxStepFrameCount;
    }

    void EditorPlaySession::consumePendingStep()
    {
        PlaySessionData* pData = findData();
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
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            setState( *pData, state );
    }

    void EditorPlaySession::update()
    {
        PlaySessionData* pData = findData();
        if ( pData != nullptr )
            update( *pData );
    }

    void EditorPlaySession::update( PlaySessionData& data )
    {
        // 첫 프레임이 지났다. 첫 틱에 스폰 위치로 되돌리는 게임이 있으므로 한 번 더 옮긴다.
        if ( data._bStartMovePending == SW_TRUE && data._state != PlaySessionState::Stopped )
        {
            data._bStartMovePending = SW_FALSE;
            (void)EditorPlaySessionInternal::moveToStartPosition( data ); // 옮길 것이 없으면 시작할 때 이미 경고했다
        }
        if ( data._bStartQueued == SW_FALSE )
            return;
        if ( editor::getSubsystem<SceneManager>().isTransitioning() )
            return;
        data._bStartQueued = SW_FALSE;
        setState( data, data._queuedState );
    }

    void EditorPlaySession::setState( PlaySessionData& data, PlaySessionState state )
    {
        // Stop 은 미룬 시작도 취소한다. 로드가 끝난 뒤 사용자가 멈춘 플레이가 저절로 시작하면 안 된다.
        if ( state == PlaySessionState::Stopped )
            data._bStartQueued = SW_FALSE;
        if ( data._state == state )
            return;

        // 씬을 여는 중이면 시작을 미룬다. 언리얼의 RequestPlaySession 처럼 요청을 등록해 두고 로드가 끝난 프레임에 update 가 시작한다. 지금 시작하면
        // 스냅샷은 곧 언로드될 씬을 찍고, 플레이 중에 로드가 끝나 씬이 바뀐다. 그러면 Stop 이 되돌릴 씬이 사용자가 막 연 씬이 아니게 된다.
        if ( data._state == PlaySessionState::Stopped )
        {
            if ( editor::getSubsystem<SceneManager>().isTransitioning() )
            {
                if ( data._bStartQueued == SW_FALSE )
                    SW_LOG_INFO( "Opening the scene - play starts when loading finishes." );
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
        pData->_lastRefusedAction.clear(); // 상태가 바뀌면 거절 기록을 비운다. Stop 뒤 다음 플레이의 첫 같은 거절도 로그로 남아야 한다

        // 편집 내역은 Play 동안 맡겨 두고 Stop 뒤에 되찾는다(아래 분기). 플레이 중의 편집은 Stop 이 스냅샷으로 되돌리므로 그동안의 기록은 버린다.
        // 플레이와 일시정지 사이에서는 비우기만 한다.
        // 선택 조회(②): 이 상태 전환은 에디터 컨텍스트 없이 테스트하므로 스택이 없을 수 있다. 스택이 없으면 맡길 편집 내역도 없다.
        CommandStack* pCommandStack            = editor::findSubsystem<CommandStack>();
        const bool    bShouldClearCommandStack = pCommandStack != nullptr && previous != PlaySessionState::Stopped && state != PlaySessionState::Stopped;
        if ( bShouldClearCommandStack )
            pCommandStack->clear();

        // 정지 상태를 떠날 때와 정지 상태로 돌아올 때로 나눈다. 목표 상태가 무엇이든 같다. Stopped 에서 Playing 으로 갈 때만 월드를 켜면, 정지에서
        // 일시정지를 누른 뒤 Play 할 때 스냅샷도 onBeginPlay 도 없이 플레이가 돌고 Stop 이 편집 씬을 되돌리지 못한다.
        // 정지에서 일시정지로 가면 일시정지 상태로 플레이를 시작한다. 스냅샷과 시작은 하고 씬은 틱하지 않는다.
        if ( previous == PlaySessionState::Stopped )
        {
            pData->_listKept.clear();
            if ( pCommandStack != nullptr )
                pCommandStack->parkHistory();
            captureSnapshot( *pData );
            EditorPlaySessionInternal::setWorldPlaying( true );
            // 카메라 위치에서 시작은 플레이어가 조종하는 세션만 해당한다. 월드가 시작한 뒤, 즉 onBeginPlay 가 스폰 위치를 정한 뒤에 옮긴다.
            if ( pData->_bStartAtPosition == SW_TRUE && pData->_bSimulate == SW_FALSE )
                pData->_bStartMovePending = EditorPlaySessionInternal::moveToStartPosition( *pData ) ? SW_TRUE : SW_FALSE;
        }
        else if ( state == PlaySessionState::Stopped )
        {
            pData->_bStartMovePending = SW_FALSE;
            EditorPlaySessionInternal::setWorldPlaying( false );
            restoreSnapshot( *pData );
            // 스냅샷이 오브젝트를 원래 id 로 되돌린 뒤이므로 맡긴 명령의 대상(id)이 다시 맞는다.
            if ( pCommandStack != nullptr )
                pCommandStack->unparkHistory();
            EditorPlaySessionInternal::applyKeptStates( *pData );
        }
    }

    bool EditorPlaySession::keepObjectState( PlaySessionData& data, const GameObject* pObject )
    {
        if ( data._state == PlaySessionState::Stopped || pObject == nullptr )
            return false;
        PlaySessionData::ObjectSnapshot entry;
        entry._identity = ObjectStateSerializer::captureIdentity( pObject );
        entry._name     = pObject->getName().c_str();
        if ( ObjectStateSerializer::saveToBinaryBuffer( pObject, entry._bytes, ObjectSaveOptions{ SerializePurpose::Authoring } ) == false )
            return false;
        for ( PlaySessionData::ObjectSnapshot& kept : data._listKept )
        {
            if ( kept._identity._objectID == entry._identity._objectID )
            {
                kept = std::move( entry );
                return true;
            }
        }
        data._listKept.push_back( std::move( entry ) );
        return true;
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

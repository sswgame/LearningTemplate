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
            static void beginPlayActiveScene()
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr )
                    return;

                GameObjectManager* pObjects = pScene->getObjectManager();
                if ( pObjects == nullptr )
                    return;

                pObjects->beginPlay();
            }

            static void endPlayActiveScene()
            {
                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr )
                    return;

                GameObjectManager* pObjects = pScene->getObjectManager();
                if ( pObjects == nullptr )
                    return;

                pObjects->endPlay();
            }

            static void capturePlaySnapshot( PlaySessionData& data )
            {
                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;

                Scene* pScene = editor::getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                    return;

                GameObjectManager* pObjects = pScene->getObjectManager();
                EditorContext*     pContext = EditorContext::get();
                // 예전엔 여기서 getAllGameObjects() 를 두 번 불렀다 — 개수를 세려고 한 번,
                // 순회하려고 한 번. 값 반환형이라 씬 전체를 두 번 할당·복사했다.
                vector<GameObject*> listAllObject;
                pObjects->getAllGameObjects( listAllObject );
                data._listSnapshot.reserve( listAllObject.size() );

                for ( GameObject* pObj : listAllObject )
                {
                    if ( pObj == nullptr )
                        continue;

                    PlaySessionData::ObjectSnapshot entry;
                    entry._objectId = pObj->getObjectId();
                    entry._name     = pObj->getName().c_str();
                    if ( pContext != nullptr )
                        entry._guid = pContext->getWorkspace().getOrAssignGuid( entry._objectId );

                    if ( ObjectStateSerializer::saveToBinaryBuffer( pObj, entry._bytes ) == false )
                        entry._xml = ObjectStateSerializer::saveToXmlString( pObj );

                    if ( entry._bytes.empty() == false || entry._xml.empty() == false )
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

                SceneManager* pSceneManager = editor::getService<SceneManager>();
                if ( pSceneManager == nullptr )
                {
                    data._listSnapshot.clear();
                    data._bHasSnapshot = SW_FALSE;
                    return;
                }

                Scene* pScene = pSceneManager->getActiveScene();
                if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
                {
                    data._listSnapshot.clear();
                    data._bHasSnapshot = SW_FALSE;
                    return;
                }

                GameObjectManager* pObjects = pScene->getObjectManager();
                EditorContext*     pContext = EditorContext::get();

                // 1. 플레이 도중 생성된 오브젝트 파괴
                {
                    unordered_set<uint64> uniqueSnapIds;
                    uniqueSnapIds.reserve( data._listSnapshot.size() );
                    for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                    {
                        uniqueSnapIds.insert( snap._objectId );
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
                    GameObject* pObj = pObjects->findGameObjectById( snap._objectId );
                    if ( pObj == nullptr && snap._guid.isNull() == false && pContext != nullptr )
                        pObj = pContext->getWorkspace().findGameObjectByGuid( snap._guid );

                    if ( pObj == nullptr )
                    {
                        pObj = pObjects->createGameObject( hashed_string( snap._name.c_str() ) );
                        if ( pObj == nullptr )
                        {
                            SW_LOG_WARNING( "Failed to recreate '%#' from play snapshot.", snap._name.c_str() );
                            continue;
                        }
                    }

                    if ( snap._guid.isNull() == false && pContext != nullptr )
                        pContext->getWorkspace().setGuid( pObj->getObjectId(), snap._guid );

                    mapRestored[snap._objectId] = pObj;

                    if ( snap._bytes.empty() == false )
                    {
                        string parentName;
                        if ( ObjectStateSerializer::loadFromBinaryBuffer( pObj, snap._bytes.data(), snap._bytes.size(), parentName ) == 0 )
                            SW_LOG_WARNING( "Failed to restore '%#' from binary play snapshot.", snap._name.c_str() );
                    }
                    else if ( snap._xml.empty() == false )
                    {
                        if ( ObjectStateSerializer::loadFromXmlString( pObj, snap._xml ) == false )
                            SW_LOG_WARNING( "Failed to restore '%#' from play snapshot.", snap._name.c_str() );
                    }
                }

                // 3. 계층 관계 리바인딩 (XML 스냅샷 폴백용)
                for ( const PlaySessionData::ObjectSnapshot& snap : data._listSnapshot )
                {
                    if ( snap._xml.empty() )
                        continue;

                    GameObject* pObj = nullptr;
                    auto        it   = mapRestored.find( snap._objectId );
                    if ( it != mapRestored.end() )
                        pObj = it->second;

                    if ( pObj == nullptr )
                        continue;

                    if ( ObjectStateSerializer::rebindSceneHierarchy( pObj, snap._xml ) == false )
                        SW_LOG_WARNING( "Failed to rebind scene hierarchy for '%#'.", snap._name.c_str() );
                }

                SW_LOG_TRACE( "Play snapshot restored (%# objects).",
                              static_cast<uint32>( data._listSnapshot.size() ) );

                data._listSnapshot.clear();
                data._bHasSnapshot = SW_FALSE;
            }

            /**
             * @brief 컨텍스트가 들고 있는 플레이 상태입니다. 컨텍스트가 없으면 nullptr.
             * @details 에디터 셸이 아직 안 섰거나 이미 내려간 시점에도 이 파사드가 불릴 수 있다
             *          (패널 정리 경로). 그때는 "정지" 로 답해야 한다.
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

    void EditorPlaySession::stepOnce()
    {
        PlaySessionData* pData = EditorPlaySessionInternal::data();
        if ( pData == nullptr )
            return;
        if ( pData->_state == PlaySessionState::Stopped )
            setState( PlaySessionState::Playing );
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
        if ( pData == nullptr || pData->_state == state )
            return;

        pData->_bStepPending            = SW_FALSE;
        const PlaySessionState previous = pData->_state;
        pData->_state                   = state;

        CommandStack* pCommandStack = editor::getService<CommandStack>();
        if ( pCommandStack != nullptr )
            pCommandStack->clear();

        // Stopped → Playing: 스냅샷 캡처 후 beginPlay
        if ( previous == PlaySessionState::Stopped && state == PlaySessionState::Playing )
        {
            EditorPlaySessionInternal::capturePlaySnapshot( *pData );
            EditorPlaySessionInternal::beginPlayActiveScene();
        }

        // Playing/Paused → Stopped: endPlay 호출 후 스냅샷 복구
        if ( ( previous == PlaySessionState::Playing || previous == PlaySessionState::Paused ) && state == PlaySessionState::Stopped )
        {
            EditorPlaySessionInternal::endPlayActiveScene();
            EditorPlaySessionInternal::restorePlaySnapshot( *pData );
        }
    }

} // namespace sw::editor

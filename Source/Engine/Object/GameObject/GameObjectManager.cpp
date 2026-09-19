#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/array.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/Debug/FrameProfiler.h"

namespace sw
{
    namespace
    {
        struct GameObjectManagerInternal
        {
            static mutex& getModuleFactoryHeadsMutex()
            {
                static mutex s_mutex;
                return s_mutex;
            }

            static unordered_map<string, ComponentFactoryRegistrar*>& getModuleFactoryHeads()
            {
                static unordered_map<string, ComponentFactoryRegistrar*> s_mapFactoryHeads;
                return s_mapFactoryHeads;
            }

            struct TickCandidate
            {
                Component*            _pComponent{ nullptr };
                ComponentHandle       _handle;
                uint32                _subTickId{ 0 };
                uint64                _componentId{ 0 };
                uint64                _objectId{ 0 };
                uint8                 _orderKey{ 64 };
                uint32                _originalIndex{ 0 };
                vector<SubTickHandle> _listPrerequisite;
            };

            static vector<vector<GameObjectManager::TickExecutionItem>> sortTickCandidates( vector<TickCandidate>& listCandidate )
            {
                const size_t count = listCandidate.size();
                if ( count == 0 )
                    return {};

                if ( count == 1 )
                {
                    return { { { listCandidate[0]._pComponent, listCandidate[0]._handle, listCandidate[0]._subTickId } } };
                }

                // SubTickHandle -> 후보자 인덱스 빠른 매핑 맵 구성
                unordered_map<SubTickHandle, size_t, SubTickHandleHash> mapLookup;
                mapLookup.reserve( count );
                for ( size_t index = 0; index < count; ++index )
                {
                    mapLookup[{ listCandidate[index]._componentId, listCandidate[index]._subTickId }] = index;
                }

                // 선행 종속성 DAG 그래프(인접 리스트 및 In-degree) 구성
                vector<vector<size_t>> listAdj( count );
                vector<uint32>         listInDegree( count, 0 );
                bool                   bHasPrerequisites = false;

                for ( size_t index = 0; index < count; ++index )
                {
                    for ( const SubTickHandle& prereq : listCandidate[index]._listPrerequisite )
                    {
                        auto it = mapLookup.find( prereq );
                        if ( it != mapLookup.end() )
                        {
                            const size_t prereqIdx = it->second;
                            if ( prereqIdx != index )
                            {
                                listAdj[prereqIdx].push_back( index );
                                ++listInDegree[index];
                                bHasPrerequisites = true;
                            }
                        }
                    }
                }

                // 종속성이 전혀 없는 경우: TickPhase + Priority 기준 단일 웨이브 안정 정렬 (Fast-Path)
                if ( bHasPrerequisites == false )
                {
                    std::stable_sort( listCandidate.begin(), listCandidate.end(), []( const TickCandidate& left, const TickCandidate& right )
                    {
                        if ( left._orderKey != right._orderKey )
                            return left._orderKey < right._orderKey;
                        return left._originalIndex < right._originalIndex;
                    } );

                    vector<GameObjectManager::TickExecutionItem> listSingleWave;
                    listSingleWave.reserve( count );
                    for ( const TickCandidate& cand : listCandidate )
                    {
                        listSingleWave.push_back( { cand._pComponent, cand._handle, cand._subTickId } );
                    }
                    return { std::move( listSingleWave ) };
                }

                // Kahn 알고리즘 기반 DAG 레벨별 분할 웨이브 생성 (Topological Level Waves)
                auto sortLevel = [&listCandidate]( vector<size_t>& listLevel )
                {
                    std::stable_sort( listLevel.begin(), listLevel.end(), [&listCandidate]( size_t left, size_t right )
                    {
                        if ( listCandidate[left]._orderKey != listCandidate[right]._orderKey )
                            return listCandidate[left]._orderKey < listCandidate[right]._orderKey;
                        return listCandidate[left]._originalIndex < listCandidate[right]._originalIndex;
                    } );
                };

                vector<size_t> listCurrentLevel;
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( listInDegree[index] == 0 )
                        listCurrentLevel.push_back( index );
                }

                vector<vector<GameObjectManager::TickExecutionItem>> listDagWave;
                vector<bool>                                         listVisited( count, false );
                size_t                                               totalProcessed{ 0 };

                while ( listCurrentLevel.empty() == false )
                {
                    sortLevel( listCurrentLevel );
                    vector<GameObjectManager::TickExecutionItem> listWaveItem;
                    listWaveItem.reserve( listCurrentLevel.size() );
                    vector<size_t> listNextLevel;

                    for ( size_t u : listCurrentLevel )
                    {
                        listVisited[u] = true;
                        ++totalProcessed;
                        listWaveItem.push_back( { listCandidate[u]._pComponent, listCandidate[u]._handle, listCandidate[u]._subTickId } );

                        for ( size_t v : listAdj[u] )
                        {
                            if ( --listInDegree[v] == 0 )
                                listNextLevel.push_back( v );
                        }
                    }

                    listDagWave.push_back( std::move( listWaveItem ) );
                    listCurrentLevel = std::move( listNextLevel );
                }

                // 순환 참조(Cycle) 방어: 미방문 노드가 남아있으면 orderKey 순으로 추가
                if ( totalProcessed < count )
                {
                    vector<size_t> listRemaining;
                    for ( size_t index = 0; index < count; ++index )
                    {
                        if ( listVisited[index] == false )
                            listRemaining.push_back( index );
                    }
                    sortLevel( listRemaining );
                    vector<GameObjectManager::TickExecutionItem> listFallbackWave;
                    listFallbackWave.reserve( listRemaining.size() );
                    for ( size_t index : listRemaining )
                    {
                        listFallbackWave.push_back( { listCandidate[index]._pComponent, listCandidate[index]._handle, listCandidate[index]._subTickId } );
                    }
                    listDagWave.push_back( std::move( listFallbackWave ) );
                }

                return listDagWave;
            }

            static vector<vector<GameObjectManager::TickExecutionItem>> splitWaveByObject( const vector<GameObjectManager::TickExecutionItem>& listWave )
            {
                vector<vector<GameObjectManager::TickExecutionItem>> listSubwave;
                vector<unordered_set<uint64>>                        listOccupied;
                for ( const GameObjectManager::TickExecutionItem& item : listWave )
                {
                    if ( item._handle.isValid() == false )
                        continue;
                    const uint64 objectId = item._handle.objectId();
                    size_t       slot{ 0 };
                    for ( ; slot < listSubwave.size(); ++slot )
                    {
                        if ( listOccupied[slot].count( objectId ) == 0 )
                            break;
                    }
                    if ( slot == listSubwave.size() )
                    {
                        listSubwave.emplace_back();
                        listOccupied.emplace_back();
                    }
                    listSubwave[slot].push_back( item );
                    listOccupied[slot].insert( objectId );
                }
                return listSubwave;
            }

            static void resolveAndTickItem( GameObjectManager* pManager, float32 deltaTime, const GameObjectManager::TickExecutionItem& item )
            {
                // 핸들로 다시 푼다 — 파도가 캐시돼 있으므로 담아 둔 포인터를 그냥 쓰고 싶지만, 그
                // 빠른 길이 실제로 이득인지 **재 보지 못했다**: 이 벤치에서 `GT.Scene.tick.components`
                // 가 0us 다(틱하는 컴포넌트가 없다). 숫자 없이 바꾸지 않는다.
                Component* pComp = item._handle.isValid() ? pManager->resolveComponent( item._handle ) : item._pComponent;
                if ( pComp == nullptr || pComp->isPendingKill() || pComp->isActive() == false )
                    return;
                GameObject* pOwner = pComp->getOwner();
                if ( pOwner == nullptr || pOwner->isActiveInHierarchy() == false || pOwner->isPendingKill() )
                    return;

                if ( item._subTickId == 0 )
                {
                    if ( pComp->canEverTick() )
                        pComp->onTick( deltaTime );
                }
                else
                {
                    if ( pComp->isSubTickActive( item._subTickId ) )
                        pComp->onSubTick( item._subTickId, deltaTime );
                }
            }

            struct ComponentWaveTick
            {
                GameObjectManager*                          _pManager{ nullptr };
                const GameObjectManager::TickExecutionItem* _pRawItems{ nullptr };
                float32                                     _deltaTime{ 0.0f };
                uint32                                      _totalCount{ 0 };

                void tickIndex( uint32 index )
                {
                    if ( index < _totalCount )
                        resolveAndTickItem( _pManager, _deltaTime, _pRawItems[index] );
                }
            };

            static void dispatchWave( GameObjectManager* pManager, float32 deltaTime, const vector<GameObjectManager::TickExecutionItem>& listWave )
            {
                if ( listWave.empty() )
                    return;

                constexpr uint32 kParallelThreshold = 16;
                if ( listWave.size() < kParallelThreshold || engine::areEngineServicesBound() == false )
                {
                    for ( const GameObjectManager::TickExecutionItem& item : listWave )
                        resolveAndTickItem( pManager, deltaTime, item );
                    return;
                }

                ComponentWaveTick waveTick{};
                waveTick._pManager   = pManager;
                waveTick._deltaTime  = deltaTime;
                waveTick._pRawItems  = listWave.data();
                waveTick._totalCount = static_cast<uint32>( listWave.size() );

                TaskStageHandle stage  = engine::getTaskManager().createAnonymousStage( "ComponentWave" );
                TaskHandle      handle = engine::getTaskManager().emplaceParallel(
                    waveTick._totalCount, SW_DELEGATE_METHOD( ParallelTaskDelegate, &ComponentWaveTick::tickIndex, &waveTick ) );
                stage.addTask( handle );
                handle.submit();
                engine::getTaskManager().waitStage( stage );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "GameObjectManager" );

    static ComponentFactoryRegistrar* _s_engineHead{ nullptr };
    static bool                       _s_engineHeadSealed{ false };

    ComponentFactoryRegistrar*& ComponentFactoryRegistrar::getHead()
    {
        static ComponentFactoryRegistrar* s_pHead{ nullptr };
        return s_pHead;
    }

    ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ) )
        : _registerFunc{ registerFunc }
        , _pNext{ getHead() }
    {
        getHead() = this;
        if ( _s_engineHeadSealed == false )
            _s_engineHead = this;
    }

    ComponentFactoryRegistrar::ComponentFactoryRegistrar( void ( *registerFunc )( GameObjectManager& ), ComponentFactoryRegistrar*& moduleHead )
        : _registerFunc{ registerFunc }
        , _pNext{ moduleHead }
    {
        moduleHead = this;
        if ( _s_engineHeadSealed == false && &moduleHead == &getHead() )
            _s_engineHead = this;
    }

    /**
     * @brief GameObjectManager 생성자: 기본 엔진 및 모듈 컴포넌트 팩토리들을 등록합니다.
     */
    GameObjectManager::GameObjectManager()
        : _poolGameObject{ 256, true }
        , _mapComponentPool{}
        , _listGameObject{}
        , _mapNameToObject{}
        , _mapIdToObject{}
        , _listPendingAdd{}
        , _listPendingDestroyObject{}
        , _listPendingDestroyComponent{}
        , _listProcessingDestroyObject{}
        , _listProcessingDestroyComponent{}
        , _listRootSceneComponent{}
        , _mutex{}
        , _nextId{ 1 }
        , _physicsWorld{}
        , _bParallelTransformReadOnly{ false }
        , _bTicking{ false }
        , _bIsTickWavesDirty{ true }
        , _listCachedTickWave{}
        , _deferredTransformMutex{}
        , _listDeferredTransformUpdate{}
        , _listProcessingTransform{}
        , _deferredPostTickMutex{}
        , _listDeferredPostTickUpdate{}
        , _listProcessingPostTick{}
        , _mapFactory{}
        , _mapFactoryModule{}
        , _activeModuleName{}
        , _dirtyTransformGeneration{ 1 }
        , _lastFlushedTransformGeneration{ 0 }
        , _primitiveRegistry{}
    {
        _listDeferredTransformUpdate.reserve( 128 );
        _listProcessingTransform.reserve( 128 );
        _listDeferredPostTickUpdate.reserve( 128 );
        _listProcessingPostTick.reserve( 128 );

        ComponentFactoryRegistrar* pEngineHead = ComponentFactoryRegistrar::getHead();
        if ( pEngineHead == nullptr )
            pEngineHead = _s_engineHead;
        registerPendingFactories( "Engine", pEngineHead );

        vector<pair<string, ComponentFactoryRegistrar*>> listModuleHead;
        {
            std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
            for ( const auto& [mod, head] : GameObjectManagerInternal::getModuleFactoryHeads() )
                listModuleHead.push_back( { mod, head } );
        }
        for ( const auto& [mod, head] : listModuleHead )
        {
            if ( head == nullptr )
                continue;
            if ( mod == "Engine" && pEngineHead != nullptr )
                continue;
            registerPendingFactories( mod.c_str(), head );
        }
    }

    GameObjectManager::~GameObjectManager()
    {
        clear();
    }

    /**
     * @brief 새로운 게임 오브젝트를 생성하고 고유 이름을 부여하며 인덱스 사전에 등록합니다.
     */
    GameObject* GameObjectManager::createGameObject( hashed_string name )
    {
        GameObject* pObj = nullptr;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            const hashed_string                 uniqueName = makeUniqueNameUnlocked( name );
            pObj                                           = _poolGameObject.create( uniqueName );

            const uint64 newObjectId = generateNewId();
            pObj->_objectId          = newObjectId;
            pObj->_pOwnerManager     = this;

            _mapNameToObject.insert_or_assign( uniqueName, pObj );
            _mapIdToObject.insert_or_assign( newObjectId, pObj );
            _objectSlotTable.store( newObjectId, pObj );

            _listPendingAdd.push_back( pObj );
        }
        return pObj;
    }

    /**
     * @brief 게임 오브젝트 이름 변경 시 이름 검색 맵을 갱신합니다.
     */
    void GameObjectManager::notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName )
    {
        if ( pObj == nullptr )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        if ( _mapIdToObject.find( pObj->getObjectId() ) == _mapIdToObject.end() )
            return;

        const auto oldIt = _mapNameToObject.find( oldName );
        if ( oldIt != _mapNameToObject.end() && oldIt->second == pObj )
            _mapNameToObject.erase( oldIt );

        const hashed_string uniqueName = makeUniqueNameUnlocked( newName );
        if ( uniqueName != newName )
            pObj->_name = uniqueName;
        _mapNameToObject.insert_or_assign( uniqueName, pObj );
    }

    bool GameObjectManager::renameGameObject( GameObject* pObj, hashed_string newName )
    {
        if ( pObj == nullptr )
            return false;
        pObj->setName( newName );
        return true;
    }

    GameObjectManager* GameObjectManager::resolveOwningManager( GameObjectManager* pPreferred )
    {
        if ( pPreferred != nullptr )
            return pPreferred;

        // 붙잡아 둔 매니저가 없으면 활성 씬의 것을 쓴다. 서비스가 묶이기 전(테스트·초기화 중)에는
        // 물어볼 곳이 없으므로 nullptr 이다 — 호출부는 그때 해석을 그냥 미룬다.
        if ( engine::areEngineServicesBound() == false )
            return nullptr;

        Scene* pScene = engine::getSceneManager().getActiveScene();
        return ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
    }

    GameObject* GameObjectManager::findGameObjectByName( hashed_string name ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapNameToObject.find( name );
        if ( it == _mapNameToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
    }

    // ======================================================================
    // ObjectSlotTable — id → GameObject* 를 락 없이 읽는 밀집 표
    // ======================================================================

    GameObjectManager::ObjectSlotTable::ObjectSlotTable()
    {
        for ( uint64 chunkIndex = 0; chunkIndex < kMaxChunk; ++chunkIndex )
        {
            _arrChunk[chunkIndex].store( nullptr, std::memory_order_relaxed );
        }
    }

    GameObjectManager::ObjectSlotTable::~ObjectSlotTable()
    {
        for ( uint64 chunkIndex = 0; chunkIndex < kMaxChunk; ++chunkIndex )
        {
            atomic<GameObject*>* pChunk = _arrChunk[chunkIndex].load( std::memory_order_relaxed );
            if ( pChunk != nullptr )
                delete[] pChunk;
            _arrChunk[chunkIndex].store( nullptr, std::memory_order_relaxed );
        }
    }

    bool GameObjectManager::ObjectSlotTable::store( uint64 objectId, GameObject* pObject )
    {
        if ( isInRange( objectId ) == false )
            return false;

        const uint64         chunkIndex = objectId / kChunkSize;
        atomic<GameObject*>* pChunk     = _arrChunk[chunkIndex].load( std::memory_order_acquire );
        if ( pChunk == nullptr )
        {
            // 지우는 길이라면 청크를 새로 만들 이유가 없다.
            if ( pObject == nullptr )
                return true;

            // 쓰기는 전부 매니저 락 안이라 여기서 두 스레드가 겹치지 않는다.
            pChunk = new atomic<GameObject*>[kChunkSize];
            for ( uint64 slot = 0; slot < kChunkSize; ++slot )
            {
                pChunk[slot].store( nullptr, std::memory_order_relaxed );
            }
            _arrChunk[chunkIndex].store( pChunk, std::memory_order_release );
        }

        pChunk[objectId % kChunkSize].store( pObject, std::memory_order_release );
        return true;
    }

    GameObject* GameObjectManager::ObjectSlotTable::load( uint64 objectId ) const
    {
        if ( isInRange( objectId ) == false )
            return nullptr;

        const atomic<GameObject*>* pChunk = _arrChunk[objectId / kChunkSize].load( std::memory_order_acquire );
        if ( pChunk == nullptr )
            return nullptr;
        return pChunk[objectId % kChunkSize].load( std::memory_order_acquire );
    }

    void GameObjectManager::ObjectSlotTable::clear()
    {
        for ( uint64 chunkIndex = 0; chunkIndex < kMaxChunk; ++chunkIndex )
        {
            atomic<GameObject*>* pChunk = _arrChunk[chunkIndex].load( std::memory_order_acquire );
            if ( pChunk == nullptr )
                continue;
            for ( uint64 slot = 0; slot < kChunkSize; ++slot )
            {
                pChunk[slot].store( nullptr, std::memory_order_release );
            }
        }
    }

    GameObject* GameObjectManager::findGameObjectById( uint64 objectId ) const
    {
        // **빠른 길: 락도 해시도 없다.** 핸들 해석이 프레임당 오브젝트 수만큼 도는 자리라
        // 공유 잠금 하나가 곧 밀리초가 된다(벤치 실측: 호출당 110ns → 프레임당 2.2ms).
        if ( ObjectSlotTable::isInRange( objectId ) )
        {
            GameObject* pSlotObject = _objectSlotTable.load( objectId );
            return ( pSlotObject != nullptr && pSlotObject->isPendingKill() == false ) ? pSlotObject : nullptr;
        }

        // id 가 표의 범위를 넘어선 경우에만 맵으로 간다 — 표는 빠른 길이지 유일한 진실이 아니다.
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        auto                                it = _mapIdToObject.find( objectId );
        if ( it == _mapIdToObject.end() )
            return nullptr;
        GameObject* pObj = it->second;
        return ( pObj != nullptr && pObj->isPendingKill() == false ) ? pObj : nullptr;
    }

    void GameObjectManager::getAllGameObjects( vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        outListGameObject.reserve( _listGameObject.size() + _listPendingAdd.size() );
        outListGameObject.insert( outListGameObject.end(), _listGameObject.begin(), _listGameObject.end() );
        outListGameObject.insert( outListGameObject.end(), _listPendingAdd.begin(), _listPendingAdd.end() );
    }

    vector<GameObject*> GameObjectManager::getAllGameObjects() const
    {
        vector<GameObject*> listAllGameObject;
        getAllGameObjects( listAllGameObject );
        return listAllGameObject;
    }

    GameObject* GameObjectManager::findGameObjectByTag( TagID tag ) const
    {
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                return pObj;
        }
        return nullptr;
    }

    void GameObjectManager::findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const
    {
        outListGameObject.clear();
        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( GameObject* pObj : _listGameObject )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
        for ( GameObject* pObj : _listPendingAdd )
        {
            if ( pObj != nullptr && pObj->isPendingKill() == false && pObj->hasTag( tag ) )
                outListGameObject.push_back( pObj );
        }
    }

    void GameObjectManager::beginPlay()
    {
        mergePendingAdds();
        forEachGameObject( []( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isActiveInHierarchy() )
                pObj->beginPlay();
        } );
    }

    void GameObjectManager::endPlay()
    {
        forEachGameObject( []( GameObject* pObj )
        {
            if ( pObj != nullptr && pObj->isActiveInHierarchy() )
                pObj->endPlay();
        } );
    }

    void GameObjectManager::tick( float32 deltaTime )
    {
        if ( engine::areEngineServicesBound() )
            engine::getTaskManager().dispatchMainThreadTasks();
        processDeferredDestruction();
        mergePendingAdds();

        if ( _listGameObject.empty() )
            return;

        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransforms" );
            flushSceneTransforms();
        }

        _bParallelTransformReadOnly.store( true, std::memory_order_relaxed );
        _bTicking.store( true, std::memory_order_release );

        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.components" );
            tickComponents( deltaTime );

            if ( engine::areEngineServicesBound() )
                engine::getTaskManager().waitAll();
        }

        _bParallelTransformReadOnly.store( false, std::memory_order_relaxed );
        _bTicking.store( false, std::memory_order_release );

        // Apply deferred transforms while instances still exist (before deferred post-tick/destruction).
        {
            std::scoped_lock<mutex> lock{ _deferredTransformMutex };
            if ( _listDeferredTransformUpdate.empty() == false )
                _listProcessingTransform.swap( _listDeferredTransformUpdate );
        }
        for ( auto& func : _listProcessingTransform )
        {
            if ( func.isBound() )
                func();
        }
        _listProcessingTransform.clear();

        // Spawns / damage / tags queued from parallel onTick.
        {
            std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
            if ( _listDeferredPostTickUpdate.empty() == false )
                _listProcessingPostTick.swap( _listDeferredPostTickUpdate );
        }
        for ( auto& func : _listProcessingPostTick )
        {
            if ( func.isBound() )
                func();
        }
        _listProcessingPostTick.clear();
        mergePendingAdds();

        if ( hasDirtySceneTransforms() )
        {
            SW_PROFILE_SCOPE( "GT.Scene.tick.flushTransformsPost" );
            flushSceneTransforms();
        }

        processDeferredDestruction();
    }

    void GameObjectManager::flushSceneTransforms()
    {
        const uint64 currentGen = _dirtyTransformGeneration.load( std::memory_order_relaxed );
        if ( currentGen == _lastFlushedTransformGeneration )
            return;

        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( SceneComponent* pRoot : _listRootSceneComponent )
        {
            if ( pRoot != nullptr )
                flushSceneComponentSubtree( pRoot, false );
        }
        _lastFlushedTransformGeneration = currentGen;
    }

    bool GameObjectManager::hasDirtySceneTransforms() const
    {
        if ( _dirtyTransformGeneration.load( std::memory_order_relaxed ) == _lastFlushedTransformGeneration )
            return false;

        std::shared_lock<std::shared_mutex> lock{ _mutex };
        for ( const SceneComponent* pRoot : _listRootSceneComponent )
        {
            if ( pRoot != nullptr )
            {
                if ( pRoot->isTransformDirty() || pRoot->hasDirtyDescendant() )
                    return true;
            }
        }
        return false;
    }

    void GameObjectManager::deferTransformUpdate( TransformUpdateDelegate func )
    {
        if ( func.isBound() == false )
            return;
        std::scoped_lock<mutex> lock{ _deferredTransformMutex };
        _listDeferredTransformUpdate.push_back( std::move( func ) );
    }

    void GameObjectManager::deferPostTick( PostTickDelegate func )
    {
        if ( func.isBound() == false )
            return;
        std::scoped_lock<mutex> lock{ _deferredPostTickMutex };
        _listDeferredPostTickUpdate.push_back( std::move( func ) );
    }

    void GameObjectManager::executeOrDeferPostTick( PostTickDelegate func )
    {
        if ( func.isBound() == false )
            return;
        if ( isStructuralMutationFrozen() )
            deferPostTick( std::move( func ) );
        else
            func();
    }

    void GameObjectManager::registerRootSceneComponent( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        for ( SceneComponent* pExisting : _listRootSceneComponent )
        {
            if ( pExisting == pComp )
                return;
        }
        _listRootSceneComponent.push_back( pComp );
    }

    void GameObjectManager::unregisterRootSceneComponent( SceneComponent* pComp )
    {
        if ( pComp == nullptr )
            return;
        std::unique_lock<std::shared_mutex> lock{ _mutex };
        for ( size_t rootIndex = 0; rootIndex < _listRootSceneComponent.size(); ++rootIndex )
        {
            if ( _listRootSceneComponent[rootIndex] == pComp )
            {
                _listRootSceneComponent[rootIndex] = _listRootSceneComponent.back();
                _listRootSceneComponent.pop_back();
                return;
            }
        }
    }

    Component* GameObjectManager::resolveComponent( sw::ComponentHandle handle )
    {
        if ( handle.isValid() == false )
            return nullptr;
        GameObject* pObj = findGameObjectById( handle.objectId() );
        if ( pObj == nullptr )
            return nullptr;
        return pObj->findComponentById( handle.componentId(), true );
    }

    void GameObjectManager::destroyObject( GameObject* pObj, bool bDestroyChildren )
    {
        if ( pObj == nullptr )
            return;

        // **표시를 먼저, 원자적으로 자리를 잡는다.** 예전에는 `isPendingKill()` 로 보고 나서
        // `markPendingKill()` 을 했다 — 둘 사이가 벌어져 있어, 같은 오브젝트를 같은 프레임에
        // 없애는 두 스레드가 나란히 통과하면 파괴 목록에 같은 포인터가 두 번 들어가고
        // `_poolGameObject.destroy` 가 같은 블록을 두 번 반납한다. `onTick` 은 병렬로 돌고
        // (총알 둘이 같은 적을 맞히는) 그 경우는 흔하다. 자리를 잡은 스레드만 진행한다.
        if ( pObj->tryMarkPendingKill() == false )
            return;

        vector<GameObject*> listChildren;
        if ( bDestroyChildren )
            listChildren = pObj->getChildren();

        for ( Component* pComp : pObj->getAllComponents() )
        {
            if ( pComp != nullptr )
                pComp->markPendingKill();
        }
        pObj->refreshActiveInHierarchy();

        if ( bDestroyChildren )
        {
            for ( GameObject* pChild : listChildren )
            {
                if ( pChild != nullptr )
                    destroyObject( pChild, true );
            }
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _listPendingDestroyObject.push_back( pObj );
        }
        markTickWavesDirty();
    }

    void GameObjectManager::destroyComponent( Component* pComp )
    {
        // destroyObject 와 같은 이유로 자리부터 잡는다 — 여기 목록에 두 번 들어가면
        // `removeComponent` 가 두 번 불리고 컴포넌트 풀이 같은 블록을 두 번 받는다.
        if ( pComp == nullptr || pComp->tryMarkPendingKill() == false )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        _listPendingDestroyComponent.push_back( pComp );
        markTickWavesDirty();
    }

    void GameObjectManager::destroyComponentInstance( Component* pComp )
    {
        if ( pComp == nullptr )
            return;

        // 등록부는 raw 포인터를 들고 있다. ComponentPtr/ComponentHandle 은 접근할 때마다 이름·타입으로
        // 다시 찾기 때문에, 프레임마다 전부 훑는 렌더 경로에 쓰면 지금 걷어내려는 순회보다 비싸진다.
        // 대신 **메모리를 실제로 반납하는 이 한 지점**에서 등록을 해제해, 등록된 채로 해제되는 경우가
        // 구조적으로 없게 만든다. 목록에서 빼는 쪽(removeComponent, clearComponents)에만 걸어두면
        // 지연 파괴 경로가 그걸 우회한다. onUnregister 는 멱등이라 두 번 불려도 된다.
        // 소멸자 호출 전이어야 가상 디스패치가 유효하다.
        pComp->onUnregister( *this );

        // 풀 포인터만 잠금 안에서 집고 **소멸자는 잠금 밖에서** 부른다. ~SceneComponent 는
        // detachFromComponent 를 타고 (un)registerRootSceneComponent 로 다시 들어오는데, 그쪽도
        // 같은 _mutex 를 unique 로 잡는다 — shared_mutex 는 재귀적이지 않아서 그대로 멈춘다.
        // 풀은 한 번 만들어지면 매니저가 죽을 때까지 그 자리에 있고 자체 잠금을 들고 있으므로,
        // 포인터를 밖으로 들고 나가도 안전하다.
        PoolAllocator*  pPool     = nullptr;
        const TypeInfo* pTypeInfo = pComp->getTypeInfo();
        if ( pTypeInfo != nullptr )
        {
            // 키는 FQN 이다 — 이유는 getOrCreateComponentPool 주석 참고.
            std::shared_lock<std::shared_mutex> lock{ _mutex };
            auto                                iter = _mapComponentPool.find( pTypeInfo->_fullyQualifiedName );
            if ( iter != _mapComponentPool.end() )
                pPool = iter->second.get();
        }

        if ( pPool != nullptr )
        {
            pComp->~Component();
            pPool->free( pComp );
            return;
        }

        sw_delete( pComp );
    }

    void GameObjectManager::processDeferredDestruction()
    {
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingDestroyObject.empty() && _listPendingDestroyComponent.empty() )
                return;

            _listProcessingDestroyObject.swap( _listPendingDestroyObject );
            _listProcessingDestroyComponent.swap( _listPendingDestroyComponent );
        }

        for ( Component* pComp : _listProcessingDestroyComponent )
        {
            if ( pComp == nullptr )
                continue;
            GameObject* pOwner = pComp->getOwner();
            if ( pOwner != nullptr )
                pOwner->removeComponent( pComp );
        }

        if ( _listProcessingDestroyObject.empty() == false )
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            for ( GameObject* pObj : _listProcessingDestroyObject )
            {
                if ( pObj != nullptr )
                {
                    auto pendingIt = std::find( _listPendingAdd.begin(), _listPendingAdd.end(), pObj );
                    if ( pendingIt != _listPendingAdd.end() )
                    {
                        *pendingIt = _listPendingAdd.back();
                        _listPendingAdd.pop_back();
                    }

                    uint32 index = pObj->_managerIndex;
                    if ( index < _listGameObject.size() && _listGameObject[index] == pObj )
                    {
                        GameObject* pBackObj    = _listGameObject.back();
                        _listGameObject[index]  = pBackObj;
                        pBackObj->_managerIndex = index;
                        _listGameObject.pop_back();
                    }
                    const auto nameIt = _mapNameToObject.find( pObj->getName() );
                    if ( nameIt != _mapNameToObject.end() && nameIt->second == pObj )
                        _mapNameToObject.erase( nameIt );
                    _mapIdToObject.erase( pObj->getObjectId() );
                    _objectSlotTable.store( pObj->getObjectId(), nullptr );
                }
            }
        }

        vector<GameObject*> listDying;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            for ( GameObject* pObj : _listProcessingDestroyObject )
            {
                if ( pObj == nullptr )
                    continue;
                listDying.push_back( pObj );
            }
            _listProcessingDestroyObject.clear();
            _listProcessingDestroyComponent.clear();
        }

        for ( GameObject* pObj : listDying )
        {
            if ( pObj != nullptr )
                _poolGameObject.destroy( pObj );
        }
        markTickWavesDirty();
    }

    void GameObjectManager::clear()
    {
        vector<GameObject*> listDying;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };

            listDying.reserve( _listGameObject.size() + _listPendingAdd.size() );
            listDying.insert( listDying.end(), _listGameObject.begin(), _listGameObject.end() );
            listDying.insert( listDying.end(), _listPendingAdd.begin(), _listPendingAdd.end() );

            _listPendingDestroyObject.clear();
            _listPendingDestroyComponent.clear();
            _listProcessingDestroyObject.clear();
            _listProcessingDestroyComponent.clear();

            {
                std::scoped_lock<mutex> lockT{ _deferredTransformMutex };
                _listDeferredTransformUpdate.clear();
                _listProcessingTransform.clear();
            }
            {
                std::scoped_lock<mutex> lockP{ _deferredPostTickMutex };
                _listDeferredPostTickUpdate.clear();
                _listProcessingPostTick.clear();
            }

            _listGameObject.clear();
            _listPendingAdd.clear();
            _mapNameToObject.clear();
            _mapIdToObject.clear();
            _objectSlotTable.clear();
            _listRootSceneComponent.clear();
            _listCachedTickWave.clear();
        }

        for ( GameObject* pObj : listDying )
        {
            if ( pObj != nullptr )
                _poolGameObject.destroy( pObj );
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _poolGameObject.clear();
            for ( auto& [typeFqn, pPool] : _mapComponentPool )
            {
                if ( pPool != nullptr )
                    pPool->clear();
            }
        }

        _listCachedTickWave.clear();
        markTickWavesDirty();
    }

    void GameObjectManager::rebindAllCachedTypeInfo()
    {
        TypeRegistry& typeRegistry = engine::getTypeRegistry();
        forEachGameObject( [&typeRegistry]( GameObject* pObj )
        {
            if ( pObj == nullptr )
                return;
            pObj->forEachComponent( [&typeRegistry]( Component* pComp )
            {
                if ( pComp == nullptr )
                    return;
                const hashed_string typeKey = pComp->getComponentName();
                if ( typeKey.empty() )
                    return;
                pComp->applyTypeDefaults( typeRegistry.findType( typeKey ) );
            } );
        } );
        markTickWavesDirty();
    }

    void GameObjectManager::mergePendingAdds()
    {
        vector<GameObject*> listLocalPending;
        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            if ( _listPendingAdd.empty() )
                return;
            listLocalPending = std::move( _listPendingAdd );
            _listPendingAdd.clear();
        }

        {
            std::unique_lock<std::shared_mutex> lock{ _mutex };
            _listGameObject.reserve( _listGameObject.size() + listLocalPending.size() );
            for ( GameObject* pObj : listLocalPending )
            {
                if ( pObj != nullptr )
                {
                    pObj->_managerIndex = static_cast<uint32>( _listGameObject.size() );
                    _listGameObject.push_back( pObj );
                    _mapNameToObject[pObj->getName()]   = pObj;
                    _mapIdToObject[pObj->getObjectId()] = pObj;
                    _objectSlotTable.store( pObj->getObjectId(), pObj );
                }
            }
        }

        for ( GameObject* pObj : listLocalPending )
        {
            if ( pObj != nullptr )
                pObj->refreshActiveInHierarchy();
        }
        markTickWavesDirty();
    }

    void GameObjectManager::registerPendingFactories( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
    {
        if ( pHead == nullptr )
            return;

        {
            std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
            GameObjectManagerInternal::getModuleFactoryHeads()[string( moduleName )] = pHead;
        }
        _activeModuleName                   = hashed_string( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        ComponentFactoryRegistrar* pCurrent = pHead;
        while ( pCurrent != nullptr )
        {
            if ( pCurrent->_registerFunc != nullptr )
                pCurrent->_registerFunc( *this );
            pCurrent = pCurrent->_pNext;
        }
        _activeModuleName = hashed_string();
    }

#if !defined( SW_SHIPPING )
    void GameObjectManager::unregisterFactoriesByModule( string_view moduleName )
    {
        const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        for ( auto it = _mapFactoryModule.begin(); it != _mapFactoryModule.end(); )
        {
            if ( it->second == hashModule )
            {
                _mapFactory.erase( it->first );
                it = _mapFactoryModule.erase( it );
            }
            else
                ++it;
        }
    }
#endif

#if !defined( SW_SHIPPING )
    uint32 GameObjectManager::destroyComponentsOfModule( string_view moduleName )
    {
        SW_ASSERT( isStructuralMutationFrozen() == false );
        // 지연 파괴 목록부터 — 거기 남은 컴포넌트의 소멸자도 모듈 코드다.
        processDeferredDestruction();

        const hashed_string hashModule( moduleName.data(), static_cast<uint32>( moduleName.size() ) );
        vector<Component*>  listDoomed;
        forEachComponent( [&]( Component* pComp )
        {
            const TypeInfo* pTypeInfo = ( pComp != nullptr ) ? pComp->getTypeInfo() : nullptr;
            if ( pTypeInfo != nullptr && pTypeInfo->_moduleName == hashModule )
                listDoomed.push_back( pComp );
        } );
        for ( Component* pComp : listDoomed )
        {
            GameObject* pOwner = pComp->getOwner();
            if ( pOwner != nullptr )
                pOwner->removeComponent( pComp );
        }
        // removeComponent 는 얼려 있으면 미룬다 — 위에서 단언했지만, 미뤄졌더라도 여기서 끝낸다.
        processDeferredDestruction();

        const uint32 count = static_cast<uint32>( listDoomed.size() );
        if ( count > 0 )
            SW_LOG_INFO( "Destroyed %# live component(s) of module '%#' before unload.", count, moduleName );
        return count;
    }
#endif

    void GameObjectManager::registerModuleFactoryHead( string_view moduleName, sw::ComponentFactoryRegistrar* pHead )
    {
        _s_engineHeadSealed = true;
        std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
        if ( pHead == nullptr )
            GameObjectManagerInternal::getModuleFactoryHeads().erase( string( moduleName ) );
        else
            GameObjectManagerInternal::getModuleFactoryHeads()[string( moduleName )] = pHead;
    }

    void GameObjectManager::unregisterModuleFactoryHead( string_view moduleName )
    {
        std::scoped_lock<mutex> lock{ GameObjectManagerInternal::getModuleFactoryHeadsMutex() };
        GameObjectManagerInternal::getModuleFactoryHeads().erase( string( moduleName ) );
    }

    Component* GameObjectManager::addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning )
    {
        if ( pGameObject == nullptr )
            return nullptr;

        hashed_string factoryName = typeName;
        auto          it          = _mapFactory.find( factoryName );
        if ( it == _mapFactory.end() )
        {
            const utf8* pRawName = typeName.c_str();
            if ( pRawName != nullptr )
            {
                const utf8* pHash = nullptr;
                for ( const utf8* pCursor = pRawName; *pCursor != '\0'; ++pCursor )
                {
                    if ( *pCursor == '#' )
                        pHash = pCursor;
                }
                if ( pHash != nullptr && pHash != pRawName )
                    factoryName = hashed_string( pRawName, static_cast<uint32>( pHash - pRawName ) );
            }
            it = _mapFactory.find( factoryName );
        }
        if ( it != _mapFactory.end() )
        {
            if ( it->second.isBound() == false )
            {
                if ( bLogWarning )
                    SW_LOG_WARNING( "Component factory for type '%#' is unbound", typeName.c_str() );
                return nullptr;
            }
            return it->second( pGameObject );
        }
        if ( bLogWarning )
            SW_LOG_WARNING( "Component factory for type '%#' not found (%# factories registered)", typeName.c_str(), static_cast<uint32>( _mapFactory.size() ) );
        return nullptr;
    }

    vector<hashed_string> GameObjectManager::getRegisteredComponentTypeNames() const
    {
        vector<hashed_string> listName;
        listName.reserve( _mapFactory.size() );
        for ( const auto& [name, factory] : _mapFactory )
        {
            (void)factory;
            listName.push_back( name );
        }
        return listName;
    }

    void GameObjectManager::tickComponents( float32 deltaTime )
    {
        if ( _bIsTickWavesDirty.exchange( false, std::memory_order_acq_rel ) )
        {
            array<vector<GameObjectManagerInternal::TickCandidate>, 4> arrListGroup;
            uint32                                                     totalCandidateCount = 0;

            forEachGameObject( [&]( GameObject* pObj )
            {
                if ( pObj == nullptr || pObj->isPendingKill() )
                    return;

                pObj->forEachComponent( [&]( Component* pComp )
                {
                    if ( pComp == nullptr || pComp->isPendingKill() )
                        return;

                    // 1. Main Tick
                    if ( pComp->canEverTick() )
                    {
                        const uint8 groupIndex = static_cast<uint8>( pComp->getTickGroup() );
                        if ( groupIndex < arrListGroup.size() )
                        {
                            GameObjectManagerInternal::TickCandidate cand{};
                            cand._pComponent    = pComp;
                            cand._handle        = pComp->getHandle();
                            cand._subTickId     = 0;
                            cand._componentId   = pComp->getComponentId();
                            cand._objectId      = pObj->getObjectId();
                            cand._orderKey      = static_cast<uint8>( TickPhase::Normal );
                            cand._originalIndex = totalCandidateCount++;
                            arrListGroup[groupIndex].push_back( std::move( cand ) );
                        }
                    }

                    // 2. SubTicks
                    for ( const SubTickInfo& subTick : pComp->getAllSubTicks() )
                    {
                        if ( subTick._bActive == SW_TRUE )
                        {
                            const uint8 groupIndex = static_cast<uint8>( subTick._group );
                            if ( groupIndex < arrListGroup.size() )
                            {
                                GameObjectManagerInternal::TickCandidate cand{};
                                cand._pComponent       = pComp;
                                cand._handle           = pComp->getHandle();
                                cand._subTickId        = subTick._subTickId;
                                cand._componentId      = pComp->getComponentId();
                                cand._objectId         = pObj->getObjectId();
                                cand._orderKey         = static_cast<uint8>( subTick._phase ) + ( subTick._priority & 63 );
                                cand._originalIndex    = totalCandidateCount++;
                                cand._listPrerequisite = subTick._listPrerequisite;
                                arrListGroup[groupIndex].push_back( std::move( cand ) );
                            }
                        }
                    }
                } );
            } );

            _listCachedTickWave.clear();
            for ( vector<GameObjectManagerInternal::TickCandidate>& groupCandidates : arrListGroup )
            {
                if ( groupCandidates.empty() )
                    continue;

                vector<vector<TickExecutionItem>> listDagWaves = GameObjectManagerInternal::sortTickCandidates( groupCandidates );
                for ( const vector<TickExecutionItem>& dagWave : listDagWaves )
                {
                    vector<vector<TickExecutionItem>> listSubwave = GameObjectManagerInternal::splitWaveByObject( dagWave );
                    for ( vector<TickExecutionItem>& subwave : listSubwave )
                    {
                        if ( subwave.empty() == false )
                            _listCachedTickWave.push_back( std::move( subwave ) );
                    }
                }
            }
        }

        for ( const vector<TickExecutionItem>& wave : _listCachedTickWave )
            GameObjectManagerInternal::dispatchWave( this, deltaTime, wave );
    }

    void GameObjectManager::registerGameObject( GameObject* pObj )
    {
        if ( pObj == nullptr )
            return;

        std::unique_lock<std::shared_mutex> lock{ _mutex };
        const uint64                        newObjectId = generateNewId();
        pObj->_objectId                                 = newObjectId;
        pObj->_pOwnerManager                            = this;

        if ( isNameTakenUnlocked( pObj->getName() ) )
            pObj->_name = makeUniqueNameUnlocked( pObj->getName() );

        _mapNameToObject.insert_or_assign( pObj->getName(), pObj );
        _mapIdToObject.insert_or_assign( newObjectId, pObj );
        _objectSlotTable.store( newObjectId, pObj );

        _listPendingAdd.push_back( pObj );
    }

    void GameObjectManager::flushSceneComponentSubtree( SceneComponent* pRoot, bool bParentChanged )
    {
        if ( pRoot == nullptr )
            return;

        // 깊은 계층에서 스택이 넘치지 않도록 명시적 스택으로 도는 DFS. 원소는 (노드, 부모가 바뀌었나).
        //
        // **버퍼는 매니저가 들고 재사용한다.** 예전에는 이 함수가 호출마다 `vector` 를 만들고
        // `reserve(32)` 했는데, 이 함수는 **루트 씬 컴포넌트마다** 불린다 — 큐브 20,000 개 벤치에서
        // 프레임당 20,000 번의 힙 할당이었다. 재사용해도 안전한 이유는 부르는 곳이 하나
        // (`flushSceneTransforms`, 게임 스레드)뿐이기 때문이다.
        vector<pair<SceneComponent*, bool>>& stack = _listTransformFlushStack;
        stack.clear();
        stack.reserve( 32 );
        stack.emplace_back( pRoot, bParentChanged );

        while ( stack.empty() == false )
        {
            auto [node, parentDirty] = stack.back();
            stack.pop_back();

            if ( node == nullptr )
                continue;

            const bool bNeedsUpdate = parentDirty || node->isTransformDirty();
            if ( bNeedsUpdate )
                node->updateWorldTransformFromParent();

            if ( bNeedsUpdate || node->hasDirtyDescendant() )
            {
                const auto& children = node->getChildren();
                for ( auto it = children.rbegin(); it != children.rend(); ++it )
                {
                    stack.emplace_back( *it, bNeedsUpdate );
                }
            }
            node->clearDirtyDescendant();
        }
    }

    uint64 GameObjectManager::generateNewId()
    {
        return _nextId.fetch_add( 1, std::memory_order_relaxed );
    }

    bool GameObjectManager::isNameTakenUnlocked( hashed_string name ) const
    {
        const auto it = _mapNameToObject.find( name );
        return it != _mapNameToObject.end() && it->second != nullptr && it->second->isPendingKill() == false;
    }

    hashed_string GameObjectManager::makeUniqueNameUnlocked( hashed_string requested ) const
    {
        if ( isNameTakenUnlocked( requested ) == false )
            return requested;

        const utf8* pBase = requested.c_str();
        if ( StringUtil::isNullOrEmpty( pBase ) )
            pBase = "GameObject";

        string_view baseView{ pBase };
        if ( baseView.size() > 96 )
            baseView = baseView.substr( 0, 96 );

        StringBuilder<constant::kMaxBuffer128> sb;
        for ( uint32 nameSuffix = 2; nameSuffix < 10000; ++nameSuffix )
        {
            sb.clear();
            sb.append( baseView ).append( '_' ).append( nameSuffix );
            const hashed_string candidate( sb.c_str(), sb.size() );
            if ( isNameTakenUnlocked( candidate ) == false )
            {
                SW_LOG_WARNING( "Duplicate name '%#' — using '%#'", requested.c_str(), candidate.c_str() );
                return candidate;
            }
        }

        static atomic<uint32> s_fallback{ 0 };
        for ( uint32 fallbackIndex = 0; fallbackIndex < 1024; ++fallbackIndex )
        {
            sb.clear();
            sb.append( baseView ).append( "_x" ).append( s_fallback.fetch_add( 1 ) );
            const hashed_string fallback( sb.c_str(), sb.size() );
            if ( isNameTakenUnlocked( fallback ) == false )
            {
                SW_LOG_ERROR( "Exhausted numeric suffixes for '%#' — using '%#'",
                              requested.c_str(), fallback.c_str() );
                return fallback;
            }
        }

        SW_LOG_ERROR( "Failed to uniquify '%#' — creating unnamed object", requested.c_str() );
        sb.clear();
        sb.append( "GameObject_anon_" ).append( s_fallback.fetch_add( 1 ) );
        return hashed_string( sb.c_str(), sb.size() );
    }
} // namespace sw

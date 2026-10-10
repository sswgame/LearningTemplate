/**
 * @file NetSimDestructionScenario.h
 * @brief 가상 서버 하니스 위의 파괴 네트워킹 시나리오 — 쇼케이스 씬(벽 200 조각 · 상자 셋 · 도화선 드럼통 사슬)을 서버 1 + 클라이언트 3 이 각자 씬 · 물리로 돌린다.
 * @details 서버만 드럼통이 터지고(권한), 클라이언트는 사건 · 덩어리 자세 · 스냅숏을 받아 같은 구조 상태가 된다. 수렴 틱 · 사건 지연 · 덩어리 오차 · 대역폭을
 *          재고 로그 줄 `[NetSimDestruction]` 로 남긴다. `NetSimDestructionTest`(깨끗한 회선 · 늦은 참가 · 빠진 사건)와 `NetSimDestructionMatrixTest`
 *          (나쁜 회선 둘 — 회선마다 한 케이스)가 함께 쓴다. 창(`_tickCount`)이 끝나도 서버가 아직 부서지고 있으면 모두 같아질 때까지 `_settleTickLimit` 안에서 더 돈다 —
 *          서버 물리가 가라앉는 시점은 구성마다 다르다(리눅스 Shipping 은 480 틱 뒤).
 */
#pragma once
#include "Core/Math/MathUtil.h"

#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Physics/Collision/CollisionLayers.h"
#include "Engine/Physics/PhysicsSettings.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimHarness.h"

#include "TestFramework/TestFramework.h"

#include <algorithm>

namespace test
{
    using namespace sw; // 이 이름공간 안에서만 — 시험 공용 헤더

    /** @brief 월드 하나의 파괴 오브젝트 — 이름순 번호가 네트워크 id 다(같은 씬 문서라 월드마다 같다). 오브젝트를 지우지 않으므로 포인터를 든다. */
    struct ShowcaseObjects
    {
        vector<FractureComponent*> _listFracture;
        vector<string>             _listName;

        void collect( GameObjectManager& manager )
        {
            vector<GameObject*> listObject;
            manager.getAllGameObjects( listObject );
            std::sort( listObject.begin(), listObject.end(), []( const GameObject* pLhs, const GameObject* pRhs )
            { return pLhs->getName().lexicalLess( pRhs->getName() ); } );
            for ( GameObject* pObject : listObject )
            {
                FractureComponent* pFracture = pObject->getComponent<FractureComponent>();
                if ( pFracture == nullptr )
                    continue;
                _listFracture.push_back( pFracture );
                _listName.push_back( string{ pObject->getName().c_str() } );
            }
        }
    };
} // namespace test

namespace test
{
    class ShowcaseServerSession final : public INetSimSession
    {
    public:
        ShowcaseServerSession( NetSimWorld& world, const SceneDocument& document )
            : _replication{}
            , _objects{}
        {
            (void)world.getScene().instantiate( document );
            _objects.collect( world.getObjectManager() );
            _replication.initialize( &world.getHost(), &world.getObjectManager(), DestructionReplicationSettings{} );
            for ( uint32 index = 0; index < static_cast<uint32>( _objects._listFracture.size() ); ++index )
            {
                _replication.registerObject( index + 1, *_objects._listFracture[index] );
            }
            world.getRouter().addHandler( &_replication );
        }

        void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)deltaTime;
            _replication.update( world.getLocalTick() );
        }

        DestructionReplicationServer _replication;
        ShowcaseObjects              _objects;
    };
} // namespace test

namespace test
{
    class ShowcaseClientSession final : public INetSimSession
    {
    public:
        ShowcaseClientSession( NetSimWorld& world, const SceneDocument& document )
            : _replication{}
            , _objects{}
        {
            (void)world.getScene().instantiate( document );
            // 드럼통 도화선 · 폭발은 서버의 게임플레이다 — 클라이언트는 사건만 받는다.
            vector<GameObject*> listObject;
            world.getObjectManager().getAllGameObjects( listObject );
            for ( GameObject* pObject : listObject )
            {
                ExplosiveBarrelComponent* pBarrel = pObject->getComponent<ExplosiveBarrelComponent>();
                if ( pBarrel != nullptr )
                    pBarrel->setActive( false );
            }
            _objects.collect( world.getObjectManager() );
            _replication.initialize( &world.getHost(), &world.getObjectManager(), DestructionReplicationSettings{} );
            for ( uint32 index = 0; index < static_cast<uint32>( _objects._listFracture.size() ); ++index )
            {
                _replication.registerObject( index + 1, *_objects._listFracture[index] );
            }
            world.getRouter().addHandler( &_replication );
        }

        void onTickBegin( NetSimWorld& world, float32 deltaTime ) override
        {
            (void)world;
            _replication.update( deltaTime );
        }

        DestructionReplicationClient _replication;
        ShowcaseObjects              _objects;
    };
} // namespace test

namespace test
{
    class ShowcaseGame final : public INetSimGame
    {
    public:
        explicit ShowcaseGame( const SceneDocument& document )
            : _pDocument{ &document }
        {
        }

        unique_ptr<INetSimSession> createSession( NetSimWorld& world ) override
        {
            if ( world.isServer() )
                return make_unique<ShowcaseServerSession>( world, *_pDocument );
            return make_unique<ShowcaseClientSession>( world, *_pDocument );
        }

    private:
        const SceneDocument* _pDocument;
    };
} // namespace test

namespace test
{
    inline ShowcaseServerSession& getServerSession( NetSimHarness& harness ) { return *static_cast<ShowcaseServerSession*>( harness.getServer().getSession() ); }
    inline ShowcaseClientSession& getClientSession( NetSimWorld& world ) { return *static_cast<ShowcaseClientSession*>( world.getSession() ); }

    /** @brief 클라이언트의 모든 파괴 오브젝트 해시가 서버와 같은지입니다. */
    inline bool matchesServer( NetSimHarness& harness, NetSimWorld& client )
    {
        const ShowcaseObjects& server = getServerSession( harness )._objects;
        const ShowcaseObjects& local  = getClientSession( client )._objects;
        if ( server._listFracture.size() != local._listFracture.size() )
            return false;
        for ( size_t index = 0; index < server._listFracture.size(); ++index )
        {
            const DestructionState& serverState = server._listFracture[index]->getState();
            const DestructionState& localState  = local._listFracture[index]->getState();
            if ( serverState.computeStateHash() != localState.computeStateHash() )
                return false;
        }
        return true;
    }

    /** @brief 서버 덩어리 자세 기록 — (오브젝트, 그룹) 마다 틱 → 자리. */
    struct ChunkHistory
    {
        struct Sample
        {
            float3 _position{};
            uint32 _tick{ 0 };
        };
        struct Track
        {
            vector<Sample> _listSample{};
            uint32         _objectIndex{ 0 };
            uint32         _groupId{ 0 };
        };

        vector<Track> _listTrack;

        void record( uint32 objectIndex, uint32 groupId, uint32 tick, const float3& position )
        {
            Track* pTrack = findTrack( objectIndex, groupId );
            if ( pTrack == nullptr )
            {
                _listTrack.push_back( Track{ {}, objectIndex, groupId } );
                pTrack = &_listTrack.back();
            }
            pTrack->_listSample.push_back( Sample{ position, tick } );
        }

        Track* findTrack( uint32 objectIndex, uint32 groupId )
        {
            for ( Track& track : _listTrack )
            {
                if ( track._objectIndex == objectIndex && track._groupId == groupId )
                    return &track;
            }
            return nullptr;
        }

        /**
         * @brief 서버 틱(소수)의 자리 — 앞뒤 기록 사이 선형. 그 틱에 서버에 그 그룹이 없었으면 false.
         * @details 서버에서 이미 갈라진 그룹을 클라이언트가 아직 들고 있는 것(가른 사건이 신뢰 채널에서 밀렸다)은 자세 오차가 아니라 사건 지연이다 — 따로 잰다.
         */
        bool sample( uint32 objectIndex, uint32 groupId, float32 tick, float3& outPosition )
        {
            Track*     pTrack   = findTrack( objectIndex, groupId );
            const bool bOutside = pTrack == nullptr || pTrack->_listSample.empty() || tick < static_cast<float32>( pTrack->_listSample.front()._tick ) ||
                                  tick > static_cast<float32>( pTrack->_listSample.back()._tick ) + 1.0f;
            if ( bOutside )
                return false;
            for ( size_t index = 0; index + 1 < pTrack->_listSample.size(); ++index )
            {
                const Sample& from = pTrack->_listSample[index];
                const Sample& to   = pTrack->_listSample[index + 1];
                if ( tick <= static_cast<float32>( to._tick ) )
                {
                    const float32 alpha = to._tick > from._tick ? ( tick - static_cast<float32>( from._tick ) ) / static_cast<float32>( to._tick - from._tick ) : 1.0f;
                    outPosition         = from._position + ( to._position - from._position ) * MathUtil::clamp( alpha, 0.0f, 1.0f );
                    return true;
                }
            }
            outPosition = pTrack->_listSample.back()._position;
            return true;
        }
    };
} // namespace test

namespace test
{
    struct ScenarioOptions
    {
        NetEmulationConditions _conditions{};
        uint32                 _tickCount{ 720 };
        int32                  _lateJoinTick{ -1 };    ///< 이 틱에 클라이언트 하나가 더 들어온다
        int32                  _skipEventClient{ -1 }; ///< 이 클라이언트(0 부터)가 벽의 다음 사건 하나를 빼먹는다
        uint32                 _seed{ 5u };
        uint32                 _settleTickLimit{ 600 }; ///< 창(_tickCount) 뒤에 모두가 서버와 같아질 때까지 더 도는 틱 상한
    };
} // namespace test

namespace test
{
    struct ScenarioResult
    {
        vector<float32> _listChunkError;       ///< 움직이는 덩어리 — 클라이언트 자리 vs 서버의 같은 렌더 틱 자리(미터)
        float32         _maxRestError{ 0.0f }; ///< 끝에 멈춘 덩어리 — 클라이언트 vs 서버(미터)
        int32           _lastServerChangeTick{ -1 };
        int32           _convergedTick{ -1 }; ///< 그 뒤 모든 클라이언트 해시가 서버와 같아진 틱(끝까지 유지)
        int32           _maxEventLagTicks{ 0 };
        float32         _meanEventLagTicks{ 0.0f };   ///< 사건마다 모든 클라이언트가 적용하기까지(틱)의 평균 — 늦게 들어온 클라이언트는 빼고
        float32         _minRenderLagTicks{ 1.0e9f }; ///< 클라이언트가 덩어리를 그리는 틱이 서버 틱보다 가장 덜 뒤처졌을 때(틱)
        int32           _lateJoinMatchTicks{ -1 };
        int32           _desyncRecoverTicks{ -1 };
        int32           _settleTicks{ 0 }; ///< 창 뒤에 더 돈 틱 — 서버 물리가 창 끝까지 움직이는 구성에서만 0 보다 크다
        uint32          _hashMismatchCount{ 0 };
        uint32          _snapshotAppliedCount{ 0 };
        uint32          _debrisViolationCount{ 0 }; ///< 파편인데 캐릭터와 부딪히는 레이어거나, 덩어리인데 클라이언트가 몰지 않는 것
        uint32          _debrisCount{ 0 };
        uint32          _chunkCount{ 0 };
        uint32          _serverEventCount{ 0 };
        float32         _serverUploadBytesPerSecond{ 0.0f };     ///< 서버가 내놓은 바이트(전 클라이언트 합 · 손실 전)
        float32         _clientDownloadBytesPerSecond{ 0.0f };   ///< 클라이언트 하나가 받은 바이트(평균)
        float32         _destructionBytesPerSecond{ 0.0f };      ///< 그중 파괴 메시지 몸
        float32         _peakServerUploadBytesPerSecond{ 0.0f }; ///< 1 초 창 가운데 가장 많이 내놓은 창
        uint64          _poseMessageCount{ 0 };
        bool            _bValid{ false };
    };

    inline float32 computePercentile( vector<float32> listValue, float32 fraction )
    {
        if ( listValue.empty() )
            return 0.0f;
        std::sort( listValue.begin(), listValue.end() );
        const size_t index = MathUtil::min( listValue.size() - 1, static_cast<size_t>( fraction * static_cast<float32>( listValue.size() - 1 ) + 0.5f ) );
        return listValue[index];
    }

    inline ScenarioResult runShowcase( const SceneDocument& document, const ScenarioOptions& options )
    {
        ScenarioResult result;
        ShowcaseGame   game{ document };
        NetSimHarness  harness;
        NetSimSettings settings;
        settings._seed = options._seed;
        if ( harness.initialize( settings, &game ) == false )
            return result;
        for ( int32 index = 0; index < 3; ++index )
        {
            (void)harness.addClient( NetSimLinkConditions::makeSymmetric( options._conditions ) );
        }

        ShowcaseObjects&     server      = getServerSession( harness )._objects;
        const uint32         objectCount = static_cast<uint32>( server._listFracture.size() );
        vector<uint64>       listServerHash( objectCount, 0 );
        vector<uint32>       listServerCount( objectCount, 0 );
        vector<uint32>       listCountTick; ///< 서버 사건 i 가 적용된 틱(모든 오브젝트를 이어 센다 — 오브젝트 · 번호로 찾는다)
        vector<uint32>       listCountObject;
        vector<uint32>       listCountIndex;
        ChunkHistory         history;
        int32                lateClient   = -1;
        int32                lateJoinTick = -1;
        int32                desyncTick   = -1;
        vector<NetSimWorld*> listClient;
        harness.collectClients( listClient );
        if ( options._skipEventClient >= 0 && options._skipEventClient < static_cast<int32>( listClient.size() ) )
        {
            // 벽(BrickWall)의 다음 사건 하나를 빼먹는다.
            const ShowcaseObjects& objects = getClientSession( *listClient[static_cast<size_t>( options._skipEventClient )] )._objects;
            for ( uint32 index = 0; index < static_cast<uint32>( objects._listName.size() ); ++index )
            {
                if ( objects._listName[index] == "BrickWall" )
                    getClientSession( *listClient[static_cast<size_t>( options._skipEventClient )] )._replication.skipNextEvent( index + 1 );
            }
        }

        uint64                    eventLagSum   = 0;
        uint32                    eventLagCount = 0;
        vector<FractureGroupPose> listPose;
        uint64                    windowStartBytes = 0;
        // 서버 물리가 언제 가라앉는지는 구성마다 다르다(리눅스 Shipping 은 벽 사건이 480 틱 뒤까지 온다) — 창이 끝나도 모두 같아질 때까지 상한 안에서 더 돈다.
        const uint32 tickLimit = options._tickCount + options._settleTickLimit;
        uint32       tick      = 0;
        for ( ; tick < tickLimit; ++tick )
        {
            if ( tick >= options._tickCount && result._convergedTick >= 0 )
                break;
            if ( tick % 60 == 0 )
            {
                const uint64 sentBytes                 = harness.getServer().getTraffic()._sentBytes;
                result._peakServerUploadBytesPerSecond = MathUtil::max( result._peakServerUploadBytesPerSecond, static_cast<float32>( sentBytes - windowStartBytes ) );
                windowStartBytes                       = sentBytes;
            }
            if ( options._lateJoinTick >= 0 && static_cast<int32>( tick ) == options._lateJoinTick )
            {
                lateClient   = harness.addClient( NetSimLinkConditions::makeSymmetric( options._conditions ) );
                lateJoinTick = static_cast<int32>( tick );
            }
            harness.step();
            const uint32 serverTick = harness.getServer().getLocalTick() - 1; // 방금 돈 틱(서버가 보낸 자세의 틱)
            harness.collectClients( listClient );

            // 서버 — 해시 바뀜 · 사건 적용 틱 · 덩어리 자세 기록.
            for ( uint32 object = 0; object < objectCount; ++object )
            {
                const DestructionState& state = server._listFracture[object]->getState();
                const uint64            hash  = state.computeStateHash();
                if ( hash != listServerHash[object] )
                {
                    listServerHash[object]       = hash;
                    result._lastServerChangeTick = static_cast<int32>( tick );
                }
                for ( uint32 index = listServerCount[object]; index < state.getEventCount(); ++index )
                {
                    listCountTick.push_back( tick );
                    listCountObject.push_back( object );
                    listCountIndex.push_back( index );
                }
                listServerCount[object] = state.getEventCount();
                server._listFracture[object]->collectGroupPoses( listPose );
                for ( const FractureGroupPose& pose : listPose )
                {
                    if ( pose._bGone == SW_FALSE && server._listFracture[object]->isChunkVolume( pose._volume ) )
                        history.record( object, pose._groupId, serverTick, pose._center );
                }
            }

            // 클라이언트 — 사건 지연 · 움직이는 덩어리 오차 · 해시.
            bool bAllMatch = true;
            for ( NetSimWorld* pClient : listClient )
            {
                ShowcaseClientSession& session = getClientSession( *pClient );
                const bool             bMatch  = matchesServer( harness, *pClient );
                bAllMatch                      = bAllMatch && bMatch;
                if ( pClient->getWorldIndex() == lateClient && bMatch && result._lateJoinMatchTicks < 0 && result._lastServerChangeTick >= 0 )
                    result._lateJoinMatchTicks = static_cast<int32>( tick ) - lateJoinTick;
                for ( size_t entry = 0; entry < listCountTick.size(); ++entry )
                {
                    const DestructionState& localState = session._objects._listFracture[listCountObject[entry]]->getState();
                    // 이 클라이언트가 그 사건까지 적용했으면 지연을 잰다(처음 본 틱 — 늦게 들어온 클라이언트는 스냅숏이 담았다).
                    if ( localState.getEventCount() > listCountIndex[entry] && pClient->getWorldIndex() != lateClient )
                        result._maxEventLagTicks = MathUtil::max( result._maxEventLagTicks, static_cast<int32>( tick - listCountTick[entry] ) );
                }
                const float32 renderTick = session._replication.getRenderTick();
                if ( renderTick >= 0.0f )
                    result._minRenderLagTicks = MathUtil::min( result._minRenderLagTicks, static_cast<float32>( serverTick ) - renderTick );
                for ( uint32 object = 0; object < objectCount; ++object )
                {
                    session._objects._listFracture[object]->collectGroupPoses( listPose );
                    for ( const FractureGroupPose& pose : listPose )
                    {
                        float3 serverPosition{};
                        if ( pose._bDriven == SW_FALSE || pose._bResting == SW_TRUE || renderTick < 0.0f )
                            continue;
                        if ( history.sample( object, pose._groupId, renderTick, serverPosition ) )
                            result._listChunkError.push_back( ( pose._center - serverPosition ).getLength() );
                    }
                }
            }
            // 이미 맞춘 사건의 지연은 다시 재지 않는다(앞에서 지운다) — 모든 클라이언트가 그 번호를 넘으면 지운다.
            for ( size_t entry = listCountTick.size(); entry > 0; --entry )
            {
                bool bEveryone = true;
                for ( NetSimWorld* pClient : listClient )
                {
                    if ( pClient->getWorldIndex() == lateClient )
                        continue;
                    bEveryone = bEveryone && getClientSession( *pClient )._objects._listFracture[listCountObject[entry - 1]]->getState().getEventCount() > listCountIndex[entry - 1];
                }
                if ( bEveryone )
                {
                    eventLagSum += static_cast<uint64>( tick - listCountTick[entry - 1] );
                    ++eventLagCount;
                    listCountTick.erase( listCountTick.begin() + static_cast<std::ptrdiff_t>( entry - 1 ) );
                    listCountObject.erase( listCountObject.begin() + static_cast<std::ptrdiff_t>( entry - 1 ) );
                    listCountIndex.erase( listCountIndex.begin() + static_cast<std::ptrdiff_t>( entry - 1 ) );
                }
            }
            if ( bAllMatch && result._convergedTick < 0 && result._lastServerChangeTick >= 0 )
                result._convergedTick = static_cast<int32>( tick );
            else if ( bAllMatch == false )
                result._convergedTick = -1;
            // 일부러 어긋난 클라이언트 — 서버와 처음 달라진 틱부터 다시 같아질 때까지.
            if ( options._skipEventClient >= 0 && static_cast<size_t>( options._skipEventClient ) < listClient.size() )
            {
                const bool                         bMatch = matchesServer( harness, *listClient[static_cast<size_t>( options._skipEventClient )] );
                const DestructionReplicationStats& stats  = getClientSession( *listClient[static_cast<size_t>( options._skipEventClient )] )._replication.getStats();
                if ( desyncTick < 0 && stats._skippedEventCount > 0 )
                    desyncTick = static_cast<int32>( tick );
                if ( desyncTick >= 0 && result._desyncRecoverTicks < 0 && bMatch && stats._snapshotAppliedCount > 0 )
                    result._desyncRecoverTicks = static_cast<int32>( tick ) - desyncTick;
            }
        }
        result._settleTicks = static_cast<int32>( tick ) - static_cast<int32>( options._tickCount );

        // 끝 — 멈춘 덩어리 오차, 파편 레이어, 대역폭.
        const PhysicsSettings*    pSettings      = harness.getServer().getObjectManager().getScenePhysics().findSettings();
        uint8                     characterLayer = 0;
        uint8                     debrisLayer    = 0;
        const bool                bLayers        = pSettings != nullptr && pSettings->findLayerIndex( "Character", characterLayer ) && pSettings->findLayerIndex( "Debris", debrisLayer );
        const CollisionLayers     layers         = pSettings != nullptr ? pSettings->makeCollisionLayers() : CollisionLayers{};
        vector<FractureGroupPose> listServerPose;
        for ( NetSimWorld* pClient : listClient )
        {
            ShowcaseClientSession& session = getClientSession( *pClient );
            for ( uint32 object = 0; object < objectCount; ++object )
            {
                const FractureComponent* pLocal = session._objects._listFracture[object];
                pLocal->collectGroupPoses( listPose );
                server._listFracture[object]->collectGroupPoses( listServerPose );
                for ( const FractureGroupPose& pose : listPose )
                {
                    if ( pose._bGone == SW_TRUE || pose._bHasBody == SW_FALSE )
                        continue;
                    if ( pLocal->isChunkVolume( pose._volume ) )
                    {
                        ++result._chunkCount;
                        for ( const FractureGroupPose& serverPose : listServerPose )
                        {
                            if ( serverPose._groupId == pose._groupId && serverPose._bResting == SW_TRUE )
                                result._maxRestError = MathUtil::max( result._maxRestError, ( pose._position - serverPose._position ).getLength() );
                        }
                        if ( pose._bDriven == SW_FALSE )
                            ++result._debrisViolationCount;
                    }
                    else
                    {
                        ++result._debrisCount;
                        const bool bTouchesPlayers = bLayers == false || pose._layer != debrisLayer || layers.shouldCollide( characterLayer, pose._layer );
                        if ( bTouchesPlayers )
                            ++result._debrisViolationCount;
                    }
                }
            }
            const DestructionReplicationStats& stats = session._replication.getStats();
            result._hashMismatchCount += stats._hashMismatchCount;
            result._snapshotAppliedCount += stats._snapshotAppliedCount;
            result._clientDownloadBytesPerSecond += static_cast<float32>( pClient->getTraffic()._receivedBytes );
        }
        const float32 seconds                = static_cast<float32>( harness.getTime() );
        result._clientDownloadBytesPerSecond = result._clientDownloadBytesPerSecond / MathUtil::max( 1.0f, static_cast<float32>( listClient.size() ) ) / seconds;
        result._serverUploadBytesPerSecond   = static_cast<float32>( harness.getServer().getTraffic()._sentBytes ) / seconds;
        result._destructionBytesPerSecond    = static_cast<float32>( getServerSession( harness )._replication.getStats()._sentBytes ) / seconds;
        result._poseMessageCount             = getServerSession( harness )._replication.getStats()._poseMessageCount;
        for ( const FractureComponent* pFracture : server._listFracture )
        {
            result._serverEventCount += pFracture->getState().getEventCount();
        }
        result._meanEventLagTicks = eventLagCount > 0 ? static_cast<float32>( eventLagSum ) / static_cast<float32>( eventLagCount ) : 0.0f;
        result._bValid            = true;
        return result;
    }

    inline void logResult( [[maybe_unused]] const utf8* pName, [[maybe_unused]] const ScenarioResult& result ) // 로그만 — Shipping 에서는 빈 함수
    {
        SW_LOG_INFO( "[NetSimDestruction] %#: events %#, last server change tick %#, converge %# ticks after it, max event lag %# ticks mean %# ticks, chunk error max %# m p99 %# m (%# samples), render lag min %# ticks, "
                     "rest error %# m, chunks %# debris %# violations %#, server up %# B/s (peak %# B/s), client down %# B/s, destruction payload %# B/s, pose messages %#, "
                     "mismatch %# snapshots %# late-join %# ticks desync-recover %# ticks settle %# ticks",
                     pName, result._serverEventCount, result._lastServerChangeTick, result._convergedTick >= 0 ? result._convergedTick - result._lastServerChangeTick : -1, result._maxEventLagTicks, result._meanEventLagTicks,
                     computePercentile( result._listChunkError, 1.0f ), computePercentile( result._listChunkError, 0.99f ), result._listChunkError.size(), result._minRenderLagTicks,
                     result._maxRestError,
                     result._chunkCount, result._debrisCount, result._debrisViolationCount, result._serverUploadBytesPerSecond, result._peakServerUploadBytesPerSecond, result._clientDownloadBytesPerSecond,
                     result._destructionBytesPerSecond, result._poseMessageCount, result._hashMismatchCount, result._snapshotAppliedCount, result._lateJoinMatchTicks,
                     result._desyncRecoverTicks, result._settleTicks );
    }

    [[nodiscard]] inline bool loadShowcase( SceneDocument& outDocument )
    {
        return ResourceUtil::initialize() && outDocument.loadXML( "game/empty/maps/destructionshowcase.scene.xml" );
    }

    inline NetEmulationConditions makeConditions( float64 latency, float64 jitter, float32 loss, float32 duplicate, float32 reorder )
    {
        NetEmulationConditions conditions;
        conditions._latency       = latency;
        conditions._jitter        = jitter;
        conditions._lossRate      = loss;
        conditions._duplicateRate = duplicate;
        conditions._reorderRate   = reorder;
        return conditions;
    }

    /** @brief 모든 조건이 지키는 것 — 모두 같은 구조, 파편은 플레이어와 안 부딪힘, 덩어리는 서버를 따르고 멈추면 서버와 같은 자리. */
    inline void expectConverged( const ScenarioResult& result, float32 maxChunkP99 )
    {
        SW_ASSERT_TRUE( result._bValid );
        SW_EXPECT_TRUE( result._serverEventCount > 3 );
        SW_EXPECT_TRUE_MSG( result._convergedTick >= 0, "every client ends with the server's structure hash (waiting past the window until the server settles)" );
        SW_EXPECT_EQUAL( 0u, result._debrisViolationCount );
        SW_EXPECT_TRUE( result._debrisCount > 0 );
        SW_EXPECT_TRUE( result._chunkCount > 0 );
        SW_EXPECT_TRUE( result._maxRestError < 1.0e-4f );
        SW_EXPECT_TRUE( computePercentile( result._listChunkError, 0.99f ) < maxChunkP99 );
        // 덩어리는 자세 간격(쇼케이스 10 Hz = 6 틱)의 두 배 이상 과거로 그린다 — 하나뿐이면 자세 하나를 잃거나 늦을 때마다 뒤 자세가 없어 멈춰 선다
        // (Shipping 물리에서 손실 회선의 덩어리 오차 p99 가 1 m 를 넘었다).
        SW_EXPECT_TRUE_MSG( result._minRenderLagTicks >= 12.0f, "chunks are drawn at least two pose intervals in the past" );
    }
} // namespace test

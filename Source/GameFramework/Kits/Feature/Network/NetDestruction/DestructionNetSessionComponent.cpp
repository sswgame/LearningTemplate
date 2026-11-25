#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSessionComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/String/JSONStringUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Destruction/Fracture/FractureComponent.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Network/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Scene/SceneObjectHash.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Combat/ExplosiveBarrelComponent.h"

#include <algorithm>

namespace sw
{
    DestructionNetSessionComponent::DestructionNetSessionComponent()
        : _hashInterval{ 1.0f }
        , _interpolationDelay{ 0.1f }
        , _session{}
        , _hashSubscription{}
    {
        setCanEverTick( true );
    }

    void DestructionNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner   = getOwner();
        GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        const NetLinkValue* pLink    = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return; // 혼자 하는 게임 - 복제할 상대가 없다
        const NetLinkValue& link = *pLink;

        DestructionReplicationConfig settings;
        settings._hashInterval        = _hashInterval;
        settings._interpolationDelay  = _interpolationDelay;
        const DestructionNetSide side = link._role == NetLinkRole::Server ? DestructionNetSide::Server : DestructionNetSide::Client;
        _session.initialize( *link._pPeers, *link._pRouter, *pManager, side, settings );
        // 해시 이력(`-gv_stateHashHistory`)이 켜져 있으면 파괴 구조 해시를 `destruction` 줄로 같은 틱에 남긴다 — 씬 해시가 같아도 구조가 갈린 틱을 찾는다.
        StateHashHistory* pHistory       = game::findSubsystem<StateHashHistory>();
        const bool        bRecordsHashes = pHistory != nullptr && pHistory->isRecording() && _session.isActive();
        if ( bRecordsHashes )
            _hashSubscription = pHistory->registerChannel( "destruction", &_session );
    }

    void DestructionNetSessionComponent::onEndPlay()
    {
        _hashSubscription.reset();
        _session.shutdown();
        Component::onEndPlay();
    }

    void DestructionNetSessionComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _session.isActive() == false )
            return;
        // 복제는 남의 파괴 컴포넌트를 쓴다 — 틱 뒤에 돈다.
        context.deferPostTickOnComponent( *this, []( DestructionNetSessionComponent& component, GameObjectManager& manager )
        {
            (void)manager;
            component.updateSession();
        } );
    }

    void DestructionNetSessionComponent::updateSession()
    {
        if ( _session.isActive() == false )
            return;
        _session.update();
        reportReplicaStats();
    }

    void DestructionNetSessionComponent::reportReplicaStats()
    {
        // 복제 표(프로파일러의 `replicas=`)는 전역 세션의 것이다 - 이 월드의 링크가 전역 세션의 창구일 때만 올린다(가상 서버 하니스의 월드는 올리지 않는다).
        GameNetSession* pNetSession = game::findSubsystem<GameNetSession>();
        if ( pNetSession == nullptr )
            return;
        // 누적 셈을 틱마다 올린다(같은 이름은 덮는다). 서버는 보낸 양, 클라이언트는 받아 적용한 자세 · 사건 수가 갱신 수이다.
        const DestructionReplicationStats& stats       = _session.getStats();
        const uint64                       updateCount = stats._eventMessageCount + stats._poseMessageCount + stats._restPoseCount + stats._snapshotCount + stats._hashMessageCount;
        (void)pNetSession->setWorldReplicaStats( *this, "destruction", updateCount, stats._sentBytes ); // 다른 월드(하니스 · 에디터의 가상 서버)면 올리지 않는다
    }
} // namespace sw

namespace sw
{
    DestructionNetSession::DestructionNetSession()
        : _server{}
        , _client{}
        , _listEntry{}
        , _mapEntryIndex{}
        , _listRecord{}
        , _pManager{ nullptr }
        , _routerSubscription{}
        , _pWorldClock{ nullptr }
        , _serverTick{ 0 }
        , _seenSpawnedRevision{ 0 }
        , _side{ DestructionNetSide::Server }
    {
    }

    DestructionNetSession::~DestructionNetSession()
    {
        shutdown();
    }

    void DestructionNetSession::initialize( INetPeers& host, NetMessageRouter& router, GameObjectManager& manager, DestructionNetSide side,
                                            const DestructionReplicationConfig& settings )
    {
        shutdown();
        _side       = side;
        _serverTick = 0;
        _listEntry.clear();
        _mapEntryIndex.clear();
        _listRecord.clear();
        _pManager    = &manager;
        _pWorldClock = &router.getWorldClock();

        if ( side == DestructionNetSide::Client )
        {
            // 피해를 내는 기믹은 서버의 게임플레이다 — 클라이언트는 사건만 받는다.
            // 끄기가 등록을 바꿀 수 있어 먼저 모은다.
            vector<ExplosiveBarrelComponent*> listBarrel;
            for ( ExplosiveBarrelComponent* pBarrel : manager.getComponentRegistry().getAll<ExplosiveBarrelComponent>() )
            {
                listBarrel.push_back( pBarrel );
            }
            for ( ExplosiveBarrelComponent* pBarrel : listBarrel )
            {
                pBarrel->setActive( false );
            }
        }

        if ( side == DestructionNetSide::Server )
            _server.initialize( &host, &manager, settings );
        else
            _client.initialize( &host, &manager, router.getWorldClock(), settings );
        NetObjectTable& table = manager.getSubsystem<NetObjectTable>();
        table.captureSceneObjects( SceneIterationKey{} );
        _seenSpawnedRevision = table.getSpawnedRevision();
        registerNumberedObjects();
        INetMessageHandler* pHandler = side == DestructionNetSide::Server ? static_cast<INetMessageHandler*>( &_server ) : static_cast<INetMessageHandler*>( &_client );
        _routerSubscription          = router.registerHandler( pHandler ); // 앞의 `shutdown` 이 이전 등록을 풀었다 — 다시 시작해도 겹치지 않는다
        SW_LOG_INFO_ID( "DestructionNetSession.DestructionNetSessionStarted", "Destruction net session started as %# with %# objects", side == DestructionNetSide::Server ? "server" : "client", static_cast<uint32>( _listEntry.size() ) );
    }

    void DestructionNetSession::registerNumberedObjects()
    {
        vector<GameObject*> listObject;
        _pManager->getSubsystem<NetObjectTable>().collectObjects( listObject );
        const NetObjectTable& table = _pManager->getSubsystem<NetObjectTable>();
        // 등록된 번호는 멤버 색인(`_mapEntryIndex`)으로 가린다 — 오브젝트마다 항목 목록을 훑지 않는다. 항목 순서는 오브젝트를 만나는 순서 그대로다.
        _mapEntryIndex.reserve( _listEntry.size() + listObject.size() );
        for ( GameObject* pObject : listObject )
        {
            FractureComponent* pFracture = pObject->getComponent<FractureComponent>();
            if ( pFracture == nullptr )
                continue;
            const NetObjectID netID       = table.findID( *pObject );
            const bool        bRegistered = _mapEntryIndex.contains( netID );
            if ( netID == 0 || bRegistered )
                continue;
            _mapEntryIndex.emplace( netID, static_cast<uint32>( _listEntry.size() ) );
            Entry entry{};
            entry._pFracture = pFracture;
            entry._name      = string{ pObject->getName().c_str() };
            entry._netID     = netID;
            _listEntry.push_back( entry );
            if ( _side == DestructionNetSide::Server )
                _server.registerObject( netID, *pFracture );
            else
                _client.registerObject( netID, *pFracture );
        }
    }

    void DestructionNetSession::collectObjectIDs( vector<NetObjectID>& outListNetID ) const
    {
        outListNetID.clear();
        for ( const Entry& entry : _listEntry )
        {
            outListNetID.push_back( entry._netID );
        }
    }

    void DestructionNetSession::shutdown()
    {
        if ( _routerSubscription.isActive() == false )
            return;
        _routerSubscription.reset();
        if ( _side == DestructionNetSide::Client )
        {
            [[maybe_unused]] const DestructionReplicationStats& stats = _client.getStats();
            SW_LOG_INFO_ID( "DestructionNetSession.ClientHashChecksMatched", "[DestructionNetSession] client hash checks matched %# mismatched %# snapshots applied %#", stats._hashMatchCount, stats._hashMismatchCount,
                            stats._snapshotAppliedCount );
        }
    }

    void DestructionNetSession::update()
    {
        if ( _routerSubscription.isActive() == false )
            return;
        // 플레이 중에 번호를 받은 파괴 오브젝트(서버의 registerSpawned · 클라이언트의 bindSpawned)를 등록한다.
        const uint32 spawnedRevision = _pManager->getSubsystem<NetObjectTable>().getSpawnedRevision();
        if ( spawnedRevision != _seenSpawnedRevision )
        {
            _seenSpawnedRevision = spawnedRevision;
            registerNumberedObjects();
        }
        if ( _side == DestructionNetSide::Server )
        {
            _serverTick = _pWorldClock->getWholeTick();
            _server.update( _serverTick );
        }
        else
        {
            _client.update();
        }
        recordChangedHashes();
    }

    const DestructionReplicationStats& DestructionNetSession::getStats() const
    {
        return _side == DestructionNetSide::Server ? _server.getStats() : _client.getStats();
    }

    uint64 DestructionNetSession::computeStateHash() const
    {
        // 등록 순서가 아니라 번호 순으로 접는다 — 서버와 클라이언트가 번호를 받는 순서가 달라도 같은 값이다.
        vector<DestructionHashRecord> listEntryHash;
        listEntryHash.reserve( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            listEntryHash.push_back( DestructionHashRecord{ entry._pFracture->getState().computeStateHash(), entry._netID, 0 } );
        }
        VectorUtil::sort( listEntryHash, []( const DestructionHashRecord& left, const DestructionHashRecord& right )
        { return left._netID < right._netID; } );
        StateHashBuilder builder;
        for ( const DestructionHashRecord& record : listEntryHash )
        {
            builder.addBytes( &record._netID, sizeof( record._netID ) );
            builder.addBytes( &record._hash, sizeof( record._hash ) );
        }
        return builder.getHash();
    }

    bool DestructionNetSession::sampleStateHash( uint64 hostTick, StateHashSample& outSample ) const
    {
        if ( isActive() == false )
            return false;
        outSample = StateHashSample{ hostTick, computeStateHash() };
        return true;
    }

    string_view DestructionNetSession::findObjectName( NetObjectID netID ) const
    {
        const auto it = _mapEntryIndex.find( netID );
        return it != _mapEntryIndex.end() ? string_view{ _listEntry[it->second]._name } : string_view{};
    }

    string DestructionNetSession::formatHashLine( DestructionNetSide side, const DestructionHashRecord& record, string_view objectName )
    {
        constexpr int32                          kHashHexDigits = static_cast<int32>( sizeof( record._hash ) * 2 ); ///< 64 비트 해시의 16 진수 자리 수
        StringBuilder<constant::buffer::kMax256> text;
        string                                   escapedName;
        JSONStringUtil::appendEscaped( escapedName, objectName );
        text.appendFormat( "{\"side\":\"%#\",\"id\":%#,\"events\":%#,\"hash\":\"%#\",\"name\":\"%#\"}", side == DestructionNetSide::Server ? "server" : "client", record._netID, record._eventCount,
                           Fmt( record._hash, Format( kHashHexDigits, Format::Padding::Zero ).hex() ), escapedName );
        return string( text.c_str(), text.size() );
    }

    void DestructionNetSession::recordChangedHashes()
    {
        for ( Entry& entry : _listEntry )
        {
            const DestructionState& state      = entry._pFracture->getState();
            const uint32            eventCount = state.getEventCount();
            if ( eventCount == entry._loggedCount )
                continue;
            entry._loggedCount = eventCount;
            const DestructionHashRecord record{ state.computeStateHash(), entry._netID, eventCount };
            _listRecord.push_back( record );
            SW_LOG_INFO_ID( "DestructionNetSession.HashLine", "%#", formatHashLine( _side, record, entry._name ).c_str() );
        }
    }
} // namespace sw

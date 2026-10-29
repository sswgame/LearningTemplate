#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSession.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Destruction/Fracture/FractureComponent.h"
#include "Engine/Foundation/StateHashBuilder.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "DestructionNetSession" );

    DestructionNetSession::DestructionNetSession()
        : _server{}
        , _client{}
        , _listEntry{}
        , _mapEntryIndex{}
        , _listRecord{}
        , _pManager{ nullptr }
        , _routerSubscription{}
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
        _pManager = &manager;

        if ( side == DestructionNetSide::Client )
        {
            // 피해를 내는 기믹은 서버의 게임플레이다 — 클라이언트는 사건만 받는다.
            vector<GameObject*> listObject;
            manager.getAllGameObjects( listObject );
            for ( GameObject* pObject : listObject )
            {
                ExplosiveBarrelComponent* pBarrel = pObject->getComponent<ExplosiveBarrelComponent>();
                if ( pBarrel != nullptr )
                    pBarrel->setActive( false );
            }
        }

        if ( side == DestructionNetSide::Server )
            _server.initialize( &host, &manager, settings );
        else
            _client.initialize( &host, &manager, settings );
        NetObjectTable& table = manager.getSubsystem<NetObjectTable>();
        table.captureSceneObjects();
        _seenSpawnedRevision = table.getSpawnedRevision();
        registerNumberedObjects();
        INetMessageHandler* pHandler = side == DestructionNetSide::Server ? static_cast<INetMessageHandler*>( &_server ) : static_cast<INetMessageHandler*>( &_client );
        _routerSubscription          = router.registerHandler( pHandler ); // 앞의 `shutdown` 이 이전 등록을 풀었다 — 다시 시작해도 겹치지 않는다
        SW_LOG_INFO( "Destruction net session started as %# with %# objects", side == DestructionNetSide::Server ? "server" : "client", static_cast<uint32>( _listEntry.size() ) );
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
            const uint32 netID       = table.findID( *pObject );
            const bool   bRegistered = _mapEntryIndex.contains( netID );
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

    void DestructionNetSession::collectObjectIDs( vector<uint32>& outListNetID ) const
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
            SW_LOG_INFO( "[DestructionNetSession] client hash checks matched %# mismatched %# snapshots applied %#", stats._hashMatchCount, stats._hashMismatchCount,
                         stats._snapshotAppliedCount );
        }
    }

    void DestructionNetSession::update( float32 deltaTime )
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
            _server.update( _serverTick++ );
        else
            _client.update( deltaTime );
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
            builder.addUint32( record._netID );
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

    string_view DestructionNetSession::findObjectName( uint32 netID ) const
    {
        const auto it = _mapEntryIndex.find( netID );
        return it != _mapEntryIndex.end() ? string_view{ _listEntry[it->second]._name } : string_view{};
    }

    string DestructionNetSession::formatHashLine( DestructionNetSide side, const DestructionHashRecord& record, string_view objectName )
    {
        StringBuilder<constant::kMaxBuffer256> text;
        text.appendFormat( "[DestructionHash] side=%# id=%# events=%# hash=%# name=%#", side == DestructionNetSide::Server ? "server" : "client", record._netID, record._eventCount,
                           Fmt( record._hash, Format( 16, Format::Padding::Zero ).hex() ), objectName );
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
            SW_LOG_INFO( "%#", formatHashLine( _side, record, entry._name ).c_str() );
        }
    }
} // namespace sw

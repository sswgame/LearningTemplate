#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionNetSession.h"

#include "Core/Network/Message/NetMessage.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Destruction/FractureComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Gimmick/Genre/ShooterGimmicks.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "DestructionNetSession" );

    DestructionNetSession::DestructionNetSession()
        : _server{}
        , _client{}
        , _listFracture{}
        , _listName{}
        , _listLoggedCount{}
        , _listRecord{}
        , _pRouter{ nullptr }
        , _serverTick{ 0 }
        , _side{ DestructionNetSide::Server }
    {
    }

    DestructionNetSession::~DestructionNetSession()
    {
        shutdown();
    }

    void DestructionNetSession::initialize( NetHost& host, NetMessageRouter& router, GameObjectManager& manager, DestructionNetSide side,
                                            const DestructionReplicationConfig& settings )
    {
        shutdown();
        _side       = side;
        _serverTick = 0;
        _listFracture.clear();
        _listName.clear();
        _listRecord.clear();

        vector<GameObject*> listObject;
        manager.getAllGameObjects( listObject );
        // 이름순이 번호다 — 서버와 클라이언트가 같은 씬 문서를 읽으므로 같은 번호가 된다.
        std::sort( listObject.begin(), listObject.end(), []( const GameObject* pLhs, const GameObject* pRhs )
        { return pLhs->getName().lexicalLess( pRhs->getName() ); } );
        for ( GameObject* pObject : listObject )
        {
            // 피해를 내는 기믹은 서버의 게임플레이다 — 클라이언트는 사건만 받는다.
            ExplosiveBarrelComponent* pBarrel = pObject->getComponent<ExplosiveBarrelComponent>();
            if ( pBarrel != nullptr && side == DestructionNetSide::Client )
                pBarrel->setActive( false );
            FractureComponent* pFracture = pObject->getComponent<FractureComponent>();
            if ( pFracture == nullptr )
                continue;
            _listFracture.push_back( pFracture );
            _listName.push_back( string{ pObject->getName().c_str() } );
        }
        _listLoggedCount.assign( _listFracture.size(), 0u );

        if ( side == DestructionNetSide::Server )
            _server.initialize( &host, &manager, settings );
        else
            _client.initialize( &host, &manager, settings );
        for ( uint32 index = 0; index < static_cast<uint32>( _listFracture.size() ); ++index )
        {
            if ( side == DestructionNetSide::Server )
                _server.registerObject( index + 1, *_listFracture[index] );
            else
                _client.registerObject( index + 1, *_listFracture[index] );
        }
        INetMessageHandler* pHandler = side == DestructionNetSide::Server ? static_cast<INetMessageHandler*>( &_server ) : static_cast<INetMessageHandler*>( &_client );
        (void)router.registerHandler( pHandler ); // 이미 단 처리기여도 true 다 — 같은 라우터에 다시 시작하는 경우
        _pRouter = &router;
        SW_LOG_INFO( "Destruction net session started as %# with %# objects", side == DestructionNetSide::Server ? "server" : "client", static_cast<uint32>( _listFracture.size() ) );
    }

    void DestructionNetSession::shutdown()
    {
        if ( _pRouter == nullptr )
            return;
        if ( _side == DestructionNetSide::Server )
        {
            _pRouter->unregisterHandler( &_server );
        }
        else
        {
            _pRouter->unregisterHandler( &_client );
            [[maybe_unused]] const DestructionReplicationStats& stats = _client.getStats();
            SW_LOG_INFO( "[DestructionNetSession] client hash checks matched %# mismatched %# snapshots applied %#", stats._hashMatchCount, stats._hashMismatchCount,
                         stats._snapshotAppliedCount );
        }
        _pRouter = nullptr;
    }

    void DestructionNetSession::update( float32 deltaTime )
    {
        if ( _pRouter == nullptr )
            return;
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

    string_view DestructionNetSession::findObjectName( uint32 netID ) const
    {
        if ( netID == 0 || netID > _listName.size() )
            return {};
        return _listName[netID - 1];
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
        for ( uint32 index = 0; index < static_cast<uint32>( _listFracture.size() ); ++index )
        {
            const DestructionState& state      = _listFracture[index]->getState();
            const uint32            eventCount = state.getEventCount();
            if ( eventCount == _listLoggedCount[index] )
                continue;
            _listLoggedCount[index] = eventCount;
            const DestructionHashRecord record{ state.computeStateHash(), index + 1, eventCount };
            _listRecord.push_back( record );
            SW_LOG_INFO( "%#", formatHashLine( _side, record, _listName[index] ).c_str() );
        }
    }
} // namespace sw

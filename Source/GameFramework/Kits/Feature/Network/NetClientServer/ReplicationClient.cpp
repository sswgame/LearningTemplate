#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/Replication/InterpolationBuffer.h"

namespace sw
{
    ReplicationClient::ReplicationClient()
        : _listSnapshot{}
        , _decodeScratch{}
        , _inputWindow{}
        , _settings{}
        , _pHost{ nullptr }
        , _clock{}
        , _decodeFailureCount{ 0 }
        , _messageWriter{}
    {
    }

    void ReplicationClient::initialize( NetHost* pHost, const ReplicationClientSettings& settings )
    {
        _pHost    = pHost;
        _settings = settings;
        NetClockSettings clockSettings;
        clockSettings._tickInterval       = settings._tickInterval;
        clockSettings._interpolationDelay = settings._interpolationDelay;
        clockSettings._sampleInterval     = settings._tickInterval; // 스냅숏은 서버 틱마다 온다
        _clock.initialize( clockSettings );

        _listSnapshot.initialize( MathUtil::max( 4, settings._historySize ) );
        _inputWindow.initialize( NetClientServerMessage::kMaxInputCount, NetClientServerMessage::kInputFormat );
        _decodeFailureCount = 0;
        resetHistory();
    }

    void ReplicationClient::resetHistory()
    {
        _listSnapshot.reset(); // 스냅숏 버퍼는 자리에 남아 다음 연결이 다시 쓴다
        _inputWindow.reset();  // 새 서버 — 옛 확인은 이 연결의 것이 아니다
        _clock.reset();        // 새 서버의 틱은 옛 것보다 작을 수 있다 — 첫 스냅숏에서 다시 선다
    }

    void ReplicationClient::onConnectionOpened( int32 connectionID )
    {
        (void)connectionID;
        resetHistory();
    }

    const NetSnapshot* ReplicationClient::getLatest() const { return _listSnapshot.hasNewest() ? _listSnapshot.find( _listSnapshot.getNewestTick() ) : nullptr; }

    NetHandleResult ReplicationClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context; // 클라이언트 — 받는 쪽은 서버 하나
        // 기준 틱을 먼저 엿보고 그 스냅샷을 찾는다.
        BitReader    peek         = body;
        const uint32 tick         = static_cast<uint32>( peek.readVarUint() );
        const uint32 baselineCode = static_cast<uint32>( peek.readVarUint() );
        if ( peek.hasOverflowed() )
            return NetHandleResult::Malformed;
        if ( _listSnapshot.hasNewest() && tick <= _listSnapshot.getNewestTick() )
            return NetHandleResult::Handled; // 늦게 온 옛것
        const NetSnapshot* pBaseline = baselineCode != 0 ? _listSnapshot.find( baselineCode - 1u ) : nullptr;
        if ( NetSnapshot::readDelta( body, pBaseline, _decodeScratch ) == false )
        {
            ++_decodeFailureCount; // 기준을 이미 잃은 델타도 여기로 온다 — 형식은 맞으니 깨짐으로 세지 않는다
            return NetHandleResult::Handled;
        }
        _inputWindow.acknowledge( _decodeScratch._firstMissingInputTick ); // 서버가 빈틈없이 받은 다음 틱 — 그 앞은 다시 싣지 않는다
        std::swap( _listSnapshot.acquire( tick ), _decodeScratch );        // 가장 새 틱도 이것이 된다. 밀려난 옛 스냅숏은 다음 해독 자리가 된다
        _clock.observeServerTick( tick );                                  // 첫 스냅숏이면 렌더 틱을 바로 (그 틱 − 지연)에 둔다
        if ( _pHost != nullptr )
        {
            BitWriter& writer = _messageWriter.begin( NetClientServerMessage::kSnapshotAck );
            writer.writeVarUint( tick );
            (void)_pHost->sendMessage( 0, NetChannelType::UnreliableSequenced, writer.getBytes() );
        }
        return NetHandleResult::Handled;
    }

    void ReplicationClient::update( float32 deltaTime )
    {
        // 렌더 틱 = 서버 틱 추정 − 지연. 추정은 받은 스냅숏 틱의 하한 + 흐른 시간(받은 가장 새 틱 + 지연까지)이라 끊겨도 되돌아가지 않고 받은 틱을 지나치지 않는다.
        _clock.advance( deltaTime );
    }

    void ReplicationClient::findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, float32& outAlpha ) const
    {
        pOutFrom = nullptr;
        pOutTo   = nullptr;
        outAlpha = 0.0f;
        if ( _listSnapshot.hasNewest() == false )
            return;
        const float32 renderTick = getRenderTick();
        // 렌더 틱 이하의 가장 새 것과 그보다 큰 가장 오래된 것(고리가 들 수 있는 틱 안에서).
        const uint32 oldestTick = _listSnapshot.computeOldestTick();
        for ( uint32 tick = _listSnapshot.getNewestTick() + 1u; tick-- > oldestTick; )
        {
            const NetSnapshot* pSnapshot = _listSnapshot.find( tick );
            if ( pSnapshot == nullptr )
                continue;
            if ( static_cast<float32>( tick ) <= renderTick )
            {
                pOutFrom = pSnapshot;
                break;
            }
            pOutTo = pSnapshot;
        }
        if ( pOutFrom == nullptr )
        {
            pOutFrom = pOutTo; // 렌더 시각이 가진 것보다 앞이다 — 가장 오래된 것
            return;
        }
        if ( pOutTo == nullptr )
        {
            pOutTo = pOutFrom; // 새 스냅샷이 늦는다 — 마지막 것에 멈춘다(외삽하지 않는다)
            return;
        }
        outAlpha = NetInterpolationUtil::computeAlpha( pOutFrom->_tick, pOutTo->_tick, renderTick );
    }

    bool ReplicationClient::sampleEntity( uint32 entityID, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, float32& outAlpha ) const
    {
        const NetSnapshot* pFrom = nullptr;
        const NetSnapshot* pTo   = nullptr;
        findBracket( pFrom, pTo, outAlpha );
        pOutFrom = pFrom != nullptr ? pFrom->findEntity( entityID ) : nullptr;
        pOutTo   = pTo != nullptr ? pTo->findEntity( entityID ) : nullptr;
        if ( pOutFrom == nullptr && pOutTo == nullptr )
            return false;
        if ( pOutFrom == nullptr )
        {
            pOutFrom = pOutTo; // 새로 생겼다 — 다음 것부터
            outAlpha = 1.0f;
        }
        if ( pOutTo == nullptr )
        {
            pOutTo   = pOutFrom; // 사라지는 중 — 마지막 모습
            outAlpha = 0.0f;
        }
        return true;
    }

    void ReplicationClient::collectVisibleEntities( vector<uint32>& outListEntity ) const
    {
        outListEntity.clear();
        const NetSnapshot* pFrom = nullptr;
        const NetSnapshot* pTo   = nullptr;
        float32            alpha = 0.0f;
        findBracket( pFrom, pTo, alpha );
        if ( pFrom == nullptr )
            return;
        for ( const NetEntityState& entity : pFrom->_listEntity )
        {
            outListEntity.push_back( entity._entityID );
        }
    }

    bool ReplicationClient::sendInput( uint32 tick, const vector<uint8>& listInput )
    {
        if ( static_cast<int32>( listInput.size() ) > NetClientServerMessage::kMaxInputBytes )
        {
            SW_LOG_WARNING( "ReplicationClient: input for tick %# has %# bytes, more than the limit %# - not sent", tick, static_cast<int32>( listInput.size() ),
                            NetClientServerMessage::kMaxInputBytes );
            return false;
        }
        // 틱이 건너뛰면 그 틱부터, 줄면(새 판) 확인까지 비우고 다시 쌓는다. 같은 틱이면 처음 값이 남고 지금 창만 다시 보낸다.
        (void)_inputWindow.push( tick, listInput.data(), static_cast<int32>( listInput.size() ) );
        if ( _pHost == nullptr )
            return true;
        BitWriter& writer = _messageWriter.begin( NetClientServerMessage::kInput );
        writer.writeVarUint( static_cast<uint64>( MathUtil::max( 0.0f, getRenderTick() ) * 256.0f ) );
        // 서버가 확인한 다음 틱부터, 메시지 상한 안에서 오래된 것부터 — 못 실은 새 것은 확인이 오른 뒤 다음 메시지가 싣는다.
        NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
        budget.reserveBits( writer.getBitCount() );
        (void)_inputWindow.write( writer, budget ); // 실은 수는 쓰지 않는다 — 못 실은 것은 다음 메시지가 싣는다
        (void)_pHost->sendMessage( 0, NetChannelType::Unreliable, writer.getBytes() );
        return true;
    }
} // namespace sw

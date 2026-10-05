#include "pch.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetSendBudget.h"

namespace sw
{
    ReplicationClient::ReplicationClient()
        : _listSnapshot{}
        , _listRecentInput{}
        , _settings{}
        , _pHost{ nullptr }
        , _renderTime{ 0.0f }
        , _decodeFailureCount{ 0 }
        , _latestTick{ 0 }
        , _latestInputTick{ 0 }
        , _bHasSnapshot{ SW_FALSE }
        , _messageWriter{}
    {
    }

    void ReplicationClient::initialize( NetHost* pHost, const ReplicationClientSettings& settings )
    {
        _pHost    = pHost;
        _settings = settings;
        _listSnapshot.assign( static_cast<size_t>( MathUtil::max( 4, settings._historySize ) ), NetSnapshot{} );
        _decodeFailureCount = 0;
        resetHistory();
    }

    void ReplicationClient::resetHistory()
    {
        for ( NetSnapshot& snapshot : _listSnapshot )
            snapshot._tick = 0xFFFFFFFFu;
        _listRecentInput.clear();
        _renderTime   = 0.0f;
        _latestTick   = 0;
        _bHasSnapshot = SW_FALSE;
    }

    void ReplicationClient::onConnectionOpened( int32 connectionId )
    {
        (void)connectionId;
        resetHistory();
    }

    const NetSnapshot* ReplicationClient::findSnapshot( uint32 tick ) const
    {
        const NetSnapshot& snapshot = _listSnapshot[static_cast<size_t>( tick % _listSnapshot.size() )];
        return snapshot._tick == tick ? &snapshot : nullptr;
    }

    const NetSnapshot* ReplicationClient::getLatest() const { return _bHasSnapshot ? findSnapshot( _latestTick ) : nullptr; }

    NetHandleResult ReplicationClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context; // 클라이언트 — 받는 쪽은 서버 하나
        // 기준 틱을 먼저 엿보고 그 스냅샷을 찾는다.
        BitReader    peek         = body;
        const uint32 tick         = static_cast<uint32>( peek.readVarUint() );
        const uint32 baselineCode = static_cast<uint32>( peek.readVarUint() );
        if ( peek.hasOverflowed() )
            return NetHandleResult::Malformed;
        if ( _bHasSnapshot && tick <= _latestTick )
            return NetHandleResult::Handled; // 늦게 온 옛것
        const NetSnapshot* pBaseline = baselineCode != 0 ? findSnapshot( baselineCode - 1u ) : nullptr;
        NetSnapshot        snapshot;
        if ( NetSnapshot::readDelta( body, pBaseline, snapshot ) == false )
        {
            ++_decodeFailureCount; // 기준을 이미 잃은 델타도 여기로 온다 — 형식은 맞으니 깨짐으로 세지 않는다
            return NetHandleResult::Handled;
        }
        const bool bFirst                                                 = _bHasSnapshot == SW_FALSE;
        _listSnapshot[static_cast<size_t>( tick % _listSnapshot.size() )] = std::move( snapshot );
        _latestTick                                                       = tick;
        _bHasSnapshot                                                     = SW_TRUE;
        if ( bFirst )
            _renderTime = static_cast<float32>( tick ) * _settings._tickInterval - _settings._interpolationDelay;
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
        if ( _bHasSnapshot == SW_FALSE || deltaTime <= 0.0f )
            return;
        // 목표 = 가장 새 스냅샷 시각 − 지연. 벗어난 만큼 조금 빠르게 · 느리게 흘려 맞춘다(튀지 않게). 크게 벗어나면 바로 맞춘다.
        const float32 target = static_cast<float32>( _latestTick ) * _settings._tickInterval - _settings._interpolationDelay;
        const float32 error  = target - ( _renderTime + deltaTime );
        if ( MathUtil::abs( error ) > _settings._interpolationDelay * 4.0f )
        {
            _renderTime = target;
            return;
        }
        const float32 scale = 1.0f + MathUtil::clamp( error / MathUtil::max( 1.0e-3f, _settings._interpolationDelay ), -1.0f, 1.0f ) * _settings._clockCorrection;
        _renderTime += deltaTime * scale;
    }

    void ReplicationClient::findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, float32& outAlpha ) const
    {
        pOutFrom = nullptr;
        pOutTo   = nullptr;
        outAlpha = 0.0f;
        if ( _bHasSnapshot == SW_FALSE )
            return;
        const float32 renderTick = getRenderTick();
        // 렌더 틱 이하의 가장 새 것과 그보다 큰 가장 오래된 것.
        const uint32 oldestTick = _latestTick >= _listSnapshot.size() ? _latestTick - static_cast<uint32>( _listSnapshot.size() ) + 1u : 0u;
        for ( uint32 tick = _latestTick + 1u; tick-- > oldestTick; )
        {
            const NetSnapshot* pSnapshot = findSnapshot( tick );
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
        const float32 span = static_cast<float32>( pOutTo->_tick - pOutFrom->_tick );
        outAlpha           = span > 0.0f ? MathUtil::saturate( ( renderTick - static_cast<float32>( pOutFrom->_tick ) ) / span ) : 0.0f;
    }

    bool ReplicationClient::sampleEntity( uint32 entityId, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, float32& outAlpha ) const
    {
        const NetSnapshot* pFrom = nullptr;
        const NetSnapshot* pTo   = nullptr;
        findBracket( pFrom, pTo, outAlpha );
        pOutFrom = pFrom != nullptr ? pFrom->findEntity( entityId ) : nullptr;
        pOutTo   = pTo != nullptr ? pTo->findEntity( entityId ) : nullptr;
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
            outListEntity.push_back( entity._entityId );
    }

    bool ReplicationClient::sendInput( uint32 tick, const vector<uint8>& listInput )
    {
        if ( static_cast<int32>( listInput.size() ) > NetClientServerMessage::kMaxInputBytes )
        {
            SW_LOG_WARNING( "ReplicationClient: input for tick %# has %# bytes, more than the limit %# - not sent", tick, static_cast<int32>( listInput.size() ),
                            NetClientServerMessage::kMaxInputBytes );
            return false;
        }
        if ( _listRecentInput.empty() == false && tick != _latestInputTick + 1u )
            _listRecentInput.clear(); // 틱이 끊겼다 — 겹쳐 실을 수 없다
        _listRecentInput.push_front( listInput );
        const int32 redundancy = MathUtil::clamp( _settings._inputRedundancy, 1, NetClientServerMessage::kMaxRedundantInputCount );
        while ( static_cast<int32>( _listRecentInput.size() ) > redundancy )
            _listRecentInput.pop_back();
        _latestInputTick = tick;
        if ( _pHost == nullptr )
            return true;
        BitWriter& writer = _messageWriter.begin( NetClientServerMessage::kInput );
        writer.writeVarUint( tick );
        writer.writeVarUint( static_cast<uint64>( MathUtil::max( 0.0f, getRenderTick() ) * 256.0f ) );
        // 새 것부터, 메시지 상한 안에 들어가는 만큼만 겹쳐 싣는다(이번 틱의 입력은 늘 들어간다 — 상한 255 바이트).
        NetSendBudget budget( NetConnection::kMaxMessageSize );
        budget.reserveBits( writer.getBitCount() + BitMath::computeVarUintBits( _listRecentInput.size() ) );
        size_t count = 0;
        while ( count < _listRecentInput.size() && budget.tryReserveBits( BitMath::computeBlobBits( static_cast<int32>( _listRecentInput[count].size() ) ) ) )
            ++count;
        writer.writeVarUint( count );
        for ( size_t index = 0; index < count; ++index )
            writer.writeBlob( _listRecentInput[index].data(), static_cast<int32>( _listRecentInput[index].size() ) );
        (void)_pHost->sendMessage( 0, NetChannelType::Unreliable, writer.getBytes() );
        return true;
    }
} // namespace sw

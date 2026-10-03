#include "pch.h"

#include "GameFramework/Kits/NetClientServer/ReplicationClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"

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
    {
    }

    void ReplicationClient::initialize( NetHost* pHost, const ReplicationClientSettings& settings )
    {
        _pHost    = pHost;
        _settings = settings;
        _listSnapshot.assign( static_cast<size_t>( MathUtil::max( 4, settings._historySize ) ), NetSnapshot{} );
        for ( NetSnapshot& snapshot : _listSnapshot )
            snapshot._tick = 0xFFFFFFFFu;
        _listRecentInput.clear();
        _renderTime         = 0.0f;
        _decodeFailureCount = 0;
        _latestTick         = 0;
        _bHasSnapshot       = SW_FALSE;
    }

    const NetSnapshot* ReplicationClient::findSnapshot( uint32 tick ) const
    {
        const NetSnapshot& snapshot = _listSnapshot[static_cast<size_t>( tick % _listSnapshot.size() )];
        return snapshot._tick == tick ? &snapshot : nullptr;
    }

    const NetSnapshot* ReplicationClient::getLatest() const { return _bHasSnapshot ? findSnapshot( _latestTick ) : nullptr; }

    bool ReplicationClient::handleMessage( const vector<uint8>& buffer )
    {
        if ( buffer.empty() || NetMessageRange::isInRange( buffer[0], NetMessageRange::kClientServer ) == false )
            return false;
        if ( buffer[0] != NetClientServerMessage::kSnapshot )
            return true;
        // 기준 틱을 먼저 엿보고 그 스냅샷을 찾는다.
        BitReader    peek( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        const uint32 tick         = static_cast<uint32>( peek.readVarUint() );
        const uint32 baselineCode = static_cast<uint32>( peek.readVarUint() );
        if ( _bHasSnapshot && tick <= _latestTick )
            return true; // 늦게 온 옛것
        const NetSnapshot* pBaseline = baselineCode != 0 ? findSnapshot( baselineCode - 1u ) : nullptr;
        BitReader          reader( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        NetSnapshot        snapshot;
        if ( NetSnapshot::readDelta( reader, pBaseline, snapshot ) == false )
        {
            ++_decodeFailureCount;
            return true;
        }
        const bool bFirst                                                 = _bHasSnapshot == SW_FALSE;
        _listSnapshot[static_cast<size_t>( tick % _listSnapshot.size() )] = std::move( snapshot );
        _latestTick                                                       = tick;
        _bHasSnapshot                                                     = SW_TRUE;
        if ( bFirst )
            _renderTime = static_cast<float32>( tick ) * _settings._tickInterval - _settings._interpolationDelay;
        if ( _pHost != nullptr )
        {
            BitWriter writer;
            writer.writeBits( NetClientServerMessage::kSnapshotAck, 8 );
            writer.writeVarUint( tick );
            (void)_pHost->sendMessage( 0, NetChannelType::UnreliableSequenced, writer.getBytes() );
        }
        return true;
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

    void ReplicationClient::sendInput( uint32 tick, const vector<uint8>& listInput )
    {
        if ( _listRecentInput.empty() == false && tick != _latestInputTick + 1u )
            _listRecentInput.clear(); // 틱이 끊겼다 — 겹쳐 실을 수 없다
        _listRecentInput.push_front( listInput );
        while ( static_cast<int32>( _listRecentInput.size() ) > MathUtil::max( 1, _settings._inputRedundancy ) )
            _listRecentInput.pop_back();
        _latestInputTick = tick;
        if ( _pHost == nullptr )
            return;
        BitWriter writer;
        writer.writeBits( NetClientServerMessage::kInput, 8 );
        writer.writeVarUint( tick );
        writer.writeVarUint( static_cast<uint64>( MathUtil::max( 0.0f, getRenderTick() ) * 256.0f ) );
        writer.writeVarUint( _listRecentInput.size() );
        for ( const vector<uint8>& input : _listRecentInput )
        {
            writer.writeVarUint( input.size() );
            if ( input.empty() == false )
                writer.writeBytes( input.data(), static_cast<int32>( input.size() ) );
        }
        (void)_pHost->sendMessage( 0, NetChannelType::Unreliable, writer.getBytes() );
    }
} // namespace sw

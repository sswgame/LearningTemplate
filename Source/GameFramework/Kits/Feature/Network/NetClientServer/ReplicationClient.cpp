#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/Replication/InterpolationBuffer.h"
#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

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

    bool ReplicationClient::initialize( INetPeers* pHost, const ReplicationClientConfig& settings )
    {
        // 실패하면 호스트 없는 클라이언트로 남기고 지난 설정을 그대로 둔다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "ReplicationClient: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        _pHost    = pHost;
        _settings = settings;
        NetClockConfig clockSettings;
        clockSettings._tickInterval       = settings._tickInterval;
        clockSettings._interpolationDelay = settings._interpolationDelay;
        clockSettings._sampleInterval     = settings._tickInterval; // 스냅샷은 서버 틱마다 온다
        _clock.initialize( clockSettings );

        _listSnapshot.initialize( settings._historySize ); // 범위(4 이상)는 위에서 봤다
        _inputWindow.initialize( NetClientServerMessage::kMaxInputCount, NetClientServerMessage::kInputFormat );
        _decodeFailureCount = 0;
        resetHistory();
        return true;
    }

    void ReplicationClient::resetHistory()
    {
        _listSnapshot.reset(); // 스냅샷 버퍼는 그대로 남아 다음 연결이 재사용한다
        _inputWindow.reset();  // 새 서버이므로 이전 확인은 이 연결의 것이 아니다
        _clock.reset();        // 새 서버의 틱은 이전 서버보다 작을 수 있으므로 첫 스냅샷에서 다시 맞춘다
    }

    void ReplicationClient::onConnectionOpened( int32 connectionID )
    {
        (void)connectionID;
        resetHistory();
    }

    const NetSnapshot* ReplicationClient::getLatest() const
    {
        return _listSnapshot.hasNewest() ? _listSnapshot.find( _listSnapshot.getNewestTick() ) : nullptr;
    }

    const NetOwnerState* ReplicationClient::findOwnerState() const
    {
        const NetSnapshot* pLatest = getLatest();
        return pLatest != nullptr && pLatest->_ownerState._bPresent == SW_TRUE ? &pLatest->_ownerState : nullptr;
    }

    std::optional<int64> ReplicationClient::findServerTickOffset() const
    {
        const NetOwnerState* pOwner = findOwnerState();
        if ( pOwner == nullptr )
            return std::nullopt;
        return static_cast<int64>( getLatest()->_tick ) - static_cast<int64>( pOwner->_inputTick );
    }

    NetHandleResult ReplicationClient::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context; // 클라이언트가 받는 쪽이므로 상대는 서버 하나뿐이다
        // 기준 틱을 먼저 읽어 보고 그 스냅샷을 찾는다.
        BitReader    peek         = body;
        const uint32 tick         = peek.readVarUint32();
        const uint32 baselineCode = peek.readVarUint32();
        if ( peek.hasOverflowed() )
            return NetHandleResult::Malformed;
        if ( _listSnapshot.hasNewest() && tick <= _listSnapshot.getNewestTick() )
            return NetHandleResult::Handled; // 늦게 도착한 오래된 스냅샷이다
        const NetSnapshot* pBaseline = baselineCode != 0 ? _listSnapshot.find( baselineCode - 1u ) : nullptr;
        if ( NetSnapshot::readDelta( body, pBaseline, _decodeScratch ) == false )
        {
            ++_decodeFailureCount; // 기준을 이미 잃은 델타도 여기로 온다. 형식은 맞으므로 손상된 메시지로 세지 않는다
            return NetHandleResult::Handled;
        }
        _inputWindow.acknowledge( _decodeScratch._firstMissingInputTick ); // 서버가 빈틈없이 받은 다음 틱이다. 그 이전 입력은 다시 싣지 않는다
        std::swap( _listSnapshot.acquire( tick ), _decodeScratch );        // 가장 새 틱도 이 스냅샷이 된다. 밀려난 이전 스냅샷의 버퍼는 다음 복원에 재사용한다
        _clock.observeServerTick( tick );                                  // 첫 스냅샷이면 렌더 틱을 바로 (그 틱 − 지연)에 둔다
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
        // 렌더 틱은 서버 틱 추정값에서 지연을 뺀 값이다.
        // 추정값은 받은 스냅샷 틱의 하한에 흐른 시간을 더한 것이고 받은 가장 새 틱 + 지연까지만 간다. 그래서 통신이 끊겨도 되돌아가지 않고 받은 틱을 지나치지도 않는다.
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
        // 렌더 틱 이하의 가장 새 스냅샷과, 렌더 틱보다 큰 가장 오래된 스냅샷을 찾는다. 링 버퍼가 보관하는 틱 범위 안에서만 찾는다.
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
            pOutFrom = pOutTo; // 렌더 시각이 가진 스냅샷보다 앞서 있으므로 가장 오래된 것을 쓴다
            return;
        }
        if ( pOutTo == nullptr )
        {
            pOutTo = pOutFrom; // 새 스냅샷이 늦으므로 마지막 것에서 멈춘다. 외삽하지 않는다
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
            pOutFrom = pOutTo; // 새로 생긴 엔티티이므로 다음 스냅샷부터 쓴다
            outAlpha = 1.0f;
        }
        if ( pOutTo == nullptr )
        {
            pOutTo   = pOutFrom; // 사라지는 중이므로 마지막 모습을 쓴다
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
        // 틱이 건너뛰면 그 틱부터 다시 쌓고, 틱이 줄면(새 게임 판) 확인까지 비우고 다시 쌓는다. 같은 틱이면 처음 값이 남고 지금 창만 다시 보낸다.
        // 스탬프는 이 입력을 만든 순간 보던 틱이다 — 다시 보낼 때도 이 값이 같이 가서 서버가 옛 입력을 그 입력의 시점으로 랙 보정한다.
        const uint32 viewTickStamp = static_cast<uint32>( MathUtil::max( 0.0f, getRenderTick() ) * static_cast<float32>( NetClientServerMessage::kViewTickStampScale ) );
        (void)_inputWindow.push( tick, listInput.data(), static_cast<int32>( listInput.size() ), viewTickStamp );
        if ( _pHost == nullptr )
            return true;
        BitWriter& writer = _messageWriter.begin( NetClientServerMessage::kInput );
        // 서버가 확인한 다음 틱부터 메시지 상한 안에서 오래된 입력부터 싣는다. 못 실은 새 입력은 확인이 올라간 뒤 다음 메시지가 싣는다.
        NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
        budget.reserveBits( writer.getBitCount() );
        (void)_inputWindow.write( writer, budget ); // 실은 수는 쓰지 않는다. 못 실은 입력은 다음 메시지가 싣는다
        (void)_pHost->sendMessage( 0, NetChannelType::Unreliable, writer.getBytes() );
        return true;
    }

    bool ReplicationClient::sendIntent( uint32 tick, const ControlIntent& intent )
    {
        BitWriter intentWriter;
        if ( intent.write( intentWriter ) == false )
        {
            SW_LOG_ERROR( "ReplicationClient: control intent for tick %# could not be written - not sent", tick );
            return false;
        }
        const uint8*        pBytes = intentWriter.getBytes().data();
        const vector<uint8> listInput( pBytes, pBytes + intentWriter.getByteCount() );
        return sendInput( tick, listInput );
    }
} // namespace sw

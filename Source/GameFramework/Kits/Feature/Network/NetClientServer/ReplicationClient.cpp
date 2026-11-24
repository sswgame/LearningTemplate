#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationClient.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetSendBudget.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    ReplicationClient::ReplicationClient()
        : _listSnapshot{}
        , _decodeScratch{}
        , _inputWindow{}
        , _settings{}
        , _pHost{ nullptr }
        , _pWorldClock{ nullptr }
        , _renderClock{}
        , _stride{ 1 }
        , _decodeFailureCount{}
        , _messageWriter{}
    {
    }

    bool ReplicationClient::initialize( INetPeers* pHost, NetClock& worldClock, const ReplicationClientConfig& settings )
    {
        // 실패하면 호스트 없는 클라이언트로 남기고 지난 설정을 그대로 둔다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "ReplicationClient.ReplicationClientRefusedStartSettings", "ReplicationClient: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        const optional<uint32> stride = NetClock::computeStride( settings._tickInterval );
        if ( stride.has_value() == false )
        {
            SW_LOG_ERROR_ID( "ReplicationClient.ReplicationClientTickInterval", "ReplicationClient: refused to start - tick interval %# s is not a multiple of the server tick (%# s)", settings._tickInterval, NetClock::kTickSeconds );
            return false;
        }
        _pHost       = pHost;
        _settings    = settings;
        _pWorldClock = &worldClock;
        _stride      = *stride;
        NetRenderClockConfig clockSettings;
        clockSettings._stride             = _stride;
        clockSettings._interpolationDelay = settings._interpolationDelay;
        clockSettings._sampleInterval     = settings._tickInterval; // 스냅샷은 그 틱마다 온다
        _renderClock.initialize( clockSettings );

        _listSnapshot.initialize( settings._historySize ); // 범위(4 이상)는 위에서 봤다
        _inputWindow.initialize( NetClientServerMessage::kMaxInputCount, NetClientServerMessage::kInputFormat );
        _decodeFailureCount.reset();
        resetHistory();
        return true;
    }

    void ReplicationClient::resetHistory()
    {
        _listSnapshot.reset(); // 스냅샷 버퍼는 그대로 남아 다음 연결이 재사용한다
        _inputWindow.reset();  // 새 서버이므로 이전 확인은 이 연결의 것이 아니다
        _renderClock.reset();  // 새 서버의 틱은 이전 서버보다 작을 수 있으므로 첫 스냅샷에서 다시 맞춘다(월드의 시계는 라우터가 되돌린다)
    }

    void ReplicationClient::onWorldOpened( uint32 generation )
    {
        (void)generation;
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

    NetHandleResult ReplicationClient::receive( NetClientServerMessage::Snapshot, const NetMessageContext& context, BitReader& body )
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
            if ( body.hasOverflowed() )
                return NetHandleResult::Malformed; // 잘림 · 상한 넘는 길이 — 깨진 메시지다
            _decodeFailureCount.add();             // 기준을 이미 잃은 델타도 여기로 온다. 형식은 맞으므로 손상된 메시지로 세지 않는다
            return NetHandleResult::Handled;
        }
        if ( body.readEnd() == false )
            return NetHandleResult::Malformed;                             // 적용 전에 본다 — 남는 바이트가 있는 스냅샷은 복원하지 않는다
        _inputWindow.acknowledge( _decodeScratch._firstMissingInputTick ); // 서버가 빈틈없이 받은 다음 틱이다. 그 이전 입력은 다시 싣지 않는다
        std::swap( _listSnapshot.acquire( tick ), _decodeScratch );        // 가장 새 틱도 이 스냅샷이 된다. 밀려난 이전 스냅샷의 버퍼는 다음 복원에 재사용한다
        _pWorldClock->observe( tick * _stride );
        _renderClock.observeSample( tick );
        NetSnapshotAckBody ack;
        ack._tick = tick;
        if ( _pHost != nullptr && _messageWriter.writeMessage<NetClientServerMessage::Table, NetClientServerMessage::SnapshotAck>( ack ) )
            (void)_messageWriter.sendMessage( *_pHost, 0 ); // 서술자의 채널(순서 있는 비신뢰 — 옛 확인은 새 확인이 덮는다)
        return NetHandleResult::Handled;
    }

    void ReplicationClient::onPollFinished()
    {
        if ( _pWorldClock != nullptr )
            _renderClock.follow( *_pWorldClock );
    }

    void ReplicationClient::findBracket( const NetSnapshot*& pOutFrom, const NetSnapshot*& pOutTo, NetSampleRatio& outRatio ) const
    {
        pOutFrom = nullptr;
        pOutTo   = nullptr;
        outRatio = NetSampleRatio::makeHeld();
        if ( _listSnapshot.hasNewest() == false )
            return;
        const float64 renderTick = getRenderTick();
        // 렌더 틱 이하의 가장 새 스냅샷과, 렌더 틱보다 큰 가장 오래된 스냅샷을 찾는다. 링 버퍼가 보관하는 틱 범위 안에서만 찾는다.
        const uint32 oldestTick = _listSnapshot.computeOldestTick();
        for ( uint32 tick = _listSnapshot.getNewestTick() + 1u; tick-- > oldestTick; )
        {
            const NetSnapshot* pSnapshot = _listSnapshot.find( tick );
            if ( pSnapshot == nullptr )
                continue;
            if ( static_cast<float64>( tick ) <= renderTick )
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
        outRatio = NetSampleRatio::make( pOutFrom->_tick, pOutTo->_tick, renderTick );
    }

    bool ReplicationClient::findSample( uint32 entityID, uint32 typeID, const NetEntityState*& pOutFrom, const NetEntityState*& pOutTo, NetSampleRatio& outRatio ) const
    {
        const NetSnapshot* pFrom = nullptr;
        const NetSnapshot* pTo   = nullptr;
        findBracket( pFrom, pTo, outRatio );
        pOutFrom = pFrom != nullptr ? pFrom->findEntity( entityID ) : nullptr;
        pOutTo   = pTo != nullptr ? pTo->findEntity( entityID ) : nullptr;
        // 같은 번호를 다른 종류가 다시 쓰면 앞의 것이 사라지고 새것이 생긴 것이다.
        if ( pOutFrom != nullptr && pOutFrom->_typeID != typeID )
            pOutFrom = nullptr;
        if ( pOutTo != nullptr && pOutTo->_typeID != typeID )
            pOutTo = nullptr;
        if ( outRatio.isPresent( pOutFrom != nullptr, pOutTo != nullptr ) == false )
            return false;
        if ( pOutFrom != nullptr && pOutTo != nullptr )
            return true;
        // 사라지는 중이거나 막 생겼다 — 있는 쪽 하나 그대로다.
        pOutFrom = pOutFrom != nullptr ? pOutFrom : pOutTo;
        pOutTo   = pOutFrom;
        outRatio = NetSampleRatio::makeHeld();
        return true;
    }

    void ReplicationClient::collectVisibleEntities( vector<uint32>& outListEntity ) const
    {
        outListEntity.clear();
        const NetSnapshot* pFrom = nullptr;
        const NetSnapshot* pTo   = nullptr;
        NetSampleRatio     ratio = NetSampleRatio::makeHeld();
        findBracket( pFrom, pTo, ratio );
        if ( pFrom == nullptr )
            return;
        for ( const NetEntityState& entity : pFrom->_listEntity )
        {
            outListEntity.push_back( entity._entityID );
        }
    }

    bool ReplicationClient::sendInput( uint32 tick, span<const uint8> listInput )
    {
        if ( static_cast<int32>( listInput.size() ) > NetClientServerMessage::kMaxInputBytes )
        {
            SW_LOG_WARNING_ID( "ReplicationClient.ReplicationClientInputTickBytes", "ReplicationClient: input for tick %# has %# bytes, more than the limit %# - not sent", tick, static_cast<int32>( listInput.size() ),
                               NetClientServerMessage::kMaxInputBytes );
            return false;
        }
        // 틱이 건너뛰면 그 틱부터 다시 쌓고, 틱이 줄면(새 게임 판) 확인까지 비우고 다시 쌓는다. 같은 틱이면 처음 값이 남고 지금 창만 다시 보낸다.
        (void)_inputWindow.push( tick, listInput.data(), static_cast<int32>( listInput.size() ) );
        if ( _pHost == nullptr )
            return true;
        BitWriter& writer = _messageWriter.beginMessage<NetClientServerMessage::Table, NetClientServerMessage::Input>();
        // 서버가 확인한 다음 틱부터 메시지 상한 안에서 오래된 입력부터 싣는다. 못 실은 새 입력은 확인이 올라간 뒤 다음 메시지가 싣는다.
        NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
        budget.reserveBits( writer.getBitCount() );
        (void)_inputWindow.write( writer, budget ); // 실은 수는 쓰지 않는다. 못 실은 입력은 다음 메시지가 싣는다
        (void)_messageWriter.sendMessage( *_pHost, 0 );
        return true;
    }

    bool ReplicationClient::sendIntent( uint32 tick, const ControlIntent& intent, vector_reference<const MeleeHitClaim> listClaim, uint32 pawnEntityID )
    {
        if ( static_cast<int32>( listClaim.size() ) > NetClientServerMessage::kMaxClaimsPerIntent )
        {
            SW_LOG_ERROR_ID( "ReplicationClient.ReplicationClientTooManyClaims", "ReplicationClient: %# melee claims for tick %#, more than the limit %# - not sent", static_cast<int32>( listClaim.size() ), tick,
                             NetClientServerMessage::kMaxClaimsPerIntent );
            return false;
        }
        BitWriter intentWriter;
        if ( intent.write( intentWriter ) == false )
        {
            SW_LOG_ERROR_ID( "ReplicationClient.ReplicationClientControlIntentTick", "ReplicationClient: control intent for tick %# could not be written - not sent", tick );
            return false;
        }
        intentWriter.writeVarUint( listClaim.size() );
        for ( const MeleeHitClaim& claim : listClaim )
        {
            claim.write( intentWriter );
        }
        intentWriter.writeVarUint( pawnEntityID );
        // 쓴 바이트를 그대로 넘긴다 — 틱마다 사본 벡터를 만들지 않는다(입력 창이 제 저장소로 베낀다).
        return sendInput( tick, span<const uint8>{ intentWriter.getBytes().data(), static_cast<size_t>( intentWriter.getByteCount() ) } );
    }
} // namespace sw

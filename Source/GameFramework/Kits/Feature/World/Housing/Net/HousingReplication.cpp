#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"

namespace sw
{
    namespace
    {
        struct HousingReplicationInternal
        {
            static constexpr int32 kOpBits     = 3;
            static constexpr int32 kOwnerBits  = 2;
            static constexpr int32 kResultBits = 8;
            static_assert( static_cast<uint32>( HousingEditOp::Count ) <= ( 1u << kOpBits ), "edit ops must fit their bits" );
            static_assert( static_cast<uint32>( HousingPlotOwner::Count ) <= ( 1u << kOwnerBits ), "the owner must fit its bits" );
            static_assert( static_cast<uint32>( HousingPlaceResult::Pending ) < ( 1u << kResultBits ), "results must fit their bits" );

            static uint64 computeIDHash( const hashed_string& id ) { return static_cast<uint64>( id.getHash() ); }

            /** @brief 부지 목록에서 id 해시가 같은 부지의 자리입니다. 없으면 목록 크기입니다. */
            static size_t findPlotIndex( const vector<HousingNetPlotRef>& listPlot, uint64 plotHash )
            {
                for ( size_t index = 0; index < listPlot.size(); ++index )
                {
                    if ( computeIDHash( listPlot[index]._pLayout->getSettings()._plotID ) == plotHash )
                        return index;
                }
                return listPlot.size();
            }

            /** @brief 카탈로그에서 id 해시가 같은 부품입니다. 받은 글을 인턴하지 않으려고 해시로 찾는다. */
            static const HousingPartDef* findPart( const HousingPartCatalog& catalog, uint64 partHash )
            {
                for ( const HousingPartDef& part : catalog.getParts() )
                {
                    if ( computeIDHash( part._id ) == partHash )
                        return &part;
                }
                return nullptr;
            }

            static void writeTransform( BitWriter& writer, const HousingTransform& transform, HousingPlacementMode mode )
            {
                writer.writeFloat( transform._position._x );
                writer.writeFloat( transform._position._y );
                writer.writeVarInt( transform._level );
                writer.writeFloat( transform._yaw );
                writer.writeBool( mode == HousingPlacementMode::Free );
            }

            /** @brief 날 변환을 읽습니다. 유한하지 않거나 범위 밖이면 false 입니다(깨진 메시지). */
            [[nodiscard]] static bool readTransform( BitReader& reader, HousingTransform& outTransform, HousingPlacementMode& outMode )
            {
                const bool bRead    = reader.readFiniteFloat( outTransform._position._x ) && reader.readFiniteFloat( outTransform._position._y );
                outTransform._level = reader.readVarInt32();
                const bool bYaw     = reader.readFiniteFloat( outTransform._yaw );
                outMode             = reader.readBool() ? HousingPlacementMode::Free : HousingPlacementMode::Grid;
                if ( bRead == false || bYaw == false || reader.hasOverflowed() )
                    return false;
                return MathUtil::abs( outTransform._position._x ) <= NetHousingMessage::kMaxRawPosition &&
                       MathUtil::abs( outTransform._position._y ) <= NetHousingMessage::kMaxRawPosition && MathUtil::abs( outTransform._yaw ) <= NetHousingMessage::kMaxRawYaw;
            }

            static HousingPlotOwner computeOwner( int32 owner, int32 connectionID )
            {
                if ( owner == constant::invalid_index::kInt32 )
                    return HousingPlotOwner::None;
                return owner == connectionID ? HousingPlotOwner::Self : HousingPlotOwner::Other;
            }

            static int64 readFunds( const HousingNetPlotRef& plot ) { return plot._pWallet->getBalance( plot._pLayout->getCatalog()->getRoot()._currency ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HousingReplicationServer::HousingReplicationServer()
        : _listPlot{}
        , _listPending{}
        , _listLoad{}
        , _listJoined{}
        , _listConnected{}
        , _config{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
    {
    }

    bool HousingReplicationServer::initialize( INetPeers* pHost, const HousingServerConfig& config )
    {
        if ( pHost == nullptr || config._maxRequestsPerTick <= 0 )
        {
            SW_LOG_ERROR_ID( "HousingReplication.ServerConfigInvalid", "Housing server: a host and a positive request limit per tick are required (got %#) - the server does not start",
                             config._maxRequestsPerTick );
            return false;
        }
        _pHost  = pHost;
        _config = config;
        _listPlot.clear();
        _listPending.clear();
        _listLoad.clear();
        _listJoined.clear();
        _stats = HousingReplicationStats{};
        return true;
    }

    HousingReplicationServer::PlotEntry* HousingReplicationServer::findEntry( uint64 plotHash )
    {
        return VectorUtil::findIf( _listPlot, [plotHash]( const PlotEntry& entry )
        { return entry._plotHash == plotHash; } );
    }

    const HousingReplicationServer::PlotEntry* HousingReplicationServer::findEntry( uint64 plotHash ) const
    {
        return VectorUtil::findIf( _listPlot, [plotHash]( const PlotEntry& entry )
        { return entry._plotHash == plotHash; } );
    }

    HousingReplicationServer::PlotEntry& HousingReplicationServer::findOrAddEntry( uint64 plotHash )
    {
        if ( PlotEntry* pEntry = findEntry( plotHash ) )
            return *pEntry;
        PlotEntry entry;
        entry._plotHash = plotHash;
        _listPlot.push_back( entry );
        return _listPlot.back();
    }

    int32 HousingReplicationServer::findOwner( const hashed_string& plotID ) const
    {
        const PlotEntry* pEntry = findEntry( HousingReplicationInternal::computeIDHash( plotID ) );
        return pEntry != nullptr ? pEntry->_owner : constant::invalid_index::kInt32;
    }

    NetHandleResult HousingReplicationServer::receive( NetHousingMessage::Edit, const NetMessageContext& context, BitReader& body )
    {
        using Internal = HousingReplicationInternal;
        PendingEdit edit;
        edit._connectionID = context._connectionID;
        edit._requestID    = body.readVarUint32();
        edit._plotHash     = body.readVarUint();
        const uint32 op    = body.readBits( Internal::kOpBits );
        if ( body.hasOverflowed() || op >= static_cast<uint32>( HousingEditOp::Count ) )
            return NetHandleResult::Malformed;
        edit._op = static_cast<HousingEditOp>( op );
        switch ( edit._op )
        {
            case HousingEditOp::Place:
                edit._partHash = body.readVarUint();
                if ( Internal::readTransform( body, edit._transform, edit._mode ) == false )
                    return NetHandleResult::Malformed;
                break;
            case HousingEditOp::Remove:
                edit._placementID = body.readVarUint32();
                break;
            case HousingEditOp::Move:
                edit._placementID = body.readVarUint32();
                if ( Internal::readTransform( body, edit._transform, edit._mode ) == false )
                    return NetHandleResult::Malformed;
                break;
            case HousingEditOp::Claim:
            case HousingEditOp::Undo:
            case HousingEditOp::Redo:
            case HousingEditOp::Count:
                break;
        }
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        ++_stats._editCount;
        // 요청률: 이번 틱에 이 연결이 넣은 수가 상한이면 판정하지 않고 바로 답한다(끊지 않는다 — 지연 뒤 몰아 온 정상 요청일 수 있다).
        ConnectionLoad* pLoad = VectorUtil::findIf( _listLoad, [&context]( const ConnectionLoad& load )
        { return load._connectionID == context._connectionID; } );
        if ( pLoad == nullptr )
        {
            _listLoad.push_back( ConnectionLoad{ context._connectionID, 0 } );
            pLoad = &_listLoad.back();
        }
        if ( pLoad->_queuedCount >= _config._maxRequestsPerTick )
        {
            reply( edit._connectionID, edit._requestID, edit._plotHash, HousingPlaceResult::RateLimited, 0 );
            return NetHandleResult::Handled;
        }
        ++pLoad->_queuedCount;
        _listPending.push_back( edit );
        return NetHandleResult::Handled;
    }

    void HousingReplicationServer::onConnectionOpened( int32 connectionID )
    {
        if ( VectorUtil::contains( _listJoined, connectionID ) == false )
            _listJoined.push_back( connectionID );
    }

    void HousingReplicationServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        (void)VectorUtil::removeValue( _listJoined, connectionID );
        (void)VectorUtil::removeIf( _listPending, [connectionID]( const PendingEdit& edit )
        { return edit._connectionID == connectionID; } );
        // shortcut: 주인은 연결이다 — 다시 붙으면 새 연결이라 부지를 다시 차지한다. 계정이 붙는 게임(`INetPeers::getConnectionPrincipal`)이 생기면 주체로 적고 세이브에 싣는다.
        for ( PlotEntry& entry : _listPlot )
        {
            if ( entry._owner != connectionID )
                continue;
            entry._owner         = constant::invalid_index::kInt32;
            entry._bClearHistory = SW_TRUE;
        }
    }

    void HousingReplicationServer::update( const vector<HousingNetPlotRef>& listPlot )
    {
        using Internal = HousingReplicationInternal;
        if ( _pHost == nullptr )
            return;
        for ( const HousingNetPlotRef& plot : listPlot )
        {
            PlotEntry& entry = findOrAddEntry( Internal::computeIDHash( plot._pLayout->getSettings()._plotID ) );
            if ( entry._bClearHistory == SW_FALSE )
                continue;
            plot._pLayout->clearHistory();
            entry._bClearHistory = SW_FALSE;
        }
        // 판정은 받은 순서대로 — 같은 칸을 두 연결이 다투면 먼저 온 쪽이 이긴다.
        vector<PendingEdit> listPending = std::move( _listPending );
        _listPending.clear();
        _listLoad.clear();
        for ( const PendingEdit& edit : listPending )
        {
            uint32                   placementID = 0;
            const HousingPlaceResult result      = evaluateEdit( edit, listPlot, placementID );
            if ( result == HousingPlaceResult::Ok )
                ++_stats._appliedCount;
            reply( edit._connectionID, edit._requestID, edit._plotHash, result, placementID );
        }
        // 바뀐 부지는 모든 연결에, 늦게 들어온 연결은 모든 부지를 받는다.
        _pHost->collectConnected( _listConnected );
        for ( const HousingNetPlotRef& plot : listPlot )
        {
            PlotEntry&  entry    = findOrAddEntry( Internal::computeIDHash( plot._pLayout->getSettings()._plotID ) );
            const int64 funds    = Internal::readFunds( plot );
            const bool  bChanged = entry._bSent == SW_FALSE || entry._sentRevision != plot._pLayout->getRevision() || entry._sentFunds != funds || entry._sentOwner != entry._owner;
            if ( bChanged )
            {
                sendPlotState( entry, plot, _listConnected );
                entry._bSent        = SW_TRUE;
                entry._sentRevision = plot._pLayout->getRevision();
                entry._sentFunds    = funds;
                entry._sentOwner    = entry._owner;
            }
            else if ( _listJoined.empty() == false )
                sendPlotState( entry, plot, _listJoined );
        }
        _listJoined.clear();
    }

    HousingPlaceResult HousingReplicationServer::evaluateEdit( const PendingEdit& edit, const vector<HousingNetPlotRef>& listPlot, uint32& outPlacementID )
    {
        using Internal         = HousingReplicationInternal;
        const size_t plotIndex = Internal::findPlotIndex( listPlot, edit._plotHash );
        if ( plotIndex == listPlot.size() )
            return HousingPlaceResult::UnknownPlot;
        const HousingNetPlotRef& plot   = listPlot[plotIndex];
        HousingLayout&           layout = *plot._pLayout;
        PlotEntry&               entry  = findOrAddEntry( edit._plotHash );
        if ( edit._op == HousingEditOp::Claim )
        {
            if ( entry._owner == edit._connectionID )
                return HousingPlaceResult::Ok;
            const bool bOwnsAnother = VectorUtil::containsIf( _listPlot, [&edit]( const PlotEntry& other )
            { return other._owner == edit._connectionID; } );
            if ( entry._owner != constant::invalid_index::kInt32 || bOwnsAnother )
                return HousingPlaceResult::NotOwner; // 남의 부지이거나 이 연결이 이미 다른 부지를 가졌다
            entry._owner = edit._connectionID;
            layout.clearHistory();
            return HousingPlaceResult::Ok;
        }
        if ( entry._owner != edit._connectionID )
            return HousingPlaceResult::NotOwner;
        switch ( edit._op )
        {
            case HousingEditOp::Place:
            {
                const HousingPartDef* pPart = Internal::findPart( *layout.getCatalog(), edit._partHash );
                if ( pPart == nullptr )
                    return HousingPlaceResult::UnknownPart;
                HousingCandidate candidate;
                candidate._partID             = pPart->_id;
                candidate._transform          = edit._transform;
                candidate._mode               = edit._mode;
                const HousingPlaceCheck check = layout.evaluate( candidate ); // 스냅한 자리로 거리를 본다(소켓이 날 자리를 옮긴다)
                if ( check._result == HousingPlaceResult::Ok && isWithinReach( entry, plot, edit._connectionID, layout.computeLocalPosition( check._transform ) ) == false )
                    return HousingPlaceResult::OutOfReach;
                return layout.place( candidate, outPlacementID );
            }
            case HousingEditOp::Remove:
            {
                const HousingPlacement* pPlacement = layout.findPlacement( edit._placementID );
                if ( pPlacement != nullptr && isWithinReach( entry, plot, edit._connectionID, layout.computeLocalPosition( pPlacement->_transform ) ) == false )
                    return HousingPlaceResult::OutOfReach;
                return layout.remove( edit._placementID );
            }
            case HousingEditOp::Move:
            {
                const HousingPlacement* pPlacement = layout.findPlacement( edit._placementID );
                const bool              bReach     = pPlacement == nullptr || ( isWithinReach( entry, plot, edit._connectionID, layout.computeLocalPosition( pPlacement->_transform ) ) &&
                                                               isWithinReach( entry, plot, edit._connectionID, layout.computeLocalPosition( edit._transform ) ) );
                if ( bReach == false )
                    return HousingPlaceResult::OutOfReach;
                return layout.move( edit._placementID, edit._transform, edit._mode );
            }
            // shortcut: 되돌리기 · 다시 하기는 거리를 보지 않는다(이력의 자리는 주인이 이미 닿았던 곳이다) — 이력이 길어 먼 곳을 되돌리는 게임이 생기면 이력 칸의 자리로 본다.
            case HousingEditOp::Undo:
                return layout.undo();
            case HousingEditOp::Redo:
                return layout.redo();
            case HousingEditOp::Claim:
            case HousingEditOp::Count:
                break;
        }
        return HousingPlaceResult::UnknownPlacement;
    }

    bool HousingReplicationServer::isWithinReach( PlotEntry& entry, const HousingNetPlotRef& plot, int32 connectionID, const float3& localPosition )
    {
        const float32 maxReach = plot._pLayout->getSettings()._maxReach;
        if ( maxReach <= 0.0f )
            return true;
        float3 caller{};
        if ( _config._locateCaller.isBound() == false )
        {
            if ( entry._bReachErrorLogged == SW_FALSE )
                SW_LOG_ERROR_ID( "HousingReplication.NoCallerLocator", "Housing server: plot '%#' limits reach to %# m but the game gave no caller locator - every edit is refused",
                                 plot._pLayout->getSettings()._plotID.c_str(), maxReach );
            entry._bReachErrorLogged = SW_TRUE;
            return false;
        }
        if ( _config._locateCaller( connectionID, caller ) == false )
            return false; // 서버가 이 연결의 자리를 모른다 — 클라이언트 값으로 대신하지 않는다
        const float3  target = plot._origin + localPosition;
        const float32 dx     = target._x - caller._x;
        const float32 dz     = target._z - caller._z;
        return dx * dx + dz * dz <= maxReach * maxReach;
    }

    void HousingReplicationServer::reply( int32 connectionID, uint32 requestID, uint64 plotHash, HousingPlaceResult result, uint32 placementID )
    {
        using Internal = HousingReplicationInternal;
        if ( result != HousingPlaceResult::Ok )
            ++_stats._rejectedCount;
        BitWriter& writer = _writer.begin( NetHousingMessage::kReply );
        writer.writeVarUint( requestID );
        writer.writeVarUint( plotHash );
        writer.writeBits( static_cast<uint32>( result ), Internal::kResultBits );
        writer.writeVarUint( placementID );
        if ( _writer.send( *_pHost, connectionID, NetChannelType::ReliableOrdered ) == false )
            ++_stats._sendFailedCount;
    }

    void HousingReplicationServer::sendPlotState( PlotEntry& entry, const HousingNetPlotRef& plot, const vector<int32>& listConnection )
    {
        using Internal = HousingReplicationInternal;
        Archive archive;
        plot._pLayout->writeState( archive );
        const int32 layoutSize = static_cast<int32>( archive.getSize() );
        const int64 funds      = Internal::readFunds( plot );
        for ( const int32 connectionID : listConnection )
        {
            BitWriter& writer = _writer.begin( NetHousingMessage::kPlotState );
            writer.writeVarUint( entry._plotHash );
            writer.writeBits( static_cast<uint32>( Internal::computeOwner( entry._owner, connectionID ) ), Internal::kOwnerBits );
            writer.writeVarInt( funds );
            writer.writeBlob( archive.getData(), layoutSize );
            if ( _writer.getByteCount() > NetConnection::kMaxReliableMessageSize )
            {
                SW_LOG_ERROR_ID( "HousingReplication.PlotStateTooLarge", "Housing server: the state of plot '%#' is %# bytes, over the reliable message limit %# - it is not sent",
                                 plot._pLayout->getSettings()._plotID.c_str(), _writer.getByteCount(), NetConnection::kMaxReliableMessageSize );
                ++_stats._sendFailedCount;
                return;
            }
            if ( _writer.send( *_pHost, connectionID, NetChannelType::ReliableOrdered ) )
                ++_stats._stateSentCount;
            else
                ++_stats._sendFailedCount;
        }
    }
} // namespace sw

namespace sw
{
    HousingReplicationClient::HousingReplicationClient()
        : _listState{}
        , _listReply{}
        , _listOwner{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _nextRequestID{ 1 }
    {
    }

    void HousingReplicationClient::initialize( INetPeers* pHost )
    {
        _pHost = pHost;
        _listState.clear();
        _listReply.clear();
        _listOwner.clear();
        _stats         = HousingReplicationStats{};
        _nextRequestID = 1;
    }

    uint32 HousingReplicationClient::sendEdit( const hashed_string& plotID, const HousingEditRequest& request )
    {
        using Internal = HousingReplicationInternal;
        if ( _pHost == nullptr || request._op >= HousingEditOp::Count )
            return 0;
        const uint32 requestID = _nextRequestID;
        BitWriter&   writer    = _writer.begin( NetHousingMessage::kEdit );
        writer.writeVarUint( requestID );
        writer.writeVarUint( Internal::computeIDHash( plotID ) );
        writer.writeBits( static_cast<uint32>( request._op ), Internal::kOpBits );
        switch ( request._op )
        {
            case HousingEditOp::Place:
                writer.writeVarUint( Internal::computeIDHash( request._candidate._partID ) );
                Internal::writeTransform( writer, request._candidate._transform, request._candidate._mode );
                break;
            case HousingEditOp::Remove:
                writer.writeVarUint( request._placementID );
                break;
            case HousingEditOp::Move:
                writer.writeVarUint( request._placementID );
                Internal::writeTransform( writer, request._candidate._transform, request._candidate._mode );
                break;
            case HousingEditOp::Claim:
            case HousingEditOp::Undo:
            case HousingEditOp::Redo:
            case HousingEditOp::Count:
                break;
        }
        if ( _writer.send( *_pHost, 0, NetChannelType::ReliableOrdered ) == false )
        {
            ++_stats._sendFailedCount;
            return 0;
        }
        ++_nextRequestID;
        return requestID;
    }

    NetHandleResult HousingReplicationClient::receive( NetHousingMessage::Reply, const NetMessageContext& context, BitReader& body )
    {
        using Internal = HousingReplicationInternal;
        (void)context;
        ReceivedReply received;
        received._requestID   = body.readVarUint32();
        received._plotHash    = body.readVarUint();
        const uint32 result   = body.readBits( Internal::kResultBits );
        received._placementID = body.readVarUint32();
        if ( body.hasOverflowed() || result > static_cast<uint32>( HousingPlaceResult::Pending ) )
            return NetHandleResult::Malformed;
        received._result = static_cast<HousingPlaceResult>( result );
        _listReply.push_back( received );
        return NetHandleResult::Handled;
    }

    NetHandleResult HousingReplicationClient::receive( NetHousingMessage::PlotState, const NetMessageContext& context, BitReader& body )
    {
        using Internal = HousingReplicationInternal;
        (void)context;
        PendingState state;
        state._plotHash    = body.readVarUint();
        const uint32 owner = body.readBits( Internal::kOwnerBits );
        state._funds       = body.readVarInt();
        const bool bBlob   = body.readBlob( state._layoutBytes, NetConnection::kMaxReliableMessageSize );
        if ( bBlob == false || body.hasOverflowed() || owner >= static_cast<uint32>( HousingPlotOwner::Count ) )
            return NetHandleResult::Malformed;
        state._owner            = static_cast<HousingPlotOwner>( owner );
        const uint64  plotHash  = state._plotHash;
        PendingState* pExisting = VectorUtil::findIf( _listState, [plotHash]( const PendingState& existing )
        { return existing._plotHash == plotHash; } );
        if ( pExisting != nullptr )
            *pExisting = std::move( state ); // 전체 상태다 — 아직 적용하지 않은 앞 상태는 버린다
        else
            _listState.push_back( std::move( state ) );
        return NetHandleResult::Handled;
    }

    void HousingReplicationClient::update( const vector<HousingNetPlotRef>& listPlot, vector<HousingEditReply>& outListReply )
    {
        using Internal = HousingReplicationInternal;
        // 상태 — 목록에 있는 부지만 적용하고 나머지는 들고 있는다(부지가 아직 열리지 않았다).
        vector<PendingState> listKept;
        for ( PendingState& state : _listState )
        {
            const size_t plotIndex = Internal::findPlotIndex( listPlot, state._plotHash );
            if ( plotIndex == listPlot.size() )
            {
                listKept.push_back( std::move( state ) );
                continue;
            }
            const HousingNetPlotRef& plot = listPlot[plotIndex];
            Archive                  archive( state._layoutBytes.data(), state._layoutBytes.size() );
            if ( plot._pLayout->readState( archive ) == false )
            {
                // 서버와 카탈로그 · 규칙이 다르다(다른 콘텐츠) — 지금 상태를 그대로 두고 까닭을 남긴다(레이아웃 읽기가 자세한 까닭을 이미 남겼다).
                SW_LOG_ERROR_ID( "HousingReplication.PlotStateRefused", "Housing client: the server state of plot '%#' does not fit this client's catalog or rules - the plot keeps its last state",
                                 plot._pLayout->getSettings()._plotID.c_str() );
                continue;
            }
            plot._pWallet->setBalance( plot._pLayout->getCatalog()->getRoot()._currency, state._funds );
            ++_stats._stateAppliedCount;
            const uint64 plotHash = state._plotHash;
            OwnerEntry*  pEntry   = VectorUtil::findIf( _listOwner, [plotHash]( const OwnerEntry& entry )
               { return entry._plotHash == plotHash; } );
            if ( pEntry == nullptr )
            {
                _listOwner.push_back( OwnerEntry{ plotHash, state._owner } );
                continue;
            }
            pEntry->_owner = state._owner;
        }
        _listState = std::move( listKept );
        // 판정 — 모르는 부지의 답은 버린다(그 부지에 요청을 보낸 적이 없다).
        for ( const ReceivedReply& received : _listReply )
        {
            const size_t plotIndex = Internal::findPlotIndex( listPlot, received._plotHash );
            if ( plotIndex == listPlot.size() )
                continue;
            HousingEditReply reply;
            reply._plotIndex   = plotIndex;
            reply._requestID   = received._requestID;
            reply._placementID = received._placementID;
            reply._result      = received._result;
            outListReply.push_back( reply );
        }
        _listReply.clear();
    }

    HousingPlotOwner HousingReplicationClient::findOwner( const hashed_string& plotID ) const
    {
        const uint64      plotHash = HousingReplicationInternal::computeIDHash( plotID );
        const OwnerEntry* pEntry   = VectorUtil::findIf( _listOwner, [plotHash]( const OwnerEntry& entry )
          { return entry._plotHash == plotHash; } );
        return pEntry != nullptr ? pEntry->_owner : HousingPlotOwner::None;
    }
} // namespace sw

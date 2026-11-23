#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Net/HousingReplication.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Fixed/ReplicatedLayout.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct HousingReplicationInternal
        {
            static constexpr int32  kOpBits           = 3;
            static constexpr int32  kOwnerBits        = 2;
            static constexpr uint32 kMaxDeltaEntries  = NetHousingMessage::kMaxDeltaBytes; ///< 델타의 id · 배치 수 상한(하나가 적어도 1 바이트다)
            static constexpr int32  kChunkHeaderBytes = 64;                                ///< 조각 메시지의 머리(종류 · 해시 · 주인 · 잔액 · 크기 · 자리 · 길이)의 넉넉한 상한
            static_assert( static_cast<uint32>( HousingEditOp::Count ) <= ( 1u << kOpBits ), "edit ops must fit their bits" );
            static_assert( static_cast<uint32>( HousingPlotOwner::Count ) <= ( 1u << kOwnerBits ), "the owner must fit its bits" );
            static_assert( NetHousingMessage::kSnapshotChunkBytes + kChunkHeaderBytes <= NetConnection::kMaxReliableMessageSize, "a snapshot chunk must fit one reliable message" );

            static uint64 computeIDHash( const hashed_string& id ) { return static_cast<uint64>( id.getHash() ); }

            /** @brief 부지 목록에서 id 해시가 같은 부지의 자리입니다. 없으면 목록 크기입니다. */
            static optional<size_t> findPlotIndex( const vector<HousingNetPlotRef>& listPlot, uint64 plotHash )
            {
                for ( size_t index = 0; index < listPlot.size(); ++index )
                {
                    if ( computeIDHash( listPlot[index]._pLayout->getSettings()._plotID ) == plotHash )
                        return index;
                }
                return nullopt;
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

            /** @brief 날 변환을 씁니다 — 칸은 `HousingTransformNetState` 의 선언이다. 선언 범위 밖이면 false 이고 @p writer 에 쓰다 만 비트가 남습니다. */
            [[nodiscard]] static bool writeTransform( BitWriter& writer, const HousingTransform& transform )
            {
                HousingTransformNetState state;
                state._position = transform._position;
                state._level    = transform._level;
                state._yaw      = transform._yaw;
                return ReplicatedLayout::writeFull( state, writer );
            }

            /** @brief 날 변환을 읽습니다. 유한하지 않거나 선언 범위 밖이면 false 입니다(깨진 메시지). */
            [[nodiscard]] static bool readTransform( BitReader& reader, HousingTransform& outTransform )
            {
                HousingTransformNetState state;
                if ( ReplicatedLayout::readFull( reader, state, nullptr ) != FixedReadResult::Ok )
                    return false;
                outTransform._position = state._position;
                outTransform._level    = state._level;
                outTransform._yaw      = state._yaw;
                return true;
            }

            static HousingPlotOwner computeOwner( uint64 owner, uint64 recipient )
            {
                if ( owner == HousingOwnerID::kNone )
                    return HousingPlotOwner::None;
                return owner == recipient ? HousingPlotOwner::Self : HousingPlotOwner::Other;
            }

            static int64 readFunds( const HousingNetPlotRef& plot ) { return plot._pWallet->getBalance( plot._pLayout->getCatalog()->getRoot()._currency ); }

            static bool isSamePlacement( const HousingPlacement& lhs, const HousingPlacement& rhs )
            {
                return lhs._partID == rhs._partID && lhs._partVersion == rhs._partVersion && lhs._transform == rhs._transform;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HousingReplicationServer::HousingReplicationServer()
        : _listPlot{}
        , _listPending{}
        , _listSnapshotRequest{}
        , _listTransfer{}
        , _listLoad{}
        , _listJoined{}
        , _listClosed{}
        , _listConnected{}
        , _listReachTarget{}
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
        _listSnapshotRequest.clear();
        _listTransfer.clear();
        _listLoad.clear();
        _listJoined.clear();
        _listClosed.clear();
        _stats = HousingReplicationStats{};
        return true;
    }

    HousingReplicationServer::PlotEntry& HousingReplicationServer::getOrCreateEntry( uint64 plotHash )
    {
        if ( PlotEntry* pEntry = VectorUtil::findIf( _listPlot, [plotHash]( const PlotEntry& entry )
        { return entry._plotHash == plotHash; } ) )
            return *pEntry;
        PlotEntry entry;
        entry._plotHash = plotHash;
        _listPlot.push_back( std::move( entry ) );
        return _listPlot.back();
    }

    uint64 HousingReplicationServer::findCallerOwner( int32 connectionID ) const
    {
        return HousingOwnerID::makeForConnection( _pHost->getConnectionPrincipal( connectionID ), connectionID );
    }

    bool HousingReplicationServer::acceptRequest( int32 connectionID )
    {
        ConnectionLoad* pLoad = VectorUtil::findIf( _listLoad, [connectionID]( const ConnectionLoad& load )
        { return load._connectionID == connectionID; } );
        if ( pLoad == nullptr )
        {
            _listLoad.push_back( ConnectionLoad{ connectionID, 0 } );
            pLoad = &_listLoad.back();
        }
        if ( pLoad->_queuedCount >= _config._maxRequestsPerTick )
            return false; // 끊지 않는다 — 지연 뒤 몰아 온 정상 요청일 수 있다
        ++pLoad->_queuedCount;
        return true;
    }

    NetHandleResult HousingReplicationServer::receive( NetHousingMessage::Edit, const NetMessageContext& context, BitReader& body )
    {
        using Internal = HousingReplicationInternal;
        PendingEdit edit;
        edit._connectionID = context._connectionID;
        edit._requestID    = body.readVarUint32();
        edit._plotHash     = body.readVarUint();
        edit._op           = body.readEnum<decltype( edit._op )>( Internal::kOpBits );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        switch ( edit._op )
        {
            case HousingEditOp::Place:
                edit._partHash = body.readVarUint();
                if ( Internal::readTransform( body, edit._transform ) == false )
                    return NetHandleResult::Malformed;
                edit._mode = body.readBool() ? HousingPlacementMode::Free : HousingPlacementMode::Grid;
                break;
            case HousingEditOp::Remove:
                edit._placementID = body.readVarUint32();
                break;
            case HousingEditOp::Move:
                edit._placementID = body.readVarUint32();
                if ( Internal::readTransform( body, edit._transform ) == false )
                    return NetHandleResult::Malformed;
                edit._mode = body.readBool() ? HousingPlacementMode::Free : HousingPlacementMode::Grid;
                break;
            case HousingEditOp::Claim:
            case HousingEditOp::Undo:
            case HousingEditOp::Redo:
            case HousingEditOp::Count:
                break;
        }
        if ( body.readEnd() == false )
            return NetHandleResult::Malformed; // 잘림 · 종류에 맞지 않는 꼬리(남는 바이트)
        ++_stats._editCount;
        if ( acceptRequest( context._connectionID ) == false )
        {
            reply( edit._connectionID, edit._requestID, edit._plotHash, HousingPlaceResult::RateLimited, 0 );
            return NetHandleResult::Handled;
        }
        _listPending.push_back( edit );
        return NetHandleResult::Handled;
    }

    NetHandleResult HousingReplicationServer::receive( NetHousingMessage::SnapshotRequest, const NetMessageContext& context, const HousingSnapshotRequestBody& body )
    {
        const uint64 plotHash = body._plotHash;
        const bool   bQueued  = VectorUtil::containsIf( _listSnapshotRequest, [&context, plotHash]( const SnapshotRequestEntry& entry )
           { return entry._connectionID == context._connectionID && entry._plotHash == plotHash; } );
        if ( bQueued == false && acceptRequest( context._connectionID ) )
            _listSnapshotRequest.push_back( SnapshotRequestEntry{ plotHash, context._connectionID } );
        return NetHandleResult::Handled; // 넘친 다시 맞추기 요청은 버린다 — 클라이언트는 델타를 버리며 기다리다 다시 요청하지 않으므로 다음 틱의 같은 요청이 온다
    }

    void HousingReplicationServer::onConnectionOpened( int32 connectionID )
    {
        VectorUtil::addUnique( _listJoined, connectionID );
    }

    void HousingReplicationServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        (void)VectorUtil::removeValue( _listJoined, connectionID );
        (void)VectorUtil::removeIf( _listPending, [connectionID]( const PendingEdit& edit )
        { return edit._connectionID == connectionID; } );
        (void)VectorUtil::removeIf( _listSnapshotRequest, [connectionID]( const SnapshotRequestEntry& entry )
        { return entry._connectionID == connectionID; } );
        (void)VectorUtil::removeIf( _listTransfer, [connectionID]( const Transfer& transfer )
        { return transfer._connectionID == connectionID; } );
        // 계정 주인은 남는다(다시 붙으면 같은 주인). 세션 주인은 다음 update 에서 푼다 — 부지 포인터는 update 동안만 빌린다.
        _listClosed.push_back( connectionID );
    }

    void HousingReplicationServer::releaseClosedOwners( const vector<HousingNetPlotRef>& listPlot )
    {
        for ( const int32 connectionID : _listClosed )
        {
            const uint64 sessionOwner = HousingOwnerID::kSessionFlag | static_cast<uint64>( static_cast<uint32>( connectionID ) );
            for ( const HousingNetPlotRef& plot : listPlot )
            {
                if ( *plot._pOwner != sessionOwner )
                    continue;
                *plot._pOwner = HousingOwnerID::kNone;
                plot._pLayout->clearHistory();
            }
        }
        _listClosed.clear();
    }

    void HousingReplicationServer::update( const vector<HousingNetPlotRef>& listPlot )
    {
        using Internal = HousingReplicationInternal;
        if ( _pHost == nullptr )
            return;
        releaseClosedOwners( listPlot );
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
        _pHost->collectConnected( _listConnected );
        vector<int32> listFresh;
        for ( const HousingNetPlotRef& plot : listPlot )
        {
            const uint64 plotHash = Internal::computeIDHash( plot._pLayout->getSettings()._plotID );
            // 처음 받는 연결 — 늦게 들어왔거나 다시 맞추기를 요청했다.
            listFresh = _listJoined;
            for ( const SnapshotRequestEntry& request : _listSnapshotRequest )
            {
                if ( request._plotHash == plotHash && VectorUtil::contains( listFresh, request._connectionID ) == false )
                    listFresh.push_back( request._connectionID );
            }
            sendPlotChanges( getOrCreateEntry( plotHash ), plot, listFresh );
        }
        _listJoined.clear();
        _listSnapshotRequest.clear();
        sendTransfers();
    }

    void HousingReplicationServer::sendPlotChanges( PlotEntry& entry, const HousingNetPlotRef& plot, const vector<int32>& listFresh )
    {
        using Internal                                = HousingReplicationInternal;
        const HousingLayout&            layout        = *plot._pLayout;
        const vector<HousingPlacement>& listPlacement = layout.getPlacements();
        const int64                     funds         = Internal::readFunds( plot );
        const uint64                    owner         = *plot._pOwner;
        const bool                      bChanged      = entry._bSent == SW_FALSE || entry._sentRevision != layout.getRevision() || entry._sentFunds != funds || entry._sentOwner != owner;
        if ( bChanged == false && listFresh.empty() )
            return;
        // 상태 바이트는 조각 전송이 하나라도 있을 때만 만든다(큰 부지를 편집마다 통째로 쓰지 않는다).
        vector<uint8> bytes;
        const auto    readBytes = [&layout, &bytes]() -> const vector<uint8>&
        {
            if ( bytes.empty() )
            {
                Archive archive;
                layout.writeState( archive );
                bytes.assign( archive.getData(), archive.getData() + archive.getSize() );
            }
            return bytes;
        };
        // 처음 받는 연결과 상태를 아직 받지 않은 부지는 조각 전송이다.
        for ( const int32 connectionID : _listConnected )
        {
            const bool bFresh = entry._bSent == SW_FALSE || VectorUtil::contains( listFresh, connectionID );
            if ( bFresh )
                startTransfer( plot, entry._plotHash, connectionID, readBytes() );
        }
        if ( bChanged && entry._bSent == SW_TRUE )
        {
            // 델타: 마지막으로 보낸 배치와 지금 배치를 id 순서로 견준다(둘 다 오름차순).
            vector<uint32>                  listRemovedID;
            vector<const HousingPlacement*> listUpsert;
            size_t                          sentIndex = 0;
            size_t                          nowIndex  = 0;
            const vector<HousingPlacement>& listSent  = entry._listSentPlacement;
            while ( sentIndex < listSent.size() || nowIndex < listPlacement.size() )
            {
                const bool bSentOnly = nowIndex == listPlacement.size() ||
                                       ( sentIndex < listSent.size() && listSent[sentIndex]._placementID < listPlacement[nowIndex]._placementID );
                if ( bSentOnly )
                {
                    listRemovedID.push_back( listSent[sentIndex++]._placementID );
                    continue;
                }
                const bool bNowOnly = sentIndex == listSent.size() || listPlacement[nowIndex]._placementID < listSent[sentIndex]._placementID;
                if ( bNowOnly || Internal::isSamePlacement( listSent[sentIndex], listPlacement[nowIndex] ) == false )
                    listUpsert.push_back( &listPlacement[nowIndex] );
                if ( bNowOnly == false )
                    ++sentIndex;
                ++nowIndex;
            }
            for ( const int32 connectionID : _listConnected )
            {
                if ( VectorUtil::contains( listFresh, connectionID ) )
                    continue;
                if ( hasTransfer( entry._plotHash, connectionID ) )
                {
                    startTransfer( plot, entry._plotHash, connectionID, readBytes() ); // 보내던 상태는 낡았다 — 새 상태로 처음부터
                    continue;
                }
                BitWriter& writer = _writer.beginMessage<NetHousingMessage::Table, NetHousingMessage::PlotDelta>();
                writer.writeVarUint( entry._plotHash );
                writer.writeBits( static_cast<uint32>( Internal::computeOwner( owner, findCallerOwner( connectionID ) ) ), Internal::kOwnerBits );
                writer.writeVarInt( funds );
                writer.writeVarUint( layout.getNextPlacementID() );
                writer.writeVarUint( listRemovedID.size() );
                for ( const uint32 removedID : listRemovedID )
                    writer.writeVarUint( removedID );
                writer.writeVarUint( listUpsert.size() );
                bool bTransformsWritten = true;
                for ( const HousingPlacement* pPlacement : listUpsert )
                {
                    writer.writeVarUint( pPlacement->_placementID );
                    writer.writeVarUint( Internal::computeIDHash( pPlacement->_partID ) );
                    writer.writeVarInt( pPlacement->_partVersion );
                    if ( Internal::writeTransform( writer, pPlacement->_transform ) == false )
                        bTransformsWritten = false;
                }
                if ( bTransformsWritten == false )
                {
                    // 서버 레이아웃이 받아 준 배치가 와이어 범위 밖이다 — 레이아웃과 와이어 선언의 어긋남(결함)이다. 상태 바이트(범위 없는 레이아웃 저장 꼴)로 보낸다.
                    SW_LOG_ERROR_ID( "HousingReplication.PlacementOutsideWireRange", "HousingReplication: a placement of plot %# is outside the wire transform range - sent as a full state", entry._plotHash );
                    startTransfer( plot, entry._plotHash, connectionID, readBytes() );
                    continue;
                }
                if ( _writer.getByteCount() > NetHousingMessage::kMaxDeltaBytes )
                {
                    startTransfer( plot, entry._plotHash, connectionID, readBytes() ); // 바뀐 것이 많다 — 조각 전송이 창을 나눠 쓴다
                    continue;
                }
                if ( _writer.sendMessage( *_pHost, connectionID ) )
                    ++_stats._deltaSentCount;
                else
                    ++_stats._sendFailedCount;
            }
        }
        entry._bSent             = SW_TRUE;
        entry._sentRevision      = layout.getRevision();
        entry._sentFunds         = funds;
        entry._sentOwner         = owner;
        entry._listSentPlacement = listPlacement;
    }

    bool HousingReplicationServer::hasTransfer( uint64 plotHash, int32 connectionID ) const
    {
        return VectorUtil::containsIf( _listTransfer, [plotHash, connectionID]( const Transfer& transfer )
        { return transfer._plotHash == plotHash && transfer._connectionID == connectionID; } );
    }

    void HousingReplicationServer::startTransfer( const HousingNetPlotRef& plot, uint64 plotHash, int32 connectionID, const vector<uint8>& bytes )
    {
        using Internal = HousingReplicationInternal;
        if ( static_cast<int64>( bytes.size() ) > NetHousingMessage::kMaxSnapshotBytes )
        {
            SW_LOG_ERROR_ID( "HousingReplication.PlotStateTooLarge", "Housing server: the state of plot '%#' is %# bytes, over the limit %# - it is not sent",
                             plot._pLayout->getSettings()._plotID.c_str(), bytes.size(), NetHousingMessage::kMaxSnapshotBytes );
            ++_stats._sendFailedCount;
            return;
        }
        (void)VectorUtil::removeIf( _listTransfer, [plotHash, connectionID]( const Transfer& transfer )
        { return transfer._plotHash == plotHash && transfer._connectionID == connectionID; } );
        Transfer transfer;
        transfer._bytes        = bytes;
        transfer._plotHash     = plotHash;
        transfer._funds        = Internal::readFunds( plot );
        transfer._connectionID = connectionID;
        transfer._owner        = Internal::computeOwner( *plot._pOwner, findCallerOwner( connectionID ) );
        _listTransfer.push_back( std::move( transfer ) );
    }

    void HousingReplicationServer::sendTransfers()
    {
        for ( Transfer& transfer : _listTransfer )
        {
            const int32 totalSize = static_cast<int32>( transfer._bytes.size() );
            do
            {
                const int32             chunkSize = MathUtil::min( NetHousingMessage::kSnapshotChunkBytes, totalSize - transfer._offset );
                HousingPlotSnapshotBody body;
                body._plotHash  = transfer._plotHash;
                body._owner     = transfer._owner;
                body._funds     = transfer._funds;
                body._totalSize = totalSize;
                body._offset    = transfer._offset;
                body._chunk.assign( transfer._bytes.begin() + transfer._offset, transfer._bytes.begin() + transfer._offset + chunkSize );
                if ( _writer.writeMessage<NetHousingMessage::Table, NetHousingMessage::PlotSnapshot>( body ) == false )
                    break; // 상태가 비었거나 주인 값이 틀렸다 — 오류 로그가 남았다
                // 창이 차면 닫지 않고 다음 틱에 잇는다(조각 여럿을 한 틱에 밀어 넣으면 신뢰 창을 넘는다).
                if ( _writer.trySendMessage( *_pHost, transfer._connectionID ) == false )
                    break;
                ++_stats._snapshotChunkCount;
                transfer._offset += chunkSize;
            } while ( transfer._offset < totalSize );
        }
        (void)VectorUtil::removeIf( _listTransfer, []( const Transfer& transfer )
        { return transfer._offset >= static_cast<int32>( transfer._bytes.size() ); } );
    }

    HousingPlaceResult HousingReplicationServer::evaluateEdit( const PendingEdit& edit, const vector<HousingNetPlotRef>& listPlot, uint32& outPlacementID )
    {
        using Internal                   = HousingReplicationInternal;
        const optional<size_t> plotIndex = Internal::findPlotIndex( listPlot, edit._plotHash );
        if ( plotIndex.has_value() == false )
            return HousingPlaceResult::UnknownPlot;
        const HousingNetPlotRef& plot        = listPlot[*plotIndex];
        HousingLayout&           layout      = *plot._pLayout;
        PlotEntry&               entry       = getOrCreateEntry( edit._plotHash );
        const uint64             callerOwner = findCallerOwner( edit._connectionID );
        if ( edit._op == HousingEditOp::Claim )
        {
            if ( *plot._pOwner == callerOwner )
                return HousingPlaceResult::Ok;
            const bool bOwnsAnother = VectorUtil::containsIf( listPlot, [callerOwner]( const HousingNetPlotRef& other )
            { return *other._pOwner == callerOwner; } );
            if ( *plot._pOwner != HousingOwnerID::kNone || bOwnsAnother )
                return HousingPlaceResult::NotOwner; // 남의 부지이거나 이 주체가 이미 다른 부지를 가졌다
            *plot._pOwner = callerOwner;
            layout.clearHistory();
            return HousingPlaceResult::Ok;
        }
        if ( *plot._pOwner != callerOwner )
            return HousingPlaceResult::NotOwner;
        // 편집이 건드릴 자리를 모아 거리를 본다 — 스냅한 자리(놓기), 대상의 지금 자리와 닿을 자리(치우기 · 옮기기), 이력 칸의 자리(되돌리기 · 다시 하기).
        _listReachTarget.clear();
        vector<HousingTransform> listTransform;
        HousingCandidate         candidate;
        switch ( edit._op )
        {
            case HousingEditOp::Place:
            {
                const HousingPartDef* pPart = Internal::findPart( *layout.getCatalog(), edit._partHash );
                if ( pPart == nullptr )
                    return HousingPlaceResult::UnknownPart;
                candidate._partID             = pPart->_id;
                candidate._transform          = edit._transform;
                candidate._mode               = edit._mode;
                const HousingPlaceCheck check = layout.evaluate( candidate );
                if ( check._result != HousingPlaceResult::Ok )
                    return check._result;
                listTransform.push_back( check._transform );
                break;
            }
            case HousingEditOp::Remove:
            case HousingEditOp::Move:
            {
                const HousingPlacement* pPlacement = layout.findPlacement( edit._placementID );
                if ( pPlacement == nullptr )
                    return HousingPlaceResult::UnknownPlacement;
                listTransform.push_back( pPlacement->_transform );
                if ( edit._op == HousingEditOp::Move )
                    listTransform.push_back( edit._transform );
                break;
            }
            case HousingEditOp::Undo:
            case HousingEditOp::Redo:
                layout.collectHistoryTargets( edit._op == HousingEditOp::Redo, listTransform );
                break;
            case HousingEditOp::Claim:
            case HousingEditOp::Count:
                break;
        }
        for ( const HousingTransform& transform : listTransform )
            _listReachTarget.push_back( layout.computeLocalPosition( transform ) );
        if ( isWithinReach( entry, plot, edit._connectionID, _listReachTarget ) == false )
            return HousingPlaceResult::OutOfReach;
        switch ( edit._op )
        {
            case HousingEditOp::Place:
                return layout.place( candidate, outPlacementID );
            case HousingEditOp::Remove:
                return layout.remove( edit._placementID );
            case HousingEditOp::Move:
                return layout.move( edit._placementID, edit._transform, edit._mode );
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

    bool HousingReplicationServer::isWithinReach( PlotEntry& entry, const HousingNetPlotRef& plot, int32 connectionID, const vector<float3>& listLocal )
    {
        const float32 maxReach = plot._pLayout->getSettings()._maxReach;
        if ( maxReach <= 0.0f || listLocal.empty() )
            return true;
        if ( _config._locateCaller.isBound() == false )
        {
            if ( entry._bReachErrorLogged == SW_FALSE )
                SW_LOG_ERROR_ID( "HousingReplication.NoCallerLocator", "Housing server: plot '%#' limits reach to %# m but the game gave no caller locator - every edit is refused",
                                 plot._pLayout->getSettings()._plotID.c_str(), maxReach );
            entry._bReachErrorLogged = SW_TRUE;
            return false;
        }
        float3 caller{};
        if ( _config._locateCaller( connectionID, caller ) == false )
            return false; // 서버가 이 연결의 자리를 모른다 — 클라이언트 값으로 대신하지 않는다
        for ( const float3& local : listLocal )
        {
            const float3  target = plot._origin + local;
            const float32 dx     = target._x - caller._x;
            const float32 dz     = target._z - caller._z;
            if ( dx * dx + dz * dz > maxReach * maxReach )
                return false;
        }
        return true;
    }

    void HousingReplicationServer::reply( int32 connectionID, uint32 requestID, uint64 plotHash, HousingPlaceResult result, uint32 placementID )
    {
        if ( result != HousingPlaceResult::Ok )
            ++_stats._rejectedCount;
        HousingReplyBody body;
        body._requestID   = requestID;
        body._plotHash    = plotHash;
        body._result      = result;
        body._placementID = placementID;
        const bool bSent  = _writer.writeMessage<NetHousingMessage::Table, NetHousingMessage::Reply>( body ) && _writer.sendMessage( *_pHost, connectionID );
        if ( bSent == false )
            ++_stats._sendFailedCount;
    }
} // namespace sw

namespace sw
{
    HousingReplicationClient::HousingReplicationClient()
        : _listOp{}
        , _listAssembly{}
        , _listAwaitingSnapshot{}
        , _listReply{}
        , _listOwner{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _nextRequestID{ 1 }
        , _bServerReached{ SW_FALSE }
    {
    }

    void HousingReplicationClient::initialize( INetPeers* pHost )
    {
        _pHost = pHost;
        _listOp.clear();
        _listAssembly.clear();
        _listAwaitingSnapshot.clear();
        _listReply.clear();
        _listOwner.clear();
        _stats          = HousingReplicationStats{};
        _nextRequestID  = 1;
        _bServerReached = SW_FALSE;
    }

    bool HousingReplicationClient::isAwaitingServer()
    {
        if ( _pHost != nullptr && _pHost->getConnectedCount() > 0 )
            _bServerReached = SW_TRUE;
        return _bServerReached == SW_FALSE;
    }

    uint32 HousingReplicationClient::sendEdit( const hashed_string& plotID, const HousingEditRequest& request )
    {
        using Internal = HousingReplicationInternal;
        if ( _pHost == nullptr || request._op >= HousingEditOp::Count )
            return 0;
        const uint32 requestID = _nextRequestID;
        BitWriter&   writer    = _writer.beginMessage<NetHousingMessage::Table, NetHousingMessage::Edit>();
        writer.writeVarUint( requestID );
        writer.writeVarUint( Internal::computeIDHash( plotID ) );
        writer.writeBits( static_cast<uint32>( request._op ), Internal::kOpBits );
        switch ( request._op )
        {
            case HousingEditOp::Place:
                writer.writeVarUint( Internal::computeIDHash( request._candidate._partID ) );
                if ( Internal::writeTransform( writer, request._candidate._transform ) == false )
                    return 0; // 서버가 깨진 메시지로 볼 범위 밖 변환은 보내지 않는다
                writer.writeBool( request._candidate._mode == HousingPlacementMode::Free );
                break;
            case HousingEditOp::Remove:
                writer.writeVarUint( request._placementID );
                break;
            case HousingEditOp::Move:
                writer.writeVarUint( request._placementID );
                if ( Internal::writeTransform( writer, request._candidate._transform ) == false )
                    return 0; // 서버가 깨진 메시지로 볼 범위 밖 변환은 보내지 않는다
                writer.writeBool( request._candidate._mode == HousingPlacementMode::Free );
                break;
            case HousingEditOp::Claim:
            case HousingEditOp::Undo:
            case HousingEditOp::Redo:
            case HousingEditOp::Count:
                break;
        }
        if ( _writer.sendMessage( *_pHost, 0 ) == false )
        {
            ++_stats._sendFailedCount;
            return 0;
        }
        ++_nextRequestID;
        return requestID;
    }

    NetHandleResult HousingReplicationClient::receive( NetHousingMessage::Reply, const NetMessageContext& context, const HousingReplyBody& body )
    {
        (void)context;
        ReceivedReply received;
        received._requestID   = body._requestID;
        received._plotHash    = body._plotHash;
        received._result      = body._result; // 등록 열거자 밖 값은 본문 읽기가 거절했다
        received._placementID = body._placementID;
        _listReply.push_back( received );
        return NetHandleResult::Handled;
    }

    NetHandleResult HousingReplicationClient::receive( NetHousingMessage::PlotSnapshot, const NetMessageContext& context, const HousingPlotSnapshotBody& body )
    {
        (void)context;
        const uint64           plotHash  = body._plotHash;
        const HousingPlotOwner owner     = body._owner;
        const int64            funds     = body._funds;
        const int32            totalSize = body._totalSize;
        const int32            offset    = body._offset;
        const vector<uint8>&   chunk     = body._chunk;
        const int32            chunkSize = static_cast<int32>( chunk.size() );
        // 열거자 · 음수 · 조각 길이는 본문 읽기가 봤다. 전체 크기 상한과 조각이 전체 안인지는 값 사이의 관계라 여기서 본다.
        const bool bShape = totalSize <= NetHousingMessage::kMaxSnapshotBytes && chunkSize > 0 && static_cast<int64>( offset ) + chunkSize <= totalSize;
        if ( bShape == false )
            return NetHandleResult::Malformed;
        Assembly* pAssembly = VectorUtil::findIf( _listAssembly, [plotHash]( const Assembly& assembly )
        { return assembly._plotHash == plotHash; } );
        if ( offset == 0 )
        {
            // 새 전송 — 받던 조각은 낡았다(서버가 새 상태로 다시 시작했다).
            if ( pAssembly == nullptr )
            {
                _listAssembly.push_back( Assembly{ {}, plotHash, totalSize } );
                pAssembly = &_listAssembly.back();
            }
            pAssembly->_bytes.clear();
            pAssembly->_totalSize = totalSize;
        }
        if ( pAssembly == nullptr || pAssembly->_totalSize != totalSize || static_cast<int32>( pAssembly->_bytes.size() ) != offset )
            return NetHandleResult::Malformed; // 신뢰 순서 채널이라 빈 조각 · 어긋난 자리는 깨진 것이다
        pAssembly->_bytes.insert( pAssembly->_bytes.end(), chunk.begin(), chunk.end() );
        if ( static_cast<int32>( pAssembly->_bytes.size() ) < totalSize )
            return NetHandleResult::Handled;
        PendingOp op;
        op._kind          = OpKind::Snapshot;
        op._plotHash      = plotHash;
        op._owner         = owner;
        op._funds         = funds;
        op._snapshotBytes = std::move( pAssembly->_bytes );
        _listOp.push_back( std::move( op ) );
        (void)VectorUtil::removeIf( _listAssembly, [plotHash]( const Assembly& assembly )
        { return assembly._plotHash == plotHash; } );
        return NetHandleResult::Handled;
    }

    NetHandleResult HousingReplicationClient::receive( NetHousingMessage::PlotDelta, const NetMessageContext& context, BitReader& body )
    {
        using Internal = HousingReplicationInternal;
        (void)context;
        PendingOp op;
        op._kind     = OpKind::Delta;
        op._plotHash = body.readVarUint();
        op._owner    = body.readEnum<decltype( op._owner )>( Internal::kOwnerBits );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        op._funds           = body.readVarInt();
        op._nextPlacementID = body.readVarUint32();
        uint32 removedCount = 0;
        removedCount        = body.readCount( Internal::kMaxDeltaEntries );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        op._listRemovedID.reserve( removedCount );
        for ( uint32 index = 0; index < removedCount && body.hasOverflowed() == false; ++index )
            op._listRemovedID.push_back( body.readVarUint32() );
        uint32 upsertCount = 0;
        upsertCount        = body.readCount( Internal::kMaxDeltaEntries );
        if ( body.hasOverflowed() )
            return NetHandleResult::Malformed;
        op._listUpsert.reserve( upsertCount );
        for ( uint32 index = 0; index < upsertCount; ++index )
        {
            DeltaPlacement placement;
            placement._placementID = body.readVarUint32();
            placement._partHash    = body.readVarUint();
            placement._partVersion = body.readVarInt32();
            if ( Internal::readTransform( body, placement._transform ) == false )
                return NetHandleResult::Malformed;
            op._listUpsert.push_back( placement );
        }
        if ( body.readEnd() == false )
            return NetHandleResult::Malformed;
        _listOp.push_back( std::move( op ) );
        return NetHandleResult::Handled;
    }

    bool HousingReplicationClient::applyDelta( const HousingNetPlotRef& plot, const PendingOp& op )
    {
        using Internal                    = HousingReplicationInternal;
        HousingLayout&           layout   = *plot._pLayout;
        vector<HousingPlacement> listNext = layout.getPlacements();
        (void)VectorUtil::removeIf( listNext, [&op]( const HousingPlacement& placement )
        { return VectorUtil::contains( op._listRemovedID, placement._placementID ); } );
        for ( const DeltaPlacement& received : op._listUpsert )
        {
            const HousingPartDef* pPart = Internal::findPart( *layout.getCatalog(), received._partHash );
            if ( pPart == nullptr )
                return false; // 서버와 카탈로그가 다르다 — 상태를 다시 받는다
            HousingPlacement placement;
            placement._placementID = received._placementID;
            placement._partID      = pPart->_id;
            placement._partVersion = received._partVersion;
            placement._transform   = received._transform;
            const auto iter        = std::lower_bound( listNext.begin(), listNext.end(), placement._placementID,
                                                       []( const HousingPlacement& existing, uint32 id )
                   { return existing._placementID < id; } );
            if ( iter != listNext.end() && iter->_placementID == placement._placementID )
                *iter = placement;
            else
                listNext.insert( iter, placement );
        }
        return layout.applyPlacements( op._nextPlacementID, std::move( listNext ) );
    }

    void HousingReplicationClient::requestSnapshot( uint64 plotHash )
    {
        if ( _pHost == nullptr || VectorUtil::contains( _listAwaitingSnapshot, plotHash ) )
            return;
        _listAwaitingSnapshot.push_back( plotHash );
        HousingSnapshotRequestBody body;
        body._plotHash   = plotHash;
        const bool bSent = _writer.writeMessage<NetHousingMessage::Table, NetHousingMessage::SnapshotRequest>( body ) && _writer.sendMessage( *_pHost, 0 );
        if ( bSent )
            ++_stats._snapshotRequestCount;
        else
            ++_stats._sendFailedCount;
    }

    void HousingReplicationClient::setOwner( uint64 plotHash, HousingPlotOwner owner )
    {
        OwnerEntry* pEntry = VectorUtil::findIf( _listOwner, [plotHash]( const OwnerEntry& entry )
        { return entry._plotHash == plotHash; } );
        if ( pEntry == nullptr )
        {
            _listOwner.push_back( OwnerEntry{ plotHash, owner } );
            return;
        }
        pEntry->_owner = owner;
    }

    void HousingReplicationClient::update( const vector<HousingNetPlotRef>& listPlot, vector<HousingEditReply>& outListReply )
    {
        using Internal = HousingReplicationInternal;
        // 상태 · 델타 — 받은 순서대로. 목록에 없는 부지의 것은 순서대로 들고 있는다(그 부지의 것은 모두 남으므로 순서가 지켜진다).
        vector<PendingOp> listKept;
        for ( PendingOp& op : _listOp )
        {
            const optional<size_t> plotIndex = Internal::findPlotIndex( listPlot, op._plotHash );
            if ( plotIndex.has_value() == false )
            {
                listKept.push_back( std::move( op ) );
                continue;
            }
            const HousingNetPlotRef& plot = listPlot[*plotIndex];
            if ( op._kind == OpKind::Snapshot )
            {
                Archive archive( op._snapshotBytes.data(), op._snapshotBytes.size() );
                if ( plot._pLayout->readState( archive ) == false )
                {
                    // 서버와 카탈로그 · 규칙이 다르다(다른 콘텐츠) — 다시 받아도 같으므로 요청하지 않고 지금 상태를 그대로 둔다.
                    SW_LOG_ERROR_ID( "HousingReplication.PlotStateRefused", "Housing client: the server state of plot '%#' does not fit this client's catalog or rules - the plot keeps its last state",
                                     plot._pLayout->getSettings()._plotID.c_str() );
                    continue;
                }
                (void)VectorUtil::removeValue( _listAwaitingSnapshot, op._plotHash );
                ++_stats._snapshotAppliedCount;
            }
            else
            {
                if ( VectorUtil::contains( _listAwaitingSnapshot, op._plotHash ) )
                    continue; // 다시 맞추기를 기다린다 — 그 뒤의 델타는 상태에 이미 들어 있다
                if ( applyDelta( plot, op ) == false )
                {
                    SW_LOG_WARNING_ID( "HousingReplication.DeltaRefused", "Housing client: a delta of plot '%#' does not apply - asking the server for the whole state",
                                       plot._pLayout->getSettings()._plotID.c_str() );
                    requestSnapshot( op._plotHash );
                    continue;
                }
                ++_stats._deltaAppliedCount;
            }
            plot._pWallet->setBalance( plot._pLayout->getCatalog()->getRoot()._currency, op._funds );
            setOwner( op._plotHash, op._owner );
        }
        _listOp = std::move( listKept );
        // 판정 — 모르는 부지의 답은 버린다(그 부지에 요청을 보낸 적이 없다).
        for ( const ReceivedReply& received : _listReply )
        {
            const optional<size_t> plotIndex = Internal::findPlotIndex( listPlot, received._plotHash );
            if ( plotIndex.has_value() == false )
                continue;
            HousingEditReply reply;
            reply._plotIndex   = *plotIndex;
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

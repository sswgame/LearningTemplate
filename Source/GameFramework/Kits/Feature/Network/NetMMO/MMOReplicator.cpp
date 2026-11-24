#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/pair.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/NetTypes.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneStreamingSubsystem.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

#include <algorithm>
#include <limits>

namespace sw
{
    namespace
    {
        struct MMOReplicatorInternal
        {
            static MMOEntityEnterHeader makeEnterHeader( const MMOEntity& entity )
            {
                MMOEntityEnterHeader header;
                header._header._entityID = entity._entityID;
                header._header._position = entity._position;
                header._typeID           = entity._typeID;
                header._staticID         = entity._staticID;
                return header;
            }

            /** @brief 엔티티 하나를 씁니다 — 머리는 칸 선언(새로 보임은 `MMOEntityEnterHeader`, 갱신은 `MMOEntityUpdateHeader`), 뒤에 상태 덩어리. 머리가 범위 밖이면 false 이고 쓰다 만 비트가 남습니다. */
            [[nodiscard]] static bool writeEntity( BitWriter& writer, const MMOEntity& entity, bool bWithType )
            {
                const MMOEntityEnterHeader header   = makeEnterHeader( entity );
                const bool                 bWritten = bWithType ? ReplicatedLayout::writeFull( header, writer ) : ReplicatedLayout::writeFull( header._header, writer );
                if ( bWritten == false )
                    return false;
                writer.writeBlob( entity._listState.data(), static_cast<int32>( entity._listState.size() ) );
                return true;
            }

            [[nodiscard]] static bool readEntity( BitReader& reader, MMOEntity& outEntity, bool bWithType )
            {
                MMOEntityEnterHeader  header;
                const FixedReadResult result = bWithType ? ReplicatedLayout::readFull( reader, header, nullptr ) : ReplicatedLayout::readFull( reader, header._header, nullptr );
                if ( result != FixedReadResult::Ok )
                    return false;
                outEntity._entityID = header._header._entityID;
                outEntity._position = header._header._position;
                if ( bWithType )
                {
                    outEntity._typeID   = header._typeID;
                    outEntity._staticID = header._staticID;
                }
                return reader.readBlob( outEntity._listState, NetMMOMessage::kMaxStateBytes ) && reader.hasOverflowed() == false;
            }

            /**
             * @brief 엔티티 하나를 메시지에 써 보고 예산 안이면 남깁니다. 넘거나 쓸 수 없으면 쓰기 전 길이로 되돌리고 false 입니다.
             * @details 크기를 미리 세는 함수를 두지 않는다 — 써 본 길이가 곧 크기라 쓰기와 어긋날 수 없다(언리얼 `FBitWriterMark`).
             */
            [[nodiscard]] static bool tryWriteEntity( BitWriter& writer, NetSendBudget& budget, const MMOEntity& entity, bool bWithType )
            {
                const int32 mark = writer.getBitCount();
                writer.writeBool( true );
                if ( writeEntity( writer, entity, bWithType ) && budget.tryReserveBits( writer.getBitCount() - mark ) )
                    return true;
                writer.truncate( mark );
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // MMOReplicator
    // ------------------------------------------------------------------------------
    MMOReplicator::MMOReplicator()
        : _mapEntity{}
        , _setAlwaysRelevant{}
        , _listObserver{}
        , _interest{}
        , _settings{}
        , _defaultPolicy{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _sentUpdateCount{ 0 }
        , _oversizedEntityCount{ 0 }
        , _outOfRangeEntityCount{ 0 }
        , _headerScratch{}
        , _parallel{}
        , _observerScratch{}
        , _tickDeltaTime{ 0.0f }
        , _cellLoadingRadius{ 0.0f }
        , _tick{ 0 }
        , _pRangeObserver{ nullptr }
    {
    }

    bool MMOReplicator::initialize( INetPeers* pHost, const MMOReplicatorConfig& settings, const StreamingCellGrid& cellGrid, const IInterestPolicy* pPolicy )
    {
        // 실패하면 호스트 없는 복제기로 남기고 지난 설정을 그대로 둔다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.MMOReplicatorRefusedStartSettings", "MMOReplicator: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        // 기본값 격자(셀 크기 0)는 `StreamingCellGrid::tryCreate` 를 지나지 않은 것이다 — 칸 크기를 대신 정하지 않는다.
        if ( ( cellGrid.getCellSize() > 0.0f ) == false )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.MMOReplicatorRefusedStartWorld", "MMOReplicator: refused to start - world cell grid was not created (cell size %#)", cellGrid.getCellSize() );
            return false;
        }
        if ( settings._leaveRadius > cellGrid.getLoadingRadius() )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.MMOReplicatorRefusedStartLeave", "MMOReplicator: refused to start - leave radius %# is larger than the world loading radius %# (visible entities could sit in unloaded cells)",
                             settings._leaveRadius, cellGrid.getLoadingRadius() );
            return false;
        }
        // 반경 관계(나가는 >= 들어오는)는 관심 영역 격자 한 곳이 거절한다 — 클라이언트-서버 키트의 관심 영역도 같은 규칙이다. 칸은 월드 격자의 셀 크기다.
        const expected<void> interest = _interest.initialize( cellGrid.getCellSize(), settings._enterRadius, settings._leaveRadius );
        if ( interest.has_value() == false )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.MMOReplicatorRefusedStartEnter", "MMOReplicator: refused to start - %# (enter %#, leave %#)", interest.error().message(), settings._enterRadius, settings._leaveRadius );
            return false;
        }
        _pHost             = pHost;
        _settings          = settings;
        _cellLoadingRadius = cellGrid.getLoadingRadius();
        _pPolicy           = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _mapEntity.clear();
        _listObserver.clear();
        _sentUpdateCount = 0;
        _tick            = 0;
        return true;
    }

    void MMOReplicator::setEntity( const MMOEntity& entity )
    {
        if ( static_cast<int32>( entity._listState.size() ) > NetMMOMessage::kMaxStateBytes )
        {
            if ( _oversizedEntityCount == 0 )
                SW_LOG_ERROR_ID( "MMOReplicator.MMOReplicatorEntityStateBytes", "MMOReplicator: entity %# has %# state bytes, more than the limit %# - ignored (a state schema defect: shrink or quantize the state; further ones are only counted)", entity._entityID,
                                 static_cast<int32>( entity._listState.size() ), NetMMOMessage::kMaxStateBytes );
            ++_oversizedEntityCount;
            return;
        }
        // 머리는 보낼 때 다시 쓴다 — 여기서 받아 두면 보내는 쪽(여러 스레드)이 범위 밖 머리를 만나지 않는다.
        _headerScratch.clear();
        if ( ReplicatedLayout::writeFull( MMOReplicatorInternal::makeEnterHeader( entity ), _headerScratch ) == false )
        {
            if ( _outOfRangeEntityCount == 0 )
                SW_LOG_ERROR_ID( "MMOReplicator.EntityHeaderOutOfRange", "MMOReplicator: entity %# at (%#, %#, %#) is outside the replicated header range - ignored (further ones are only counted)", entity._entityID,
                                 entity._position._x, entity._position._y, entity._position._z );
            ++_outOfRangeEntityCount;
            return;
        }
        _mapEntity[entity._entityID] = entity;
        _interest.setPoint( entity._entityID, entity._position );
        if ( entity._bAlwaysRelevant )
            _setAlwaysRelevant.insert( entity._entityID );
        else
            _setAlwaysRelevant.erase( entity._entityID );
    }

    void MMOReplicator::removeEntity( uint32 entityID )
    {
        _mapEntity.erase( entityID );
        _interest.removePoint( entityID );
        _setAlwaysRelevant.erase( entityID );
    }

    void MMOReplicator::setObserver( int32 connectionID, uint32 entityID )
    {
        if ( connectionID < 0 )
            return;
        if ( connectionID >= static_cast<int32>( _listObserver.size() ) )
            _listObserver.resize( static_cast<size_t>( connectionID + 1 ) );
        Observer& observer = _listObserver[static_cast<size_t>( connectionID )];
        if ( observer._bActive == SW_FALSE )
        {
            observer = Observer{};
            observer._updateResult.initialize( static_cast<int32>( NetMMOMessage::kMaxUnconfirmedTicks ) );
            observer._streamingRadius = _cellLoadingRadius;
        }
        observer._entityID = entityID;
        observer._bActive  = SW_TRUE;
    }

    bool MMOReplicator::setObserverStreaming( int32 connectionID, float32 loadingRadius, StreamingSourcePriority priority )
    {
        const bool bKnown = connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() ) && _listObserver[static_cast<size_t>( connectionID )]._bActive;
        if ( bKnown == false )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.ObserverStreamingUnknown", "MMOReplicator: refused streaming source for connection %# - no observer", connectionID );
            return false;
        }
        // 나가는 반경보다 작으면 보이는 엔티티가 서버에 오르지 않은 셀에 있을 수 있다 — `initialize` 의 격자 규칙을 관찰자마다 지킨다.
        // 상한을 넘으면 서버가 올리는 셀이 끝없이 커진다 — 상한이 없으면(0) 바꾸지 못한다(기본값으로 메우지 않는다).
        if ( ( loadingRadius <= _settings._maxStreamingRadius ) == false )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.ObserverStreamingTooLarge", "MMOReplicator: refused streaming radius %# for connection %# - above the configured maximum %# (_maxStreamingRadius, 0 = not configured)",
                             loadingRadius, connectionID, _settings._maxStreamingRadius );
            return false;
        }
        if ( MathUtil::isFinite( loadingRadius ) == false || loadingRadius < _settings._leaveRadius )
        {
            SW_LOG_ERROR_ID( "MMOReplicator.ObserverStreamingRadius", "MMOReplicator: refused streaming radius %# for connection %# - it must be finite and at least the leave radius %#",
                             loadingRadius, connectionID, _settings._leaveRadius );
            return false;
        }
        Observer& observer          = _listObserver[static_cast<size_t>( connectionID )];
        observer._streamingRadius   = loadingRadius;
        observer._streamingPriority = priority;
        return true;
    }

    void MMOReplicator::removeObserver( int32 connectionID )
    {
        if ( connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() ) )
            _listObserver[static_cast<size_t>( connectionID )] = Observer{};
    }

    NetHandleResult MMOReplicator::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        (void)body;
        return NetHandleResult::NotMine;
    }

    void MMOReplicator::onDeliveryNotification( const NetDeliveryNotification& notification )
    {
        // 메시지 펌프(게임 스레드)에서 불린다. 같은 스레드에서 차례로 실행되므로 `update` 의 병렬 본문과 겹치지 않는다.
        const bool bUnknownConnection = notification._connectionID < 0 || notification._connectionID >= static_cast<int32>( _listObserver.size() );
        if ( bUnknownConnection )
            return;
        Observer& observer = _listObserver[static_cast<size_t>( notification._connectionID )];
        if ( observer._bActive == SW_FALSE )
            return;
        // 테이블 크기 이상 오래된 틱은 적지 않는다. 적으면 링 버퍼에서 새 틱의 슬롯을 덮어쓰게 된다(그 갱신은 `resolveInFlightUpdates` 가 이미 잃은 것으로 정리했다).
        const uint32 tick     = notification._tag;
        const bool   bTooOld  = _tick - tick >= NetMMOMessage::kMaxUnconfirmedTicks;
        const bool   bFromNow = tick > _tick; // 이 복제기가 단 값이 아니다(재초기화 전의 통지)
        if ( bTooOld || bFromNow )
            return;
        observer._updateResult.acquire( tick ) = notification._result == NetDeliveryResult::Delivered ? SW_TRUE : SW_FALSE;
    }

    void MMOReplicator::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        removeObserver( connectionID );
    }

    int32 MMOReplicator::getVisibleCount( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() )
                 ? static_cast<int32>( _listObserver[static_cast<size_t>( connectionID )]._mapVisible.size() )
                 : 0;
    }

    void MMOReplicator::collectObserverPositions( vector<float3>& outListPosition ) const
    {
        outListPosition.clear();
        for ( const Observer& observer : _listObserver )
        {
            if ( observer._bActive == SW_FALSE )
                continue;
            const auto entityIter = _mapEntity.find( observer._entityID );
            if ( entityIter != _mapEntity.end() )
                outListPosition.push_back( entityIter->second._position );
        }
    }

    void MMOReplicator::collectStreamingSources( vector<StreamingSource>& outListSource ) const
    {
        outListSource.clear();
        for ( const Observer& observer : _listObserver )
        {
            if ( observer._bActive == SW_FALSE )
                continue;
            const auto entityIter = _mapEntity.find( observer._entityID );
            if ( entityIter != _mapEntity.end() )
                outListSource.push_back( StreamingSource{ entityIter->second._position, observer._streamingRadius, observer._streamingPriority } );
        }
    }

    bool MMOReplicator::initializeWithStreaming( INetPeers* pHost, const MMOReplicatorConfig& settings, const SceneStreamingSubsystem& streaming, const IInterestPolicy* pPolicy )
    {
        return initialize( pHost, settings, streaming.getGrid(), pPolicy );
    }

    void MMOReplicator::updateAndFeedStreaming( float32 deltaTime, SceneStreamingSubsystem& streaming )
    {
        update( deltaTime );
        feedStreamingSources( streaming );
    }

    void MMOReplicator::feedStreamingSources( SceneStreamingSubsystem& streaming )
    {
        collectStreamingSources( _listScratchSource );
        // 반경은 격자 로딩 반경이거나 `setObserverStreaming` 이 검사한 값이라 거절될 수 없다.
        const bool bAccepted = streaming.setExternalSources( span<const StreamingSource>( _listScratchSource.data(), _listScratchSource.size() ) );
        SW_ASSERT( bAccepted );
        (void)bAccepted;
    }

    void MMOReplicator::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold )
    {
        _parallel.setTaskManager( pTaskManager, serialThreshold );
    }

    void MMOReplicator::update( float32 deltaTime )
    {
        if ( _pHost == nullptr )
            return;
        _tickDeltaTime = deltaTime;
        // 관찰자마다 이번 틱의 예산 — 설정값과 그 연결의 지금 송신 속도(`NetSendThrottle` 이 조절한다)에서 나온 할당량 중 작은 값. 호스트 락은 나눠 돌기 전에 여기서만 잡는다.
        for ( size_t index = 0; index < _listObserver.size(); ++index )
        {
            Observer& observer = _listObserver[index];
            if ( observer._bActive == SW_FALSE )
                continue;
            NetConnectionStats stats;
            const int32        bytesPerSecond = _pHost->getConnectionStats( static_cast<int32>( index ), stats ) ? static_cast<int32>( stats._sendBytesPerSecond ) : 0;
            observer._updateBudgetBytes       = NetSendBudget::computeTickBudget( _settings._updateBudgetBytes, bytesPerSecond, static_cast<float64>( deltaTime ) );
        }
        ++_tick;
        _observerScratch.prepare( _parallel );
        _pRangeObserver = _listObserver.data();
        _parallel.run( static_cast<uint32>( _listObserver.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &MMOReplicator::updateObserverRange, this ) );
        for ( ObserverScratch& scratch : _observerScratch.getSlots() )
        {
            _sentUpdateCount += scratch._sentUpdateCount;
            scratch._sentUpdateCount = 0;
        }
    }

    void MMOReplicator::updateObserverRange( uint32 start, uint32 end )
    {
        ObserverScratch& scratch = _observerScratch.acquire( _parallel );
        for ( uint32 index = start; index < end; ++index )
        {
            Observer& observer = _pRangeObserver[index];
            if ( observer._bActive )
                updateObserver( static_cast<int32>( index ), observer, _tickDeltaTime, scratch );
        }
    }

    const MMOEntity& MMOReplicator::getEntity( uint32 entityID ) const
    {
        // 병렬 본문에서 부른다. `operator[]` 는 없으면 항목을 넣으므로(쓰기) 쓰지 않는다. 보이지 않게 된 단계가 사라진 엔티티를 이미 뺐으므로 보이는 엔티티는 항상 존재한다.
        const auto entityIter = _mapEntity.find( entityID );
        SW_ASSERT( entityIter != _mapEntity.end() );
        return entityIter->second;
    }

    void MMOReplicator::updateObserver( int32 connectionID, Observer& observer, float32 deltaTime, ObserverScratch& scratch )
    {
        // 관찰자의 위치는 엔티티 테이블에서 읽는다. 격자와 테이블은 setEntity 와 removeEntity 가 함께 갱신한다.
        const auto observerIter = _mapEntity.find( observer._entityID );
        if ( observerIter == _mapEntity.end() )
            return;
        const float3 center = observerIter->second._position;

        // 1) 보이지 않게 됨: 사라졌거나 나가는 반경 밖으로 간 엔티티이다. 항상 보이는 엔티티는 제외한다.
        vector<uint32>& listLeave = scratch._listLeave;
        listLeave.clear();
        for ( const auto& visible : observer._mapVisible )
        {
            const auto entityIter = _mapEntity.find( visible.first );
            if ( entityIter == _mapEntity.end() )
            {
                listLeave.push_back( visible.first );
                continue;
            }
            const bool bAlways = entityIter->second._bAlwaysRelevant && _pPolicy->isAlwaysRelevant( connectionID, entityIter->second );
            if ( bAlways == false && _interest.isOutsideLeaveRadius( center, entityIter->second._position ) )
                listLeave.push_back( visible.first );
        }
        VectorUtil::sort( listLeave );
        const size_t pendingLeaveIndex = sendLeaves( connectionID, observer, scratch );

        // 2) 새로 보임 · 3) 갱신
        sendEnters( connectionID, observer, center, scratch );
        sendUpdates( connectionID, observer, center, pendingLeaveIndex, deltaTime, scratch );
    }

    void MMOReplicator::sendEnters( int32 connectionID, Observer& observer, const float3& center, ObserverScratch& scratch )
    {
        // 2) 새로 보임: 들어오는 반경 안의 엔티티와 항상 보임 후보 가운데 정책이 고른 것이다. 가까운 것부터 처리하고 틱마다 상한을 둔다.
        vector<SlotHandle>& listNear = scratch._listNear;
        _interest.queryEntering( center, listNear );
        // 후보 집합만 묻는다 — 관찰자 × 월드 전체 엔티티가 아니라 관찰자 × 후보 수이다(언리얼 Replication Graph 의 AlwaysRelevant 노드가 미리 만든 목록을 주는 것과 같다).
        for ( const uint32 entityID : _setAlwaysRelevant )
        {
            if ( _pPolicy->isAlwaysRelevant( connectionID, getEntity( entityID ) ) )
                listNear.push_back( NetInterestGrid::makeKey( entityID ) );
        }
        // 거리는 한 번만 계산하고 (거리, id) 로 정렬한다. 비교마다 해시를 찾지 않으려는 것이다. 이미 보이는 엔티티는 뺀다.
        vector<pair<float32, uint32>>& listRank = scratch._listRank;
        listRank.clear();
        for ( const SlotHandle nearKey : listNear )
        {
            const uint32 entityID = NetInterestGrid::getEntityID( nearKey );
            if ( observer._mapVisible.find( entityID ) != observer._mapVisible.end() )
                continue;
            const auto entityIter = _mapEntity.find( entityID );
            if ( entityIter != _mapEntity.end() )
                listRank.emplace_back( float3::getHorizontalDistance( center, entityIter->second._position ), entityID );
        }
        VectorUtil::sortUnique( listRank ); // 반경 안의 엔티티와 항상 보이는 엔티티가 겹친 항목을 제거한다
        // 신뢰 메시지 하나에 묶어 보낸다. 엔티티마다 신뢰 창 슬롯과 헤더를 쓰지 않으려는 것이며, Core 가 64 KB까지 조각으로 나눠 전송한다. 전송이 받아들여져야 보이는 목록에 넣는다.
        vector<uint32>& listEnter = scratch._listEnter;
        listEnter.clear();
        {
            BitWriter& writer = scratch._messageWriter.beginMessage<NetMMOMessage::Table, NetMMOMessage::Enter>();
            writer.writeVarUint( _tick );
            NetSendBudget budget( NetConnection::kMaxReliableMessageSize, NetConnection::kMaxReliableMessageSize );
            budget.reserveBits( writer.getBitCount() + 1 ); // 헤더와 끝 표시
            for ( const auto& ranked : listRank )
            {
                if ( static_cast<int32>( listEnter.size() ) >= _settings._maxEnterPerTick )
                    break; // 가까운 것부터 처리했으므로 나머지는 다음 틱에 보낸다
                const auto entityIter = _mapEntity.find( ranked.second );
                if ( entityIter == _mapEntity.end() )
                    continue;
                if ( MMOReplicatorInternal::tryWriteEntity( writer, budget, entityIter->second, true ) == false )
                    break; // 64 KB에 도달했으므로 나머지는 다음 틱에 보낸다(머리 범위는 `setEntity` 가 받을 때 봤다)
                listEnter.push_back( ranked.second );
            }
            writer.writeBool( false );
            if ( listEnter.empty() == false && scratch._messageWriter.trySendMessage( *_pHost, connectionID ) )
            {
                for ( const uint32 entityID : listEnter )
                {
                    VisibleEntry& entry  = observer._mapVisible[entityID];
                    entry._listSentState = getEntity( entityID )._listState; // 신뢰 전송이므로 확인된 것으로 둔다
                    entry._bInFlight     = SW_FALSE;
                }
            }
            // 전송이 거절됐으면(신뢰 창이 찼다) 아무것도 보이는 목록에 넣지 않고, 다음 틱에 다시 고른다.
        }
    }

    void MMOReplicator::sendUpdates( int32 connectionID, Observer& observer, const float3& center, size_t pendingLeaveIndex, float32 deltaTime,
                                     ObserverScratch& scratch )
    {
        const vector<uint32>& listLeave = scratch._listLeave;
        // 3) 갱신: 우선도를 쌓고 예산 안에서 큰 것부터 보낸다. 보이지 않게 됨을 아직 못 보낸 엔티티는 건너뛴다. 신뢰 창이 찼기 때문이며, 사라진 엔티티일 수 있다.
        resolveInFlightUpdates( observer );
        NetPrioritizer& prioritizer = observer._prioritizer;
        for ( const auto& visible : observer._mapVisible )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), visible.first ) )
                continue;
            const MMOEntity&    entity   = getEntity( visible.first );
            const float32       distance = float3::getHorizontalDistance( center, entity._position );
            const VisibleEntry& entry    = visible.second;
            // 전송 중인 갱신이 있으면 그것과 비교한다. 확인을 기다리는 동안 같은 상태를 "바뀜" 으로 가속하지 않으려는 것이다. 잃으면 확인된 상태(오래된 것)와 비교하게 된다.
            const vector<uint8>& reference = entry._bInFlight == SW_TRUE ? entry._listInFlightState : entry._listSentState;
            const bool           bChanged  = entity._listState != reference;
            prioritizer.accumulate( visible.first, _pPolicy->computePriority( connectionID, entity, distance ) * ( bChanged ? _settings._changedBoost : 1.0f ), deltaTime );
        }
        vector<uint32>& listOrder = scratch._listOrder;
        prioritizer.collectOrder( listOrder );
        BitWriter& writer = scratch._messageWriter.beginMessage<NetMMOMessage::Table, NetMMOMessage::Update>();
        writer.writeVarUint( _tick );
        NetSendBudget budget( observer._updateBudgetBytes );
        budget.reserveBits( writer.getBitCount() + 1 ); // 헤더와 끝 표시
        vector<uint32>& listSent = scratch._listSent;
        listSent.clear();
        for ( const uint32 entityID : listOrder )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), entityID ) )
                continue;
            const MMOEntity& entity = getEntity( entityID );
            if ( MMOReplicatorInternal::tryWriteEntity( writer, budget, entity, false ) == false )
                continue;
            listSent.push_back( entityID );
        }
        writer.writeBool( false );
        if ( listSent.empty() )
            return;
        if ( scratch._messageWriter.sendNotifiedMessage( *_pHost, connectionID, _tick ) == false )
            return; // 연결이 없다 — 보낸 것으로 적지 않으므로 우선도가 남는다
        for ( const uint32 entityID : listSent )
        {
            // 확인될 때까지 보낸 상태를 따로 보관한다. 잃으면 확인된 상태(`_listSentState`)가 오래된 것이므로 "바뀜" 가중치가 다시 적용된다.
            prioritizer.markSentUnconfirmed( entityID, _tick );
            VisibleEntry& entry      = observer._mapVisible[entityID];
            entry._listInFlightState = getEntity( entityID )._listState;
            entry._inFlightTick      = _tick;
            entry._bInFlight         = SW_TRUE;
        }
        scratch._sentUpdateCount += listSent.size();
    }

    void MMOReplicator::resolveInFlightUpdates( Observer& observer ) const
    {
        // 엔티티마다 독립적으로 판정하므로 맵 순회 순서와 상관없이 결과(우선도와 상태)가 같다.
        for ( auto& visible : observer._mapVisible )
        {
            VisibleEntry& entry = visible.second;
            if ( entry._bInFlight == SW_FALSE )
                continue;
            const uint8* pResult  = observer._updateResult.find( entry._inFlightTick );
            const bool   bExpired = _tick - entry._inFlightTick >= NetMMOMessage::kMaxUnconfirmedTicks;
            if ( pResult == nullptr && bExpired == false )
                continue; // 아직 판정이 없다(패킷이 오가는 중이다)
            const bool bDelivered = pResult != nullptr && *pResult == SW_TRUE;
            if ( bDelivered )
                entry._listSentState.swap( entry._listInFlightState );
            entry._listInFlightState.clear();
            entry._bInFlight = SW_FALSE;
            observer._prioritizer.resolveSend( visible.first, bDelivered );
        }
    }

    size_t MMOReplicator::sendLeaves( int32 connectionID, Observer& observer, ObserverScratch& scratch )
    {
        // 조각나지 않는 크기(1 KB)로 쪼갠다. 하나로 묶어도 신뢰 창에서 차지하는 슬롯 수는 같고, 나눠 두면 창이 찼을 때도 앞쪽 메시지는 나가기 때문이다.
        // 보낸 엔티티만 보이는 목록에서 뺀다. 그래야 클라이언트에 사라지지 않는 엔티티가 남지 않는다.
        const vector<uint32>& listLeave = scratch._listLeave;
        size_t                sentCount = 0;
        while ( sentCount < listLeave.size() )
        {
            BitWriter&    writer = scratch._messageWriter.beginMessage<NetMMOMessage::Table, NetMMOMessage::Leave>();
            NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
            budget.reserveBits( writer.getBitCount() + BitMath::computeVarUintBits( listLeave.size() - sentCount ) );
            size_t count = 0;
            while ( sentCount + count < listLeave.size() && budget.tryReserveBits( BitMath::computeVarUintBits( listLeave[sentCount + count] ) ) )
            {
                ++count;
            }
            writer.writeVarUint( count );
            for ( size_t index = 0; index < count; ++index )
            {
                writer.writeVarUint( listLeave[sentCount + index] );
            }
            if ( scratch._messageWriter.trySendMessage( *_pHost, connectionID ) == false )
                break; // 신뢰 창이 찼다. 남은 엔티티는 보이는 채로 두고 다음 틱에 보낸다
            for ( size_t index = 0; index < count; ++index )
            {
                observer._mapVisible.erase( listLeave[sentCount + index] );
                observer._prioritizer.remove( listLeave[sentCount + index] );
            }
            sentCount += count;
        }
        return sentCount;
    }

    // ------------------------------------------------------------------------------
    // MMOClientView
    // ------------------------------------------------------------------------------
    MMOSceneClientWorld::MMOSceneClientWorld( const Scene& scene )
        : _scene{ scene }
        , _mapFileIDToObjectID{}
        , _cachedSerial{ std::numeric_limits<uint64>::max() }
    {
    }

    bool MMOSceneClientWorld::isPositionStreamed( const float3& position ) const
    {
        const SceneStreamingSubsystem& streaming = _scene.getObjectManager()->getSubsystem<SceneStreamingSubsystem>();
        return streaming.isStreaming() == false || streaming.isCellStreamingCompleted( streaming.getGrid().computeCell( position ) );
    }

    uint64 MMOSceneClientWorld::findStaticObject( uint64 staticID ) const
    {
        const GameObjectManager&       manager   = *_scene.getObjectManager();
        const SceneStreamingSubsystem& streaming = manager.getSubsystem<SceneStreamingSubsystem>();
        if ( streaming.getChangeSerial() != _cachedSerial )
        {
            // 셀이 오르내릴 때만 다시 만든다 — 보류한 엔티티마다 씬 전체 표를 훑지 않게.
            ObjectSavedIDMap mapSavedID;
            _scene.collectSavedIDMap( mapSavedID );
            _mapFileIDToObjectID.clear();
            for ( const auto& [objectID, fileID] : mapSavedID )
            {
                _mapFileIDToObjectID[fileID] = objectID;
            }
            _cachedSerial = streaming.getChangeSerial();
        }
        const auto idIter = _mapFileIDToObjectID.find( staticID );
        if ( idIter == _mapFileIDToObjectID.end() )
            return 0;
        // 표를 만든 뒤 지워졌거나 아직 드러나지 않은(짓는 중인 셀) 오브젝트는 없는 것이다.
        const GameObject* pObject = manager.findGameObjectByID( idIter->second );
        return pObject != nullptr && pObject->isPendingDestroy() == false ? idIter->second : 0;
    }

    void MMOClientView::onConnectionOpened( int32 connectionID )
    {
        (void)connectionID;
        _mapEntity.clear();
        _mapBoundObject.clear();
        _heldCount = 0;
    }

    bool MMOClientView::setWorld( const INetClientWorld& world, const MMOClientViewConfig& settings )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "MMOClientView.RefusedWorldSettings", "MMOClientView: refused world - settings '%#'", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        _pWorld   = &world;
        return true;
    }

    void MMOClientView::detachWorld()
    {
        _pWorld = nullptr;
        // 기다릴 셀이 없다 — 동적 엔티티는 월드 없는 보기처럼 곧바로 보인다. 정적 엔티티는 묶을 곳이 생기거나 사라짐이 올 때까지 보류한다.
        collectHeldIDs();
        for ( const uint32 entityID : _listScratchHeld )
        {
            ClientEntity& entry = _mapEntity.find( entityID )->second;
            if ( entry._entity._staticID == 0 )
                makeVisible( entityID, entry, 0 );
        }
    }

    void MMOClientView::update( float32 deltaSeconds )
    {
        if ( _pWorld == nullptr || _heldCount == 0 )
            return;
        collectHeldIDs();
        for ( const uint32 entityID : _listScratchHeld )
        {
            const auto    entityIter   = _mapEntity.find( entityID );
            ClientEntity& entry        = entityIter->second;
            const float32 previousHeld = entry._heldSeconds;
            entry._heldSeconds += deltaSeconds;
            uint64 staticObjectID = 0;
            if ( tryResolve( entityID, entry._entity, staticObjectID ) )
            {
                makeVisible( entityID, entry, staticObjectID );
                continue;
            }
            const bool bCrossedTimeout = previousHeld < _settings._cellHoldTimeoutSeconds && entry._heldSeconds >= _settings._cellHoldTimeoutSeconds;
            if ( bCrossedTimeout == false )
                continue;
            if ( entry._entity._staticID != 0 && _pWorld->findStaticObject( entry._entity._staticID ) != 0 )
            {
                // 서로 다른 엔티티가 한 셀 오브젝트를 가리킨다 — 서버 데이터 결함이다. 두 번째 묶기는 받지 않고 앞 엔티티가 풀릴 때까지 보류한다(한 오브젝트에 두 주인 없음).
                SW_LOG_ERROR_ID( "MMOClientView.StaticObjectAlreadyBound", "MMOClientView: entity %# (static id %#) refused - its cell object is already bound to another entity", entityID,
                                 entry._entity._staticID );
                continue;
            }
            if ( entry._entity._staticID != 0 )
            {
                // 묶을 셀 오브젝트가 없다 — 스폰하면 셀이 오를 때 둘이 된다(언리얼도 풀지 못한 정적 NetGUID 는 스폰하지 않는다). 사라짐이 올 때까지 보류한다.
                SW_LOG_WARNING_ID( "MMOClientView.StaticEntityUnresolved", "MMOClientView: entity %# (static id %#) has no loaded cell object after %# s - still held", entityID,
                                   entry._entity._staticID, _settings._cellHoldTimeoutSeconds );
                continue;
            }
            // 셀이 끝내 오르지 않는다 — 서버의 나가는 반경이 이 클라이언트의 로딩 반경보다 큰 설정 결함일 수 있다. 영원히 숨기지 않고 보인다.
            SW_LOG_WARNING_ID( "MMOClientView.CellHoldTimedOut", "MMOClientView: entity %# waited %# s for its cell at (%#, %#) - shown without it", entityID,
                               _settings._cellHoldTimeoutSeconds, entry._entity._position._x, entry._entity._position._z );
            makeVisible( entityID, entry, 0 );
        }
    }

    void MMOClientView::collectHeldIDs()
    {
        // 놓는 순서가 해시 순서에 달리지 않게 엔티티 id 순으로 훑는다.
        _listScratchHeld.clear();
        for ( const auto& entity : _mapEntity )
        {
            if ( entity.second._state == ClientEntityState::Held )
                _listScratchHeld.push_back( entity.first );
        }
        VectorUtil::sort( _listScratchHeld );
    }

    bool MMOClientView::tryResolve( uint32 entityID, const MMOEntity& entity, uint64& outStaticObjectID ) const
    {
        outStaticObjectID = 0;
        if ( _pWorld == nullptr )
            return entity._staticID == 0; // 월드 없는 보기: 동적은 곧바로, 정적은 묶을 곳이 없다
        if ( entity._staticID == 0 )
            return _pWorld->isPositionStreamed( entity._position );
        const uint64 staticObjectID = _pWorld->findStaticObject( entity._staticID );
        if ( staticObjectID == 0 )
            return false;
        const auto boundIter = _mapBoundObject.find( staticObjectID );
        if ( boundIter != _mapBoundObject.end() && boundIter->second != entityID )
            return false; // 다른 엔티티가 이미 묶었다 — 두 번째 묶기는 받지 않는다
        outStaticObjectID = staticObjectID;
        return true;
    }

    void MMOClientView::makeVisible( uint32 entityID, ClientEntity& entry, uint64 staticObjectID )
    {
        if ( staticObjectID != 0 )
            _mapBoundObject[staticObjectID] = entityID; // `tryResolve` 가 다른 주인이 없음을 봤다
        entry._state          = ClientEntityState::Visible;
        entry._staticObjectID = staticObjectID;
        _heldCount -= 1;
        _eventBuffer.push( MMOClientEvent{ entityID, staticObjectID != 0 ? MMOClientEvent::Kind::Bound : MMOClientEvent::Kind::Entered, staticObjectID } );
    }

    NetHandleResult MMOClientView::receive( NetMMOMessage::Enter, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        BitReader&        reader = body;
        const uint32      tick   = reader.readVarUint32();
        vector<MMOEntity> listEntered; // 끝까지 읽고 남는 비트가 없을 때만 적용한다 — 깨진 메시지의 앞부분이 보이는 목록에 들지 않게
        while ( reader.hasOverflowed() == false && reader.readBool() )
        {
            MMOEntity entity;
            if ( MMOReplicatorInternal::readEntity( reader, entity, true ) == false )
                return NetHandleResult::Malformed;
            listEntered.push_back( std::move( entity ) );
        }
        if ( reader.readEnd() == false )
            return NetHandleResult::Malformed;
        for ( MMOEntity& entity : listEntered )
        {
            const uint32 entityID   = entity._entityID;
            const auto   entityIter = _mapEntity.find( entityID );
            if ( entityIter != _mapEntity.end() )
            {
                // 이미 아는 엔티티의 새로 보임(순서가 뒤바뀐 재전송 등)은 상태만 새로 한다 — 한 id 는 보류 · 보임 중 하나이고 Entered · Bound 는 한 번이다.
                ClientEntity& known = entityIter->second;
                if ( tick < known._tick )
                {
                    _staleUpdateCount.add();
                    continue;
                }
                known._tick              = tick;
                known._entity._position  = entity._position;
                known._entity._listState = std::move( entity._listState );
                if ( known._state == ClientEntityState::Visible )
                    _eventBuffer.push( MMOClientEvent{ entityID, MMOClientEvent::Kind::Updated, known._staticObjectID } );
                continue;
            }
            if ( _pWorld == nullptr && entity._staticID != 0 )
                SW_LOG_ERROR_ID( "MMOClientView.StaticEntityWithoutWorld", "MMOClientView: entity %# is placed in a cell (static id %#) but no client world is set - held until a world or its leave",
                                 entityID, entity._staticID );
            ClientEntity& entry = _mapEntity[entityID];
            entry._entity       = std::move( entity );
            entry._tick         = tick;
            entry._state        = ClientEntityState::Held;
            _heldCount += 1;
            uint64 staticObjectID = 0;
            if ( tryResolve( entityID, entry._entity, staticObjectID ) )
                makeVisible( entityID, entry, staticObjectID ); // 셀이 이미 올라 있으면 기다리지 않는다. 아니면 `update` 가 놓는다
        }
        return NetHandleResult::Handled;
    }

    NetHandleResult MMOClientView::receive( NetMMOMessage::Leave, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        BitReader& reader = body;
        uint32     count  = 0;
        // 보내는 쪽은 조각나지 않는 크기(`kMaxSingleMessageSize`)로 쪼개고 id 하나가 적어도 1 바이트다 — 그보다 큰 수는 깨진 것이다.
        count = reader.readCount( static_cast<uint32>( NetConnection::kMaxSingleMessageSize ) );
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        vector<uint32> listLeft;
        listLeft.reserve( count );
        for ( uint32 index = 0; index < count && reader.hasOverflowed() == false; ++index )
            listLeft.push_back( reader.readVarUint32() );
        if ( reader.readEnd() == false )
            return NetHandleResult::Malformed;
        for ( const uint32 entityID : listLeft )
        {
            const auto entityIter = _mapEntity.find( entityID );
            if ( entityIter == _mapEntity.end() )
                continue;
            const ClientEntity& entry = entityIter->second;
            if ( entry._state == ClientEntityState::Held )
            {
                _heldCount -= 1; // 아직 놓지 않았다 — 알릴 것이 없다
            }
            else
            {
                const uint64 staticObjectID = entry._staticObjectID;
                if ( staticObjectID != 0 )
                    _mapBoundObject.erase( staticObjectID );
                _eventBuffer.push( MMOClientEvent{ entityID, staticObjectID != 0 ? MMOClientEvent::Kind::Unbound : MMOClientEvent::Kind::Left, staticObjectID } );
            }
            _mapEntity.erase( entityIter );
        }
        return NetHandleResult::Handled;
    }

    NetHandleResult MMOClientView::receive( NetMMOMessage::Update, const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        BitReader&   reader = body;
        const uint32 tick   = reader.readVarUint32();
        if ( reader.hasOverflowed() )
            return NetHandleResult::Malformed;
        vector<MMOEntity> listUpdate; // 끝까지 읽고 남는 비트가 없을 때만 적용한다
        while ( reader.hasOverflowed() == false && reader.readBool() )
        {
            MMOEntity update;
            if ( MMOReplicatorInternal::readEntity( reader, update, false ) == false )
                return NetHandleResult::Malformed;
            listUpdate.push_back( std::move( update ) );
        }
        if ( reader.readEnd() == false )
            return NetHandleResult::Malformed;
        for ( MMOEntity& update : listUpdate )
        {
            const auto entityIter = _mapEntity.find( update._entityID );
            if ( entityIter == _mapEntity.end() )
                continue; // 새로 보임보다 먼저 왔거나 이미 보이지 않게 된 엔티티이므로 버린다
            ClientEntity& entry = entityIter->second;
            if ( tick < entry._tick )
            {
                _staleUpdateCount.add(); // 순서가 뒤바뀐 오래된 갱신이다. 더 새로운 상태를 덮어쓰지 않게 버린다
                continue;
            }
            entry._tick              = tick;
            entry._entity._position  = update._position;
            entry._entity._listState = std::move( update._listState );
            if ( entry._state == ClientEntityState::Visible ) // 보류 중이면 놓을 때 마지막 상태로 알린다
                _eventBuffer.push( MMOClientEvent{ update._entityID, MMOClientEvent::Kind::Updated, entry._staticObjectID } );
        }
        return NetHandleResult::Handled;
    }

    void MMOClientView::pollEvents( vector<MMOClientEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    const MMOEntity* MMOClientView::findEntity( uint32 entityID ) const
    {
        const auto entityIter = _mapEntity.find( entityID );
        return entityIter != _mapEntity.end() && entityIter->second._state == ClientEntityState::Visible ? &entityIter->second._entity : nullptr;
    }
} // namespace sw

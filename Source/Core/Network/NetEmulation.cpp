#include "pch.h"

#include "Core/Network/NetEmulation.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct NetEmulationInternal
        {
            static constexpr float64 kMillisecond   = 0.001;
            static constexpr float32 kPercent       = 0.01f;
            static constexpr int32   kKilobyte      = 1024;
            static constexpr uint32  kDefaultRandom = 0x9E3779B9u;
        };
    } // namespace

    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuLatencyMs, 0, "네트워크 흉내: 한쪽 지연(ms) — 언리얼 PktLag" );
    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuJitterMs, 0, "네트워크 흉내: ± 흔들림(ms) — PktLagVariance" );
    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuLossPercent, 0, "네트워크 흉내: 손실(%) — PktLoss" );
    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuDuplicatePercent, 0, "네트워크 흉내: 중복(%) — PktDup" );
    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuReorderPercent, 0, "네트워크 흉내: 순서 뒤바뀜(%) — PktOrder" );
    SW_TEST_GLOBAL_VARIABLE_INT( gv_netEmuBandwidthKilobytesPerSecond, 0, "네트워크 흉내: 회선 속도(KB/s, 0=제한 없음)" );
} // namespace sw

namespace sw
{
    bool NetEmulationConditions::isActive() const
    {
        return _latency > 0.0 || _jitter > 0.0 || _lossRate > 0.0f || _duplicateRate > 0.0f || _reorderRate > 0.0f || _bandwidthBytesPerSecond > 0;
    }

    NetEmulationConditions NetEmulationConditions::makeFromGlobalVariables()
    {
        using Internal = NetEmulationInternal;
        NetEmulationConditions conditions;
        conditions._latency                 = static_cast<float64>( MathUtil::max( 0, gv_netEmuLatencyMs ) ) * Internal::kMillisecond;
        conditions._jitter                  = static_cast<float64>( MathUtil::max( 0, gv_netEmuJitterMs ) ) * Internal::kMillisecond;
        conditions._lossRate                = static_cast<float32>( MathUtil::clamp( gv_netEmuLossPercent, 0, 100 ) ) * Internal::kPercent;
        conditions._duplicateRate           = static_cast<float32>( MathUtil::clamp( gv_netEmuDuplicatePercent, 0, 100 ) ) * Internal::kPercent;
        conditions._reorderRate             = static_cast<float32>( MathUtil::clamp( gv_netEmuReorderPercent, 0, 100 ) ) * Internal::kPercent;
        conditions._bandwidthBytesPerSecond = MathUtil::max( 0, gv_netEmuBandwidthKilobytesPerSecond ) * Internal::kKilobyte;
        return conditions;
    }

    NetEmulationTransport::NetEmulationTransport( INetTransport* pInner, uint32 seed )
        : _mutex{}
        , _pInner{ pInner }
        , _listPending{}
        , _listLink{}
        , _defaultConditions{}
        , _stats{}
        , _time{ 0.0 }
        , _order{ 0 }
        , _randomState{ seed != 0 ? seed : NetEmulationInternal::kDefaultRandom }
    {
    }

    void NetEmulationTransport::setDefaultConditions( const NetEmulationConditions& conditions )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        _defaultConditions = conditions;
        for ( LinkState& link : _listLink )
        {
            if ( link._bOverride == SW_FALSE )
                link._conditions = conditions;
        }
    }

    void NetEmulationTransport::setConditions( const NetAddress& to, const NetEmulationConditions& conditions )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        LinkState&              link = findOrAddLink( to );
        link._conditions             = conditions;
        link._bOverride              = SW_TRUE;
    }

    void NetEmulationTransport::clearConditions( const NetAddress& to )
    {
        std::scoped_lock<mutex> lock{ _mutex };
        LinkState&              link = findOrAddLink( to );
        link._conditions             = _defaultConditions;
        link._bOverride              = SW_FALSE;
    }

    NetEmulationConditions NetEmulationTransport::findConditions( const NetAddress& to ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        for ( const LinkState& link : _listLink )
        {
            if ( link._to == to )
                return link._conditions;
        }
        return _defaultConditions;
    }

    bool NetEmulationTransport::send( const NetAddress& to, const uint8* pData, int32 size )
    {
        if ( _pInner == nullptr || pData == nullptr || size <= 0 )
            return false;
        std::scoped_lock<mutex>       lock{ _mutex };
        LinkState&                    link       = findOrAddLink( to );
        const NetEmulationConditions& conditions = link._conditions;
        // 나쁘게 할 것이 없고 줄도 비었으면 그대로 넘긴다(순서를 지킨다).
        if ( conditions.isActive() == false && link._queuedBytes == 0 )
        {
            ++_stats._sentCount;
            return _pInner->send( to, pData, size );
        }
        if ( nextRandom() < conditions._lossRate )
        {
            ++_stats._droppedCount;
            return true; // 손실은 보내는 쪽이 모른다
        }
        const bool  bDuplicate = nextRandom() < conditions._duplicateRate;
        const int32 copyCount  = bDuplicate ? 2 : 1;
        _stats._duplicatedCount += bDuplicate ? 1u : 0u;
        for ( int32 copy = 0; copy < copyCount; ++copy )
        {
            if ( conditions._maxQueuedBytes > 0 && link._queuedBytes + static_cast<uint64>( size ) > static_cast<uint64>( conditions._maxQueuedBytes ) )
            {
                ++_stats._droppedCount;
                ++_stats._queueDropCount;
                continue;
            }
            float64 delay = conditions._latency + static_cast<float64>( nextRandom() * 2.0f - 1.0f ) * conditions._jitter;
            if ( nextRandom() < conditions._reorderRate )
            {
                delay += conditions._reorderDelay;
                ++_stats._reorderedCount;
            }
            float64 departTime = _time;
            if ( conditions._bandwidthBytesPerSecond > 0 )
            {
                // 회선이 비어야 나간다 — 이 패킷이 회선을 차지하는 시간만큼 뒤 패킷이 밀린다.
                departTime      = MathUtil::max( _time, link._busyUntil );
                link._busyUntil = departTime + static_cast<float64>( size ) / static_cast<float64>( conditions._bandwidthBytesPerSecond );
                departTime      = link._busyUntil;
            }
            Pending pending;
            pending._buffer.assign( pData, pData + size );
            pending._to          = to;
            pending._deliverTime = departTime + MathUtil::max( 0.0, delay );
            pending._order       = _order++;
            link._queuedBytes += static_cast<uint64>( size );
            _listPending.push_back( std::move( pending ) );
        }
        return true;
    }

    bool NetEmulationTransport::receive( NetAddress& outFrom, vector<uint8>& outBuffer )
    {
        return _pInner != nullptr && _pInner->receive( outFrom, outBuffer );
    }

    NetAddress NetEmulationTransport::getLocalAddress() const
    {
        return _pInner != nullptr ? _pInner->getLocalAddress() : NetAddress{};
    }

    void NetEmulationTransport::update( float64 time )
    {
        vector<Pending> listDue;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _time = MathUtil::max( _time, time );
            // 때가 된 것을 전할 때 · 보낸 순서로 넘긴다.
            const auto dueEnd = std::partition( _listPending.begin(), _listPending.end(), [this]( const Pending& pending )
            { return pending._deliverTime <= _time; } );
            listDue.assign( std::make_move_iterator( _listPending.begin() ), std::make_move_iterator( dueEnd ) );
            _listPending.erase( _listPending.begin(), dueEnd );
            std::sort( listDue.begin(), listDue.end(), []( const Pending& lhs, const Pending& rhs )
            { return lhs._deliverTime != rhs._deliverTime ? lhs._deliverTime < rhs._deliverTime : lhs._order < rhs._order; } );
            for ( const Pending& pending : listDue )
            {
                LinkState& link   = findOrAddLink( pending._to );
                link._queuedBytes = link._queuedBytes >= pending._buffer.size() ? link._queuedBytes - pending._buffer.size() : 0;
                ++_stats._sentCount;
            }
        }
        if ( _pInner == nullptr )
            return;
        for ( const Pending& pending : listDue )
            (void)_pInner->send( pending._to, pending._buffer.data(), static_cast<int32>( pending._buffer.size() ) ); // 안쪽 실패는 손실과 같다
        _pInner->update( time );
    }

    bool NetEmulationTransport::waitForReceive( float64 timeoutSeconds )
    {
        return _pInner != nullptr && _pInner->waitForReceive( timeoutSeconds );
    }

    NetEmulationStats NetEmulationTransport::getStats() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _stats;
    }

    size_t NetEmulationTransport::getQueuedCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _listPending.size();
    }

    uint64 NetEmulationTransport::getQueuedBytes() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        uint64                  total = 0;
        for ( const LinkState& link : _listLink )
            total += link._queuedBytes;
        return total;
    }

    NetEmulationTransport::LinkState& NetEmulationTransport::findOrAddLink( const NetAddress& to )
    {
        for ( LinkState& link : _listLink )
        {
            if ( link._to == to )
                return link;
        }
        LinkState link{};
        link._to         = to;
        link._conditions = _defaultConditions;
        _listLink.push_back( link );
        return _listLink.back();
    }

    float32 NetEmulationTransport::nextRandom()
    {
        // xorshift32 — 씨앗이 같으면 같은 수열(시험 · 재현).
        uint32 value = _randomState;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        _randomState = value;
        return static_cast<float32>( value >> 8 ) * ( 1.0f / 16777216.0f );
    }
} // namespace sw

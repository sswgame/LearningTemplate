#include "pch.h"

#include "Core/Network/Connection/NetFlowLog.h"

#if SW_NET_FLOW_LOG_ENABLED

    #include "Core/Common/HashUtil.h"
    #include "Core/Common/StdHeaders.h"
    #include "Core/Concurrency/atomic.h"
    #include "Core/Container/CompareUtil.h"
    #include "Core/Time/MonotonicClock.h"

namespace sw
{
    namespace
    {
        /** @brief 링의 칸입니다. 칸마다 스탬프로 덮이는 중인지 가린다(칸 단위 seqlock). */
        struct NetFlowRing
        {
            static constexpr uint32 kCapacity = NetFlowLog::kCapacity;

            atomic<uint32> _recorderCount{ 0 }; ///< 기록하는 쪽의 수 — 0 이면 남기지 않는다
            atomic<uint64> _writeCount{ 0 };
            atomic<uint64> _arrStamp[kCapacity]{}; ///< 쓴 번호 + 1, 쓰는 중이면 0
            atomic<uint64> _arrNanos[kCapacity]{};
            atomic<uint64> _arrID[kCapacity]{};
            atomic<uint32> _arrKindPhase[kCapacity]{}; ///< 종류(아래 8 비트) · 받음(9 번째 비트)
        };

        NetFlowRing& getRing()
        {
            static NetFlowRing s_ring;
            return s_ring;
        }

        constexpr uint32 kReceiveBit = 0x100u;
    } // namespace

    uint64 NetFlowLog::makeFlowID( uint64 connectionToken, bool bFromServer, NetChannelType channel, uint64 wireIndex )
    {
        constexpr uint64 kIndexBits      = 24;
        constexpr uint64 kChannelBits    = 2;
        constexpr uint64 kConnectionBits = 27;
        static_assert( kIndexBits + kChannelBits + kConnectionBits == 53, "the flow id must fit a double's mantissa" );
        static_assert( static_cast<uint64>( NetChannelType::Count ) <= ( 1ull << kChannelBits ), "the channel field must hold every channel" );
        // 방향은 토큰에 황금비를 섞어 가른다 — 같은 연결의 두 방향이 같은 번호를 쓴다.
        const uint64 direction  = bFromServer ? HashUtil::kGoldenRatio64 : 0ull;
        const uint64 connection = HashUtil::mix64( connectionToken ^ direction ) & ( ( 1ull << kConnectionBits ) - 1ull );
        const uint64 channelBit = static_cast<uint64>( channel ) & ( ( 1ull << kChannelBits ) - 1ull );
        return ( connection << ( kIndexBits + kChannelBits ) ) | ( channelBit << kIndexBits ) | ( wireIndex & ( ( 1ull << kIndexBits ) - 1ull ) );
    }

    void NetFlowLog::addRecorder()
    {
        getRing()._recorderCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void NetFlowLog::removeRecorder()
    {
        getRing()._recorderCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    void NetFlowLog::record( uint64 id, uint8 kind, bool bReceive )
    {
        NetFlowRing& ring = getRing();
        if ( ring._recorderCount.load( std::memory_order_relaxed ) == 0 )
            return;
        const uint64 writeIndex = ring._writeCount.fetch_add( 1, std::memory_order_relaxed );
        const uint32 cell       = static_cast<uint32>( writeIndex % NetFlowRing::kCapacity );
        ring._arrStamp[cell].store( 0, std::memory_order_relaxed );
        std::atomic_thread_fence( std::memory_order_release );
        ring._arrNanos[cell].store( static_cast<uint64>( MonotonicClock::nowNanoseconds() ), std::memory_order_relaxed );
        ring._arrID[cell].store( id, std::memory_order_relaxed );
        ring._arrKindPhase[cell].store( static_cast<uint32>( kind ) | ( bReceive ? kReceiveBit : 0u ), std::memory_order_relaxed );
        ring._arrStamp[cell].store( writeIndex + 1, std::memory_order_release );
    }

    void NetFlowLog::collect( uint64 beginNanos, uint64 endNanos, vector<NetFlowEvent>& outListFlow )
    {
        outListFlow.clear();
        const NetFlowRing& ring    = getRing();
        const uint64       written = ring._writeCount.load( std::memory_order_acquire );
        const uint64       first   = written > NetFlowRing::kCapacity ? written - NetFlowRing::kCapacity : 0;
        for ( uint64 flowIndex = first; flowIndex < written; ++flowIndex )
        {
            const uint32 cell        = static_cast<uint32>( flowIndex % NetFlowRing::kCapacity );
            const uint64 stampBefore = ring._arrStamp[cell].load( std::memory_order_acquire );
            if ( stampBefore != flowIndex + 1 )
                continue; // 아직 쓰는 중이거나 다음 바퀴가 덮었다
            NetFlowEvent flow;
            flow._nanos            = ring._arrNanos[cell].load( std::memory_order_relaxed );
            flow._id               = ring._arrID[cell].load( std::memory_order_relaxed );
            const uint32 kindPhase = ring._arrKindPhase[cell].load( std::memory_order_relaxed );
            flow._kind             = static_cast<uint8>( kindPhase & 0xFFu );
            flow._bReceive         = ( kindPhase & kReceiveBit ) != 0;
            std::atomic_thread_fence( std::memory_order_acquire );
            const bool bOverwritten = ring._arrStamp[cell].load( std::memory_order_relaxed ) != stampBefore;
            const bool bOutside     = flow._nanos < beginNanos || flow._nanos > endNanos;
            if ( bOverwritten || bOutside )
                continue;
            outListFlow.push_back( flow );
        }
        std::stable_sort( outListFlow.begin(), outListFlow.end(), Ascending<&NetFlowEvent::_nanos>{} );
    }
} // namespace sw

#endif // SW_NET_FLOW_LOG_ENABLED

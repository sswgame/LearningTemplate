#include "pch.h"

#include "Engine/Utility/Debug/FrameProfiler.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

#include "Engine/Common/EngineServices.h"

#include <chrono>

namespace sw
{
    namespace
    {
        /** @brief 단조 시계의 현재 나노초입니다. */
        uint64 nowNanos() noexcept
        {
            return static_cast<uint64>(
                std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count() );
        }

        /** @brief 값이 들어갈 칸입니다. 옥타브 = floor(log2) 이고, 그 안을 상위 비트로 등분합니다. 한 옥타브 미만은 값 그대로입니다. */
        uint32 bucketOf( uint64 nanos )
        {
            if ( nanos < FrameProfiler::kSubBucketCount )
                return static_cast<uint32>( nanos );
            uint32 octave = 0;
            for ( uint64 value = nanos; value > 1u; value >>= 1 )
                ++octave;
            const uint32 mantissa = static_cast<uint32>( nanos >> ( octave - FrameProfiler::kSubBucketBit ) ) & FrameProfiler::kSubBucketMask;
            return ( octave << FrameProfiler::kSubBucketBit ) + mantissa;
        }

        /** @brief 칸의 아래 끝 값입니다. 백분위는 이 값으로 답합니다. 표본보다 작거나 같고, 차이는 한 칸 안입니다. */
        uint64 bucketLowerNanos( uint32 bucket )
        {
            if ( bucket < FrameProfiler::kSubBucketCount )
                return bucket;
            const uint32 octave   = bucket >> FrameProfiler::kSubBucketBit;
            const uint64 mantissa = bucket & FrameProfiler::kSubBucketMask;
            return ( uint64( FrameProfiler::kSubBucketCount ) + mantissa ) << ( octave - FrameProfiler::kSubBucketBit );
        }

        // 이 둘은 report() 의 표 출력에만 쓰인다. 배포본에서는 SW_LOG_INFO 가 사라져 report()
        // 본문이 통째로 빠지므로 여기도 같은 조건으로 묶는다. 묶지 않으면 쓰이지 않는 함수 경고가 난다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        /**
         * @brief 나노초를 마이크로초 정수로 바꿉니다.
         * @details 실수로 찍으면 값마다 소수 자릿수가 달라져 표가 어긋납니다. 정수 us 로 고정합니다.
         */
        uint64 toMicros( uint64 nanos ) { return nanos / 1000; }

        /** @brief 표가 어긋나지 않게 이름을 고정 폭으로 맞춥니다. */
        void padRight( fixed_string<constant::kMaxBuffer64>& out, const utf8* pText, size_t width )
        {
            out.clear();
            out.append( pText != nullptr ? pText : "" );
            while ( out.size() < width && out.size() + 1 < out.capacity() )
                out.append( " " );
        }
#endif
    } // namespace

    uint32 FrameProfiler::registerScope( const utf8* pName )
    {
        if ( pName == nullptr )
            return kInvalidSlot;

        // 같은 이름이 이미 있으면 그 슬롯을 쓴다. 등록은 최초 1회뿐이라 선형 탐색으로 충분하다.
        // **표 크기로 자른다.** 표가 꽉 찬 뒤에도 아래 `fetch_add` 는 카운터를 계속 올리므로
        // 그 값이 `kMaxScope` 를 넘어간다 — 자르지 않으면 이 순회가 고정 배열 **밖**을 읽고,
        // 배열 바로 뒤에 있는 것이 `_scopeCount` 자신이라 그 비트가 `const utf8*` 로 읽혀
        // 문자열 비교에 들어간다(프로세스가 죽는다). 이 파일의 다른 세 순회
        // (`endFrame` · `report` · `reset`)는 전부 이미 자르고 있었는데, 넘침을 **만드는**
        // 이 함수만 자르지 않았다.
        const uint32 count = MathUtil::min( _scopeCount.load( std::memory_order_acquire ), kMaxScope );
        for ( uint32 index = 0; index < count; ++index )
        {
            // 아직 이름이 실리지 않은 칸일 수 있다. 슬롯을 잡는 것과 이름을 적는 것은 별개의 두 단계다.
            const utf8* pExisting = _arrScope[index]._pName.load( std::memory_order_acquire );
            if ( pExisting != nullptr && StringUtil::equals( pExisting, pName ) )
                return index;
        }

        const uint32 slot = _scopeCount.fetch_add( 1, std::memory_order_acq_rel );
        if ( slot >= kMaxScope )
        {
            // 넘친 뒤에는 카운터를 표 크기로 고정해 둔다. 그러지 않으면 등록 시도마다 계속 자라고,
            // 오래 돌면 `uint32` 를 한 바퀴 돌아 0 이 되어 남의 슬롯을 내주게 된다.
            _scopeCount.store( kMaxScope, std::memory_order_release );

            // 측정이 실행을 막으면 안 된다. 한 번만 알리고 조용히 무시한다.
            static bool s_bWarned = false;
            if ( s_bWarned == false )
            {
                s_bWarned = true;
                SW_LOG_WARNING( "FrameProfiler: 구간이 %#개를 넘었습니다 — '%#' 이후는 무시합니다.", kMaxScope, pName );
            }
            return kInvalidSlot;
        }

        _arrScope[slot]._pName.store( pName, std::memory_order_release );
        return slot;
    }

    void FrameProfiler::addSample( uint32 slot, uint64 nanos )
    {
        if ( slot >= kMaxScope )
            return;
        _arrScope[slot]._frameNanos.fetch_add( nanos, std::memory_order_relaxed );
        _arrScope[slot]._frameCalls.fetch_add( 1, std::memory_order_relaxed );
    }

    void FrameProfiler::addCount( uint32 slot, uint64 count )
    {
        if ( slot >= kMaxScope || isEnabled() == false )
            return;
        _arrScope[slot]._frameCalls.fetch_add( count, std::memory_order_relaxed );
    }

    void FrameProfiler::beginFrame()
    {
        // **여기서 누적을 지우지 않는다.** 예전에는 지웠는데, 그러면 `endFrame` 과 이 호출 사이에
        // 렌더 스레드가 더한 샘플이 통째로 버려진다. 렌더 스레드는 게임 스레드와 다른 박자로 돌기
        // 때문에 그 구간에 걸리는 스코프가 **매 프레임 같은 것들**이었고, 그 스코프만 골라 표에서
        // 사라지거나(sampledFrames=0) 평균이 부풀었다 — 중첩된 구간의 합이 바깥 구간보다 커지는
        // 표가 나왔다(upload 2628 + submitGraph 2703 > RT.Frame 4271).
        //
        // 지금은 `endFrame` 이 exchange 로 **읽으면서 0 으로 바꾼다**. 창이 닫힌 뒤 더해진 샘플은
        // 버려지지 않고 다음 창으로 넘어간다. 그래서 이 함수는 창이 열렸다는 표시일 뿐이다.
    }

    void FrameProfiler::endFrame()
    {
        if ( isEnabled() == false )
            return;

        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            Scope& scope = _arrScope[index];
            // 읽기와 비우기가 한 연산이어야 한다. 그 사이에 렌더 스레드가 더한 값이 사라지면 안 된다.
            const uint64 calls = scope._frameCalls.exchange( 0, std::memory_order_relaxed );
            const uint64 nanos = scope._frameNanos.exchange( 0, std::memory_order_relaxed );
            if ( calls == 0 )
                continue;

            scope._totalNanos += nanos;
            scope._totalCalls += calls;
            if ( scope._sampledFrames == 0 || nanos < scope._minNanos )
                scope._minNanos = nanos;
            if ( nanos > scope._maxNanos )
                scope._maxNanos = nanos;
            ++scope._arrBucket[bucketOf( nanos )];
            ++scope._sampledFrames;
        }
        _frameCount.fetch_add( 1, std::memory_order_relaxed );
    }

    uint64 FrameProfiler::getPercentileNanos( uint32 slot, uint32 percent ) const
    {
        if ( slot >= kMaxScope || percent == 0 )
            return 0;
        const Scope& scope = _arrScope[slot];
        if ( scope._sampledFrames == 0 )
            return 0;
        if ( percent > kPercentMax )
            percent = kPercentMax;

        // 순위는 1 부터 세고 올림한다. p99 는 100 프레임 중 99 번째다.
        const uint64 rank       = ( scope._sampledFrames * percent + ( kPercentMax - 1 ) ) / kPercentMax;
        uint64       cumulative = 0;
        for ( uint32 bucket = 0; bucket < kBucketCount; ++bucket )
        {
            cumulative += scope._arrBucket[bucket];
            if ( cumulative >= rank )
                return bucketLowerNanos( bucket );
        }
        return scope._maxNanos;
    }

    void FrameProfiler::report( [[maybe_unused]] const utf8* pTitle ) const
    {
        // 보고는 Info 로그로만 나간다. 배포본에서는 SW_LOG_INFO 가 사라지므로 아래 전부가 출력
        // 없는 계산이 된다. 구간을 다 돌고 평균까지 내고 버렸다. 로그가 컴파일될 때만 돈다.
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        const uint64 frames = _frameCount.load( std::memory_order_relaxed );
        if ( frames == 0 )
        {
            SW_LOG_INFO( "[Profile] %# — 수집된 프레임이 없습니다.", pTitle != nullptr ? pTitle : "" );
            return;
        }

        fixed_string<constant::kMaxBuffer64> nameCol;
        padRight( nameCol, "scope", 32 );

        SW_LOG_INFO( "[Profile] ===== %# — %# frames =====", pTitle != nullptr ? pTitle : "", frames );
        SW_LOG_INFO( "[Profile] %#  avg_us   p50_us   p99_us   min_us   max_us   per_frame", nameCol.c_str() );

        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            const Scope& scope      = _arrScope[index];
            const utf8*  pScopeName = scope._pName.load( std::memory_order_acquire );
            if ( scope._sampledFrames == 0 || pScopeName == nullptr )
                continue;

            padRight( nameCol, pScopeName, 32 );

            // 시간이 0 인 구간은 순수 카운터(SW_PROFILE_COUNT)다. 시간 열은 의미가 없다.
            const uint64 avgUs       = toMicros( scope._totalNanos / scope._sampledFrames );
            const uint64 perFrameX10 = ( scope._totalCalls * 10 ) / scope._sampledFrames;

            SW_LOG_INFO( "[Profile] %#  %#   %#   %#   %#   %#   %#.%#",
                         nameCol.c_str(), avgUs, toMicros( getPercentileNanos( index, 50 ) ),
                         toMicros( getPercentileNanos( index, 99 ) ), toMicros( scope._minNanos ),
                         toMicros( scope._maxNanos ), perFrameX10 / 10, perFrameX10 % 10 );
        }
#endif
    }

    void FrameProfiler::reset()
    {
        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            Scope& scope = _arrScope[index];
            scope._frameNanos.store( 0, std::memory_order_relaxed );
            scope._frameCalls.store( 0, std::memory_order_relaxed );
            scope._totalNanos    = 0;
            scope._totalCalls    = 0;
            scope._minNanos      = 0;
            scope._maxNanos      = 0;
            scope._sampledFrames = 0;
            Memory::set( scope._arrBucket, 0, sizeof( scope._arrBucket ) );
        }
        _frameCount.store( 0, std::memory_order_relaxed );
    }

    ScopedFrameProfile::ScopedFrameProfile( uint32 slot ) noexcept
        : _startNanos{ 0 }
        , _slot{ slot }
    {
        if ( slot < FrameProfiler::kMaxScope && engine::getFrameProfiler().isEnabled() )
            _startNanos = nowNanos();
        else
            _slot = FrameProfiler::kInvalidSlot;
    }

    ScopedFrameProfile::~ScopedFrameProfile() noexcept
    {
        if ( _slot >= FrameProfiler::kMaxScope )
            return;
        engine::getFrameProfiler().addSample( _slot, nowNanos() - _startNanos );
    }
} // namespace sw

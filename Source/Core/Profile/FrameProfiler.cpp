#include "pch.h"

#include "Core/Profile/FrameProfiler.h"

#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/String/fixed_string.h"

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

        // 이 둘은 report() 의 표 출력에만 쓰인다. 배포본에서는 SW_LOG_INFO 가 사라져 report()
        // 본문이 통째로 빠지므로 여기도 같은 조건으로 묶는다 — 안 묶으면 쓰이지 않는 함수 경고가 난다.
#if SW_LOG_LEVEL_COMPILED( 2 )
        /**
         * @brief 나노초를 마이크로초 정수로 바꿉니다.
         * @details 실수로 찍으면 값마다 소수 자릿수가 달라져 표가 어긋난다. 정수 us 로 고정한다.
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

    FrameProfiler& FrameProfiler::get()
    {
        // 함수 지역 static — 로거보다 먼저 초기화될 위험이 없다.
        static FrameProfiler s_instance;
        return s_instance;
    }

    uint32 FrameProfiler::registerScope( const utf8* pName )
    {
        if ( pName == nullptr )
            return kInvalidSlot;

        // 같은 이름이 이미 있으면 그 슬롯을 쓴다. 등록은 최초 1회뿐이라 선형 탐색으로 충분하다.
        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count; ++index )
        {
            if ( _arrScope[index]._pName != nullptr && StringUtil::equals( _arrScope[index]._pName, pName ) )
                return index;
        }

        const uint32 slot = _scopeCount.fetch_add( 1, std::memory_order_acq_rel );
        if ( slot >= kMaxScope )
        {
            // 측정이 실행을 막으면 안 된다 — 한 번만 알리고 조용히 무시한다.
            static bool s_bWarned = false;
            if ( s_bWarned == false )
            {
                s_bWarned = true;
                SW_LOG_WARNING( "FrameProfiler: 구간이 %#개를 넘었습니다 — '%#' 이후는 무시합니다.", kMaxScope, pName );
            }
            return kInvalidSlot;
        }

        _arrScope[slot]._pName = pName;
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
        if ( isEnabled() == false )
            return;
        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            _arrScope[index]._frameNanos.store( 0, std::memory_order_relaxed );
            _arrScope[index]._frameCalls.store( 0, std::memory_order_relaxed );
        }
    }

    void FrameProfiler::endFrame()
    {
        if ( isEnabled() == false )
            return;

        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            Scope&       scope = _arrScope[index];
            const uint64 nanos = scope._frameNanos.load( std::memory_order_relaxed );
            const uint64 calls = scope._frameCalls.load( std::memory_order_relaxed );
            if ( calls == 0 )
                continue;

            scope._totalNanos += nanos;
            scope._totalCalls += calls;
            if ( scope._sampledFrames == 0 || nanos < scope._minNanos )
                scope._minNanos = nanos;
            if ( nanos > scope._maxNanos )
                scope._maxNanos = nanos;
            ++scope._sampledFrames;
        }
        _frameCount.fetch_add( 1, std::memory_order_relaxed );
    }

    void FrameProfiler::report( [[maybe_unused]] const utf8* pTitle ) const
    {
        // 보고는 Info 로그로만 나간다. 배포본에서는 SW_LOG_INFO 가 사라지므로 아래 전부가 출력
        // 없는 계산이 된다 — 구간을 다 돌고 평균까지 내고 버렸다. 로그가 컴파일될 때만 돈다.
#if SW_LOG_LEVEL_COMPILED( 2 )
        const uint64 frames = _frameCount.load( std::memory_order_relaxed );
        if ( frames == 0 )
        {
            SW_LOG_INFO( "[Profile] %# — 수집된 프레임이 없습니다.", pTitle != nullptr ? pTitle : "" );
            return;
        }

        fixed_string<constant::kMaxBuffer64> nameCol;
        padRight( nameCol, "scope", 32 );

        SW_LOG_INFO( "[Profile] ===== %# — %# frames =====", pTitle != nullptr ? pTitle : "", frames );
        SW_LOG_INFO( "[Profile] %#  avg_us   min_us   max_us   per_frame", nameCol.c_str() );

        const uint32 count = _scopeCount.load( std::memory_order_acquire );
        for ( uint32 index = 0; index < count && index < kMaxScope; ++index )
        {
            const Scope& scope = _arrScope[index];
            if ( scope._sampledFrames == 0 || scope._pName == nullptr )
                continue;

            padRight( nameCol, scope._pName, 32 );

            // 시간이 0 인 구간은 순수 카운터(SW_PROFILE_COUNT)다 — 시간 열은 의미가 없다.
            const uint64 avgUs       = toMicros( scope._totalNanos / scope._sampledFrames );
            const uint64 perFrameX10 = ( scope._totalCalls * 10 ) / scope._sampledFrames;

            SW_LOG_INFO( "[Profile] %#  %#   %#   %#   %#.%#",
                         nameCol.c_str(), avgUs, toMicros( scope._minNanos ), toMicros( scope._maxNanos ),
                         perFrameX10 / 10, perFrameX10 % 10 );
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
        }
        _frameCount.store( 0, std::memory_order_relaxed );
    }

    ScopedFrameProfile::ScopedFrameProfile( uint32 slot ) noexcept
        : _startNanos{ 0 }
        , _slot{ slot }
    {
        if ( slot < FrameProfiler::kMaxScope && FrameProfiler::get().isEnabled() )
            _startNanos = nowNanos();
        else
            _slot = FrameProfiler::kInvalidSlot;
    }

    ScopedFrameProfile::~ScopedFrameProfile() noexcept
    {
        if ( _slot >= FrameProfiler::kMaxScope )
            return;
        FrameProfiler::get().addSample( _slot, nowNanos() - _startNanos );
    }
} // namespace sw

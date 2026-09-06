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

        /**
         * @brief 나노초를 마이크로초 정수로 바꿉니다.
         * @details 로거 포맷은 `%#` 뿐이라 폭·정밀도 지정자가 없다. 실수로 찍으면 자릿수가 제각각이라
         *          표가 어긋나므로 정수 us 로 고정한다.
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

    void FrameProfiler::report( const utf8* pTitle ) const
    {
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

#include "pch.h"

#include "Engine/Resource/AssetLoadProfiler.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/Profiling/FrameProfiler.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "AssetLoadProfiler" );

    namespace
    {
        struct AssetLoadProfilerInternal
        {
            static constexpr const utf8* kArrPhaseName[]      = { "Io", "Decode", "Upload" };
            static constexpr float64     kNanosPerMillisecond = 1.0e6;

            [[maybe_unused]] static float64 toMilliseconds( uint64 nanos ) { return static_cast<float64>( nanos ) / kNanosPerMillisecond; }

            static uint64 computeSummaryTotal( const AssetLoadKindSummary& summary )
            {
                uint64 total = 0;
                for ( const uint64 nanos : summary._arrPhaseNanos )
                    total += nanos;
                return total;
            }
        };
        static_assert( std::size( AssetLoadProfilerInternal::kArrPhaseName ) == static_cast<size_t>( AssetLoadPhase::Count ) );
    } // namespace

    /** @brief `-gv_assetLoadProfile=0` — 에셋 로드 기록을 끕니다(기본 켬, 로드마다 시계 몇 번이라 싸다). */
    SW_GLOBAL_VARIABLE( int32, gv_assetLoadProfile, 1, "에셋 로드 시간 · 바이트 기록 (0=끄기)" );
    /** @brief `-gv_assetLoadReport=1` — 엔진을 끌 때 에셋 로드 표(종류별 · 가장 느린 로드)를 로그에 남깁니다. 배포본 측정에도 쓴다. */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_assetLoadReport, 0, "엔진 종료 때 에셋 로드 표를 로그로 (1=켜기)" );
} // namespace sw

namespace sw
{
    uint64 AssetLoadRecord::computeTotalNanos() const
    {
        uint64 total = 0;
        for ( const uint64 nanos : _arrPhaseNanos )
            total += nanos;
        return total;
    }

    AssetLoadProfiler& AssetLoadProfiler::get()
    {
        static AssetLoadProfiler s_profiler;
        return s_profiler;
    }

    AssetLoadProfiler::AssetLoadProfiler()
        : _mutex{}
        , _listKind{}
        , _listSlowest{}
        , _loadCount{ 0 }
    {
    }

    bool AssetLoadProfiler::isEnabled() const
    {
        return gv_assetLoadProfile != 0;
    }

    void AssetLoadProfiler::setEnabled( bool bEnabled )
    {
        gv_assetLoadProfile = bEnabled ? 1 : 0;
    }

    void AssetLoadProfiler::submit( const AssetLoadRecord& record )
    {
        if ( isEnabled() == false )
            return;
        // 프레임 프로파일러 구간은 잠금 밖에서 더한다(그쪽도 스레드 안전하다). 번호는 종류마다 한 번 받는다.
        uint32     arrSlot[static_cast<uint32>( AssetLoadPhase::Count )]{};
        const bool bFrameProfiler = engine::areEngineServicesBound();
        {
            std::scoped_lock<mutex> lock{ _mutex };
            KindEntry*              pEntry = nullptr;
            for ( KindEntry& entry : _listKind )
            {
                if ( entry._summary._kind == record._kind )
                {
                    pEntry = &entry;
                    break;
                }
            }
            if ( pEntry == nullptr )
            {
                KindEntry entry{};
                entry._summary._kind = record._kind;
                for ( uint32 phase = 0; phase < static_cast<uint32>( AssetLoadPhase::Count ); ++phase )
                {
                    entry._arrProfilerSlot[phase] = FrameProfiler::kInvalidSlot;
                    if ( bFrameProfiler )
                    {
                        const string name             = string( "Asset." ) + record._kind.c_str() + "." + AssetLoadProfilerInternal::kArrPhaseName[phase];
                        entry._arrProfilerSlot[phase] = engine::getFrameProfiler().registerScope( name.c_str() );
                    }
                }
                _listKind.push_back( entry );
                pEntry = &_listKind.back();
            }
            AssetLoadKindSummary& summary = pEntry->_summary;
            const uint64          total   = record.computeTotalNanos();
            for ( uint32 phase = 0; phase < static_cast<uint32>( AssetLoadPhase::Count ); ++phase )
            {
                summary._arrPhaseNanos[phase] += record._arrPhaseNanos[phase];
                arrSlot[phase] = pEntry->_arrProfilerSlot[phase];
            }
            summary._maxTotalNanos = std::max( summary._maxTotalNanos, total );
            summary._bytes += record._bytes;
            ++summary._count;
            summary._asyncCount += record._bAsync != SW_FALSE ? 1u : 0u;
            summary._failedCount += record._bSucceeded == SW_FALSE ? 1u : 0u;
            ++_loadCount;

            // 가장 느린 것들 — 자리가 있거나 가장 빠른 것보다 느리면 넣고 다시 줄 세운다.
            const bool bFits = _listSlowest.size() < kSlowestCount || total > _listSlowest.back().computeTotalNanos();
            if ( bFits )
            {
                _listSlowest.push_back( record );
                std::stable_sort( _listSlowest.begin(), _listSlowest.end(), []( const AssetLoadRecord& lhs, const AssetLoadRecord& rhs )
                { return lhs.computeTotalNanos() > rhs.computeTotalNanos(); } );
                if ( _listSlowest.size() > kSlowestCount )
                    _listSlowest.resize( kSlowestCount );
            }
        }
        if ( bFrameProfiler == false )
            return;
        FrameProfiler& profiler = engine::getFrameProfiler();
        for ( uint32 phase = 0; phase < static_cast<uint32>( AssetLoadPhase::Count ); ++phase )
        {
            if ( record._arrPhaseNanos[phase] != 0 && arrSlot[phase] != FrameProfiler::kInvalidSlot )
                profiler.addSample( arrSlot[phase], record._arrPhaseNanos[phase] );
        }
        static const uint32 s_bytesSlot = profiler.registerScope( "Asset.Bytes" );
        profiler.addCount( s_bytesSlot, record._bytes );
    }

    void AssetLoadProfiler::collectSummaries( vector<AssetLoadKindSummary>& outListSummary ) const
    {
        outListSummary.clear();
        {
            std::scoped_lock<mutex> lock{ _mutex };
            for ( const KindEntry& entry : _listKind )
            {
                if ( entry._summary._count != 0 ) // 비운 뒤 아직 받지 않은 종류
                    outListSummary.push_back( entry._summary );
            }
        }
        std::stable_sort( outListSummary.begin(), outListSummary.end(), []( const AssetLoadKindSummary& lhs, const AssetLoadKindSummary& rhs )
        { return AssetLoadProfilerInternal::computeSummaryTotal( lhs ) > AssetLoadProfilerInternal::computeSummaryTotal( rhs ); } );
    }

    void AssetLoadProfiler::collectSlowest( vector<AssetLoadRecord>& outListRecord ) const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        outListRecord = _listSlowest;
    }

    uint32 AssetLoadProfiler::getLoadCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _loadCount;
    }

    void AssetLoadProfiler::report( [[maybe_unused]] const utf8* pTitle ) const
    {
#if SW_LOG_LEVEL_COMPILED( SW_LOG_VERBOSITY_INFO )
        using Internal = AssetLoadProfilerInternal;
        vector<AssetLoadKindSummary> listSummary;
        vector<AssetLoadRecord>      listSlowest;
        collectSummaries( listSummary );
        collectSlowest( listSlowest );
        if ( listSummary.empty() )
        {
            SW_LOG_INFO( "[AssetLoad] %#: no asset loads recorded", pTitle );
            return;
        }
        SW_LOG_INFO( "[AssetLoad] %# — kind count async failed bytes total_ms io_ms decode_ms upload_ms max_ms", pTitle );
        for ( const AssetLoadKindSummary& summary : listSummary )
        {
            SW_LOG_INFO( "[AssetLoad]   %# %# %# %# %# %# %# %# %# %#", summary._kind.c_str(), summary._count, summary._asyncCount, summary._failedCount,
                         summary._bytes, Internal::toMilliseconds( Internal::computeSummaryTotal( summary ) ),
                         Internal::toMilliseconds( summary._arrPhaseNanos[0] ), Internal::toMilliseconds( summary._arrPhaseNanos[1] ),
                         Internal::toMilliseconds( summary._arrPhaseNanos[2] ), Internal::toMilliseconds( summary._maxTotalNanos ) );
        }
        SW_LOG_INFO( "[AssetLoad] slowest — total_ms io_ms decode_ms upload_ms bytes async kind path" );
        for ( const AssetLoadRecord& record : listSlowest )
        {
            SW_LOG_INFO( "[AssetLoad]   %# %# %# %# %# %# %# %#", Internal::toMilliseconds( record.computeTotalNanos() ),
                         Internal::toMilliseconds( record._arrPhaseNanos[0] ), Internal::toMilliseconds( record._arrPhaseNanos[1] ),
                         Internal::toMilliseconds( record._arrPhaseNanos[2] ), record._bytes, record._bAsync != SW_FALSE ? "async" : "sync",
                         record._kind.c_str(), record._path.c_str() );
        }
#endif // 표는 Info 로그로만 나간다 — 그것이 사라지는 빌드(Shipping)에서는 모을 것만 모은다
    }

    void AssetLoadProfiler::reportIfRequested( const utf8* pTitle ) const
    {
        if ( gv_assetLoadReport != 0 )
            report( pTitle );
    }

    void AssetLoadProfiler::reset()
    {
        std::scoped_lock<mutex> lock{ _mutex };
        // 프레임 프로파일러 구간 번호는 그대로 둔다(같은 이름은 같은 번호다) — 누적만 비운다.
        for ( KindEntry& entry : _listKind )
        {
            const hashed_string kind = entry._summary._kind;
            entry._summary           = AssetLoadKindSummary{};
            entry._summary._kind     = kind;
        }
        _listSlowest.clear();
        _loadCount = 0;
    }

    // ------------------------------------------------------------------------------
    // 로드 한 번
    // ------------------------------------------------------------------------------
    AssetLoadScope::AssetLoadScope( const utf8* pKind, string_view path, bool bAsync )
        : _record{}
        , _phaseStartNanos{ 0 }
        , _phase{ AssetLoadPhase::Io }
        , _bActive{ SW_FALSE }
    {
        if ( AssetLoadProfiler::get().isEnabled() == false )
            return;
        _record._kind       = hashed_string( pKind );
        _record._path       = string( path );
        const bool bWorker  = engine::areEngineServicesBound() && engine::getTaskManager().isMainThread() == false;
        _record._bAsync     = ( bAsync || bWorker ) ? SW_TRUE : SW_FALSE;
        _record._bSucceeded = SW_FALSE;
        _phaseStartNanos    = MonotonicClock::nowNanoseconds();
        _bActive            = SW_TRUE;
    }

    AssetLoadScope::~AssetLoadScope()
    {
        if ( _bActive == SW_FALSE )
            return;
        beginPhase( AssetLoadPhase::Count );
        AssetLoadProfiler::get().submit( _record );
    }

    void AssetLoadScope::beginPhase( AssetLoadPhase phase )
    {
        if ( _bActive == SW_FALSE )
            return;
        const int64 now = MonotonicClock::nowNanoseconds();
        if ( _phase != AssetLoadPhase::Count && now > _phaseStartNanos )
            _record._arrPhaseNanos[static_cast<uint32>( _phase )] += static_cast<uint64>( now - _phaseStartNanos );
        _phase           = phase;
        _phaseStartNanos = now;
    }
} // namespace sw

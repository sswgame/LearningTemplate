/**
 * @file TracyProfilerBackend.cpp
 * @brief Tracy C API 호출부입니다. 저장소에서 Tracy 헤더를 include 하는 유일한 파일입니다.
 * @details Tracy 를 링크하지 않은 빌드(`SW_PROFILER_TRACY` 없음 — Shipping · `SW_ENABLE_TRACY=OFF`)에서는 몸통이 모두 빈 함수입니다.
 *          C API 만 씁니다 — 함수만 가져다 쓰므로 TracyClient.dll 을 지연 로드할 수 있습니다(데이터 임포트가 있으면 지연 로드가 안 된다).
 */
#include "pch.h"

#include "Engine/Profiling/Tracy/TracyProfilerBackend.h"

#include "Core/Common/StdHeaders.h"

#if defined( SW_PROFILER_TRACY )
    #include <tracy/TracyC.h>
#endif

namespace sw
{
    namespace
    {
        struct TracyProfilerBackendInternal
        {
            /** @brief Tracy 기본 데이터 포트입니다(클라이언트 · 뷰어 공통). */
            static constexpr uint16 kDefaultDataPort = 8086;
            /** @brief 포트 번호 최댓값입니다. */
            static constexpr int32 kMaxPort = 65535;

#if defined( SW_PROFILER_TRACY )
            // 지점 구조체를 Tracy 의 소스 위치 구조체로 그대로 넘긴다 — 배치가 같아야 한다.
            static_assert( sizeof( ProfileZoneSite ) == sizeof( ___tracy_source_location_data ), "ProfileZoneSite must mirror Tracy's source location" );
            static_assert( offsetof( ProfileZoneSite, _pName ) == offsetof( ___tracy_source_location_data, name ), "name offset" );
            static_assert( offsetof( ProfileZoneSite, _pFunction ) == offsetof( ___tracy_source_location_data, function ), "function offset" );
            static_assert( offsetof( ProfileZoneSite, _pFile ) == offsetof( ___tracy_source_location_data, file ), "file offset" );
            static_assert( offsetof( ProfileZoneSite, _line ) == offsetof( ___tracy_source_location_data, line ), "line offset" );
            static_assert( offsetof( ProfileZoneSite, _color ) == offsetof( ___tracy_source_location_data, color ), "color offset" );

            /** @brief Tracy 의 GPU 컨텍스트 종류 번호입니다(서버 `GpuContextType` — OpenGl 1 · Vulkan 2 · Direct3D12 4 · Direct3D11 5). */
            static uint8 toTracyGpuType( ProfilerGpuApi api )
            {
                switch ( api )
                {
                    case ProfilerGpuApi::OpenGl:
                        return 1;
                    case ProfilerGpuApi::Vulkan:
                        return 2;
                    case ProfilerGpuApi::Direct3D12:
                        return 4;
                    case ProfilerGpuApi::Direct3D11:
                        return 5;
                }
                return 1;
            }

            /** @brief Tracy 의 구간 문맥(번호 · 활성)을 토큰 하나로 묶습니다. */
            static uint64 packZone( const TracyCZoneCtx& zone )
            {
                return ( static_cast<uint64>( static_cast<uint32>( zone.active ) ) << 32 ) | static_cast<uint64>( zone.id );
            }

            /** @brief `packZone` 의 반대입니다. */
            static TracyCZoneCtx unpackZone( uint64 token )
            {
                TracyCZoneCtx zone{};
                zone.id     = static_cast<uint32_t>( token & 0xFFFFFFFFull );
                zone.active = static_cast<int32_t>( token >> 32 );
                return zone;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    TracyProfilerBackend::TracyProfilerBackend()
        : _zoneCount{ 0 }
        , _gpuZoneCount{ 0 }
        , _gpuContextCount{ 0 }
        , _nextGpuQueryId{ 0 }
    {
    }

    bool TracyProfilerBackend::isCompiled()
    {
#if defined( SW_PROFILER_TRACY )
        return true;
#else
        return false;
#endif
    }

    uint16 TracyProfilerBackend::getDataPort()
    {
        // Tracy 클라이언트와 같은 규칙이다(TracyProfiler.cpp — TRACY_PORT 환경 변수가 있으면 그 값).
        const utf8* pPort = std::getenv( "TRACY_PORT" );
        if ( pPort == nullptr || pPort[0] == '\0' )
            return TracyProfilerBackendInternal::kDefaultDataPort;
        const int32 port = std::atoi( pPort );
        if ( 0 < port && port <= TracyProfilerBackendInternal::kMaxPort )
            return static_cast<uint16>( port );
        return TracyProfilerBackendInternal::kDefaultDataPort;
    }

    bool TracyProfilerBackend::isViewerConnected() const
    {
#if defined( SW_PROFILER_TRACY )
        return ___tracy_connected() != 0;
#else
        return false;
#endif
    }

    uint64 TracyProfilerBackend::beginZone( [[maybe_unused]] const ProfileZoneSite& site )
    {
#if defined( SW_PROFILER_TRACY )
        _zoneCount.fetch_add( 1, std::memory_order_relaxed );
        const TracyCZoneCtx zone = ___tracy_emit_zone_begin( reinterpret_cast<const ___tracy_source_location_data*>( &site ), 1 );
        return TracyProfilerBackendInternal::packZone( zone );
#else
        return 0;
#endif
    }

    void TracyProfilerBackend::endZone( [[maybe_unused]] uint64 zoneToken )
    {
#if defined( SW_PROFILER_TRACY )
        ___tracy_emit_zone_end( TracyProfilerBackendInternal::unpackZone( zoneToken ) );
#endif
    }

    void TracyProfilerBackend::markFrame( [[maybe_unused]] const utf8* pFrameName )
    {
#if defined( SW_PROFILER_TRACY )
        ___tracy_emit_frame_mark( pFrameName );
#endif
    }

    void TracyProfilerBackend::plotValue( [[maybe_unused]] const utf8* pPlotName, [[maybe_unused]] float64 value )
    {
#if defined( SW_PROFILER_TRACY )
        if ( pPlotName != nullptr )
            ___tracy_emit_plot( pPlotName, value );
#endif
    }

    void TracyProfilerBackend::onAllocate( [[maybe_unused]] const void* pPtr, [[maybe_unused]] size_t size,
                                           [[maybe_unused]] const utf8* pPoolName )
    {
#if defined( SW_PROFILER_TRACY )
        ___tracy_emit_memory_alloc_named( pPtr, size, pPoolName );
#endif
    }

    void TracyProfilerBackend::onFree( [[maybe_unused]] const void* pPtr, [[maybe_unused]] const utf8* pPoolName )
    {
#if defined( SW_PROFILER_TRACY )
        ___tracy_emit_memory_free_named( pPtr, pPoolName );
#endif
    }

    uint32 TracyProfilerBackend::createGpuContext( [[maybe_unused]] ProfilerGpuApi api, [[maybe_unused]] const utf8* pName,
                                                   [[maybe_unused]] int64 gpuNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( _gpuContextCount >= kMaxGpuContext )
            return kInvalidGpuContext;
        const uint32 context = _gpuContextCount++;

        // 시각은 나노초로 넘긴다(period 1). 이 순간의 GPU 시계와 Tracy 가 찍는 CPU 시계의 차가 이 큐의 기준이 된다.
        ___tracy_gpu_new_context_data newContext{};
        newContext.gpuTime = gpuNanos;
        newContext.period  = 1.0f;
        newContext.context = static_cast<uint8_t>( context );
        newContext.flags   = 0;
        newContext.type    = TracyProfilerBackendInternal::toTracyGpuType( api );
        ___tracy_emit_gpu_new_context( newContext );

        if ( pName != nullptr )
        {
            ___tracy_gpu_context_name_data nameData{};
            nameData.context = static_cast<uint8_t>( context );
            nameData.name    = pName;
            nameData.len     = static_cast<uint16_t>( std::strlen( pName ) );
            ___tracy_emit_gpu_context_name( nameData );
        }
        return context;
#else
        return kInvalidGpuContext;
#endif
    }

    void TracyProfilerBackend::syncGpuClock( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] int64 gpuNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        ___tracy_gpu_time_sync_data syncData{};
        syncData.gpuTime = gpuNanos;
        syncData.context = static_cast<uint8_t>( gpuContext );
        ___tracy_emit_gpu_time_sync( syncData );
#endif
    }

    void TracyProfilerBackend::beginGpuZone( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] const ProfileZoneSite& site,
                                             [[maybe_unused]] int64 gpuBeginNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        // 시각을 이미 안다 — 구간을 열면서 그 쿼리의 시각을 바로 넘긴다. 서버는 쿼리 번호로 둘을 잇는다.
        const uint16                 queryId = allocateGpuQueryId();
        ___tracy_gpu_zone_begin_data beginData{};
        beginData.srcloc  = static_cast<uint64_t>( reinterpret_cast<uintptr_t>( &site ) );
        beginData.queryId = queryId;
        beginData.context = static_cast<uint8_t>( gpuContext );
        ___tracy_emit_gpu_zone_begin( beginData );

        ___tracy_gpu_time_data timeData{};
        timeData.gpuTime = gpuBeginNanos;
        timeData.queryId = queryId;
        timeData.context = static_cast<uint8_t>( gpuContext );
        ___tracy_emit_gpu_time( timeData );
#endif
    }

    void TracyProfilerBackend::endGpuZone( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] int64 gpuEndNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        const uint16               queryId = allocateGpuQueryId();
        ___tracy_gpu_zone_end_data endData{};
        endData.queryId = queryId;
        endData.context = static_cast<uint8_t>( gpuContext );
        ___tracy_emit_gpu_zone_end( endData );

        ___tracy_gpu_time_data timeData{};
        timeData.gpuTime = gpuEndNanos;
        timeData.queryId = queryId;
        timeData.context = static_cast<uint8_t>( gpuContext );
        ___tracy_emit_gpu_time( timeData );
        _gpuZoneCount.fetch_add( 1, std::memory_order_relaxed );
#endif
    }

    uint16 TracyProfilerBackend::allocateGpuQueryId()
    {
        const uint16 queryId = _nextGpuQueryId;
        _nextGpuQueryId      = static_cast<uint16>( _nextGpuQueryId + 1u );
        return queryId;
    }
} // namespace sw

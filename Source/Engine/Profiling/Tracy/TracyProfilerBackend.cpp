/**
 * @file TracyProfilerBackend.cpp
 * @brief Tracy C API 호출부입니다. 저장소에서 Tracy 헤더를 include 하는 유일한 파일입니다.
 * @details Tracy 를 링크하지 않은 빌드(`SW_PROFILER_TRACY` 없음 — Shipping · `SW_ENABLE_TRACY=OFF`)에서는 몸통이 모두 빈 함수입니다.
 *          C API 만 씁니다 — 함수 표(`TracyProfilerBackendInternal::TracyClientAPI`)로 부르므로 Windows 는 켤 때 TracyClient.dll 을 올립니다.
 */
#include "pch.h"

#include "Engine/Profiling/Tracy/TracyProfilerBackend.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Module/ModuleLibraryUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/ProcessEnvironment.h"

#if defined( SW_PROFILER_TRACY )
    #include "Core/Foundation/PlatformOSHeaders.h"

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
            static uint8 toTracyGPUType( ProfilerGraphicsAPI api )
            {
                switch ( api )
                {
                    case ProfilerGraphicsAPI::OpenGl:
                        return 1;
                    case ProfilerGraphicsAPI::Vulkan:
                        return 2;
                    case ProfilerGraphicsAPI::Direct3D12:
                        return 4;
                    case ProfilerGraphicsAPI::Direct3D11:
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

#if defined( SW_PROFILER_TRACY )
            /**
             * @brief Tracy C API 함수 표입니다. 리눅스는 링크한 함수의 주소를, Windows 는 `-gv_tracy` 로 켤 때 TracyClient.dll 을 올려 찾은 주소를 담습니다.
             * @details Windows 는 import 라이브러리를 링크하지 않는다 — 일반 import 면 DLL 이 올라오는 순간(프로세스 시작) Tracy 수집 스레드 · 리슨 소켓이
             *          서고, 지연 로드(/DELAYLOAD)는 lld 썽크가 첫 호출의 float 인자(`___tracy_emit_plot` 의 값)를 망가뜨려 쓰지 않는다.
             */
            struct TracyClientAPI
            {
                decltype( &___tracy_connected )               _pfnConnected{ nullptr };
                decltype( &___tracy_emit_zone_begin )         _pfnZoneBegin{ nullptr };
                decltype( &___tracy_emit_zone_end )           _pfnZoneEnd{ nullptr };
                decltype( &___tracy_emit_frame_mark )         _pfnFrameMark{ nullptr };
                decltype( &___tracy_emit_plot )               _pfnPlot{ nullptr };
                decltype( &___tracy_emit_memory_alloc_named ) _pfnMemoryAllocate{ nullptr };
                decltype( &___tracy_emit_memory_free_named )  _pfnMemoryFree{ nullptr };
                decltype( &___tracy_emit_gpu_new_context )    _pfnGPUNewContext{ nullptr };
                decltype( &___tracy_emit_gpu_context_name )   _pfnGPUContextName{ nullptr };
                decltype( &___tracy_emit_gpu_time_sync )      _pfnGPUTimeSync{ nullptr };
                decltype( &___tracy_emit_gpu_zone_begin )     _pfnGPUZoneBegin{ nullptr };
                decltype( &___tracy_emit_gpu_time )           _pfnGPUTime{ nullptr };
                decltype( &___tracy_emit_gpu_zone_end )       _pfnGPUZoneEnd{ nullptr };
                bool                                          _bLoaded{ false }; ///< 모든 칸이 찼다

                /** @brief 모든 칸이 찼는지 봅니다. */
                bool hasEveryFunction() const
                {
                    return _pfnConnected != nullptr && _pfnZoneBegin != nullptr && _pfnZoneEnd != nullptr && _pfnFrameMark != nullptr && _pfnPlot != nullptr &&
                           _pfnMemoryAllocate != nullptr && _pfnMemoryFree != nullptr && _pfnGPUNewContext != nullptr && _pfnGPUContextName != nullptr &&
                           _pfnGPUTimeSync != nullptr && _pfnGPUZoneBegin != nullptr && _pfnGPUTime != nullptr && _pfnGPUZoneEnd != nullptr;
                }
            };

    #if defined( SW_PLATFORM_WINDOWS )
            /** @brief TracyClient.dll 에서 @p pSymbol 을 찾아 @p outFunction 에 넣습니다. */
            template <typename TFunction>
            static void findClientFunction( void* pClient, const utf8* pSymbol, TFunction& outFunction )
            {
                outFunction = reinterpret_cast<TFunction>( ModuleLibraryUtil::getDynamicSymbol( pClient, pSymbol ) );
            }
    #endif

            /** @brief 함수 표를 채웁니다. Windows 는 `Bin` 의 TracyClient.dll 을 올립니다(없으면 빈 표). */
            static TracyClientAPI loadClientAPI()
            {
                TracyClientAPI api{};
    #if defined( SW_PLATFORM_WINDOWS )
                // Tracy 는 DLL 이 올라올 때 모든 인터페이스에서 연결을 기다려 Windows 방화벽 허용 창이 뜬다. 같은 PC 의 뷰어만 받도록 loopback 에 묶는다.
                // 다른 PC 의 뷰어를 받으려면 실행 전에 환경 변수 TRACY_ONLY_LOCALHOST=0 을 정한다(빈 값도 사용자가 정한 값이다). Tracy 클라이언트가 CRT 환경 복사본을 읽으므로 `trySet` 이 복사본도 바꾼다.
                string tracyOnlyLocalhost;
                if ( ProcessEnvironment::tryRead( "TRACY_ONLY_LOCALHOST", tracyOnlyLocalhost ) == false )
                    (void)ProcessEnvironment::trySet( "TRACY_ONLY_LOCALHOST", "1" ); // 못 정해도 방화벽 허용 창이 뜰 뿐이다
                // 엔진 DLL 과 같은 폴더(`Bin`)에 빌드가 둔다. 없으면(Tracy 를 끈 배포) 빈 표다 - 선택 구성 요소라 로드 실패 경고를 내지 않게 먼저 묻는다.
                const string                      clientText = FileUtil::joinPath( FileUtil::getBinaryDirectory(), "TracyClient.dll" );
                const std::optional<AbsolutePath> clientPath = AbsolutePath::tryMake( clientText );
                if ( clientPath.has_value() == false || FileUtil::isRegularFile( clientText ) == false )
                    return api;
                void* const hClient = ModuleLibraryUtil::loadLibraryAt( *clientPath );
                if ( hClient == nullptr )
                    return api;
                findClientFunction( hClient, "___tracy_connected", api._pfnConnected );
                findClientFunction( hClient, "___tracy_emit_zone_begin", api._pfnZoneBegin );
                findClientFunction( hClient, "___tracy_emit_zone_end", api._pfnZoneEnd );
                findClientFunction( hClient, "___tracy_emit_frame_mark", api._pfnFrameMark );
                findClientFunction( hClient, "___tracy_emit_plot", api._pfnPlot );
                findClientFunction( hClient, "___tracy_emit_memory_alloc_named", api._pfnMemoryAllocate );
                findClientFunction( hClient, "___tracy_emit_memory_free_named", api._pfnMemoryFree );
                findClientFunction( hClient, "___tracy_emit_gpu_new_context", api._pfnGPUNewContext );
                findClientFunction( hClient, "___tracy_emit_gpu_context_name", api._pfnGPUContextName );
                findClientFunction( hClient, "___tracy_emit_gpu_time_sync", api._pfnGPUTimeSync );
                findClientFunction( hClient, "___tracy_emit_gpu_zone_begin", api._pfnGPUZoneBegin );
                findClientFunction( hClient, "___tracy_emit_gpu_time", api._pfnGPUTime );
                findClientFunction( hClient, "___tracy_emit_gpu_zone_end", api._pfnGPUZoneEnd );
    #else
                api._pfnConnected      = &___tracy_connected;
                api._pfnZoneBegin      = &___tracy_emit_zone_begin;
                api._pfnZoneEnd        = &___tracy_emit_zone_end;
                api._pfnFrameMark      = &___tracy_emit_frame_mark;
                api._pfnPlot           = &___tracy_emit_plot;
                api._pfnMemoryAllocate = &___tracy_emit_memory_alloc_named;
                api._pfnMemoryFree     = &___tracy_emit_memory_free_named;
                api._pfnGPUNewContext  = &___tracy_emit_gpu_new_context;
                api._pfnGPUContextName = &___tracy_emit_gpu_context_name;
                api._pfnGPUTimeSync    = &___tracy_emit_gpu_time_sync;
                api._pfnGPUZoneBegin   = &___tracy_emit_gpu_zone_begin;
                api._pfnGPUTime        = &___tracy_emit_gpu_time;
                api._pfnGPUZoneEnd     = &___tracy_emit_gpu_zone_end;
    #endif
                // 한 칸이라도 비면(다른 판의 DLL) 쓰지 않는다 — 계측 지점마다 칸을 검사하지 않기 위해서다.
                api._bLoaded = api.hasEveryFunction();
                return api;
            }

            /** @brief 함수 표입니다. 처음 부를 때 채웁니다(정적 지역 초기화라 여러 스레드가 함께 불러도 한 번). */
            static const TracyClientAPI& getClientAPI()
            {
                static const TracyClientAPI s_api = loadClientAPI();
                return s_api;
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
        , _nextGPUQueryID{ 0 }
    {
    }

    bool TracyProfilerBackend::loadClient()
    {
#if defined( SW_PROFILER_TRACY )
        return TracyProfilerBackendInternal::getClientAPI()._bLoaded;
#else
        return false;
#endif
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
        string portText;
        if ( ProcessEnvironment::tryReadNonEmpty( "TRACY_PORT", portText ) == false )
            return TracyProfilerBackendInternal::kDefaultDataPort;
        int32      port{ 0 };
        const bool bParsed  = StringUtil::parseInt( portText, port );
        const bool bInRange = 0 < port && port <= TracyProfilerBackendInternal::kMaxPort;
        if ( bParsed && bInRange )
            return static_cast<uint16>( port );
        return TracyProfilerBackendInternal::kDefaultDataPort;
    }

    bool TracyProfilerBackend::isViewerConnected() const
    {
#if defined( SW_PROFILER_TRACY )
        const TracyProfilerBackendInternal::TracyClientAPI& api = TracyProfilerBackendInternal::getClientAPI();
        return api._bLoaded && api._pfnConnected() != 0;
#else
        return false;
#endif
    }

    uint64 TracyProfilerBackend::beginZone( [[maybe_unused]] const ProfileZoneSite& site )
    {
#if defined( SW_PROFILER_TRACY )
        _zoneCount.fetch_add( 1, std::memory_order_relaxed );
        const TracyCZoneCtx zone = TracyProfilerBackendInternal::getClientAPI()._pfnZoneBegin( reinterpret_cast<const ___tracy_source_location_data*>( &site ), 1 );
        return TracyProfilerBackendInternal::packZone( zone );
#else
        return 0;
#endif
    }

    void TracyProfilerBackend::endZone( [[maybe_unused]] uint64 zoneToken )
    {
#if defined( SW_PROFILER_TRACY )
        TracyProfilerBackendInternal::getClientAPI()._pfnZoneEnd( TracyProfilerBackendInternal::unpackZone( zoneToken ) );
#endif
    }

    void TracyProfilerBackend::markFrame( [[maybe_unused]] const utf8* pFrameName )
    {
#if defined( SW_PROFILER_TRACY )
        TracyProfilerBackendInternal::getClientAPI()._pfnFrameMark( pFrameName );
#endif
    }

    void TracyProfilerBackend::plotValue( [[maybe_unused]] const utf8* pPlotName, [[maybe_unused]] float64 value )
    {
#if defined( SW_PROFILER_TRACY )
        if ( pPlotName != nullptr )
            TracyProfilerBackendInternal::getClientAPI()._pfnPlot( pPlotName, value );
#endif
    }

    void TracyProfilerBackend::onAllocate( [[maybe_unused]] const void* pPtr, [[maybe_unused]] size_t size,
                                           [[maybe_unused]] const utf8* pPoolName )
    {
#if defined( SW_PROFILER_TRACY )
        TracyProfilerBackendInternal::getClientAPI()._pfnMemoryAllocate( pPtr, size, pPoolName );
#endif
    }

    void TracyProfilerBackend::onFree( [[maybe_unused]] const void* pPtr, [[maybe_unused]] const utf8* pPoolName )
    {
#if defined( SW_PROFILER_TRACY )
        TracyProfilerBackendInternal::getClientAPI()._pfnMemoryFree( pPtr, pPoolName );
#endif
    }

    uint32 TracyProfilerBackend::createGPUContext( [[maybe_unused]] ProfilerGraphicsAPI api, [[maybe_unused]] const utf8* pName,
                                                   [[maybe_unused]] int64 gpuNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( _gpuContextCount >= kMaxGPUContext )
            return kInvalidGPUContext;
        const uint32 context = _gpuContextCount++;

        // 시각은 나노초로 넘긴다(period 1). 이 순간의 GPU 시계와 Tracy 가 찍는 CPU 시계의 차가 이 큐의 기준이 된다.
        ___tracy_gpu_new_context_data newContext{};
        newContext.gpuTime = gpuNanos;
        newContext.period  = 1.0f;
        newContext.context = static_cast<uint8_t>( context );
        newContext.flags   = 0;
        newContext.type    = TracyProfilerBackendInternal::toTracyGPUType( api );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUNewContext( newContext );

        if ( pName != nullptr )
        {
            ___tracy_gpu_context_name_data nameData{};
            nameData.context = static_cast<uint8_t>( context );
            nameData.name    = pName;
            nameData.len     = static_cast<uint16_t>( std::strlen( pName ) );
            TracyProfilerBackendInternal::getClientAPI()._pfnGPUContextName( nameData );
        }
        return context;
#else
        return kInvalidGPUContext;
#endif
    }

    void TracyProfilerBackend::syncGPUClock( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] int64 gpuNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        ___tracy_gpu_time_sync_data syncData{};
        syncData.gpuTime = gpuNanos;
        syncData.context = static_cast<uint8_t>( gpuContext );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUTimeSync( syncData );
#endif
    }

    void TracyProfilerBackend::beginGPUZone( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] const ProfileZoneSite& site,
                                             [[maybe_unused]] int64 gpuBeginNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        // 시각을 이미 안다 — 구간을 열면서 그 쿼리의 시각을 바로 넘긴다. 서버는 쿼리 번호로 둘을 잇는다.
        const uint16                 queryID = allocateGPUQueryID();
        ___tracy_gpu_zone_begin_data beginData{};
        beginData.srcloc  = static_cast<uint64_t>( reinterpret_cast<uintptr_t>( &site ) );
        beginData.queryId = queryID;
        beginData.context = static_cast<uint8_t>( gpuContext );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUZoneBegin( beginData );

        ___tracy_gpu_time_data timeData{};
        timeData.gpuTime = gpuBeginNanos;
        timeData.queryId = queryID;
        timeData.context = static_cast<uint8_t>( gpuContext );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUTime( timeData );
#endif
    }

    void TracyProfilerBackend::endGPUZone( [[maybe_unused]] uint32 gpuContext, [[maybe_unused]] int64 gpuEndNanos )
    {
#if defined( SW_PROFILER_TRACY )
        if ( gpuContext >= _gpuContextCount )
            return;
        const uint16               queryID = allocateGPUQueryID();
        ___tracy_gpu_zone_end_data endData{};
        endData.queryId = queryID;
        endData.context = static_cast<uint8_t>( gpuContext );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUZoneEnd( endData );

        ___tracy_gpu_time_data timeData{};
        timeData.gpuTime = gpuEndNanos;
        timeData.queryId = queryID;
        timeData.context = static_cast<uint8_t>( gpuContext );
        TracyProfilerBackendInternal::getClientAPI()._pfnGPUTime( timeData );
        _gpuZoneCount.fetch_add( 1, std::memory_order_relaxed );
#endif
    }

    uint16 TracyProfilerBackend::allocateGPUQueryID()
    {
        const uint16 queryID = _nextGPUQueryID;
        _nextGPUQueryID      = static_cast<uint16>( _nextGPUQueryID + 1u );
        return queryID;
    }
} // namespace sw

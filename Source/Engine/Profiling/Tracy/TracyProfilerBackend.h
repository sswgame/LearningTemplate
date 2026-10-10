/**
 * @file TracyProfilerBackend.h
 * @brief Tracy 출력(`IProfilerBackend`)입니다. Tracy 헤더는 짝 .cpp 하나만 include 합니다(`CheckThirdPartyIsolation.py`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include "Engine/Profiling/IProfilerBackend.h"

namespace sw
{
    /**
     * @class TracyProfilerBackend
     * @brief Tracy C API 로 구간 · 프레임 · 그래프 · 메모리 · GPU 타임라인을 냅니다.
     * @details **GPU 구간은 "수동 GPU 컨텍스트" 로 냅니다.** Tracy 의 API 별 헤더(TracyD3D12 · TracyVulkan …)를 쓰지 않습니다 — 그것들은
     *          RHI 백엔드 DLL 안에서 네이티브 쿼리를 두 번째로 만들고, 백엔드 DLL 마다 클라이언트를 링크하게 만듭니다. 대신 엔진이 이미
     *          네 백엔드에서 모으는 타임스탬프(`IRHIDevice::readTimestamps`)를 그대로 Tracy 에 넘깁니다 — 쿼리는 한 벌이고, Tracy 클라이언트는
     *          Engine.dll 하나만 링크합니다.
     *
     *          Tracy 가 이 빌드에 없으면 모든 함수가 아무것도 하지 않습니다(`ProfilerBackend` 가 이 출력을 켜지도 않는다).
     */
    class SW_API TracyProfilerBackend final : public IProfilerBackend
    {
    public:
        /** @brief GPU 컨텍스트 수 상한입니다. Tracy 의 컨텍스트 번호는 1 바이트이고, 디바이스를 바꿀 때마다 하나씩 씁니다. */
        static constexpr uint32 kMaxGPUContext = 255;

        TracyProfilerBackend();
        ~TracyProfilerBackend() override = default;

        TracyProfilerBackend( const TracyProfilerBackend& )            = delete;
        TracyProfilerBackend& operator=( const TracyProfilerBackend& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) IProfilerBackend
        // ------------------------------------------------------------------------------
        const utf8* getBackendName() const override { return "Tracy"; }
        bool        isViewerConnected() const override;

        uint64 beginZone( const ProfileZoneSite& site ) override;
        void   endZone( uint64 zoneToken ) override;

        void markFrame( const utf8* pFrameName ) override;
        void plotValue( const utf8* pPlotName, float64 value ) override;

        void onAllocate( const void* pPtr, size_t size, const utf8* pPoolName ) override;
        void onFree( const void* pPtr, const utf8* pPoolName ) override;

        uint32 createGPUContext( ProfilerGraphicsAPI api, const utf8* pName, int64 gpuNanos ) override;
        void   syncGPUClock( uint32 gpuContext, int64 gpuNanos ) override;
        void   beginGPUZone( uint32 gpuContext, const ProfileZoneSite& site, int64 gpuBeginNanos ) override;
        void   endGPUZone( uint32 gpuContext, int64 gpuEndNanos ) override;

        // ------------------------------------------------------------------------------
        // 2) 이 빌드 · 상태
        // ------------------------------------------------------------------------------
        /** @brief Tracy 클라이언트가 이 빌드에 링크되어 있으면 true 입니다. */
        static bool isCompiled();
        /** @brief Tracy 데이터 포트입니다(`TRACY_PORT` 환경 변수가 있으면 그 값, 없으면 8086). */
        static uint16 getDataPort();
        /** @brief 지금까지 낸 GPU 구간 수입니다(시험 · 에디터 표시). */
        uint64 getGPUZoneCount() const { return _gpuZoneCount.load( std::memory_order_relaxed ); }
        /** @brief 지금까지 연 CPU 구간 수입니다(시험 · 에디터 표시). */
        uint64 getZoneCount() const { return _zoneCount.load( std::memory_order_relaxed ); }

    private:
        /** @brief 다음 GPU 쿼리 번호입니다. 시각을 begin · end 와 같은 자리에서 바로 넘기므로 번호는 돌려 써도 겹치지 않습니다. */
        uint16 allocateGPUQueryId();

    private:
        atomic<uint64>          _zoneCount;       ///< 연 CPU 구간 수
        atomic<uint64>          _gpuZoneCount;    ///< 닫은 GPU 구간 수
        [[maybe_unused]] uint32 _gpuContextCount; ///< 만든 GPU 컨텍스트 수(렌더 스레드만 쓴다). Tracy 없는 빌드에서는 쓰이지 않는다
        uint16                  _nextGPUQueryId;  ///< 렌더 스레드만 쓴다
    };
} // namespace sw

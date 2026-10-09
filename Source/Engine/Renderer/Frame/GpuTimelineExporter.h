/**
 * @file GpuTimelineExporter.h
 * @brief 엔진이 읽은 GPU 타임스탬프 한 프레임을 외부 프로파일러(Tracy)의 GPU 타임라인으로 내보냅니다.
 *
 * [쿼리는 한 벌]
 * 네 백엔드는 이미 패스마다 타임스탬프를 적고 몇 프레임 뒤에 읽습니다(`IRHIDevice::readTimestamps`). 그 값이 엔진 표의 `GPU.<패스>` 줄이 되고,
 * 같은 값이 여기서 Tracy GPU 구간이 됩니다. Tracy 의 API 별 헤더(TracyD3D12 …)로 쿼리를 따로 만들지 않습니다 — 두 번 재면 서로를 잽니다.
 *
 * [왜 기록 때가 아니라 읽을 때 내는가]
 * 구간 시작 · 끝을 패스를 기록하는 워커가 내면 병렬 기록의 워커마다 컨텍스트 · 쿼리 번호를 나눠야 하고, 읽기를 놓친 프레임의 구간이 뷰어에서
 * 영영 열린 채 남습니다. 여기서는 렌더 스레드가 **이미 읽힌** 한 프레임을 열고 닫기 짝을 맞춰 한 번에 냅니다. 대가는 뷰어의 "CPU 쪽 발행 시각"
 * 이 기록 시각이 아니라 읽은 시각이라는 것 하나입니다(GPU 타임라인의 위치 · 길이는 정확하다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Profiling/IProfilerBackend.h"

namespace sw
{
    struct RHIGpuTimestampFrame;

    /**
     * @class GpuTimelineExporter
     * @brief GPU 컨텍스트 하나(디바이스 하나)의 수명 · 시계 맞추기 · 프레임 내보내기입니다. 렌더 스레드만 씁니다.
     */
    class SW_API GpuTimelineExporter
    {
    public:
        /** @brief 싼 시계(DX12 · GL)를 몇 프레임마다 다시 맞추는가입니다. GPU 와 CPU 시계는 서로 흐릅니다. */
        static constexpr uint32 kResyncFrameInterval = 240;

        GpuTimelineExporter();

        /** @brief @p pDeviceIdentity 디바이스 · @p pBackend 출력의 컨텍스트가 열려 있으면 true 입니다. 둘 중 하나가 바뀌면 다시 엽니다. */
        bool isContextOpenFor( const void* pDeviceIdentity, const IProfilerBackend* pBackend ) const;
        /**
         * @brief 컨텍스트를 엽니다. @p gpuNowNanos 는 **지금** GPU 시계(타임스탬프와 같은 영역)입니다.
         * @return 출력이 컨텍스트를 만들었으면 true 입니다.
         */
        [[nodiscard]] bool openContext( IProfilerBackend& backend, ProfilerGpuApi api, const utf8* pName, const void* pDeviceIdentity, int64 gpuNowNanos );
        /** @brief 디바이스가 사라졌을 때 잊습니다(출력 쪽 컨텍스트는 Tracy 가 닫지 않는다 — 다음 디바이스는 새 번호를 쓴다). */
        void forgetContext();

        /** @brief 프레임 하나를 세고, 시계를 다시 맞출 차례면 true 입니다. */
        bool advanceAndCheckResync();
        /** @brief 시계를 다시 맞춥니다. */
        void resyncClock( IProfilerBackend& backend, int64 gpuNowNanos );

        /**
         * @brief 이미 읽힌 한 프레임을 GPU 구간 트리(프레임 ⊃ 컴퓨트 프리패스 · 패스들)로 냅니다.
         * @param listPassSite 패스 번호 → 지점(없으면 nullptr — 그 패스는 건너뛴다). 칸 배치는 `FrameRendererUtil` 의 타임스탬프 칸 배치입니다.
         * @return 낸 구간 수(프레임 구간 포함)입니다. 컨텍스트가 없거나 적힌 칸이 없으면 0 입니다.
         * @details 형제 구간이 겹치거나(같은 큐라 실제로는 순서대로다 — 칸 해상도 차이) 부모 밖으로 나가면 뷰어의 트리가 깨지므로 시작 순으로
         *          정렬하고 앞 형제의 끝 · 부모의 범위로 자릅니다.
         */
        uint32 exportFrame( IProfilerBackend& backend, const RHIGpuTimestampFrame& frame, const ProfileZoneSite& frameSite,
                            const ProfileZoneSite& computeSite, const vector<const ProfileZoneSite*>& listPassSite );

    private:
        /** @brief 내보낼 구간 하나(나노초, GPU 시계 영역)입니다. */
        struct Span
        {
            int64                  _beginNanos;
            int64                  _endNanos;
            const ProfileZoneSite* _pSite;
        };

    private:
        vector<Span>            _listScratchSpan; ///< 프레임마다 다시 쓴다(할당을 되풀이하지 않게 든다)
        const void*             _pDeviceIdentity; ///< 컨텍스트를 연 디바이스(비교만 한다)
        const IProfilerBackend* _pBackend;        ///< 컨텍스트를 연 출력(비교만 한다)
        uint32                  _gpuContext;      ///< 출력의 컨텍스트 번호
        uint32                  _framesSinceSync; ///< 마지막 시계 맞추기 뒤 프레임 수
    };
} // namespace sw

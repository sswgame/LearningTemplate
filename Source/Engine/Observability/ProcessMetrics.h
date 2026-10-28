/**
 * @file ProcessMetrics.h
 * @brief 프로세스 지표(상주 메모리, 최대 상주 메모리, 시작 시각)를 지표 레지스트리에 올리고 갱신합니다.
 * @details - 이름은 Prometheus 클라이언트 라이브러리의 표준 프로세스 지표(`process_resident_memory_bytes`, `process_start_time_seconds`)를 따릅니다.
 *            그래서 대시보드와 경보 규칙을 다른 서버와 같이 씁니다.
 *          - 운영체제에 묻는 비용이 있으므로 서버는 초에 한 번쯤 `update` 를 부릅니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class MetricGauge;
    class MetricRegistry;

    /**
     * @class ProcessMetrics
     * @brief 프로세스 지표 묶음입니다. 레지스트리를 빌려 쓰며 레지스트리보다 먼저 사라져야 합니다.
     */
    class SW_API ProcessMetrics
    {
    public:
        ProcessMetrics();

        /** @brief 지표를 올리고 시작 시각을 적습니다. @p pRegistry 가 null 이면 아무것도 세지 않습니다. */
        void initialize( MetricRegistry* pRegistry, int64 startUnixMilliseconds );
        /** @brief 메모리를 다시 읽어 게이지를 바꿉니다. */
        void update();

        /** @brief 이 프로세스의 상주 메모리(바이트)와 지금까지의 최대값입니다. 운영체제가 답하지 못하면 false 입니다. */
        [[nodiscard]] static bool tryGetResidentBytes( uint64& outResidentBytes, uint64& outPeakResidentBytes );

    private:
        MetricGauge* _pResidentGauge;
        MetricGauge* _pPeakResidentGauge;
    };
} // namespace sw

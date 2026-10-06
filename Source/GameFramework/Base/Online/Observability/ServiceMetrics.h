/**
 * @file ServiceMetrics.h
 * @brief 서비스 하나의 표준 지표 — `service_requests_total{service,method,result}` · `service_request_seconds{service,method}` · `service_store_pending{service}`.
 * @details 메서드 · 결과 이름은 서비스가 등록 때 넘기는 닫힌 집합이다(라벨에 계정 id · 추적 id 를 넣지 않는다). 등록부(`Engine/Observability/MetricRegistry`)가
 *          null 이면 모든 호출이 아무것도 하지 않는다. 지연은 응답 시점에 재므로 서비스는 받은 시각을 일 객체에 들고 `complete` 에서 `observe` 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class MetricCounter;
    class MetricGauge;
    class MetricHistogram;
    class MetricRegistry;
} // namespace sw

namespace sw
{
    /** @class ServiceMetrics @brief 서비스 하나의 지표 묶음입니다. 메서드 · 결과는 0 부터의 번호로 부른다. */
    class SW_GF_API ServiceMetrics
    {
    public:
        ServiceMetrics();

        /**
         * @brief 등록합니다. @p arrMethodName · @p arrResultName 은 번호 순 이름(정적 문자열)입니다.
         * @details 시리즈 수 = 메서드 × 결과 + 메서드 + 1 — 서비스당 수십 개.
         */
        void initialize( MetricRegistry* pRegistry, const utf8* pServiceName, const utf8* const* arrMethodName, int32 methodCount, const utf8* const* arrResultName,
                         int32 resultCount );

        /** @brief 범위 밖 번호는 무시합니다. */
        void             countRequest( int32 methodIndex, int32 resultIndex );
        MetricHistogram* findLatencyHistogram( int32 methodIndex ) const;
        void             setPendingStoreWorkCount( int32 pendingCount );

    private:
        vector<MetricCounter*>   _listRequestCounter; ///< 메서드 × 결과
        vector<MetricHistogram*> _listLatency;        ///< 메서드
        MetricGauge*             _pPending;
        int32                    _methodCount;
        int32                    _resultCount;
    };
} // namespace sw

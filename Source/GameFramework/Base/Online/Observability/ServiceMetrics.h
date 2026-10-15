/**
 * @file ServiceMetrics.h
 * @brief 서비스 하나의 표준 지표 — `service_requests_total{service,method,result}` · `service_request_seconds{service,method}` · `service_store_pending{service}`.
 * @details 메서드 · 결과 이름은 서비스가 등록 때 넘기는 닫힌 집합이다(라벨에 계정 id · 추적 id 를 넣지 않는다). 레지스트리(`Engine/Observability/MetricRegistry`)가
 *          null 이면 모든 호출이 아무것도 하지 않는다. 지연은 응답 시점에 재므로 서비스는 받은 시각을 일 객체에 들고 `complete` 에서 `observe` 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/EnumName.h"

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
         * @brief 등록합니다. 메서드 라벨은 @p TMethodName(키트의 메서드 이름 표, `OnlineMethodTable`), 결과 라벨은 @p TResult 의 열거자 이름(`ENUM()`)을
         *        snake_case 로 바꾼 것입니다(`NotSignedIn` → `not_signed_in`). 번호는 각각 `0 .. Count - 1` 입니다.
         * @details 시리즈 수 = 메서드 × 결과 + 메서드 + 1 — 서비스당 수십 개.
         */
        template <typename TMethodName, typename TResult>
        void initialize( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            initializeWithNames( pRegistry, pServiceName, makeLabelNames<TMethodName>(), makeLabelNames<TResult>() );
        }

        /** @brief 범위 밖 번호는 무시합니다. */
        void             countRequest( int32 methodIndex, int32 resultIndex );
        MetricHistogram* findLatencyHistogram( int32 methodIndex ) const;
        void             setPendingStoreWorkCount( int32 pendingCount );

    private:
        /** @brief `ENUM( Count = Count )` 열거형의 열거자 이름을 순서대로 snake_case 로 바꿉니다. */
        template <typename TEnum>
        static vector<string> makeLabelNames()
        {
            vector<string> listName;
            for ( int32 index = 0; index < static_cast<int32>( TEnum::Count ); ++index )
            {
                listName.push_back( StringUtil::toSnakeCase( getEnumName( static_cast<TEnum>( index ) ), false ) );
            }
            return listName;
        }

        void initializeWithNames( MetricRegistry* pRegistry, const utf8* pServiceName, const vector<string>& listMethodName, const vector<string>& listResultName );

        vector<MetricCounter*>   _listRequestCounter; ///< 메서드 × 결과
        vector<MetricHistogram*> _listLatency;        ///< 메서드
        MetricGauge*             _pPending;
        int32                    _methodCount;
        int32                    _resultCount;
    };
} // namespace sw

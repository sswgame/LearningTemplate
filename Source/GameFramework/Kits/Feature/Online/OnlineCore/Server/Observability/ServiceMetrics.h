/**
 * @file ServiceMetrics.h
 * @brief 서비스 하나의 표준 지표입니다. `service_requests_total{service,method,result}`, `service_request_seconds{service,method}`, `service_store_pending{service}` · `service_store_pending_peak{service}` 를 둡니다.
 * @details 메서드와 결과 이름은 서비스가 등록 때 넘기는 닫힌 집합이다(라벨에 계정 id 나 추적 id 를 넣지 않는다). 레지스트리(`Engine/Observability/MetricRegistry`)가
 *          null 이면 모든 호출이 아무것도 하지 않는다. 지연은 응답 시점에 측정한다 — 서버 호출 표(`OnlineServerCallTable`)가 받은 시각을 들고 응답할 때 `observeRequest` 를 부른다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

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
    /** @class ServiceMetrics @brief 서비스 하나의 지표 그룹입니다. 메서드와 결과는 0 부터의 번호로 부릅니다. */
    class SW_KIT_SERVER_API ServiceMetrics
    {
    public:
        ServiceMetrics();

        /**
         * @brief 등록합니다. 메서드 라벨은 @p TMethodName(키트의 메서드 이름 테이블, `OnlineMethodTable`), 결과 라벨은 @p TResult 의 열거자 이름(`ENUM()`)을
         *        snake_case 로 바꾼 것입니다(`NotSignedIn` → `not_signed_in`). 번호는 각각 `0 .. Count - 1` 입니다.
         * @details 시리즈 수는 메서드 × 결과 + 메서드 + 1 이라 서비스당 수십 개다.
         */
        template <typename TMethodName, typename TResult>
        void initialize( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            initializeWithNames( pRegistry, pServiceName, makeLabelNames<TMethodName>(), makeLabelNames<TResult>() );
        }

        /** @brief `initialize` 와 같고 결과 라벨 뒤에 공통 범주(`OnlineError`)를 더합니다. 프로토콜 표로 선 키트(`OnlineServerCallTable`)가 쓰며, 공통 범주는 `observeError` 로 셉니다. */
        template <typename TMethodName, typename TResult>
        void initializeWithErrors( MetricRegistry* pRegistry, const utf8* pServiceName )
        {
            vector<string> listResultName = makeLabelNames<TResult>();
            const int32    errorBase      = static_cast<int32>( listResultName.size() );
            appendErrorLabelNames( listResultName );
            initializeWithNames( pRegistry, pServiceName, makeLabelNames<TMethodName>(), listResultName );
            _errorResultBase = errorBase;
        }

        /** @brief 끝난 요청 하나를 셉니다. 요청 수에 더하고 @p receivedNanoseconds(`MonotonicClock`)부터 지금까지를 지연 히스토그램에 넣습니다. 범위 밖 번호는 무시합니다. */
        void observeRequest( int32 methodIndex, int32 resultIndex, int64 receivedNanoseconds );
        /** @brief 공통 범주 @p errorCode 로 끝난 요청 하나를 셉니다. 표에 없는 코드는 `internal` 로 셉니다. */
        void observeError( int32 methodIndex, uint16 errorCode, int64 receivedNanoseconds );
        /** @brief 기다리는 호출 수를 씁니다. 실행 중 최대치도 `service_store_pending_peak` 에 남깁니다 — 호출 표의 저장 모양(선형 · 색인)을 고를 근거가 이 값이다. */
        void setPendingCallCount( int32 pendingCount );

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

        static void appendErrorLabelNames( vector<string>& inoutListName );
        void        initializeWithNames( MetricRegistry* pRegistry, const utf8* pServiceName, const vector<string>& listMethodName, const vector<string>& listResultName );

    private:
        vector<MetricCounter*>   _listRequestCounter; ///< 메서드 × 결과
        vector<MetricHistogram*> _listLatency;        ///< 메서드
        MetricGauge*             _pPending;
        MetricGauge*             _pPendingPeak;
        int32                    _pendingPeakCount;
        int32                    _methodCount;
        int32                    _resultCount;
        int32                    _errorResultBase; ///< 공통 범주 라벨의 첫 결과 번호(`initializeWithErrors`). 없으면 음수이고 `observeError` 는 지연만 잰다
    };
} // namespace sw

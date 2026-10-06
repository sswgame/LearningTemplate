/**
 * @file MetricRegistry.h
 * @brief 서버 운영 지표 — 카운터(늘기만) · 게이지(현재 값) · 히스토그램(분포)을 이름 + 고정 라벨로 등록하고, Prometheus 텍스트 형식(0.0.4)으로 씁니다.
 * @details - 등록은 잠금 하나(기동 때), 값 바꾸기는 원자 연산(아무 스레드), 쓰기(`writePrometheusText`)는 잠금 안에서 원자 값을 읽는다(I/O 스레드).
 *          - 이름 `[a-zA-Z_:][a-zA-Z0-9_:]*`, 라벨 이름 `[a-zA-Z_][a-zA-Z0-9_]*`(`__` 로 시작 · `le` 금지). 카운터 이름은 관례로 `_total` 로 끝낸다.
 *          - 라벨에 계정 id · 추적 id · 자유 글을 넣지 않는다(시리즈가 끝없이 는다). 결과 코드 · 메서드 이름 같은 닫힌 집합만.
 *          - 등록부는 프로세스(서버 조립)가 하나 들고 쓰는 쪽에 포인터로 넘긴다(정적 없음 — 핫 리로드 · 시험 격리). 포인터가 null 이면 세지 않는다.
 *          - `FrameProfiler`(개발 중 프레임 구간, 기본 꺼짐) · `TelemetryService`(동의 받은 클라이언트 사건)와 다른 자리 — 늘 켜진 서버 누계다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /** @brief 라벨 하나입니다. */
    struct MetricLabel
    {
        string _name{};
        string _value{};
    };
} // namespace sw

namespace sw
{
    /** @class MetricCounter @brief 늘기만 하는 수입니다. */
    class SW_API MetricCounter
    {
    public:
        MetricCounter();

        void   add( uint64 amount = 1 ) { _value.fetch_add( amount, std::memory_order_relaxed ); }
        uint64 getValue() const { return _value.load( std::memory_order_relaxed ); }

    private:
        atomic<uint64> _value;
    };
} // namespace sw

namespace sw
{
    /** @class MetricGauge @brief 오르내리는 현재 값입니다(실수 — 비트로 원자 저장). */
    class SW_API MetricGauge
    {
    public:
        MetricGauge();

        void    set( float64 value );
        void    add( float64 delta );
        float64 getValue() const;

    private:
        atomic<uint64> _bits;
    };
} // namespace sw

namespace sw
{
    /** @class MetricHistogram @brief 분포 — 칸 경계(오름차순, 마지막 `+Inf` 는 저절로)마다 개수, 그리고 합 · 개수입니다. */
    class SW_API MetricHistogram
    {
    public:
        explicit MetricHistogram( const vector<float64>& listUpperBound );

        /** @brief 값 하나를 넣습니다 — 경계와 같으면 그 칸이다(Prometheus `le`). */
        void observe( float64 value );

        int32   getBucketCount() const { return static_cast<int32>( _listUpperBound.size() ); }
        float64 getUpperBound( int32 bucketIndex ) const { return _listUpperBound[static_cast<size_t>( bucketIndex )]; }
        /** @brief 칸 하나의 개수(누적 아님)입니다. 마지막 칸 다음(= `getBucketCount()`)은 넘침 칸입니다. */
        uint64  getBucketValue( int32 bucketIndex ) const;
        uint64  getCount() const { return _count.load( std::memory_order_relaxed ); }
        float64 getSum() const;

    private:
        vector<float64>              _listUpperBound;
        unique_ptr<atomic<uint64>[]> _arrBucketValue; ///< 칸 수 + 1(넘침)
        atomic<uint64>               _count;
        atomic<uint64>               _sumBits;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScopedMetricTimer
     * @brief 만든 때부터 사라질 때까지의 초를 히스토그램에 넣습니다(null 이면 아무것도 하지 않는다). 동기 구간용 — 비동기 응답은 받은 시각을 들고 다니다 `observe` 한다.
     */
    class SW_API ScopedMetricTimer
    {
    public:
        explicit ScopedMetricTimer( MetricHistogram* pHistogram );
        ~ScopedMetricTimer();

        ScopedMetricTimer( const ScopedMetricTimer& )            = delete;
        ScopedMetricTimer& operator=( const ScopedMetricTimer& ) = delete;

    private:
        int64            _startNanoseconds;
        MetricHistogram* _pHistogram;
    };
} // namespace sw

namespace sw
{
    /**
     * @class MetricRegistry
     * @brief 지표 모음입니다. 돌려준 포인터는 등록부가 사는 동안 유효합니다.
     */
    class SW_API MetricRegistry
    {
    public:
        MetricRegistry();
        ~MetricRegistry();

        MetricRegistry( const MetricRegistry& )            = delete;
        MetricRegistry& operator=( const MetricRegistry& ) = delete;

        /** @brief 이름 · 라벨이 규칙 밖이거나 같은 이름이 다른 종류 · 다른 라벨 이름이면 nullptr 입니다(오류 로그). 같은 시리즈 두 번째는 같은 포인터입니다. */
        MetricCounter* registerCounter( string_view name, string_view help, const vector<MetricLabel>& listLabel = {} );
        MetricGauge*   registerGauge( string_view name, string_view help, const vector<MetricLabel>& listLabel = {} );
        /** @brief @p listUpperBound 는 엄격한 오름차순이어야 한다(아니면 nullptr). 같은 이름의 시리즈는 같은 경계여야 한다. */
        MetricHistogram* registerHistogram( string_view name, string_view help, const vector<float64>& listUpperBound, const vector<MetricLabel>& listLabel = {} );

        /** @brief 모든 지표를 Prometheus 텍스트(0.0.4)로 @p outText 뒤에 붙입니다. 이름 순, 한 이름 안은 등록 순입니다. */
        void  writePrometheusText( string& outText ) const;
        int32 getSeriesCount() const;

        /** @brief 요청 지연용 칸(초): 0.001 · 0.0025 · 0.005 · 0.01 · 0.025 · 0.05 · 0.1 · 0.25 · 0.5 · 1 · 2.5 · 5 · 10. */
        static vector<float64> makeLatencyBounds();

    private:
        enum class Kind : uint8
        {
            Counter = 0,
            Gauge,
            Histogram
        };

        struct Series
        {
            string                      _labelText{}; ///< `a="1",b="2"`(중괄호 없이, 이미 이스케이프)
            unique_ptr<MetricCounter>   _counter{};
            unique_ptr<MetricGauge>     _gauge{};
            unique_ptr<MetricHistogram> _histogram{};
        };

        struct Family
        {
            vector<unique_ptr<Series>> _listSeries{};
            vector<string>             _listLabelName{};
            vector<float64>            _listUpperBound{};
            string                     _name{};
            string                     _help{};
            Kind                       _kind{ Kind::Counter };
        };

        static bool isFamilyNameLess( const Family* pLeft, const Family* pRight ) { return pLeft->_name < pRight->_name; }

        Series* findOrAddSeries( Kind kind, string_view name, string_view help, const vector<MetricLabel>& listLabel, const vector<float64>* pUpperBound );

        vector<unique_ptr<Family>> _listFamily;
        mutable mutex              _mutex;
    };
} // namespace sw

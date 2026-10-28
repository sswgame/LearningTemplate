/**
 * @file CacheStats.h
 * @brief 캐시마다 적중(hit) · 실패(miss) · 비우기(clear) 횟수와 항목 수를 세는 계측 레지스트리입니다. 캐시는 이름으로 한 칸을 올리고 조회할 때 센다.
 * @details 상용 엔진의 같은 자리는 언리얼의 `stat` 카운터 그룹과 유니티 Profiler 의 카운터 카테고리입니다. 캐시를 늘리면서 크기와 적중률을 보지 못하면
 *          상한을 정할 근거가 없으므로 캐시는 만들 때 이 레지스트리에 이름으로 올립니다.
 *
 *          **센 값은 캐시 객체가 아니라 이 레지스트리가 이름으로 듭니다.** 캐시 구현이 조회하는 자리에서 `recordHit` · `recordMiss` 를 부릅니다. `IAssetCache` 가
 *          아닌 캐시(텍스트 측정 · 물리 셰이프 · 경로 캐시)도 같은 길로 센다. `IAssetCache` 구현은 생성자에서 `CacheTracking`(칸을 올리거나 세지 않는 사유를 적는다)을
 *          선언해야 지을 수 있어 "계측을 잊은 캐시" 가 생기지 않는다(`IAssetCache.h`).
 *
 *          **비용.** 카운터는 `relaxed` 원자 증가 하나이고 락 · 할당이 없습니다. Shipping 에서도 센다(보고만 Info 로그라 사라진다). 센 값은 모든 스레드가
 *          한 줄에 더하므로 경합이 큰 캐시는 칸 나눔(`ShardedCounter`)이 필요하지만, 지금 캐시는 게임 스레드가 대부분이고 워커는 로드 때만 더해 필요 없다.
 *          측정으로 경합이 보이면 이 클래스의 카운터만 `ShardedCounter` 로 바꾼다(호출부는 그대로).
 *
 *          **저장소는 힙이 아니라 고정 배열입니다.** 캐시 구현은 센 칸의 핸들을 함수 정적 변수에 쥐는데, 엔진 수명 범위(`ScopeOwned`)나 힙 레지스트리에 칸을
 *          두면 엔진을 다시 세울 때 그 핸들이 죽은 칸을 가리키고, 종료 누수 보고에도 오릅니다. 칸 수는 `kMaxCacheCount` 로 막고 넘치면 오류로 멈춘다(조용히 안 센다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class CacheStatsCounters
     * @brief 캐시 하나의 카운터 칸입니다. 이름이 하나 있고 프로세스가 끝날 때까지 사라지지 않습니다(`CacheStats` 가 고정 배열에 둡니다).
     */
    class alignas( 64 ) CacheStatsCounters final
    {
    public:
        /** @brief 이름 길이 상한입니다(널 제외). 캐시 종류 이름은 코드의 문자열 상수이므로 이 안에 든다. */
        static constexpr uint32 kMaxNameLength = 47;

        CacheStatsCounters()
            : _hitCount{ 0 }
            , _missCount{ 0 }
            , _clearCount{ 0 }
            , _itemCount{ 0 }
            , _bItemCountReported{ SW_FALSE }
            , _windowHitBase{ 0 }
            , _windowMissBase{ 0 }
            , _windowClearBase{ 0 }
            , _arrName{}
        {
        }

        CacheStatsCounters( const CacheStatsCounters& )            = delete;
        CacheStatsCounters& operator=( const CacheStatsCounters& ) = delete;

        /** @brief 조회가 캐시에서 답을 찾았습니다. */
        void recordHit() { _hitCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 조회가 답을 찾지 못해 새로 지었거나 읽었습니다. */
        void recordMiss() { _missCount.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 캐시를 통째로 비웠습니다(`clear`). 한 번에 몇 항목을 버렸는지는 세지 않는다. */
        void recordClear() { _clearCount.fetch_add( 1, std::memory_order_relaxed ); }
        /**
         * @brief 캐시가 들고 있는 항목 수를 @p delta 만큼 바꿉니다(음수는 줄어듦). 항목 수를 이 칸에서 세는 캐시만 부릅니다.
         * @details 같은 이름의 캐시 객체가 여럿(텍스트 측정 캐시는 UI 시스템마다 하나)이면 합이 됩니다. 부른 적 없는 칸은 항목 수를 모릅니다(`CacheStatsSnapshot::_bItemCountKnown`).
         */
        void addItemCount( int64 delta )
        {
            _itemCount.fetch_add( delta, std::memory_order_relaxed );
            _bItemCountReported.store( SW_TRUE, std::memory_order_relaxed );
        }

        /**
         * @brief 이름을 정합니다(레지스트리가 칸을 올릴 때 한 번). 비었거나 `kMaxNameLength` 보다 길면 바꾸지 않고 false 입니다.
         * @details 핸들은 칸을 const 로만 내주므로 캐시 구현은 부를 수 없습니다.
         */
        [[nodiscard]] bool trySetName( string_view name )
        {
            if ( name.empty() || kMaxNameLength < name.size() )
                return false;
            Memory::copy( _arrName, name.data(), name.size() );
            _arrName[name.size()] = '\0';
            return true;
        }

        /** @brief 이 칸의 이름입니다. */
        string_view getName() const { return string_view{ _arrName }; }
        /** @brief 지금까지 센 적중 수입니다. */
        uint64 getHitCount() const { return _hitCount.load( std::memory_order_relaxed ); }
        /** @brief 지금까지 센 실패 수입니다. */
        uint64 getMissCount() const { return _missCount.load( std::memory_order_relaxed ); }
        /** @brief 지금까지 센 비우기 수입니다. */
        uint64 getClearCount() const { return _clearCount.load( std::memory_order_relaxed ); }

    private:
        friend class CacheStats;

        atomic<uint64> _hitCount;
        atomic<uint64> _missCount;
        atomic<uint64> _clearCount;
        atomic<int64>  _itemCount;
        atomic<uint8>  _bItemCountReported;
        uint64         _windowHitBase;   ///< `CacheStats::markWindowStart` 때의 적중 수(레지스트리 잠금 아래)
        uint64         _windowMissBase;  ///< 같은 때의 실패 수
        uint64         _windowClearBase; ///< 같은 때의 비우기 수
        utf8           _arrName[kMaxNameLength + 1];
    };
} // namespace sw

namespace sw
{
    /**
     * @class CacheStatsHandle
     * @brief 카운터 칸을 가리키는 값입니다. 올리지 못한 칸(이름 중복 · 칸 부족)의 핸들은 비어 있고, 기록은 아무 일도 하지 않습니다.
     * @details 캐시 구현은 이 핸들을 `recordHit` 앞에서 검사하지 않고 그대로 부릅니다. 올리지 못한 이유는 올릴 때 오류 로그로 이미 남았습니다.
     */
    class CacheStatsHandle
    {
    public:
        CacheStatsHandle()
            : _pCounters{ nullptr }
        {
        }

        explicit CacheStatsHandle( CacheStatsCounters* pCounters )
            : _pCounters{ pCounters }
        {
        }

        /** @brief 칸을 가리키면 true 입니다. */
        bool isValid() const { return _pCounters != nullptr; }
        /** @brief `CacheStatsCounters::recordHit` 입니다. 빈 핸들이면 아무 일도 하지 않습니다. */
        void recordHit() const
        {
            if ( _pCounters != nullptr )
                _pCounters->recordHit();
        }
        /** @brief `CacheStatsCounters::recordMiss` 입니다. 빈 핸들이면 아무 일도 하지 않습니다. */
        void recordMiss() const
        {
            if ( _pCounters != nullptr )
                _pCounters->recordMiss();
        }
        /** @brief `CacheStatsCounters::recordClear` 입니다. 빈 핸들이면 아무 일도 하지 않습니다. */
        void recordClear() const
        {
            if ( _pCounters != nullptr )
                _pCounters->recordClear();
        }
        /** @brief `CacheStatsCounters::addItemCount` 입니다. 빈 핸들이면 아무 일도 하지 않습니다. */
        void addItemCount( int64 delta ) const
        {
            if ( _pCounters != nullptr )
                _pCounters->addItemCount( delta );
        }
        /** @brief 가리키는 칸입니다(없으면 nullptr). 테스트와 보고가 값을 읽을 때 씁니다. */
        const CacheStatsCounters* getCounters() const { return _pCounters; }

    private:
        CacheStatsCounters* _pCounters;
    };
} // namespace sw

namespace sw
{
    /** @brief 캐시 하나의 센 값을 한 시점에 읽은 것입니다(`CacheStats::collect`). 적중 · 실패 · 비우기는 측정 창 시작(`markWindowStart`) 뒤의 값입니다. */
    struct CacheStatsSnapshot
    {
        string _name{};
        uint64 _hitCount{ 0 };
        uint64 _missCount{ 0 };
        uint64 _clearCount{ 0 };
        int64  _itemCount{ 0 };           ///< `_bItemCountKnown` 일 때만 뜻이 있다.
        bool   _bItemCountKnown{ false }; ///< 캐시가 항목 수를 이 레지스트리에 알렸는가(`addItemCount`). 아니면 보고하는 쪽이 캐시 자신에게 묻는다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class CacheStats
     * @brief 캐시 이름 → 카운터 칸 레지스트리입니다. 프로세스에 하나이고 모든 함수는 어느 스레드에서 불러도 됩니다.
     */
    class SW_API CacheStats
    {
    public:
        /** @brief 칸 수 상한입니다. 엔진 캐시(약 35)와 테스트가 쓰고 남는 크기이고, 넘치면 `registerCache` 가 오류로 멈춥니다. */
        static constexpr uint32 kMaxCacheCount = 64;

        /**
         * @brief 이름의 칸을 새로 올립니다. 같은 이름이 이미 있으면 오류를 남기고 거절하며(먼저 칸을 둡니다) 빈 핸들을 돌려줍니다.
         * @details 프로세스에 한 번만 만들어지는 캐시(함수 정적 변수로 핸들을 쥐는 자리)가 씁니다. 엔진을 다시 세울 때 객체가 다시 만들어지는 캐시는 `getOrCreate`.
         */
        [[nodiscard]] static CacheStatsHandle registerCache( string_view name );
        /**
         * @brief 이름의 칸을 돌려주고 없으면 올립니다. 객체가 여러 번 만들어지는 캐시(엔진 수명 범위가 소유해 다시 서는 표 · UI 시스템마다 하나)가 같은 칸을 이어 씁니다.
         * @details 서로 다른 두 캐시가 같은 이름으로 부르면 한 칸에 섞이므로 이름은 캐시 종류 이름(레지스트리 종류 이름과 같은 값)으로 정한다.
         */
        static CacheStatsHandle getOrCreate( string_view name );

        /** @brief 측정 창을 지금 시작합니다 — 이후 `collect` 의 적중 · 실패 · 비우기는 이 시점부터의 값입니다. 항목 수는 영향이 없습니다(지금 값). */
        static void markWindowStart();
        /** @brief 올라 있는 칸 전부를 올린 순서대로 @p outListSnapshot 에 담습니다(비운 뒤 채웁니다). */
        static void collect( vector<CacheStatsSnapshot>& outListSnapshot );
        /** @brief 올라 있는 칸 수입니다. */
        static uint32 getCount();
    };
} // namespace sw

/**
 * @file SpawnDirector.h
 * @brief 스폰 감독 — 시간에 따라 쌓이는 위협 예산으로 무엇을 언제 낼지 정합니다(리썰 컴퍼니 · 공포 게임 몬스터 · 배틀로얄 보급 · 로그라이트 웨이브).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/Data/GameCurve.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class XmlNode;

    /** @brief 낼 수 있는 것 하나입니다. */
    struct SW_GF_API SpawnEntryDef
    {
        hashed_string         _id{};
        vector<hashed_string> _listTag{}; ///< 비면 태그 거르기와 상관없이 늘 낸다
        float32               _cost{ 1.0f };
        float32               _weight{ 1.0f };
        float32               _minTime{ 0.0f }; ///< 이 시각(초) 전에는 내지 않는다
        int32                 _max{ -1 };       ///< 동시에 살아 있는 상한. −1 이면 없음

        bool hasAnyTag( const vector<hashed_string>& listTag ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SpawnTable
     * @brief `<SpawnTable budgetPerMinute="4" maxBudget="12" startBudget="0" refund="false">
     *        <Entry id="bracken" cost="3" weight="2" max="1" minTime="60" tags="Indoor"/>
     *        <Curve time="0" scale="0.5"/><Curve time="600" scale="2"/></SpawnTable>` 를 읽습니다.
     */
    class SW_GF_API SpawnTable
    {
    public:
        SpawnTable();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );

        /** @brief @p time 의 예산 배율입니다 — 곡선 점 사이는 선형, 끝 밖은 끝 값, 곡선이 없으면 1 입니다. */
        float32 computeScale( float32 time ) const { return _curve.evaluate( time, 1.0f ); }

        const SpawnEntryDef*         findEntry( const hashed_string& id ) const { return _catalog.find( id ); }
        int32                        findEntryIndex( const hashed_string& id ) const { return _catalog.findIndex( id ); }
        const vector<SpawnEntryDef>& getEntries() const { return _catalog.getAll(); }
        const GameCurve&             getCurve() const { return _curve; }
        float32                      getBudgetPerMinute() const { return _budgetPerMinute; }
        float32                      getMaxBudget() const { return _maxBudget; }
        float32                      getStartBudget() const { return _startBudget; }
        bool                         isRefundOnDespawn() const { return _bRefundOnDespawn == SW_TRUE; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<SpawnEntryDef> _catalog;
        GameCurve                  _curve;
        float32                    _budgetPerMinute;
        float32                    _maxBudget;
        float32                    _startBudget;
        uint8                      _bRefundOnDespawn;
    };
} // namespace sw

namespace sw
{
    /** @brief 감독이 낸 일입니다. */
    struct SpawnEvent
    {
        enum class Kind : uint8
        {
            Spawned = 0, ///< 게임이 `_entryId` 를 실제로 만든다 — 죽으면 `_spawnId` 로 `notifyDespawned`
            Despawned
        };
        hashed_string _entryId{};
        float32       _time{ 0.0f };
        float32       _cost{ 0.0f }; ///< 낸 비용(Despawned 는 돌려받은 비용)
        uint32        _spawnId{ 0 };
        Kind          _kind{ Kind::Spawned };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SpawnDirector
     * @brief 예산은 `budgetPerMinute × 곡선 배율` 로 쌓여 `maxBudget` 에서 멈춥니다. 다음에 낼 것을 가중치로 미리 골라 두고(비용이 상한 안인 것 중)
     *        예산이 그 비용에 닿으면 냅니다 — 싼 것만 계속 나와 비싼 것이 영영 못 나오는 일이 없습니다.
     * @details 고를 수 있는 것: 최소 시각이 지났고, 동시 상한 아래이고, 태그 거르기를 지나는 것. 골라 둔 것이 그 조건에서 빠지면 다시 고릅니다.
     *          씨앗이 같고 같은 순서로 부르면 같은 것을 같은 때에 냅니다. 테이블은 빌려 씁니다.
     */
    class SW_GF_API SpawnDirector
    {
    public:
        static constexpr int32 kMaxSpawnsPerUpdate = 16; ///< 한 번에 내는 수 상한(긴 dt · 큰 시작 예산에서 한 프레임에 몰리지 않게)

        SpawnDirector();

        /** @brief 테이블(빌림) · 씨앗을 정하고 시계 · 예산 · 살아 있는 개체를 처음으로 돌립니다. */
        void initialize( const SpawnTable* pTable, uint32 seed );
        /** @brief 이 태그 중 하나를 가진 항목(과 태그 없는 항목)만 냅니다. 비우면 모두입니다. */
        void setAllowedTags( const vector<hashed_string>& listTag );
        /** @brief 죽은 개체의 비용을 예산으로 돌려줄지입니다(테이블 기본값을 덮는다). */
        void setRefundOnDespawn( bool bRefund ) { _bRefundOnDespawn = bRefund ? SW_TRUE : SW_FALSE; }
        /**
         * @brief 쌓이는 예산에 곱할 배율입니다(기본 1). 페이싱(`AiDirector`)이 단계마다 바꿉니다.
         * @details 0 이하이면 예산이 쌓이지도 쓰이지도 않습니다 — 쉬는 단계에서 모아 둔 예산으로 내지 않게 합니다.
         */
        void    setBudgetScale( float32 scale ) { _budgetScale = scale; }
        float32 getBudgetScale() const { return _budgetScale; }
        /** @brief 시간을 흘리고 낼 수 있는 만큼 냅니다. 이번에 낸 수입니다. */
        int32 update( float32 deltaTime );
        /** @brief 게임 쪽 개체가 사라졌음을 알립니다. 모르는(이미 알린) id 면 false 입니다. */
        bool notifyDespawned( uint32 spawnId );
        /** @brief 쌓인 일을 @p outListEvent 뒤에 붙이고 비웁니다. */
        void drainEvents( vector<SpawnEvent>& outListEvent );

        float32 getBudget() const { return _budget; }
        float32 getTime() const { return _time; }
        int32   getAliveCount( const hashed_string& entryId ) const;
        int32   getTotalAliveCount() const { return static_cast<int32>( _listAlive.size() ); }
        /** @brief 다음에 낼 것으로 골라 둔 항목입니다. 없으면 빈 이름입니다. */
        hashed_string getPendingEntry() const;

    private:
        /** @brief 살아 있는 개체 하나입니다. */
        struct SpawnAlive
        {
            uint32 _spawnId{ 0 };
            int32  _entryIndex{ -1 };
        };

        bool isEligible( int32 entryIndex ) const;
        void pickPending();

        vector<SpawnAlive>    _listAlive;
        vector<int32>         _listAliveCount; ///< 항목마다
        vector<hashed_string> _listAllowedTag;
        vector<SpawnEvent>    _listEvent;
        const SpawnTable*     _pTable;
        GameRandom            _random;
        float32               _budget;
        float32               _budgetScale;
        float32               _time;
        int32                 _pendingIndex; ///< −1 = 골라 둔 것 없음
        uint32                _nextSpawnId;
        uint8                 _bRefundOnDespawn;
    };
} // namespace sw

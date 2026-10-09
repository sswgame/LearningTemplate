/**
 * @file RtsWorld.h
 * @brief RTS 한 판 — 플레이어(자원 · 보급 · 팀) · 유닛 · 명령 대기열 · 채취 · 건설 · 생산 대기열 · 테크 · 전투 · 전장의 안개 · 승패입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Navigation/FlowField.h"
#include "GameFramework/Base/Actor/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Actor/Navigation/NavAgent.h"
#include "GameFramework/Base/Actor/Navigation/NavGrid.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/Foundation/Utility/Time/Countdown.h"
#include "GameFramework/Base/Foundation/Utility/Time/FixedStepTimer.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsCatalog.h"

namespace sw
{
    class Archive;
    class Wallet;

    /** @brief 유닛 id 입니다. 죽은 유닛의 자리가 다시 쓰여도 옛 id 는 세대가 달라 찾지 못합니다. */
    using RtsUnitId = SlotHandle;

    /** @brief 규칙의 수치입니다. 시간은 게임 초입니다. */
    struct RtsSettings
    {
        float32       _fixedStep{ 0.05f };             ///< 시뮬레이션 걸음(20 Hz — 스타크래프트의 게임 틱 근처)
        float32       _minimumDamage{ 0.5f };          ///< 방어가 높아도 이만큼은 들어간다
        float32       _repathInterval{ 0.5f };         ///< 움직이는 목표를 쫓을 때 경로를 다시 구하는 간격
        float32       _visionInterval{ 0.25f };        ///< 안개를 다시 칠하는 간격
        float32       _interactSlack{ 0.35f };         ///< 채취 · 짓기 · 반납이 닿았다고 보는 가장자리 거리
        float32       _resourceSearchRadius{ 8.0f };   ///< 다 캔 광물 대신 찾을 반경
        float32       _constructionStartRatio{ 0.1f }; ///< 짓기 시작한 건물의 체력 몫
        float32       _underAttackCooldown{ 10.0f };   ///< "공격받고 있다" 알림 간격(플레이어마다)
        int32         _productionQueueMax{ 5 };
        int32         _flowFieldGroupSize{ 6 };       ///< 이만큼 이상을 한 곳으로 보내면 A* 대신 흐름장 하나
        int32         _bucketSize{ 4 };               ///< 이웃 찾기 버킷(칸)
        hashed_string _mineralCurrency{ "Minerals" }; ///< 플레이어 지갑에서 광물로 쓰는 통화
        hashed_string _gasCurrency{ "Gas" };          ///< 플레이어 지갑에서 가스로 쓰는 통화
    };
} // namespace sw

namespace sw
{
    /** @brief 명령 종류입니다. */
    enum class RtsOrderType : uint8
    {
        Move = 0,
        AttackMove, ///< 가는 길에 적을 만나면 싸운다
        Attack,     ///< 그 유닛을 친다
        Gather,     ///< 자원(광물 · 정제소)을 캐서 본진으로 나른다(반복)
        Build,      ///< 일꾼 — 그 자리에 건물을 짓는다(이미 놓인 건물이면 이어 짓는다)
        Hold        ///< 제자리 — 사거리 안만 친다
    };

    /** @brief 명령 하나입니다. */
    struct RtsOrder
    {
        hashed_string    _buildId{};
        float3           _target{};
        int2             _buildCell{ -1, -1 };
        RtsUnitId        _targetUnit{};
        const FlowField* _pFlowField{ nullptr }; ///< 무리 이동 — 월드가 빌려 준 흐름장(명령이 끝나면 돌려준다)
        float32          _arriveRadius{ 0.0f };  ///< 무리 이동 — 목표 이 안이면 다 왔다(한 점에 모두 설 수 없다)
        RtsOrderType     _type{ RtsOrderType::Move };
    };
} // namespace sw

namespace sw
{
    /** @brief 명령 · 생산 결과입니다. */
    enum class RtsCommandResult : uint8
    {
        Ok = 0,
        InvalidUnit,
        NotOwner,
        CannotDo, ///< 그 유닛이 할 수 없다(일꾼이 아님 · 그 건물이 만들지 않음 등)
        NotEnoughMinerals,
        NotEnoughGas,
        NotEnoughSupply,
        TechRequired,
        QueueFull,
        InvalidPlacement
    };

    SW_GF_API const utf8* toString( RtsCommandResult result );

    /** @brief 일꾼의 채취 단계입니다. */
    enum class RtsGatherPhase : uint8
    {
        ToResource = 0,
        Waiting, ///< 다른 일꾼이 캐는 중
        Harvesting,
        Returning
    };

    /** @brief 유닛 · 건물 · 자원 하나입니다. */
    struct RtsUnit
    {
        NavAgent             _agent{};
        deque<RtsOrder>      _listOrder{};      ///< 앞이 지금 명령(쉬프트로 뒤에 붙인다)
        deque<hashed_string> _listProduction{}; ///< 생산 대기열(값은 넣을 때 낸다)
        const RtsUnitDef*    _pDef{ nullptr };
        float3               _position{};
        float3               _rallyPoint{};
        float3               _moveGoal{};     ///< 지금 다가가는 자리(다시 구할지 정한다)
        int2                 _cell{ -1, -1 }; ///< 건물 · 자원 — 왼쪽 아래 칸
        RtsUnitId            _id{};
        RtsUnitId            _attackTarget{};   ///< 지금 치는 유닛(명령 · 자동)
        RtsUnitId            _gatherTarget{};   ///< 캐는 자원 · 정제소
        RtsUnitId            _harvester{};      ///< 자원 · 정제소 — 지금 캐는 일꾼
        RtsUnitId            _builder{};        ///< 짓는 중인 건물 — 짓는 일꾼
        RtsUnitId            _linkedResource{}; ///< 정제소 — 아래 간헐천
        float32              _hp{ 0.0f };
        Countdown            _cooldown{};            ///< 다음 공격까지
        float32              _buildProgress{ 1.0f }; ///< 0..1 — 1 이면 다 지었다
        float32              _productionTimer{ 0.0f };
        float32              _gatherTimer{ 0.0f };
        Countdown            _repathTimer{};
        int32                _owner{ -1 }; ///< −1 = 주인 없음(자원)
        int32                _resourceLeft{ 0 };
        int32                _cargoAmount{ 0 };
        int32                _supplyReserved{ 0 }; ///< 건물 — 생산 중인 유닛의 보급
        RtsResourceType      _cargoType{ RtsResourceType::None };
        RtsGatherPhase       _gatherPhase{ RtsGatherPhase::ToResource };
        uint8                _bAlive{ SW_TRUE };
        uint8                _bHasRally{ SW_FALSE };
        uint8                _bOrderStarted{ SW_FALSE }; ///< 앞 명령의 경로를 구했다
        uint8                _bSupplyBlocked{ SW_FALSE };

        bool            isConstructed() const { return _buildProgress >= 1.0f; }
        bool            isBuilding() const { return _pDef != nullptr && _pDef->_kind == RtsUnitKind::Building; }
        bool            isResource() const { return _pDef != nullptr && _pDef->_kind == RtsUnitKind::Resource; }
        bool            isMobile() const { return _pDef != nullptr && _pDef->isMobile(); }
        bool            isIdle() const { return _listOrder.empty(); }
        const RtsOrder* findOrder() const { return _listOrder.empty() ? nullptr : &_listOrder.front(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어 하나입니다. */
    struct RtsPlayer
    {
        float3  _startPosition{};
        Wallet* _pWallet{ nullptr }; ///< 빌린 지갑(광물 · 가스 — `RtsSettings` 의 통화) — 지역 플레이어는 공유 지갑, AI 는 게임이 든다
        int32   _supplyUsed{ 0 };
        int32   _supplyCap{ 0 };
        int32   _team{ 0 };
        float32 _nextUnderAttackTime{ 0.0f }; ///< "공격받고 있다" 를 다시 알릴 수 있는 시각
        uint8   _bDefeated{ SW_FALSE };
        uint8   _bHadBuilding{ SW_FALSE }; ///< 건물이 있었던 적이 있다(시작 전에는 지지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 안개 상태입니다. */
    enum class RtsVisibility : uint8
    {
        Unexplored = 0,
        Explored, ///< 본 적은 있다(지형 · 마지막으로 본 건물)
        Visible
    };

    /** @brief 게임에 알리는 일입니다. */
    struct RtsEvent
    {
        enum class Kind : uint8
        {
            UnitCreated = 0, ///< 생산 · 소환 · 건설 시작
            UnitDied,
            ConstructionComplete,
            ProductionComplete,
            ResourceDepleted,
            ResourcesDeposited, ///< _value = 양
            SupplyBlocked,
            UnderAttack,
            PlayerDefeated,
            GameOver ///< _player = 이긴 팀
        };
        hashed_string _defId{};
        float3        _position{};
        RtsUnitId     _unit{};
        RtsUnitId     _other{}; ///< 죽음 — 죽인 유닛
        int32         _player{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::UnitCreated };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RtsWorld
     * @brief 스타크래프트 규칙을 줄인 한 판입니다. 화면 · 입력은 게임이 하고 여기는 규칙만 돌립니다.
     * @details 한 걸음(`_fixedStep`)마다:
     *          1. 보급 — 다 지은 건물이 주는 양(상한 `supplyMax`)과 산 유닛 · 생산 중 유닛이 쓰는 양을 다시 센다.
     *          2. 명령 — 유닛마다 앞 명령을 처리한다. 이동은 `NavAgent`(A*), 여럿을 한 곳으로 보내면 흐름장 하나를 나눠 쓴다. 공격 · 채취 · 짓기는
     *             목표 가장자리까지 다가가 멈춘다. 할 일이 없거나 공격 이동 · 제자리면 시야(제자리는 사거리) 안의 가장 가까운 적을 고른다.
     *          3. 채취 — 자원 → 캐기(한 자원에 한 일꾼) → 가장 가까운 본진에 내려놓기 → 다시. 다 캔 광물은 사라지고 근처 다른 광물로 간다.
     *          4. 건설 · 생산 — 일꾼이 붙어 있는 동안 건물이 오르고(체력도 함께), 생산은 대기열 앞부터 보급이 되면 시작한다.
     *          5. 전투 — 피해 = max(최소, 공격 − 방어). 맞은 쪽 주인에게 "공격받고 있다" 를 간격을 두고 알린다.
     *          6. 안개 — 팀마다 시야를 칠한다(본 칸은 Explored 로 남는다). 남은 건물이 없는 플레이어는 진다.
     *          카탈로그는 빌려 씁니다(월드보다 오래 · 바뀌지 않게).
     */
    class SW_GF_API RtsWorld
    {
    public:
        static constexpr int32 kNoOwner = -1;

        RtsWorld();

        void initialize( const RtsCatalog* pCatalog, int32 width, int32 height, const RtsSettings& settings );
        /** @brief 지형 막힘(절벽 · 물)입니다. 유닛을 놓기 전에 칠합니다. */
        void setTerrainBlocked( int32 x, int32 y, bool bBlocked );
        /**
         * @brief 공유 땅을 빌립니다(월드 칸 (0, 0) = 땅 칸 @p origin). 그 뒤로 건물은 놓을 때 땅을 얻고(막힘) 부서지면 놓습니다.
         * @details 다른 키트가 막아 둔 땅은 땅 격자에서 막힘이다 — 땅 리비전이 바뀐 `update` 머리에서 땅 격자를 다시 칠한다(하늘 격자는 그대로).
         *          막히지 않은 남의 땅(도로)도 짓지는 못한다. 이미 놓인 건물은 얻지 않으므로 건물을 놓기 전에 묶습니다. @p pLand 가 nullptr 이면 풉니다.
         */
        void bindLand( LandRegistry* pLand, const int2& origin );
        /** @brief 플레이어를 더합니다. 자원은 빌린 지갑(@p pWallet — 월드보다 오래 살아야 한다, nullptr 이면 아무것도 사지 못한다)입니다. 플레이어 번호입니다. */
        int32 addPlayer( int32 team, Wallet* pWallet, const float3& startPosition );
        /** @brief 유닛 · 건물(다 지은 채) · 자원을 놓습니다. 건물 · 자원은 @p position 이 든 칸이 왼쪽 아래입니다. 놓을 수 없으면 무효 id 입니다. */
        RtsUnitId spawnUnit( const hashed_string& defId, int32 owner, const float3& position );

        // 명령 — @p bQueue 면 명령 대기열 뒤에 붙인다(쉬프트).
        RtsCommandResult issueMove( RtsUnitId unitId, const float3& target, bool bQueue = false );
        RtsCommandResult issueAttackMove( RtsUnitId unitId, const float3& target, bool bQueue = false );
        RtsCommandResult issueAttack( RtsUnitId unitId, RtsUnitId targetId, bool bQueue = false );
        RtsCommandResult issueGather( RtsUnitId unitId, RtsUnitId resourceId, bool bQueue = false );
        /** @brief 일꾼이 @p cell(왼쪽 아래)에 @p buildingId 를 짓게 합니다. 값은 자리에 닿아 짓기 시작할 때 냅니다. */
        RtsCommandResult issueBuild( RtsUnitId workerId, const hashed_string& buildingId, const int2& cell, bool bQueue = false );
        RtsCommandResult issueHold( RtsUnitId unitId );
        RtsCommandResult issueStop( RtsUnitId unitId );
        /**
         * @brief 오른쪽 클릭 — 적이면 공격, 자원 · 내 정제소면 채취(일꾼), 내 덜 지은 건물이면 이어 짓기(일꾼), 아니면 이동입니다.
         * @param targetId 클릭한 유닛(없으면 무효 id).
         */
        RtsCommandResult issueSmart( RtsUnitId unitId, const float3& position, RtsUnitId targetId, bool bQueue = false );
        /** @brief 여럿에게 같은 이동 · 공격 이동을 줍니다. 무리가 크면 흐름장 하나를 함께 씁니다. 받은 유닛 수입니다. */
        int32 issueGroupMove( const vector<RtsUnitId>& listUnit, const float3& target, bool bAttackMove, bool bQueue = false );

        /** @brief 건물의 생산 대기열에 넣습니다. 값(광물 · 가스)은 지금 내고 보급은 생산을 시작할 때 봅니다. */
        RtsCommandResult train( RtsUnitId buildingId, const hashed_string& unitId );
        /** @brief 대기열 마지막을 빼고 값을 돌려받습니다. */
        bool cancelTrain( RtsUnitId buildingId );
        void setRallyPoint( RtsUnitId buildingId, const float3& position );

        /** @brief 시간을 넘깁니다(고정 걸음). */
        void update( float32 deltaTime );
        void drainEvents( vector<RtsEvent>& outListEvent );

        // 조회
        const RtsUnit*   findUnit( RtsUnitId unitId ) const;
        const RtsPlayer* findPlayer( int32 player ) const;
        int32            getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        /** @brief 그 플레이어 지갑의 광물 · 가스입니다(지갑이 없으면 0). */
        int64 getMinerals( int32 player ) const;
        int64 getGas( int32 player ) const;
        /** @brief 산 유닛마다 부릅니다. */
        template <typename TFunction>
        void forEachUnit( TFunction&& function ) const
        {
            for ( const RtsUnit& unit : _listUnit )
            {
                if ( unit._bAlive )
                    function( unit );
            }
        }
        /** @brief @p center 둘레 @p radius 안(몸 가장자리 기준)의 산 유닛입니다. */
        void queryUnits( const float3& center, float32 radius, vector<RtsUnitId>& outListUnit ) const;
        /** @brief 그 자리를 덮는 유닛(건물 · 자원은 칸, 유닛은 몸)입니다 — 클릭 고르기. */
        RtsUnitId pickUnit( const float3& position ) const;
        int32     countUnits( int32 player, const hashed_string& defId, bool bIncludeUnfinished ) const;
        /** @brief 다 지은 그 건물이 있는가입니다(테크). */
        bool hasConstructed( int32 player, const hashed_string& defId ) const;
        /** @brief 생산 대기열 · 짓는 중까지 셉니다(AI 가 같은 것을 두 번 시키지 않게). */
        int32 countPlanned( int32 player, const hashed_string& defId ) const;

        /** @brief 건물을 그 칸에 놓을 수 있는가입니다(격자 · 다른 건물 · 땅 유닛 · 정제소는 간헐천 위 · 빌린 공유 땅이 남의 것이 아님). @p ignoreUnit 은 짓는 일꾼. */
        bool canPlaceBuilding( const hashed_string& buildingId, const int2& cell, RtsUnitId ignoreUnit = RtsUnitId{} ) const;
        /** @brief @p nearPosition 둘레에서 둘레 한 칸을 비운 건물 자리를 찾습니다(AI · 자동 배치). 정제소는 가까운 빈 간헐천입니다. */
        [[nodiscard]] bool findBuildSite( const hashed_string& buildingId, const float3& nearPosition, int32 minRadius, int32 maxRadius, int2& outCell ) const;
        /** @brief @p position 에서 가장 가까운 자원(그 종류)입니다. @p maxDistance 밖이면 무효 id 입니다. */
        RtsUnitId findNearestResource( const float3& position, RtsResourceType type, float32 maxDistance ) const;
        /** @brief 그 플레이어의 가장 가까운 다 지은 본진입니다. */
        RtsUnitId findNearestDepot( int32 player, const float3& position ) const;

        RtsVisibility getVisibility( int32 player, const int2& cell ) const;
        /** @brief 그 플레이어(팀)가 지금 그 유닛을 보는가입니다. */
        bool isVisibleTo( int32 player, RtsUnitId unitId ) const;
        /** @brief 두 플레이어가 서로 적인가입니다(팀이 다르면 적 — `TeamAttitudeUtil::isHostile`). 없는 플레이어(주인 없음)는 누구와도 적이 아닙니다. */
        bool areEnemies( int32 playerA, int32 playerB ) const;
        /** @brief 남은 팀이 하나면 그 팀, 아니면 −1 입니다. */
        int32 getWinningTeam() const { return _winningTeam; }

        const NavGrid&     getGrid() const { return _grid; }
        const RtsCatalog*  getCatalog() const { return _pCatalog; }
        const RtsSettings& getSettings() const { return _settings; }
        float32            getTime() const { return _time; }
        /** @brief 그 칸 왼쪽 아래 건물의 가운데 자리입니다. */
        float3 computeFootprintCenter( const int2& cell, int32 footprint ) const;

        /**
         * @brief 땅 · 유닛(자리 · 명령 · 생산 · 채취) · 플레이어 · 안개 · 시간을 씁니다(핫 리로드 · 세이브). 설정 · 카탈로그는 쓰지 않습니다.
         * @details 정의는 카탈로그 id, 유닛 참조는 id(세대 포함)로 적습니다. 길(경로 · 흐름장)은 쓰지 않습니다 — 읽은 쪽이 앞 명령의 길을 다시 구합니다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 의 바이트로 바꿉니다. `initialize` 한 뒤에 부릅니다(같은 크기 · 카탈로그).
         * @details 격자는 땅 + 서 있는 건물 · 자원의 발자국으로 다시 칠하고, 움직이던 유닛은 앞 명령을 처음부터 다시 걷습니다(흐름장을 쓰던 무리 이동은 다시 빌린다).
         * @return 크기가 다르거나, 카탈로그에 없는 유닛이 있거나, 깨졌으면 false 이고 그대로입니다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct FlowFieldSlot
        {
            FlowField _field{};
            int2      _goal{ -1, -1 };
            int32     _userCount{ 0 };
        };

        RtsUnit*  findUnitMutable( RtsUnitId unitId );
        void      freeDeadUnits();
        void      beginOrder( RtsUnit& unit );
        void      releaseOrder( RtsUnit& unit, RtsOrder& order );
        void      nudgeToWalkable( RtsUnit& unit );
        float32   computeRectDistance( const float3& position, const int2& cell, int32 footprint ) const;
        RtsUnitId allocateUnit();
        void      stepFixed( float32 deltaTime );
        void      recomputeSupply();
        void      rebuildBuckets();
        void      updateUnit( RtsUnit& unit, float32 deltaTime );
        void      updateOrder( RtsUnit& unit, float32 deltaTime );
        void      updateGather( RtsUnit& unit, RtsOrder& order, float32 deltaTime );
        void      updateBuild( RtsUnit& unit, RtsOrder& order, float32 deltaTime );
        void      updateProduction( RtsUnit& building, float32 deltaTime );
        void      updateMovement( RtsUnit& unit, float32 deltaTime );
        void      updateCombat( RtsUnit& unit, float32 deltaTime );
        void      updateVision();
        void      updateDefeat();

        RtsCommandResult pushOrder( RtsUnit& unit, const RtsOrder& order, bool bQueue );
        void             finishOrder( RtsUnit& unit );
        void             clearOrders( RtsUnit& unit );
        /** @brief 목표 쪽으로 걷습니다(목표가 움직이면 간격을 두고 경로를 다시). */
        void      approach( RtsUnit& unit, const float3& target, bool bForce );
        bool      isWithinReach( const RtsUnit& unit, const RtsUnit& target, float32 reach ) const;
        float32   computeEdgeDistance( const RtsUnit& unit, const RtsUnit& target ) const;
        bool      canAttack( const RtsUnit& attacker, const RtsUnit& target ) const;
        RtsUnitId findAutoTarget( const RtsUnit& unit, float32 radius ) const;
        void      dealDamage( RtsUnit& attacker, RtsUnit& target );
        void      killUnit( RtsUnit& unit, RtsUnitId killerId );
        void      placeFootprint( const RtsUnit& unit, bool bBlocked );
        /** @brief 땅 격자를 지형 · 남의 막힌 땅 · 서 있는 건물 · 자원의 발자국으로 다시 칠합니다. */
        void             repaintGrid();
        void             startConstruction( RtsUnit& worker, RtsOrder& order );
        void             completeProduction( RtsUnit& building );
        bool             findSpawnPosition( const RtsUnit& building, float3& outPosition ) const;
        RtsCommandResult evaluateCost( int32 player, const RtsUnitDef& def ) const;
        void             payCost( RtsPlayer& player, const RtsUnitDef& def );
        void             refundCost( RtsPlayer& player, const RtsUnitDef& def );
        void             pushEvent( RtsEvent::Kind kind, int32 player, RtsUnitId unitId, const hashed_string& defId, int32 value = 0, RtsUnitId otherId = RtsUnitId{} );
        FlowField*       acquireFlowField( const int2& goal );
        void             releaseFlowField( const FlowField* pField );
        int32            computeBucketIndex( const float3& position ) const;

        NavGrid               _grid;
        NavGrid               _airGrid; ///< 하늘 — 모두 열린 같은 크기의 격자(공중 유닛은 지형 · 건물을 넘는다)
        GridPathfinder        _pathfinder;
        deque<RtsUnit>        _listUnit;       ///< 자리 = id 의 index(deque — 걸음 중에 생겨도 다른 유닛 참조가 살아 있다)
        vector<uint32>        _listGeneration; ///< 자리마다 세대
        vector<uint32>        _listFreeSlot;
        vector<RtsPlayer>     _listPlayer;
        EventBuffer<RtsEvent> _eventBuffer;
        vector<uint8>         _listTerrainBlocked;
        vector<vector<uint8>> _listTeamVisibility; ///< 팀마다 칸의 RtsVisibility
        vector<int32>         _listBucketHead;     ///< 버킷마다 첫 유닛 자리(−1 끝)
        vector<int32>         _listBucketNext;     ///< 유닛 자리마다 다음 자리
        vector<float3>        _listNeighborScratch;
        vector<RtsUnitId>     _listQueryScratch;
        deque<FlowFieldSlot>  _listFlowField; ///< deque — 자리가 움직이지 않아 행위자가 포인터를 들 수 있다
        const RtsCatalog*     _pCatalog;
        RtsSettings           _settings;
        FixedStepTimer        _stepTimer;
        float32               _time;
        Countdown             _visionTimer;
        GridTopology          _bucketTopology; ///< 이웃 찾기 버킷 격자(버킷 = `_settings._bucketSize` 칸)
        LandBinding           _land;           ///< 빌린 공유 땅(없으면 단독)
        int32                 _teamCount;
        int32                 _winningTeam;
        uint32                _landRevision; ///< 마지막으로 땅 격자를 칠한 땅 리비전
    };
} // namespace sw

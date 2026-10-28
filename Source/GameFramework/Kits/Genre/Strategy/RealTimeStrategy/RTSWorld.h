/**
 * @file RTSWorld.h
 * @brief RTS 한 판의 규칙을 담습니다. 플레이어, 유닛, 명령 대기열, 채취, 건설, 생산, 테크, 전투, 전장의 안개, 승패를 다룹니다.
 */
#pragma once
#include "Core/Container/EventBuffer.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Character/Navigation/FlowField.h"
#include "GameFramework/Base/Character/Navigation/GridPathfinder.h"
#include "GameFramework/Base/Character/Navigation/NavAgent.h"
#include "GameFramework/Base/Character/Navigation/NavGrid.h"
#include "GameFramework/Base/Foundation/Utility/Grid/GridTopology.h"
#include "GameFramework/Base/World/Land/LandRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/RTSCatalog.h"

namespace sw
{
    class Archive;
    class Wallet;

    /** @brief 유닛 id 입니다. 죽은 유닛의 슬롯이 다시 쓰여도 세대가 달라서 옛 id 로는 찾을 수 없습니다. */
    using RTSUnitID = SlotHandle;

    /**
     * @brief 규칙의 수치입니다. 시간 단위는 게임 초입니다.
     * @details 범위는 필드의 `PROPERTY( Min · Max )` 이고, 벗어난 값은 `RTSWorld::initialize` 가 오류로 거절한다(`ConfigRangeUtil`). 값을 조용히 맞추지 않는다(폴백 금지 원칙).
     */
    REFLECT()
    struct SW_KIT_API RTSSettings
    {
        REFLECT_BODY();
        PROPERTY( Min = 0.001, Max = 1.0 )
        float32 _fixedStep{ 0.05f }; ///< 시뮬레이션 한 걸음의 길이(20 Hz, 스타크래프트의 게임 틱과 비슷하다)
        PROPERTY( Min = 0.0 )
        float32 _minimumDamage{ 0.5f }; ///< 방어가 높아도 최소한 이만큼은 피해가 들어간다
        PROPERTY( Min = 0.0 )
        float32 _repathInterval{ 0.5f }; ///< 움직이는 목표를 쫓을 때 경로를 다시 구하는 간격
        PROPERTY( Min = 0.0 )
        float32 _visionInterval{ 0.25f }; ///< 안개를 다시 칠하는 간격
        PROPERTY( Min = 0.0 )
        float32 _interactSlack{ 0.35f }; ///< 채취, 건설, 반납에서 닿았다고 보는 가장자리 거리
        PROPERTY( Min = 0.0 )
        float32 _resourceSearchRadius{ 8.0f }; ///< 광물이 바닥났을 때 대신할 광물을 찾는 반경
        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _constructionStartRatio{ 0.1f }; ///< 건설을 시작한 건물이 처음 가지는 체력 비율
        PROPERTY( Min = 0.0 )
        float32 _underAttackCooldown{ 10.0f }; ///< "공격받고 있다" 알림의 간격(플레이어마다 적용)
        PROPERTY( Min = 1 )
        int32 _productionQueueMax{ 5 };
        PROPERTY( Min = 1 )
        int32 _flowFieldGroupSize{ 6 }; ///< 이 수 이상을 한 곳으로 보내면 A* 대신 흐름장 하나를 쓴다
        PROPERTY( Min = 1 )
        int32 _bucketSize{ 4 }; ///< 이웃 탐색 버킷의 한 변 길이(셀 수)
        PROPERTY()
        hashed_string _mineralCurrency{ "Minerals" }; ///< 플레이어 지갑에서 광물로 쓰는 통화
        PROPERTY()
        hashed_string _gasCurrency{ "Gas" }; ///< 플레이어 지갑에서 가스로 쓰는 통화
    };
} // namespace sw

namespace sw
{
    /** @brief 명령 종류입니다. */
    enum class RTSOrderType : uint8
    {
        Move = 0,
        AttackMove, ///< 이동 중 적을 만나면 싸운다
        Attack,     ///< 지정한 유닛을 공격한다
        Gather,     ///< 자원(광물, 정제소)을 캐서 본진으로 나르는 일을 반복한다
        Build,      ///< 일꾼이 그 위치에 건물을 짓는다. 이미 놓인 건물이면 이어서 짓는다
        Hold        ///< 제자리를 지키며 사거리 안의 적만 공격한다
    };

    /** @brief 명령 하나입니다. */
    struct RTSOrder
    {
        hashed_string    _buildID{};
        float3           _target{};
        int2             _buildCell{ -1, -1 };
        RTSUnitID        _targetUnit{};
        const FlowField* _pFlowField{ nullptr }; ///< 무리 이동에 쓴다. 월드가 빌려 준 흐름장이며 명령이 끝나면 돌려준다
        float32          _arriveRadius{ 0.0f };  ///< 무리 이동에 쓴다. 목표에서 이 거리 안이면 도착한 것으로 본다(모두가 한 점에 설 수는 없다)
        RTSOrderType     _type{ RTSOrderType::Move };
    };
} // namespace sw

namespace sw
{
    /** @brief 명령과 생산의 결과입니다. */
    ENUM()
    enum class RTSCommandResult : uint8
    {
        Ok = 0,
        InvalidUnit,
        NotOwner,
        CannotDo, ///< 그 유닛이 할 수 없는 일이다(일꾼이 아니거나 그 건물이 만들 수 없는 유닛 등)
        NotEnoughMinerals,
        NotEnoughGas,
        NotEnoughSupply,
        TechRequired,
        QueueFull,
        InvalidPlacement
    };

    /** @brief 일꾼의 채취 단계입니다. */
    enum class RTSGatherPhase : uint8
    {
        ToResource = 0,
        Waiting, ///< 다른 일꾼이 캐는 중이라 기다린다
        Harvesting,
        Returning
    };

    /** @brief 유닛, 건물, 자원 하나입니다. */
    struct RTSUnit
    {
        NavAgent             _agent{};
        deque<RTSOrder>      _listOrder{};      ///< 맨 앞이 현재 명령이다(쉬프트 입력은 뒤에 붙인다)
        deque<hashed_string> _listProduction{}; ///< 생산 대기열이다(비용은 넣을 때 낸다)
        const RTSUnitDef*    _pDef{ nullptr };
        float3               _position{};
        float3               _rallyPoint{};
        float3               _moveGoal{};     ///< 지금 다가가는 위치(경로를 다시 구할지 판단하는 기준)
        int2                 _cell{ -1, -1 }; ///< 건물과 자원이 쓴다. 왼쪽 아래 셀이다
        RTSUnitID            _id{};
        RTSUnitID            _attackTarget{};   ///< 지금 공격하는 유닛(명령이나 자동 선택)
        RTSUnitID            _gatherTarget{};   ///< 채취하는 자원 또는 정제소
        RTSUnitID            _harvester{};      ///< 자원과 정제소가 쓴다. 지금 캐고 있는 일꾼
        RTSUnitID            _builder{};        ///< 건설 중인 건물이 쓴다. 짓고 있는 일꾼
        RTSUnitID            _linkedResource{}; ///< 정제소가 쓴다. 아래에 있는 간헐천
        float32              _hp{ 0.0f };
        Countdown            _cooldown{};            ///< 다음 공격까지 남은 시간
        float32              _buildProgress{ 1.0f }; ///< 0..1 범위이고 1이면 건설이 끝난 것이다
        float32              _productionTimer{ 0.0f };
        float32              _gatherTimer{ 0.0f };
        Countdown            _repathTimer{};
        int32                _owner{ -1 }; ///< -1 이면 주인이 없다(자원)
        int32                _resourceLeft{ 0 };
        int32                _cargoAmount{ 0 };
        int32                _supplyReserved{ 0 }; ///< 건물이 쓴다. 생산 중인 유닛이 쓸 보급
        RTSResourceType      _cargoType{ RTSResourceType::None };
        RTSGatherPhase       _gatherPhase{ RTSGatherPhase::ToResource };
        uint8                _bAlive{ SW_TRUE };
        uint8                _bHasRally{ SW_FALSE };
        uint8                _bOrderStarted{ SW_FALSE }; ///< 맨 앞 명령의 경로를 이미 구했다
        uint8                _bSupplyBlocked{ SW_FALSE };

        bool            isConstructed() const { return _buildProgress >= 1.0f; }
        bool            isBuilding() const { return _pDef != nullptr && _pDef->_kind == RTSUnitKind::Building; }
        bool            isResource() const { return _pDef != nullptr && _pDef->_kind == RTSUnitKind::Resource; }
        bool            isMobile() const { return _pDef != nullptr && _pDef->isMobile(); }
        bool            isIdle() const { return _listOrder.empty(); }
        const RTSOrder* findOrder() const { return _listOrder.empty() ? nullptr : &_listOrder.front(); }
    };
} // namespace sw

namespace sw
{
    /** @brief 플레이어 하나입니다. */
    struct RTSPlayer
    {
        float3  _startPosition{};
        Wallet* _pWallet{ nullptr }; ///< 빌린 지갑이다. `RTSSettings` 의 통화(광물, 가스)를 담는다. 지역 플레이어는 공유 지갑을 쓰고, AI 의 지갑은 게임이 가지고 있다
        int32   _supplyUsed{ 0 };
        int32   _supplyCap{ 0 };
        int32   _team{ 0 };
        float32 _nextUnderAttackTime{ 0.0f }; ///< "공격받고 있다" 를 다시 알릴 수 있는 시각
        uint8   _bDefeated{ SW_FALSE };
        uint8   _bHadBuilding{ SW_FALSE }; ///< 건물을 가진 적이 있다(시작 전에는 패배하지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 안개의 상태입니다. */
    enum class RTSVisibility : uint8
    {
        Unexplored = 0,
        Explored, ///< 본 적이 있다(지형과 마지막으로 본 건물)
        Visible
    };

    /** @brief 게임에 알리는 이벤트입니다. */
    struct RTSEvent
    {
        enum class Kind : uint8
        {
            UnitCreated = 0, ///< 생산, 소환, 건설 시작
            UnitDied,
            ConstructionComplete,
            ProductionComplete,
            ResourceDepleted,
            ResourcesDeposited, ///< _value 가 양이다
            SupplyBlocked,
            UnderAttack,
            PlayerDefeated,
            GameOver ///< _player 가 이긴 팀이다
        };
        hashed_string _defID{};
        float3        _position{};
        RTSUnitID     _unit{};
        RTSUnitID     _other{}; ///< 죽음 이벤트에서 죽인 유닛
        int32         _player{ -1 };
        int32         _value{ 0 };
        Kind          _kind{ Kind::UnitCreated };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RTSWorld
     * @brief 스타크래프트 규칙을 줄인 RTS 한 판을 진행합니다. 화면과 입력은 게임이 맡고, 이 클래스는 규칙만 돌립니다.
     * @details 고정 걸음(`_fixedStep`)마다 아래 순서로 처리한다.
     *          1. 보급: 다 지은 건물이 주는 양(상한 `supplyMax`)과, 살아 있거나 생산 중인 유닛이 쓰는 양을 다시 센다.
     *          2. 명령: 유닛마다 맨 앞 명령을 처리한다. 이동은 `NavAgent`(A*)가 맡고, 여럿을 한 곳으로 보내면 흐름장 하나를 나눠 쓴다.
     *             공격, 채취, 건설은 목표의 가장자리까지 다가가 멈춘다. 할 일이 없거나 공격 이동이나 제자리 상태이면 시야(제자리는 사거리) 안에서 가장 가까운 적을 고른다.
     *          3. 채취: 자원으로 가서 캐고(자원 하나에 일꾼 하나), 가장 가까운 본진에 내려놓고, 다시 돌아가는 일을 반복한다. 다 캔 광물은 사라지고 일꾼은 근처 다른 광물로 간다.
     *          4. 건설과 생산: 일꾼이 붙어 있는 동안 건물이 올라가고(체력도 함께 오른다), 생산은 대기열 맨 앞부터 보급이 허락하면 시작한다.
     *          5. 전투: 피해는 max(최소 피해, 공격 - 방어)이다. 맞은 쪽 주인에게는 "공격받고 있다" 를 간격을 두고 알린다.
     *          6. 안개: 팀마다 시야를 칠한다(본 셀은 Explored 로 남는다). 건물이 남지 않은 플레이어는 진다.
     *
     *          카탈로그는 빌려 쓴다. 월드보다 오래 살아야 하고 바뀌지 않아야 한다.
     */
    class SW_KIT_API RTSWorld
    {
    public:
        static constexpr int32 kNoOwner = -1;

        RTSWorld();

        /** @brief 카탈로그와 격자 크기, 설정으로 판을 비웁니다. 크기가 1 보다 작거나 설정이 범위 밖이면 오류를 남기고 false 를 반환하며 판은 그대로입니다(1 로 올려 받지 않는다). */
        [[nodiscard]] bool initialize( const RTSCatalog* pCatalog, int32 width, int32 height, const RTSSettings& settings );
        /** @brief 지형이 막힌 곳(절벽, 물)을 지정합니다. 유닛을 놓기 전에 칠합니다. */
        void setTerrainBlocked( int32 x, int32 y, bool bBlocked );
        /**
         * @brief 공유 땅을 빌립니다. 월드 셀 (0, 0) 이 땅 셀 @p origin 에 해당합니다. 이후 건물은 놓을 때 땅을 얻고(막힘) 부서지면 반납합니다.
         * @details 다른 키트가 막아 둔 땅은 땅 격자에서 막힌 곳이다. 땅 리비전이 바뀌면 `update` 머리에서 땅 격자를 다시 칠한다(하늘 격자는 그대로 둔다).
         *          막히지 않은 남의 땅(도로)에도 건물을 짓지 못한다. 이미 놓인 건물은 땅을 얻지 않으므로 건물을 놓기 전에 호출해야 한다. @p pLand 가 nullptr 이면 빌린 땅을 반납한다.
         */
        void bindLand( LandRegistry* pLand, const int2& origin );
        /** @brief 플레이어를 추가하고 플레이어 번호를 반환합니다. 자원은 빌린 지갑 @p pWallet 에서 쓰며, 지갑은 월드보다 오래 살아야 합니다. nullptr 이면 아무것도 살 수 없습니다. */
        int32 addPlayer( int32 team, Wallet* pWallet, const float3& startPosition );
        /** @brief 유닛, 건물(건설이 끝난 상태), 자원을 놓습니다. 건물과 자원은 @p position 이 속한 셀이 왼쪽 아래가 됩니다. 놓을 수 없으면 무효 id 를 반환합니다. */
        RTSUnitID spawnUnit( const hashed_string& defID, int32 owner, const float3& position );

        // 명령. @p bQueue 가 true 이면 명령 대기열 뒤에 붙인다(쉬프트 입력).
        RTSCommandResult issueMove( RTSUnitID unitID, const float3& target, bool bQueue = false );
        RTSCommandResult issueAttackMove( RTSUnitID unitID, const float3& target, bool bQueue = false );
        RTSCommandResult issueAttack( RTSUnitID unitID, RTSUnitID targetID, bool bQueue = false );
        RTSCommandResult issueGather( RTSUnitID unitID, RTSUnitID assetID, bool bQueue = false );
        /** @brief 일꾼이 @p cell(왼쪽 아래)에 @p buildingID 를 짓게 합니다. 비용은 그 위치에 도착해 건설을 시작할 때 냅니다. */
        RTSCommandResult issueBuild( RTSUnitID workerID, const hashed_string& buildingID, const int2& cell, bool bQueue = false );
        RTSCommandResult issueHold( RTSUnitID unitID );
        RTSCommandResult issueStop( RTSUnitID unitID );
        /**
         * @brief 오른쪽 클릭에 해당하는 명령을 내립니다.
         * @details 적이면 공격하고, 자원이나 내 정제소면 채취하고(일꾼), 내가 덜 지은 건물이면 이어 짓고(일꾼), 그 밖에는 이동한다.
         * @param targetID 클릭한 유닛이다. 없으면 무효 id 이다.
         */
        RTSCommandResult issueSmart( RTSUnitID unitID, const float3& position, RTSUnitID targetID, bool bQueue = false );
        /** @brief 여러 유닛에게 같은 이동이나 공격 이동을 시킵니다. 무리가 크면 흐름장 하나를 함께 씁니다. 명령을 받은 유닛 수를 반환합니다. */
        int32 issueGroupMove( const vector<RTSUnitID>& listUnit, const float3& target, bool bAttackMove, bool bQueue = false );

        /** @brief 건물의 생산 대기열에 유닛을 넣습니다. 비용(광물, 가스)은 지금 내고, 보급은 생산을 시작할 때 확인합니다. */
        RTSCommandResult train( RTSUnitID buildingID, const hashed_string& unitID );
        /** @brief 대기열의 마지막 항목을 빼고 비용을 돌려받습니다. */
        bool cancelTrain( RTSUnitID buildingID );
        void setRallyPoint( RTSUnitID buildingID, const float3& position );

        /** @brief 시간을 진행합니다. 내부에서는 고정 걸음으로 나눠 처리합니다. */
        void update( float32 deltaTime );
        /** @brief 이벤트를 꺼낼 쪽을 정합니다(둘째 쪽은 false). 꺼내기는 이 표가 있어야 한다 — 같은 월드를 붙인 화면 · 시스템이 서로의 이벤트를 가져가지 못한다. */
        [[nodiscard]] bool claimEvents( DrainLease& outLease ) { return _eventBuffer.claim( outLease ); }
        void               drainEvents( const DrainLease& lease, vector<RTSEvent>& outListEvent );

        // 조회
        const RTSUnit*   findUnit( RTSUnitID unitID ) const;
        const RTSPlayer* findPlayer( int32 player ) const;
        int32            getPlayerCount() const { return static_cast<int32>( _listPlayer.size() ); }
        /** @brief 그 플레이어 지갑의 광물과 가스를 반환합니다. 지갑이 없으면 0 입니다. */
        int64 getMinerals( int32 player ) const;
        int64 getGas( int32 player ) const;
        /** @brief 살아 있는 유닛마다 @p function 을 호출합니다. */
        template <typename TFunction>
        void forEachUnit( TFunction&& function ) const
        {
            for ( const RTSUnit& unit : _listUnit )
            {
                if ( unit._bAlive )
                    function( unit );
            }
        }
        /** @brief @p center 둘레 @p radius 안(몸 가장자리 기준)에 있는 살아 있는 유닛을 모읍니다. */
        void queryUnits( const float3& center, float32 radius, vector<RTSUnitID>& outListUnit ) const;
        /** @brief 그 위치를 덮는 유닛을 반환합니다. 건물과 자원은 셀, 유닛은 몸 기준이며 클릭 선택에 씁니다. */
        RTSUnitID pickUnit( const float3& position ) const;
        int32     countUnits( int32 player, const hashed_string& defID, bool bIncludeUnfinished ) const;
        /** @brief 그 건물을 건설이 끝난 채로 가지고 있는지 반환합니다(테크 조건 확인). */
        bool hasConstructed( int32 player, const hashed_string& defID ) const;
        /** @brief 생산 대기열과 건설 중인 것까지 센 수를 반환합니다. AI 가 같은 것을 두 번 시키지 않게 하려는 용도입니다. */
        int32 countPlanned( int32 player, const hashed_string& defID ) const;

        /** @brief 건물을 그 셀에 놓을 수 있는지 반환합니다. 격자, 다른 건물, 땅 위의 유닛, 정제소는 간헐천 위인지, 빌린 공유 땅이 남의 것이 아닌지를 확인합니다. @p ignoreUnit 은 건설 중인 일꾼입니다. */
        bool canPlaceBuilding( const hashed_string& buildingID, const int2& cell, RTSUnitID ignoreUnit = RTSUnitID{} ) const;
        /** @brief @p nearPosition 둘레에서 건물을 놓을 자리를 찾습니다. 건물 둘레 한 셀은 비워 둡니다(AI 와 자동 배치용). 정제소는 가까운 빈 간헐천을 고릅니다. */
        [[nodiscard]] bool findBuildSite( const hashed_string& buildingID, const float3& nearPosition, int32 minRadius, int32 maxRadius, int2& outCell ) const;
        /** @brief @p position 에서 가장 가까운 그 종류의 자원을 반환합니다. @p maxDistance 밖이면 무효 id 입니다. */
        RTSUnitID findNearestResource( const float3& position, RTSResourceType type, float32 maxDistance ) const;
        /** @brief 그 플레이어의 가장 가까운, 건설이 끝난 본진을 반환합니다. */
        RTSUnitID findNearestDepot( int32 player, const float3& position ) const;

        RTSVisibility getVisibility( int32 player, const int2& cell ) const;
        /** @brief 그 플레이어(팀)가 지금 그 유닛을 볼 수 있는지 반환합니다. */
        bool isVisibleTo( int32 player, RTSUnitID unitID ) const;
        /** @brief 두 플레이어가 서로 적인지 반환합니다. 팀이 다르면 적이며(`TeamAttitudeUtil::isHostile`), 주인 없음 같은 없는 플레이어는 누구와도 적이 아닙니다. */
        bool areEnemies( int32 playerA, int32 playerB ) const;
        /** @brief 남은 팀이 하나면 그 팀을, 아니면 -1 을 반환합니다. */
        int32 getWinningTeam() const { return _winningTeam; }

        const NavGrid&     getGrid() const { return _grid; }
        const RTSCatalog*  getCatalog() const { return _pCatalog; }
        const RTSSettings& getSettings() const { return _settings; }
        float32            getTime() const { return _time; }
        /** @brief 그 셀을 왼쪽 아래로 하는 건물의 가운데 위치를 반환합니다. */
        float3 computeFootprintCenter( const int2& cell, int32 footprint ) const;

        /**
         * @brief 땅, 유닛(위치, 명령, 생산, 채취), 플레이어, 안개, 시간을 씁니다. 핫 리로드와 세이브에 쓰며 설정과 카탈로그는 쓰지 않습니다.
         * @details 정의는 카탈로그 id 로, 유닛 참조는 세대를 포함한 id 로 적는다. 경로와 흐름장은 쓰지 않으므로, 읽은 쪽에서 맨 앞 명령의 경로를 다시 구한다.
         */
        void writeState( Archive& outArchive ) const;
        /**
         * @brief `writeState` 가 쓴 바이트를 읽어 상태를 복원합니다. `initialize` 를 먼저 호출해야 하며 크기와 카탈로그가 같아야 합니다.
         * @details 격자는 땅과 서 있는 건물, 자원의 발자국으로 다시 칠한다. 이동 중이던 유닛은 맨 앞 명령을 처음부터 다시 진행하고, 흐름장을 쓰던 무리 이동은 흐름장을 다시 빌린다.
         * @return 크기가 다르거나, 카탈로그에 없는 유닛이 있거나, 데이터가 깨졌으면 false 이며 상태는 그대로 둔다.
         */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        struct FlowFieldSlot
        {
            FlowField _field{};
            int2      _goal{ -1, -1 };
            int32     _userCount{ 0 };
        };

        RTSUnit*  findUnitMutable( RTSUnitID unitID );
        void      freeDeadUnits();
        void      beginOrder( RTSUnit& unit );
        void      releaseOrder( RTSUnit& unit, RTSOrder& order );
        void      nudgeToWalkable( RTSUnit& unit );
        float32   computeRectDistance( const float3& position, const int2& cell, int32 footprint ) const;
        RTSUnitID allocateUnit();
        void      stepFixed( float32 deltaTime );
        void      recomputeSupply();
        void      rebuildBuckets();
        /** @brief 읽은 뒤 살아 있는 이동 유닛의 길찾기 에이전트를 맞추고, 흐름장을 쓰던 명령(@p listFlowFieldOrder, 유닛 · 명령 순서)에 흐름장을 다시 빌립니다. */
        void restoreAgents( const vector<uint8>& listFlowFieldOrder );
        void updateUnit( RTSUnit& unit, float32 deltaTime );
        void updateOrder( RTSUnit& unit, float32 deltaTime );
        void updateGather( RTSUnit& unit, RTSOrder& order, float32 deltaTime );
        /** @brief 실은 자원을 가까운 저장소로 가져가 지갑에 넣습니다(채취의 `Returning` 단계). */
        void returnCargo( RTSUnit& unit );
        /** @brief 채취 시간이 차면 @p source 에서 캐 싣고 돌아가는 단계로 넘깁니다. @p target 은 일꾼이 붙은 유닛(광물이나 정제소)입니다. */
        void updateHarvest( RTSUnit& unit, RTSUnit& target, RTSUnit& source, float32 deltaTime );
        void updateBuild( RTSUnit& unit, RTSOrder& order, float32 deltaTime );
        void updateProduction( RTSUnit& building, float32 deltaTime );
        void updateMovement( RTSUnit& unit, float32 deltaTime );
        void updateCombat( RTSUnit& unit, float32 deltaTime );
        void updateVision();
        void updateDefeat();

        RTSCommandResult pushOrder( RTSUnit& unit, const RTSOrder& order, bool bQueue );
        void             finishOrder( RTSUnit& unit );
        void             clearOrders( RTSUnit& unit );
        /** @brief 목표 쪽으로 걷습니다. 목표가 움직이면 간격을 두고 경로를 다시 구합니다. */
        void    approach( RTSUnit& unit, const float3& target, bool bForce );
        bool    isWithinReach( const RTSUnit& unit, const RTSUnit& target, float32 reach ) const;
        float32 computeEdgeDistance( const RTSUnit& unit, const RTSUnit& target ) const;
        bool    canAttack( const RTSUnit& attacker, const RTSUnit& target ) const;
        /** @brief 반경 안의 가장 먼저 칠 목표입니다. 후보는 `_listQueryScratch` 에 모은다(프레임마다 유닛마다 불려 지역 목록이면 그만큼 할당한다). */
        RTSUnitID findAutoTarget( const RTSUnit& unit, float32 radius );
        void      dealDamage( RTSUnit& attacker, RTSUnit& target );
        void      killUnit( RTSUnit& unit, RTSUnitID killerID );
        void      placeFootprint( const RTSUnit& unit, bool bBlocked );
        /** @brief 땅 격자를 지형, 남이 막은 땅, 서 있는 건물과 자원의 발자국으로 다시 칠합니다. */
        void               repaintGrid();
        void               startConstruction( RTSUnit& worker, RTSOrder& order );
        void               completeProduction( RTSUnit& building );
        [[nodiscard]] bool findSpawnPosition( const RTSUnit& building, float3& outPosition ) const;
        RTSCommandResult   evaluateCost( int32 player, const RTSUnitDef& def ) const;
        void               payCost( RTSPlayer& player, const RTSUnitDef& def );
        void               refundCost( RTSPlayer& player, const RTSUnitDef& def );
        void               pushEvent( RTSEvent::Kind kind, int32 player, RTSUnitID unitID, const hashed_string& defID, int32 value = 0, RTSUnitID otherID = RTSUnitID{} );
        FlowField*         acquireFlowField( const int2& goal );
        void               releaseFlowField( const FlowField* pField );
        int32              computeBucketIndex( const float3& position ) const;

        NavGrid               _grid;
        NavGrid               _airGrid; ///< 하늘 격자이다. 모두 열려 있고 크기는 같으며, 공중 유닛은 지형과 건물을 넘는다
        GridPathfinder        _pathfinder;
        deque<RTSUnit>        _listUnit;       ///< 슬롯 번호가 id 의 index 이다(deque 라서 걸음 중에 유닛이 생겨도 다른 유닛 참조가 유지된다)
        vector<uint32>        _listGeneration; ///< 슬롯마다의 세대
        vector<uint32>        _listFreeSlot;
        vector<RTSPlayer>     _listPlayer;
        EventBuffer<RTSEvent> _eventBuffer;
        vector<uint8>         _listTerrainBlocked;
        vector<vector<uint8>> _listTeamVisibility; ///< 팀마다 셀의 RTSVisibility 를 담는다
        vector<int32>         _listBucketHead;     ///< 버킷마다 첫 유닛의 슬롯(-1 이면 끝)
        vector<int32>         _listBucketNext;     ///< 유닛 슬롯마다 다음 유닛의 슬롯
        vector<float3>        _listNeighborScratch;
        vector<RTSUnitID>     _listQueryScratch;
        deque<FlowFieldSlot>  _listFlowField; ///< deque 라서 슬롯이 움직이지 않으므로 행위자가 포인터를 보관할 수 있다
        const RTSCatalog*     _pCatalog;
        RTSSettings           _settings;
        FixedStepTimer        _stepTimer;
        float32               _time;
        Countdown             _visionTimer;
        GridTopology          _bucketTopology; ///< 이웃 탐색 버킷 격자(버킷 하나는 `_settings._bucketSize` 셀)
        LandBinding           _land;           ///< 빌린 공유 땅(없으면 단독 동작)
        int32                 _teamCount;
        int32                 _winningTeam;
        uint32                _landRevision; ///< 마지막으로 땅 격자를 칠한 땅 리비전
    };
} // namespace sw

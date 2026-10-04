/**
 * @file SrpgBattlefield.h
 * @brief 택틱스 SRPG 의 전장 — 격자 지형 · 유닛 배치 · 팀(아군 · 적 · 제3세력) · 페이즈 또는 개별 행동 순 · 이동 범위(ZOC) · 무기 사용 조건 · MAP 병기 범위 ·
 *        HP · EN · 탄수 · 기력 · 이동 회피 · 경험치 · 개발입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Combat/TurnOrder.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Strategy/TacticsSrpg/SrpgCatalog.h"
#include "GameFramework/Progression/LevelProgress.h"
#include "GameFramework/Utility/EventBuffer.h"
#include "GameFramework/Utility/GameRandom.h"
#include "GameFramework/Utility/GridTopology.h"

namespace sw
{
    class GridReachability;

    /** @brief 팀입니다. 다른 팀끼리는 모두 적입니다(제3세력은 양쪽 모두와 싸운다). */
    enum class SrpgTeam : uint8
    {
        Player = 0,
        Enemy,
        Third
    };

    /** @brief 팀 수입니다. */
    static constexpr int32 kSrpgTeamCount = 3;

    /** @brief 차례 방식입니다. */
    enum class SrpgTurnMode : uint8
    {
        Phases = 0, ///< 아군 페이즈 → 적 페이즈 → 제3세력 페이즈 — 팀의 모든 유닛이 행동하면 넘긴다(G 제네레이션 · 파이어 엠블렘)
        Individual  ///< 유닛마다 반응 + 운동성 순서로(`TurnOrder` 라운드제)
    };

    /** @brief 무기를 지금 쓸 수 있는가의 답입니다. */
    enum class SrpgWeaponStatus : uint8
    {
        Ok = 0,
        InvalidWeapon,
        NotEnoughEnergy,
        NoAmmo,
        LowMorale,
        NotAfterMove, ///< 이동한 뒤에는 못 쓰는 무기
        CannotAct,    ///< 그 유닛의 차례가 아니거나 이미 공격했다
        OutOfRange,
        InvalidTarget
    };

    /** @brief 규칙의 수치입니다. 확률은 %, 보정은 %p 입니다. */
    struct SrpgSettings
    {
        int32        _baseHit{ 70 };
        int32        _baseCrit{ 5 };
        int32        _critPercent{ 150 }; ///< 크리티컬 피해 배율(%)
        int32        _minimumDamage{ 10 };
        int32        _sizeHitStep{ 5 }; ///< 크기 한 단계 차이마다 명중 보정
        int32        _moraleStart{ 100 };
        int32        _moraleMin{ 50 };
        int32        _moraleMax{ 150 };
        int32        _moraleOnHit{ 2 };              ///< 맞혔을 때
        int32        _moraleOnKill{ 5 };             ///< 격파했을 때(맞힌 것에 더해서)
        int32        _moraleOnEvade{ 3 };            ///< 피했을 때
        int32        _moraleOnDamaged{ 1 };          ///< 맞았을 때
        int32        _supportDefenseReduction{ 50 }; ///< 지원 방어로 대신 맞을 때 줄어드는 몫(%)
        int32        _syncDamagePercent{ 50 };       ///< 동기 공격의 피해 몫(%)
        int32        _dodgePerCell{ 5 };             ///< 이동 회피 — 움직인 칸마다 쌓이는 회피(%p)
        int32        _dodgeMax{ 30 };
        int32        _xpHit{ 10 };      ///< 맞혔을 때 경험치
        int32        _xpKill{ 30 };     ///< 격파했을 때 경험치(맞힌 것에 더해서)
        int32        _xpLevelStep{ 2 }; ///< 상대와 레벨 한 차이마다 더하고 빼는 경험치
        SrpgTurnMode _turnMode{ SrpgTurnMode::Phases };
        uint8        _bZoneOfControl{ SW_FALSE };   ///< 적과 이웃한 칸에 들어가면 거기서 멈춘다
        uint8        _bSupportAttack{ SW_TRUE };    ///< 공격자와 이웃한 아군이 함께 친다
        uint8        _bSupportDefense{ SW_TRUE };   ///< 방어자와 이웃한 아군이 대신 맞는다
        uint8        _bSyncAttack{ SW_FALSE };      ///< 메탈슬러그 택틱스 — 같은 적을 사거리에 둔 아군이 함께 쏜다
        uint8        _bMoveDodge{ SW_FALSE };       ///< 메탈슬러그 택틱스 — 움직인 칸만큼 회피가 쌓인다(자기 차례가 오면 0)
        uint8        _bMapFriendlyFire{ SW_FALSE }; ///< MAP 병기가 아군도 친다
    };
} // namespace sw

namespace sw
{
    /** @brief 전장의 유닛 하나입니다. 번호는 `addUnit` 이 돌려준 자리이고 격파되어도 자리는 남습니다. */
    struct SrpgUnit
    {
        vector<const SrpgWeaponDef*> _listWeapon{}; ///< 기체 정의의 무기 순서
        vector<int32>                _listAmmo{};   ///< 무기마다 남은 탄(탄 없는 무기는 0)
        LevelProgress                _pilotLevel{};
        LevelProgress                _unitLevel{}; ///< 기체 레벨 — 개발 조건
        const SrpgUnitDef*           _pDef{ nullptr };
        const SrpgPilotDef*          _pPilot{ nullptr };
        int2                         _cell{};
        int32                        _hp{ 0 };
        int32                        _en{ 0 };
        int32                        _morale{ 100 };
        int32                        _dodge{ 0 };        ///< 이동 회피로 쌓인 것
        int32                        _rosterIndex{ -1 }; ///< 캠페인 명단의 자리(없으면 −1)
        SrpgTeam                     _team{ SrpgTeam::Player };
        uint8                        _bAlive{ SW_TRUE };
        uint8                        _bMoved{ SW_FALSE };
        uint8                        _bAttacked{ SW_FALSE };
        uint8                        _bActed{ SW_FALSE };
        uint8                        _bCommander{ SW_FALSE };
        uint8                        _bSupportUsed{ SW_FALSE }; ///< 이번 차례에 지원 공격 · 방어를 했다

        int32 computeStat( SrpgPilotStat stat ) const { return _pPilot != nullptr ? _pPilot->computeStat( stat, _pilotLevel.getLevel() ) : 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 전장 알림입니다. */
    struct SrpgEvent
    {
        enum class Kind : uint8
        {
            TurnStarted = 0, ///< _value = 턴
            PhaseStarted,    ///< _team
            UnitTurnStarted, ///< 개별 행동 순 — _unit
            Moved,           ///< _unit, _value = 걸은 칸
            Hit,             ///< _unit 이 _other 를, _value = 피해
            Missed,          ///< _unit 이 _other 를 놓쳤다
            Destroyed,       ///< _unit 이 _other 에게 격파되었다(_other 는 −1 일 수 있다)
            PilotLevelUp,    ///< _unit, _value = 새 레벨
            UnitLevelUp,     ///< _unit, _value = 새 기체 레벨
            Developed        ///< _unit
        };

        int32    _unit{ -1 };
        int32    _other{ -1 };
        int32    _value{ 0 };
        Kind     _kind{ Kind::TurnStarted };
        SrpgTeam _team{ SrpgTeam::Player };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SrpgBattlefield
     * @brief 엔진 없이 도는 전장 상태입니다. 전투 계산은 `SrpgCombat`, 적 AI 는 `SrpgAiController`, 승패는 `SrpgMission` 이 이 위에서 합니다.
     * @details 카탈로그는 전장보다 오래 살아야 합니다(유닛이 정의 포인터를 든다). 결정적입니다 — 같은 씨앗 · 같은 명령이면 같은 결과입니다.
     */
    class SW_GF_API SrpgBattlefield
    {
    public:
        SrpgBattlefield();

        void               initialize( const SrpgCatalog* pCatalog, int32 width, int32 height, const hashed_string& defaultTerrain, const SrpgSettings& settings, uint32 seed );
        [[nodiscard]] bool setTerrain( const int2& cell, const hashed_string& terrainId );
        /** @brief 사각형(양 끝 포함)을 한 지형으로 칠합니다. 칠한 칸 수입니다. */
        int32 fillTerrain( const int2& fromCell, const int2& toCell, const hashed_string& terrainId );
        /** @brief 유닛을 놓습니다. 모르는 기체 · 파일럿, 격자 밖, 이미 선 칸, 못 들어가는 지형이면 −1 입니다. */
        int32 addUnit( const hashed_string& unitId, const hashed_string& pilotId, SrpgTeam team, const int2& cell, int32 pilotLevel = 1 );
        void  setCommander( int32 unitIndex, bool bCommander );
        /** @brief 1 턴을 엽니다(유닛을 다 놓은 뒤). */
        void beginBattle();

        // --- 차례 ---
        bool canAct( int32 unitIndex ) const;
        /** @brief 유닛의 행동을 끝냅니다. 페이즈의 모두가 끝났으면 다음 페이즈(또는 다음 유닛)로 넘깁니다. */
        void endUnitAction( int32 unitIndex );
        /** @brief 지금 페이즈를 통째로 끝냅니다(남은 유닛은 대기). 개별 행동 순이면 지금 유닛만 끝냅니다. */
        void     endPhase();
        SrpgTeam getPhaseTeam() const { return _phaseTeam; }
        int32    getTurn() const { return _turn; }
        /** @brief 개별 행동 순에서 지금 차례인 유닛입니다(페이즈 방식이면 −1). */
        int32 getActiveUnit() const { return _activeUnit; }

        // --- 이동 ---
        /** @brief 유닛이 그 칸에 들어가는 비용입니다(지형 · 이동 타입 · 적성). 못 들어가면 −1 입니다 — 다른 유닛은 보지 않는다. */
        int32 computeTerrainCost( const SrpgUnit& unit, const int2& cell ) const;
        /** @brief 그 칸이 @p unit 의 적과 이웃한가(ZOC)입니다. */
        bool isInEnemyZone( const SrpgUnit& unit, const int2& cell ) const;
        /** @brief 이동 범위 — 적 칸은 막히고 아군 칸은 지나가되 서지 못한다. ZOC 면 적과 이웃한 칸에서 더 나아가지 못한다. */
        void computeMoveRange( int32 unitIndex, GridReachability& outReach ) const;
        /** @brief 이동합니다(차례 · 아직 안 움직임 · 범위 안). 걸은 칸만큼 이동 회피가 쌓입니다. */
        [[nodiscard]] bool moveUnit( int32 unitIndex, const int2& cell );
        /** @brief "빨간 칸" — 이동 후 쓸 수 있는 무기는 갈 수 있는 모든 칸에서, 나머지는 지금 칸에서 닿는 칸입니다. */
        void collectThreatCells( int32 unitIndex, vector<int2>& outListCell ) const;

        // --- 무기 ---
        const SrpgWeaponDef* findWeapon( const SrpgUnit& unit, int32 weaponIndex ) const;
        /** @brief EN · 탄 · 기력 · 이동 후 조건입니다(차례와 사거리는 보지 않는다). */
        SrpgWeaponStatus computeWeaponStatus( const SrpgUnit& unit, int32 weaponIndex, bool bAfterMove ) const;
        static bool      isInWeaponRange( const SrpgWeaponDef& weapon, const int2& fromCell, const int2& toCell );
        /** @brief EN · 탄을 씁니다. */
        void consumeWeapon( int32 unitIndex, int32 weaponIndex );
        /**
         * @brief MAP 병기가 덮는 칸입니다(격자 밖은 뺀다). `Self` 는 @p aimCell 쪽으로 무늬를 돌리고, `Target` 은 @p aimCell 이 사거리 안이어야 합니다.
         * @return 겨눌 수 없으면 false 입니다.
         */
        [[nodiscard]] bool collectMapCells( int32 unitIndex, int32 weaponIndex, const int2& aimCell, vector<int2>& outListCell ) const;

        // --- 상태 변화(전투 계산이 부른다) ---
        /** @brief 피해를 줍니다. 격파하면 true 입니다. */
        [[nodiscard]] bool applyDamage( int32 unitIndex, int32 amount, int32 attackerIndex );
        void               addMorale( int32 unitIndex, int32 delta );
        /** @brief 파일럿과 기체에 경험치를 줍니다. */
        void grantXp( int32 unitIndex, int64 amount );
        void pushEvent( const SrpgEvent& event ) { _eventBuffer.push( event ); }
        void drainEvents( vector<SrpgEvent>& outListEvent );

        // --- 개발 ---
        /** @brief 기체 레벨 조건을 채운 개발 갈래입니다. */
        void collectDevelopOptions( int32 unitIndex, vector<hashed_string>& outListUnitId ) const;
        /** @brief 다른 기체로 개발합니다(파일럿 · 위치 그대로, 기체 레벨 1, HP · EN · 탄 가득). 조건이 안 되면 false 입니다. */
        [[nodiscard]] bool developUnit( int32 unitIndex, const hashed_string& targetUnitId );

        // --- 조회 ---
        static bool           isHostile( SrpgTeam lhs, SrpgTeam rhs ) { return lhs != rhs; }
        static int32          computeDistance( const int2& lhs, const int2& rhs );
        bool                  isInside( const int2& cell ) const { return 0 <= cell._x && cell._x < _width && 0 <= cell._y && cell._y < _height; }
        const SrpgTerrainDef* findTerrainAt( const int2& cell ) const;
        /** @brief 그 칸에 선 살아 있는 유닛입니다. 없으면 −1 입니다. */
        int32 findUnitAt( const int2& cell ) const;
        /** @brief 유닛이 그 칸에서 쓰는 적성(%)입니다. */
        int32 computeAptitude( const SrpgUnit& unit, const int2& cell ) const;
        /** @brief 그 팀의 살아 있는 유닛 수입니다. */
        int32 countAlive( SrpgTeam team ) const;

        SrpgUnit*               findUnit( int32 unitIndex );
        const SrpgUnit*         findUnit( int32 unitIndex ) const;
        const vector<SrpgUnit>& getUnits() const { return _listUnit; }
        const SrpgSettings&     getSettings() const { return _settings; }
        const SrpgCatalog*      getCatalog() const { return _pCatalog; }
        GameRandom&             getRandom() { return _random; }
        int32                   getWidth() const { return _width; }
        int32                   getHeight() const { return _height; }

    private:
        void startPhase( SrpgTeam team );
        void advancePhase();
        void activateNextUnit();
        void resetUnitTurn( SrpgUnit& unit );
        void refillUnit( SrpgUnit& unit );
        bool isPhaseFinished() const;

        vector<const SrpgTerrainDef*> _listTerrain; ///< 칸 → 지형
        vector<SrpgUnit>              _listUnit;
        EventBuffer<SrpgEvent>        _eventBuffer;
        mutable GridSearchScratch     _cellMarks; ///< `collectThreatCells` 의 "한 칸 한 번" 표시
        SrpgSettings                  _settings;
        TurnOrder                     _turnOrder;
        GameRandom                    _random;
        const SrpgCatalog*            _pCatalog;
        int32                         _width;
        int32                         _height;
        int32                         _turn;
        int32                         _activeUnit;
        SrpgTeam                      _phaseTeam;
    };
} // namespace sw

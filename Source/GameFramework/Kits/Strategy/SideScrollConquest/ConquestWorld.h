/**
 * @file ConquestWorld.h
 * @brief 횡스크롤 정복 한 판 — 1 차원 전선(x 레인)의 거점 · 건물 · 일꾼 · 생산 · 병력 훈련, 지휘관이 이끄는 부대(따라오기 · 대기 · 돌격, 진형 간격,
 *        가장 가까운 적과 자동 전투, 지휘관 근처 사기), 공성(성문 · 성벽 · 충차 · 사다리), 점령 → 영토 · 수입, 시간 · 영토에 비례하는 반격 웨이브입니다.
 * @details 시간은 고정 걸음(`FixedStepTimer`)으로만 흐르고 유닛은 늘 같은 순서로 처리하며 난수를 쓰지 않습니다 — 같은 입력이면 같은 판입니다.
 *          플레이어는 왼쪽(작은 x)에서 오른쪽으로, 적은 오른쪽에서 왼쪽으로 갑니다. 성문은 거점의 점령 구역 바깥 가장자리(공격하는 쪽)에 있고,
 *          서 있는 동안 상대편은 지나지 못합니다 — 그 성문에 자기편 사다리가 걸쳐 있으면 넘습니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Data/StatBlock.h"
#include "GameFramework/Base/Utility/Countdown.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Strategy/SideScrollConquest/ConquestCatalog.h"

namespace sw
{
    class Archive;

    /** @brief 부대 명령입니다. */
    enum class ConquestOrder : uint8
    {
        Follow = 0, ///< 지휘관 뒤 진형 자리 — 지휘관에게서 끈 길이 안의 적만 쫓는다
        Hold,       ///< 명령한 자리의 진형 — 사거리 안의 적만 때린다
        Charge      ///< 가장 가까운 적에게 — 없으면 앞쪽의 가장 가까운 남의 거점으로(점령)
    };

    /** @brief 짓기 · 배정 · 훈련 결과입니다. */
    enum class ConquestResult : uint8
    {
        Ok = 0,
        UnknownDef,
        InvalidBuilding,
        SiteNotOwned,
        NoBuildSlot,
        NotEnoughResources,
        PopulationCap,
        CannotTrainHere,
        NoFreeWorkers
    };

    SW_GF_API const utf8* toString( ConquestResult result );

    /** @brief 거점의 지금 상태입니다. */
    struct ConquestSite
    {
        const ConquestSiteDef* _pDef{ nullptr };
        float32                _gateHealth{ 0.0f };
        float32                _wallHealth{ 0.0f };
        float32                _captureProgress{ 0.0f };
        ConquestTeam           _owner{ ConquestTeam::Neutral };
        ConquestTeam           _captureTeam{ ConquestTeam::Neutral }; ///< 지금 점령 중인 편
    };
} // namespace sw

namespace sw
{
    /** @brief 지은 건물 하나입니다. */
    struct ConquestBuilding
    {
        const ConquestBuildingDef* _pDef{ nullptr };
        vector<hashed_string>      _listQueue{}; ///< 훈련 대기열(값은 넣을 때 냈다)
        float32                    _cycleProgress{ 0.0f };
        float32                    _trainProgress{ 0.0f };
        int32                      _siteIndex{ -1 };
        int32                      _workers{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 병사 하나입니다. */
    struct ConquestUnit
    {
        const ConquestUnitDef* _pDef{ nullptr };
        float32                _x{ 0.0f };
        float32                _health{ 0.0f };
        Countdown              _attackCooldown{};
        float32                _holdX{ 0.0f }; ///< 대기 명령의 자기 자리
        float32                _damageDealt{ 0.0f };
        int32                  _unitId{ -1 };
        int32                  _squadSlot{ -1 }; ///< 지휘관 부대의 자리(0 = 바로 뒤), 부대가 아니면 −1
        ConquestTeam           _team{ ConquestTeam::Neutral };
        ConquestOrder          _order{ ConquestOrder::Follow };
        uint8                  _bAlive{ SW_TRUE };
        uint8                  _bPlanted{ SW_FALSE }; ///< 사다리 — 성벽에 걸쳤다
    };
} // namespace sw

namespace sw
{
    /** @brief 지휘관(플레이어)입니다. */
    struct ConquestCommander
    {
        float32   _x{ 0.0f };
        float32   _health{ 0.0f };
        Countdown _attackCooldown{};
        Countdown _respawnTimer{};
        float32   _moveAxis{ 0.0f };
        float32   _facing{ 1.0f }; ///< +1 = 오른쪽
        uint8     _bAlive{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 한 판의 알림입니다. */
    struct ConquestEvent
    {
        enum class Kind : uint8
        {
            UnitTrained = 0, ///< `_id` = 병종, `_value` = 병사 번호
            UnitDied,
            ResourceProduced, ///< `_id` = 자원, `_value` = 양
            IncomePaid,       ///< `_value` = 수입을 낸 거점 수
            GateBroken,       ///< `_id` = 거점
            WallBroken,
            SiteCaptured, ///< `_id` = 거점, `_team` = 새 주인
            WaveSpawned,  ///< `_value` = 병사 수
            CommanderDied,
            CommanderRespawned,
            Victory,
            Defeat
        };

        Kind          _kind{ Kind::UnitTrained };
        hashed_string _id{};
        int32         _value{ 0 };
        ConquestTeam  _team{ ConquestTeam::Neutral };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ConquestWorld
     * @brief 한 판의 상태입니다. 카탈로그는 빌려 씁니다.
     * @details 매 걸음: 지휘관(이동 · 공격 · 부활) → 병사(명령에 따라 움직이고 가장 가까운 적을 때린다, 번호 순) → 죽은 병사 정리 · 부대 자리 다시 매기기 →
     *          점령 → 생산 · 훈련 · 수입 → 반격 웨이브 → 승패. 사기: 지휘관이 살아 있고 `_moraleRadius` 안이면 플레이어 병사의 피해가 늘어납니다.
     *          성벽이 서 있는 거점의 점령 구역 안 수비대는 `_wallProtection` 만큼 덜 받습니다. 충차는 성문(부서지면 성벽)만, 사다리는 싸우지 않습니다.
     */
    class SW_GF_API ConquestWorld
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "CNQW" );
        static constexpr uint32 kStateVersion = 1;

        ConquestWorld();

        /** @brief 새 판 — 거점 · 주둔군 · 시작 자원을 두고 지휘관을 첫 플레이어 거점(가장 왼쪽)에 세웁니다. */
        void initialize( const ConquestCatalog* pCatalog );
        /** @brief 프레임 시간을 받아 고정 걸음으로 진행합니다. */
        void update( float32 deltaTime );

        /** @brief 지휘관의 이동 입력(−1..1)입니다. 다음 입력까지 유지됩니다. */
        void setCommanderMove( float32 axis );
        /** @brief 부대 전체에 명령합니다. 대기는 지금 지휘관 자리를 기준으로 진형 자리를 정합니다. */
        void issueOrder( ConquestOrder order );

        ConquestResult placeBuilding( const hashed_string& buildingId, const hashed_string& siteId );
        /** @brief 건물의 일꾼 수를 정합니다(0..자리 수). 늘리는 만큼 남는 일꾼이 있어야 합니다. */
        ConquestResult assignWorkers( int32 buildingIndex, int32 workerCount );
        /** @brief 건물에서 병종을 훈련합니다(값은 지금 낸다, 인구 한도는 대기열까지 센다). */
        ConquestResult trainUnit( int32 buildingIndex, const hashed_string& unitId );
        /**
         * @brief 병사를 바로 세웁니다(스크립트 · 시험). 플레이어 병사는 지휘관 부대에 듭니다.
         * @return 병사 번호. 모르는 병종이면 −1 입니다.
         */
        int32 spawnUnit( const hashed_string& unitId, ConquestTeam team, float32 x, ConquestOrder order );
        void  addResource( const hashed_string& resource, float32 amount ) { _resource.addValue( resource, amount ); }

        void drainEvents( vector<ConquestEvent>& outListEvent );

        /**
         * @brief 거점(정의 id · 성문 · 성벽 · 점령) · 건물(정의 id · 대기열 · 진행 · 자리 · 일꾼) · 병사 · 자원 · 지휘관 · 고정 걸음 · 지난 시간 · 수입 · 웨이브 타이머 ·
         *        다음 병사 번호 · 승패를 씁니다. 정의는 id 로 싣고 카탈로그에서 찾습니다. 알림은 읽을 때 비웁니다.
         */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 같은 카탈로그로 `initialize` 한 뒤에 부릅니다. 깨졌거나 없는 정의면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

        int32 getResource( const hashed_string& resource ) const;
        int32 computePopulation() const;
        int32 computePopulationCap() const;
        int32 computeFreeWorkers() const;
        /** @brief 플레이어 거점 수입니다. */
        int32 computeTerritory() const;
        /** @brief 지금 웨이브가 오면 몇 명인가입니다(기본 + 지난 분 × 분당 + (영토 − 1) × 거점당). */
        int32                           computeWaveSize() const;
        const ConquestSite*             findSite( const hashed_string& siteId ) const;
        const ConquestUnit*             findUnit( int32 unitId ) const;
        int32                           getSquadSize() const;
        const vector<ConquestSite>&     getSites() const { return _listSite; }
        const vector<ConquestBuilding>& getBuildings() const { return _listBuilding; }
        const vector<ConquestUnit>&     getUnits() const { return _listUnit; }
        const ConquestCommander&        getCommander() const { return _commander; }
        float32                         getElapsed() const { return _elapsed; }
        float32                         getWaveTimer() const { return _waveTimer; }
        bool                            isVictory() const { return _bVictory == SW_TRUE; }
        bool                            isDefeat() const { return _bDefeat == SW_TRUE; }

    private:
        /** @brief 노릴 대상 — 병사 · 지휘관 · 거점 구조물(성문 · 성벽) 중 하나입니다. */
        struct Target
        {
            float32 _x{ 0.0f };
            int32   _unitIndex{ -1 };
            int32   _siteIndex{ -1 };
            uint8   _bCommander{ SW_FALSE };

            bool isValid() const { return _unitIndex >= 0 || _siteIndex >= 0 || _bCommander == SW_TRUE; }
        };

        void    step( float32 deltaTime );
        void    stepCommander( float32 deltaTime );
        void    stepUnit( int32 unitIndex, float32 deltaTime );
        void    stepCapture( float32 deltaTime );
        void    stepEconomy( float32 deltaTime );
        void    stepWaves( float32 deltaTime );
        void    refreshSquadSlots();
        void    refreshOutcome();
        Target  findNearestTarget( ConquestTeam team, float32 x, float32 maxDistance, bool bUnits, bool bStructures ) const;
        float32 computeGateX( const ConquestSite& site, ConquestTeam attacker ) const;
        bool    isLaddered( int32 siteIndex, ConquestTeam attacker ) const;
        float32 clampMove( ConquestTeam team, float32 fromX, float32 toX ) const;
        float32 computeFormationX( const ConquestUnit& unit ) const;
        float32 computeMoraleScale( ConquestTeam team, float32 x ) const;
        void    applyAttack( ConquestTeam attacker, float32 baseDamage, float32 structureScale, const Target& target, float32& inoutDealt );
        int32   findHomeSiteIndex() const;
        void    pushEvent( ConquestEvent::Kind kind, const hashed_string& id, int32 value = 0, ConquestTeam team = ConquestTeam::Neutral );

        vector<ConquestSite>       _listSite;
        vector<ConquestBuilding>   _listBuilding;
        vector<ConquestUnit>       _listUnit;
        EventBuffer<ConquestEvent> _eventBuffer;
        StatBlock                  _resource;
        ConquestCommander          _commander;
        FixedStepTimer             _timer;
        const ConquestCatalog*     _pCatalog;
        float32                    _elapsed;
        float32                    _incomeTimer;
        float32                    _waveTimer;
        int32                      _nextUnitId;
        uint8                      _bVictory;
        uint8                      _bDefeat;
    };
} // namespace sw

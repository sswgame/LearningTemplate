/**
 * @file VelmoraGameModeComponent.h
 * @brief 벨모라 골짜기 디렉터 — 플레이어를 지역 사이로 걷게 하고(입력 맵 `Move`), 지역에 들어서면 진행 규칙(`VelmoraCampaign`)과 그 지역의 전투(액션 룸 홀 → 보스)를 돌리고,
 *        봉화 · 야영지를 쓰게 하고(`Velmora.Use`), 퀘스트 보상을 주고, 자동 저장에 지역 이동 · 보스 앞 · 체크포인트를 알립니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/Rule/AdventureVitals.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"

#include "Games/Velmora/VelmoraCampaign.h"

namespace sw
{
    class SessionStateComponent;
    class VelmoraRegionCatalog;
    struct VelmoraRegionDef;

    /** @brief 판의 단계입니다(감시 값 `Velmora.Phase` 의 수). */
    enum class VelmoraPhase : uint8
    {
        Explore = 0, ///< 지역 사이를 걷는다(전투 없음)
        Hall,        ///< 지역 전투의 홀
        BossWait,    ///< 홀을 깨고(또는 보스만 있는 지역에 들어서) 보스가 나오기를 기다린다
        Boss         ///< 보스 전투
    };
} // namespace sw

namespace sw
{
    /**
     * @class VelmoraGameModeComponent
     * @brief 오픈월드 싱글 수직 슬라이스 한 판입니다. 같은 오브젝트의 판 상태(`SessionStateComponent`, 맨 앞)를 열고 퀘스트 일지 · 깃발 · 지갑을 그곳에 둔다.
     * @details 지역 · 전투 배치는 게임 서비스 `VelmoraRegionCatalog`, 적 수치는 `MonsterCatalog`, 퀘스트는 `QuestCatalog` 다(게임 인스턴스가 건다 — 확장 팩이 층으로 항목을 더한다).
     *          - 걷기: 지역 밖으로는 나가지 못한다(지역이 땅이다). 전투 중에는 그 지역 경계 안에 묶인다(게이트가 닫힌다).
     *          - 지역 진입: 진행 규칙에 알리고 자동 저장에 지역 이동을 알린다. 깨지 않은 전투가 있으면 홀(없으면 보스)을 연다. 보스 앞에서 자동 저장에 알린다.
     *          - `Velmora.Use`: 곁의 봉화를 켜거나(퀘스트 `Light`), 곁의 야영지에서 쉰다(체력을 채우고 일어설 지역 · 자동 저장 체크포인트).
     *          - 쓰러지면 룸이 비워지고 일어설 지역의 자리에서 체력을 채워 일어선다. 그 지역의 전투는 깨지 않은 채로 남는다.
     *          - 자동 저장 요청은 게임 스레드에서 낸다(틱 뒤 플러시) — 관리자는 게임 스레드 전용이다.
     *          - 불러온 판의 지역 · 전투 지역이 지금 표에 없으면(확장 팩을 끈 판) 플레이어를 일어설 지역(없으면 시작 지역)으로 옮기고 전투를 닫는다 — 판은 이어진다.
     *          모습은 디버그 그리기(플레이어 · 룸 · 봉화 · 야영지)와 씬의 땅 메시다. 카메라는 `FollowCameraComponent` 가 `readCameraFocus` 로 따라간다.
     */
    REFLECT( Category = "Velmora", DisplayName = "Velmora Game Mode", Tooltip = "Runs the Velmora valley: regions, beacons, camps, region fights, quest rewards and autosave hooks" )
    class VelmoraGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "VLDR" );
        static constexpr uint32 kStateVersion = 1;

        VelmoraGameModeComponent();
        virtual ~VelmoraGameModeComponent() override;

        void               serializeState( StateIO& io ) override;
        [[nodiscard]] bool readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32                getPhase() const { return static_cast<int32>( _phase ); }
        float32              getPlayerX() const { return _playerPosition._x; }
        float32              getPlayerZ() const { return _playerPosition._y; }
        int32                getHealthQuarters() const { return _campaign.getVitals().computeHealthQuarters(); }
        int32                getHeartCount() const { return _campaign.getVitals().getHeartCount(); }
        int32                getAliveEnemyCount() const { return _room.getAliveEnemyCount(); }
        int32                getFallCount() const { return _fallCount; }
        const hashed_string& getCurrentRegion() const { return _currentRegion; }
        const hashed_string& getRespawnRegion() const { return _campaign.getRespawnRegion(); }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onRegister( GameObjectManager& manager ) override;
        void               onUnregister( GameObjectManager& manager ) override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        bool               hasPendingSpawn() const override;

    private:
        /** @brief 판 상태 · 지역 표를 찾습니다. 판을 연 뒤에는 늘 있다(없으면 판을 열지 않았다). */
        SessionStateComponent*      findState() const;
        const VelmoraRegionCatalog* findRegions() const;
        /** @brief 지역 표의 전투 종이 모두 몬스터 표에 있는지 봅니다. 없는 종마다 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validateRegionMonsters( const VelmoraRegionCatalog& regions ) const;
        /** @brief 이번 프레임의 걷기를 적용합니다. 지역 밖(전투 중이면 그 지역 밖)으로는 가지 않는다. */
        void movePlayer( const float2& move, float32 deltaTime, const VelmoraRegionCatalog& regions );
        /** @brief 지금 자리의 지역이 바뀌었으면 진입을 처리합니다. */
        void updateRegion( const VelmoraRegionCatalog& regions, SessionStateComponent& state );
        /** @brief @p region 의 전투를 엽니다(홀이 있으면 홀, 없으면 보스 대기). */
        void beginEncounter( const VelmoraRegionDef& region );
        /** @brief 곁의 봉화를 켜거나 야영지에서 쉽니다. */
        void interactWithSite( const VelmoraRegionDef& region, SessionStateComponent& state );
        /** @brief 전투 한 프레임을 돌리고 결과(피해 · 클리어)를 적용합니다. */
        void updateEncounter( float32 deltaTime, const VelmoraRegionCatalog& regions, SessionStateComponent& state );
        /** @brief 쓰러졌습니다. 룸을 비우고 일어설 지역의 자리에서 일어섭니다. */
        void handlePlayerDowned( const VelmoraRegionCatalog& regions );
        /** @brief 불러온 판이 지금 표에 없는 지역을 가리키면 일어설 자리로 옮기고 전투를 닫습니다. */
        void reconcileWithRegions( const VelmoraRegionCatalog& regions );
        void drawWorld( const VelmoraRegionCatalog& regions, const SessionStateComponent& state ) const;

    private:
        PROPERTY( Category = "Start", DisplayName = "Start Region", Tooltip = "Region id (regions.xml) a new game starts in, at its respawn point" )
        hashed_string _startRegion;
        PROPERTY( Category = "Player", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _moveSpeed;
        PROPERTY( Category = "Player", DisplayName = "Use Radius", Tooltip = "How near a beacon or camp Velmora.Use reaches", Min = 0.1, Units = m )
        float32 _useRadius;
        PROPERTY( Category = "Player", DisplayName = "Vitals" )
        AdventureVitalsSettings _vitalsSettings;
        PROPERTY( Category = "Combat", DisplayName = "Boss Delay", Tooltip = "Seconds between clearing the hall (or entering a boss region) and the boss fight", Min = 0.1, Units = s )
        float32 _bossDelaySeconds;

        VelmoraCampaign     _campaign;
        ActionRoom          _room;
        Countdown           _bossDelay;
        float2              _playerPosition;
        FacingDir           _facing;
        hashed_string       _currentRegion;     ///< 지금 서 있는 지역(비면 다음 틱이 진입을 처리한다)
        hashed_string       _encounterRegion;   ///< 전투 중인 지역(탐험 중이면 비었다)
        hashed_string       _pendingArea;       ///< 게임 스레드에서 자동 저장에 알릴 지역 이동
        hashed_string       _pendingBoss;       ///< 보스 앞
        hashed_string       _pendingCheckpoint; ///< 체크포인트(야영지)
        vector<QuestEvent>  _listQuestEvent;    ///< 프레임마다 비워 다시 쓰는 알림 그릇(할당을 되풀이하지 않는다)
        vector<WalletEvent> _listWalletEvent;
        vector<HealthEvent> _listHealthEvent;
        int32               _fallCount;
        VelmoraPhase        _phase;
    };
} // namespace sw

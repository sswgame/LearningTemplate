/**
 * @file VelmoraEncounterGameModeComponent.h
 * @brief 벨모라 골짜기의 지역 전투 — 액션 어드벤처 디렉터(`AdventureGameModeComponent`)가 기다리게 한 전투를 액션 룸(홀 → 보스)으로 돌립니다.
 * @details 키트 둘을 섞는 자리라 게임이 가진다 — 지역 · 몸 · 퀘스트는 GF_ActionAdventure, 룸 전투 · 몬스터 표는 GF_ActionCombat 이고 두 키트는 서로를 모른다
 *          (도시 · RTS 를 섞는 `FrontierSkirmishGameModeComponent` 와 같은 자리).
 */
#pragma once
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    class AdventureGameModeComponent;
    class AdventureRegionCatalog;
    struct AdventureRegionDef;

    /** @brief 전투의 단계입니다(감시 값 `Velmora.Phase` 의 수). */
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
     * @class VelmoraEncounterGameModeComponent
     * @brief 같은 오브젝트의 앞에 붙은 액션 어드벤처 디렉터의 지역 전투를 돌립니다. 적 수치는 게임 서비스 `MonsterCatalog` 다.
     * @details 전투가 열리면 디렉터가 게이트를 닫고(플레이어가 그 지역에 묶인다), 여기서 홀 → 보스 대기 → 보스를 돌린다. 닿은 피해(`MonsterStatsDef::_atk`)는 4분의 1 하트 단위로 디렉터에 준다.
     *          입력은 명령(`submitCombatInput`)으로 받는다 — 읽는 쪽 · 룸 그리기는 클라이언트 전용 `VelmoraEncounterViewComponent` 다.
     *          싸움은 싣지 않는다(디렉터도 싣지 않는다) — 불러온 판은 그 지역에 다시 들어서며 처음부터 연다. 확장 팩을 끈 판이 확장 지역의 적 종을 모르는 바이트를 만나지 않는다.
     */
    REFLECT( Category = "Velmora", DisplayName = "Velmora Encounter Game Mode",
             Tooltip = "Runs the region fights (hall, boss) of the Adventure Game Mode in front of it on the same object with the ActionCombat room" )
    class VelmoraEncounterGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        static constexpr uint32 kStateTag     = FourCCUtil::make( "VLEN" );
        static constexpr uint32 kStateVersion = 1;

        VelmoraEncounterGameModeComponent();
        virtual ~VelmoraEncounterGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        /** @brief 이번 프레임의 공격 · 대시 누름을 넘깁니다. 걸음이 쓸 때까지 모은다(싸움이 없는 걸음은 버린다). 판이 열리지 않았으면 false 입니다. */
        [[nodiscard]] bool submitCombatInput( bool bAttackPressed, bool bDashPressed );

        // ---- 읽기(클라이언트 · 감시 값) ----
        const ActionRoom& getRoom() const { return _room; }
        bool              isFighting() const { return _phase == VelmoraPhase::Hall || _phase == VelmoraPhase::Boss; }
        int32             getPhase() const { return static_cast<int32>( _phase ); }
        int32             getAliveEnemyCount() const { return _room.getAliveEnemyCount(); }
        /** @brief 다음 공격 입력이 먹으면 true 입니다 — 쿨다운이 끝났고 걸음을 기다리는 공격도 없다. */
        bool isAttackReady() const { return _room.isAttackReady() && _bAttackQueued == SW_FALSE; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 지역 표의 전투 종이 모두 몬스터 표에 있는지 봅니다. 없는 종마다 오류를 남기고 false 입니다. */
        [[nodiscard]] static bool validateRegionMonsters( const AdventureRegionCatalog& regions );
        /** @brief @p region 의 전투를 엽니다(홀이 있으면 홀, 없으면 보스 대기). */
        void beginEncounter( const AdventureRegionDef& region, AdventureGameModeComponent& director );
        void beginBossWait( AdventureGameModeComponent& director );
        /** @brief 룸을 비우고 탐험으로 되돌립니다(클리어가 아닌 끝). */
        void closeEncounter();

    private:
        PROPERTY( Category = "Combat", DisplayName = "Boss Delay", Tooltip = "Seconds between clearing the hall (or entering a boss region) and the boss fight", Min = 0.1, Units = s )
        float32 _bossDelaySeconds;

        ActionRoom             _room;
        Countdown              _bossDelay;
        VelmoraPhase           _phase;
        uint8                  _bAttackQueued : 1; ///< 다음 걸음이 쓸 공격 누름
        uint8                  _bDashQueued   : 1; ///< 다음 걸음이 쓸 대시 누름
        [[maybe_unused]] uint8 _reserved      : 6;
    };
} // namespace sw

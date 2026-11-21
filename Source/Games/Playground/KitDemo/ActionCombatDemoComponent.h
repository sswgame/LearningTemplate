/**
 * @file ActionCombatDemoComponent.h
 * @brief 장르 키트 시연 — 액션 컴뱃(`GF_ActionCombat`)의 홀 → 보스 → 결과 한 판입니다. 키트의 `ActionRoom` 을 플레이어 입력(입력 맵 `Move` · `KitDemo.Attack` · `KitDemo.Dash`)으로 돌립니다.
 * @details 룸은 전투만 안다. 플레이어 자리 · HP · 판의 단계(홀, 보스 대기, 보스, 승리, 패배)는 이 디렉터가 가진다(언리얼 GameMode 가 폰의 죽음을 받는 자리).
 *          적 배치는 씬 데이터(PROPERTY), 적 수치는 게임 서비스 `MonsterCatalog`(Playground 의 `data/monsters.xml`)이다. 모습은 룸의 디버그 그리기와 플레이어 구 하나다.
 *          플레이어 HP 는 걸음마다 씬의 뷰 오브젝트(`_playerView`)의 `RuleHealthViewComponent` 에 스냅숏으로 넣어 HP 바가 같은 체력 계약으로 읽는다.
 *          판이 끝나면(승리 · 패배) `_resultHoldSeconds` 뒤 미니게임을 끝낸다(디렉터 계약 ③) — 승리는 결과 값 `heal`(`_victoryHeal`)만 내보내고 허브 캐릭터를 직접 고치지 않는다
 *          (허브의 `MinigameRewardComponent` 가 표대로 이펙트로 건다).
 *          장르 완결 루프 시나리오: `game/playground/automation/kitdemo_actioncombat.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /** @brief 액션 룸 시연의 판 단계입니다(감시 값 `KitDemo.Combat.Phase` 의 수). */
    ENUM()
    enum class ActionCombatDemoPhase : uint8
    {
        Hall = 0, ///< 홀 전투 중
        BossWait, ///< 홀을 깨고 보스 룸이 열리기를 기다린다
        Boss,     ///< 보스 전투 중
        Victory,  ///< 보스를 쓰러뜨렸다
        Defeat    ///< 플레이어 HP 가 0 이 되었다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ActionCombatDemoComponent
     * @brief 홀(적 여럿) → 보스 → 승리 · 패배를 플레이어 입력으로 진행합니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Action Combat Demo", Tooltip = "Runs one ActionCombat kit room run (hall, boss, result) from the player's input actions" )
    class ActionCombatDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ActionCombatDemoComponent();
        virtual ~ActionCombatDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32   getPhase() const { return static_cast<int32>( _phase ); }
        int32   getAliveEnemyCount() const { return _room.getAliveEnemyCount(); }
        int32   getPlayerHP() const { return _playerHP; }
        float32 getPlayerX() const { return _playerPosition._x; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 승리면 결과 값 `heal`(`_victoryHeal`)을 냅니다. 패배는 넘길 값이 없다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 씬 PROPERTY 의 종 · 자리 목록으로 룸 배치를 만듭니다. 두 목록의 길이가 다르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool makeLayout( ActionRoomLayout& outLayout ) const;
        /** @brief 이번 프레임의 입력을 읽어 플레이어를 움직이고 룸 입력을 만듭니다. */
        ActionRoomFrameInput readInput( float32 deltaTime );
        /** @brief 룸 결과(피해 · 클리어)로 단계를 넘깁니다. */
        void applyResult( const ActionRoomFrameResult& result );
        /** @brief 단계에 맞춰 룸을 한 걸음 돌립니다(`updateGame` 의 규칙 몸통). */
        void stepRoom( float32 deltaTime );
        void drawPlayer() const;
        /** @brief 지금 플레이어 HP 를 뷰 오브젝트의 체력 뷰에 넣습니다. 칸이 비었으면 할 일이 없고, 가리킨 오브젝트에 체력 뷰가 없으면 한 번 오류를 남깁니다. */
        void pushPlayerHealthView();

    private:
        PROPERTY( Category = "Room", DisplayName = "Hall Monsters", Tooltip = "Monster ids (MonsterCatalog) of the hall fight, one per position" )
        vector<hashed_string> _listHallMonster;
        PROPERTY( Category = "Room", DisplayName = "Hall Positions", Tooltip = "Room-plane positions (m) of the hall monsters" )
        vector<float2> _listHallPosition;
        PROPERTY( Category = "Room", DisplayName = "Boss Monsters", Tooltip = "Monster ids (MonsterCatalog) of the boss fight, one per position" )
        vector<hashed_string> _listBossMonster;
        PROPERTY( Category = "Room", DisplayName = "Boss Positions", Tooltip = "Room-plane positions (m) of the boss monsters" )
        vector<float2> _listBossPosition;
        PROPERTY( Category = "Player", DisplayName = "Max HP", Min = 1 )
        int32 _playerMaxHP;
        PROPERTY( Category = "Player", DisplayName = "Move Speed", Min = 0.0, Units = "m/s" )
        float32 _playerSpeed;
        PROPERTY( Category = "Room", DisplayName = "Boss Delay", Tooltip = "Seconds between clearing the hall and the boss fight", Min = 0.1, Units = s )
        float32 _bossDelaySeconds;
        PROPERTY( Category = "Player", DisplayName = "Player View", Tooltip = "Object whose RuleHealthViewComponent shows the player's rule HP (health bar)" )
        GameObjectHandle _playerView;
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Result", DisplayName = "Victory Heal", Tooltip = "Result value 'heal' reported on victory (the hub turns it into an effect)", Min = 0 )
        int32 _victoryHeal;

        ActionRoom _room;
        Countdown  _bossDelay;
        Countdown  _resultHold; ///< 판이 끝난 뒤 미니게임을 끝내기까지(결과를 보여 주는 시간)
        PROPERTY( RuntimeState )
        float2    _playerPosition;
        FacingDir _facing; ///< 리플렉션 열거가 아니다 — 손 본문이 싣는다
        PROPERTY( RuntimeState )
        int32 _playerHP;
        PROPERTY( RuntimeState )
        ActionCombatDemoPhase  _phase;
        uint8                  _bPlayerViewMissing : 1; ///< 가리킨 뷰 오브젝트에 체력 뷰가 없다고 이미 알렸다(매 걸음 되풀이하지 않는다)
        [[maybe_unused]] uint8 _reserved           : 7;
    };
} // namespace sw

/**
 * @file ActionCombatGameModeComponent.h
 * @brief 액션 컴뱃 키트(`GF_ActionCombat`)의 디렉터 — 홀 → 보스 → 결과 한 판을 진행합니다. 키트의 `ActionRoom` 을 플레이어 입력 명령(`submitPlayerInput`)으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트의 `ActionRoom`)을 들고 씬과 잇는다. 입력 읽기 · 모습 · HP 바는 클라이언트 모듈
 *          (`GF_Client_ActionCombat` 의 `ActionCombatAvatarComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          룸은 전투만 안다. 플레이어 자리 · HP · 판의 단계(홀, 보스 대기, 보스, 승리, 패배)는 이 디렉터가 가진다(언리얼 GameMode 가 폰의 죽음을 받는 자리).
 *          적 배치는 씬 데이터(PROPERTY), 적 수치는 게임 서비스 `MonsterCatalog`(Playground 의 `data/monsters.xml`)이다.
 *          판이 끝나면(승리 · 패배) `_resultHoldSeconds` 뒤 미니게임을 끝낸다(디렉터 계약 ③) — 승리는 결과 값 `heal`(`_victoryHeal`)만 내보내고 허브 캐릭터를 직접 고치지 않는다
 *          (허브의 `MinigameRewardComponent` 가 표대로 이펙트로 건다).
 *          장르 완결 루프 시나리오: `game/playground/automation/kitdemo_actioncombat.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"

namespace sw
{
    /** @brief 액션 컴뱃 판의 단계입니다(감시 값 `ActionCombat.Phase` 의 수). */
    ENUM()
    enum class ActionCombatPhase : uint8
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
     * @class ActionCombatGameModeComponent
     * @brief 홀(적 여럿) → 보스 → 승리 · 패배를 플레이어 입력 명령으로 진행합니다.
     */
    REFLECT( Category = "ActionCombat", DisplayName = "Action Combat Game Mode", Tooltip = "Runs one ActionCombat room run (hall, boss, result) from player input commands" )
    class SW_KIT_API ActionCombatGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        ActionCombatGameModeComponent();
        virtual ~ActionCombatGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /**
         * @brief 이번 프레임의 플레이어 입력을 넘깁니다. 다음 규칙 걸음(`updateGame`)이 걷기 · 공격 · 대시로 씁니다.
         * @details 이동은 마지막 값으로 덮고, 공격 · 대시 누름은 걸음이 쓸 때까지 모은다(한 걸음 사이에 들어온 누름을 잃지 않는다).
         * @return 판이 열렸고 승패가 나지 않아 입력을 받으면 true 입니다(`isAcceptingInput`). 아니면 받지 않고 false 입니다.
         */
        [[nodiscard]] bool submitPlayerInput( float2 move, bool bAttackPressed, bool bDashPressed );

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 판이 열렸고 승패가 나지 않아 입력 명령을 받으면 true 입니다. */
        bool              isAcceptingInput() const;
        const ActionRoom& getRoom() const { return _room; }
        ActionCombatPhase getCombatPhase() const { return _phase; }
        int32             getPhase() const { return static_cast<int32>( _phase ); }
        int32             getAliveEnemyCount() const { return _room.getAliveEnemyCount(); }
        int32             getPlayerHP() const { return _playerHP; }
        int32             getPlayerMaxHP() const { return _playerMaxHP; }
        const float2&     getPlayerPosition() const { return _playerPosition; }
        float32           getPlayerX() const { return _playerPosition._x; }

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
        /** @brief 받아 둔 입력 명령으로 플레이어를 움직이고 룸 입력을 만듭니다. 모은 누름은 여기서 비운다. */
        ActionRoomFrameInput consumeInput( float32 deltaTime );
        /** @brief 룸 결과(피해 · 클리어)로 단계를 넘깁니다. */
        void applyResult( const ActionRoomFrameResult& result );

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
        PROPERTY( Category = "Result", DisplayName = "Result Hold", Tooltip = "Seconds the result stays on screen before the minigame finishes", Min = 0.1, Units = s )
        float32 _resultHoldSeconds;
        PROPERTY( Category = "Result", DisplayName = "Victory Heal", Tooltip = "Result value 'heal' reported on victory (the hub turns it into an effect)", Min = 0 )
        int32 _victoryHeal;

        ActionRoom _room;
        Countdown  _bossDelay;
        Countdown  _resultHold; ///< 판이 끝난 뒤 미니게임을 끝내기까지(결과를 보여 주는 시간)
        PROPERTY( RuntimeState )
        float2          _playerPosition;
        FacingDirection _facing; ///< 리플렉션 열거가 아니다 — 손 본문이 싣는다
        PROPERTY( RuntimeState )
        int32 _playerHP;
        PROPERTY( RuntimeState )
        ActionCombatPhase      _phase;
        float2                 _commandMove;       ///< 마지막 입력 명령의 이동 축 — 클라이언트가 프레임마다 덮는 입력이라 싣지 않는다
        uint8                  _bAttackQueued : 1; ///< 다음 걸음이 쓸 공격 누름
        uint8                  _bDashQueued   : 1; ///< 다음 걸음이 쓸 대시 누름
        [[maybe_unused]] uint8 _reserved      : 6;
    };
} // namespace sw

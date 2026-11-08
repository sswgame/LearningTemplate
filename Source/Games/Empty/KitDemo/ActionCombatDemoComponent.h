/**
 * @file ActionCombatDemoComponent.h
 * @brief 장르 키트 시연 — 액션 컴뱃(`GF_ActionCombat`)의 홀 → 보스 → 결과 한 판입니다. 키트의 `ActionRoom` 을 플레이어 입력(입력 맵 `Move` · `KitDemo.Attack` · `KitDemo.Dash`)으로 돌립니다.
 * @details 룸은 전투만 안다. 플레이어 자리 · HP · 판의 단계(홀, 보스 대기, 보스, 승리, 패배)는 이 디렉터가 가진다(언리얼 GameMode 가 폰의 죽음을 받는 자리).
 *          적 배치는 씬 데이터(PROPERTY), 적 수치는 게임 서비스 `MonsterCatalog`(Empty 의 `data/monsters.xml`)이다. 모습은 룸의 디버그 그리기와 플레이어 구 하나다.
 *          장르 완결 루프 시나리오: `game/empty/automation/kitdemo_actioncombat.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Name/hashed_string.h"
#include "Core/Time/Countdown.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Rule/ActionRoom.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /** @brief 액션 룸 시연의 판 단계입니다(감시 값 `KitDemo.Combat.Phase` 의 수). */
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

        void serializeState( StateIO& io ) override;

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

    private:
        /** @brief 씬 PROPERTY 의 종 · 자리 목록으로 룸 배치를 만듭니다. 두 목록의 길이가 다르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool makeLayout( ActionRoomLayout& outLayout ) const;
        /** @brief 이번 프레임의 입력을 읽어 플레이어를 움직이고 룸 입력을 만듭니다. */
        ActionRoomFrameInput readInput( float32 deltaTime );
        /** @brief 룸 결과(피해 · 클리어)로 단계를 넘깁니다. */
        void applyResult( const ActionRoomFrameResult& result );
        void drawPlayer() const;

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

        ActionRoom            _room;
        Countdown             _bossDelay;
        float2                _playerPosition;
        FacingDir             _facing;
        int32                 _playerHP;
        ActionCombatDemoPhase _phase;
    };
} // namespace sw

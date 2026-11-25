#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct ActionCombatGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "KDAC" };

            static constexpr const utf8* kHealValue = "heal"; ///< 승리 결과 값 이름 — 허브 캐릭터의 보상 표(`MinigameRewardComponent`)가 같은 이름을 적는다

            /** @brief 축 입력이 이보다 작으면 바라보는 쪽을 바꾸지 않는다(패드 스틱의 떨림). */
            static constexpr float32 kFacingDeadZone = 0.2f;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( actionCombatPhase, "ActionCombat.Phase", "Action combat phase (0 hall, 1 boss wait, 2 boss, 3 victory, 4 defeat)", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( actionCombatAlive, "ActionCombat.Alive", "Living enemies in the action combat room", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( actionCombatPlayerHP, "ActionCombat.PlayerHP", "Player HP in the action combat run", ActionCombatGameModeComponent, &ActionCombatGameModeComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( actionCombatPlayerX, "ActionCombat.PlayerX", "Player room-plane X (m) in the action combat run", ActionCombatGameModeComponent,
                                &ActionCombatGameModeComponent::getPlayerX );

    ActionCombatGameModeComponent::ActionCombatGameModeComponent()
        : _listHallMonster{}
        , _listHallPosition{}
        , _listBossMonster{}
        , _listBossPosition{}
        , _playerMaxHP{ 100 }
        , _playerSpeed{ 4.0f }
        , _bossDelaySeconds{ 1.0f }
        , _victoryHeal{ 25 }
        , _roomSettings{}
        , _room{}
        , _bossDelay{}
        , _playerPosition{ 0.0f, 0.0f }
        , _facing{ FacingDirection::Right }
        , _playerHP{ 100 }
        , _phase{ ActionCombatPhase::Hall }
        , _commandMove{ 0.0f, 0.0f }
        , _bAttackQueued{ SW_FALSE }
        , _bDashQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionCombatGameModeComponent::~ActionCombatGameModeComponent() = default;

    bool ActionCombatGameModeComponent::makeLayout( ActionRoomLayout& outLayout ) const
    {
        const bool bHallMatches = _listHallMonster.size() == _listHallPosition.size();
        const bool bBossMatches = _listBossMonster.size() == _listBossPosition.size();
        if ( bHallMatches == false || bBossMatches == false )
        {
            SW_LOG_ERROR_ID( "ActionCombat.LayoutMismatch", "Action combat game mode: monster and position lists differ in length (hall %# / %#, boss %# / %#) - the room does not open",
                             _listHallMonster.size(), _listHallPosition.size(), _listBossMonster.size(), _listBossPosition.size() );
            return false;
        }
        outLayout = ActionRoomLayout{};
        for ( size_t index = 0; index < _listHallMonster.size(); ++index )
        {
            outLayout._listHallSpawn.push_back( ActionRoomSpawnDef{ _listHallMonster[index], _listHallPosition[index] } );
        }
        for ( size_t index = 0; index < _listBossMonster.size(); ++index )
        {
            outLayout._listBossSpawn.push_back( ActionRoomSpawnDef{ _listBossMonster[index], _listBossPosition[index] } );
        }
        return true;
    }

    bool ActionCombatGameModeComponent::startGame()
    {
        ActionRoomLayout layout;
        if ( makeLayout( layout ) == false )
            return false;
        _room.setSettings( _roomSettings );
        _room.setLayout( layout );
        _room.beginHall(); // 종이 카탈로그에 없으면 룸이 오류를 남기고 그 적을 세우지 않는다 — 시나리오의 첫 Alive 판정이 진다
        _facing   = FacingDirection::Right;
        _playerHP = _playerMaxHP;
        _bossDelay.clear();
        _commandMove   = float2{ 0.0f, 0.0f };
        _bAttackQueued = SW_FALSE;
        _bDashQueued   = SW_FALSE;
        return true;
    }

    void ActionCombatGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "ActionCombat.Started", "[ActionCombat] hall with %# enemies - the player input arrives as commands (move, attack, dash)", _room.getAliveEnemyCount() );
    }

    bool ActionCombatGameModeComponent::isAcceptingInput() const
    {
        return isStarted() && _phase != ActionCombatPhase::Victory && _phase != ActionCombatPhase::Defeat;
    }

    bool ActionCombatGameModeComponent::submitPlayerInput( float2 move, bool bAttackPressed, bool bDashPressed )
    {
        if ( isAcceptingInput() == false )
            return false;
        _commandMove = move;
        if ( bAttackPressed )
            _bAttackQueued = SW_TRUE;
        if ( bDashPressed )
            _bDashQueued = SW_TRUE;
        return true;
    }

    ActionRoomFrameInput ActionCombatGameModeComponent::consumeInput( float32 deltaTime )
    {
        _playerPosition._x += _commandMove._x * _playerSpeed * deltaTime;
        _playerPosition._y += _commandMove._y * _playerSpeed * deltaTime;
        if ( _commandMove._x > ActionCombatGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Right;
        else if ( _commandMove._x < -ActionCombatGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Left;

        ActionRoomFrameInput input;
        input._playerPos      = _playerPosition;
        input._facing         = _facing;
        input._bAttackPressed = _bAttackQueued;
        input._bDashPressed   = _bDashQueued;
        _bAttackQueued        = SW_FALSE;
        _bDashQueued          = SW_FALSE;
        return input;
    }

    void ActionCombatGameModeComponent::applyResult( const ActionRoomFrameResult& result )
    {
        if ( DepletingValue::take( _playerHP, result._damageToPlayer )._bDepleted == SW_TRUE )
        {
            _room.onPlayerDefeated();
            _phase = ActionCombatPhase::Defeat;
            endMatch( hashed_string( kLoseResult ) );
            SW_LOG_INFO_ID( "ActionCombat.Defeat", "[ActionCombat] the player fell" );
            return;
        }
        if ( result._bClearedThisFrame == SW_FALSE )
            return;
        if ( _phase == ActionCombatPhase::Hall )
        {
            _phase = ActionCombatPhase::BossWait;
            _bossDelay.start( _bossDelaySeconds );
            SW_LOG_INFO_ID( "ActionCombat.HallCleared", "[ActionCombat] hall cleared, boss in %# s", _bossDelaySeconds );
            return;
        }
        _phase = ActionCombatPhase::Victory;
        endMatch( hashed_string( kWinResult ) );
        SW_LOG_INFO_ID( "ActionCombat.Victory", "[ActionCombat] boss defeated (%#) with %# HP left", result._bBossDefeated == SW_TRUE ? "boss" : "room", _playerHP );
    }

    void ActionCombatGameModeComponent::updateMatch( float32 deltaTime )
    {
        const ActionRoomFrameInput input = consumeInput( deltaTime );
        if ( _phase == ActionCombatPhase::BossWait )
        {
            if ( _bossDelay.update( deltaTime ) )
            {
                _room.beginBoss();
                _phase = ActionCombatPhase::Boss;
                SW_LOG_INFO_ID( "ActionCombat.BossStarted", "[ActionCombat] boss fight with %# enemies", _room.getAliveEnemyCount() );
            }
            return;
        }
        applyResult( _room.update( deltaTime, input ) );
    }

    void ActionCombatGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase != ActionCombatPhase::Victory || _victoryHeal <= 0 )
            return;
        outListValue.push_back( MinigameResultValue{ hashed_string( ActionCombatGameModeComponentInternal::kHealValue ), _victoryHeal } );
    }

    void ActionCombatGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( ActionCombatGameModeComponentInternal::kStateFormat );
        if ( io.isOk() == false )
            return;
        const bool bRoom = io.valueOpened( _room, [this]( ActionRoom& room )
        {
            ActionRoomLayout layout;
            if ( makeLayout( layout ) == false )
                return false;
            room.setSettings( _roomSettings );
            room.setLayout( layout );
            return true;
        } );
        if ( bRoom == false )
            return;
        // 자리 · HP · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 손 본문은 룸 · 보스 지연 · 방향(리플렉션 열거가 아니다)만.
        io.value( _bossDelay );
        io.enumValue( _facing, FacingDirection::Up );
        io.end();
    }
} // namespace sw

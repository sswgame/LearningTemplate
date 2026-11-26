#include "pch.h"

#include "GameFramework/Kits/Genre/Action/ActionCombat/Component/ActionCombatGameModeComponent.h"

#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/StateIO.h"

#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct ActionCombatGameModeComponentInternal
        {
            static constexpr FormatDesc kStateFormat{ "KDAC" };

            static constexpr const utf8* kHealValue = "heal"; ///< 승리 결과 값 이름 — 허브 캐릭터의 보상 표(`MinigameRewardComponent`)가 같은 이름을 적는다

            /** @brief 축 입력이 이보다 작으면 바라보는 쪽을 바꾸지 않는다(패드 스틱의 떨림). */
            static constexpr float32 kFacingDeadZone = 0.2f;
        };
    } // namespace

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

    bool ActionCombatGameModeComponent::makeLayout( ActionCombatRoomLayout& outLayout ) const
    {
        const bool bHallMatches = _listHallMonster.size() == _listHallPosition.size();
        const bool bBossMatches = _listBossMonster.size() == _listBossPosition.size();
        if ( bHallMatches == false || bBossMatches == false )
        {
            SW_LOG_ERROR_ID( "ActionCombat.LayoutMismatch", "Action combat game mode: monster and position lists differ in length (hall %# / %#, boss %# / %#) - the room does not open",
                             _listHallMonster.size(), _listHallPosition.size(), _listBossMonster.size(), _listBossPosition.size() );
            return false;
        }
        outLayout = ActionCombatRoomLayout{};
        for ( size_t index = 0; index < _listHallMonster.size(); ++index )
        {
            outLayout._listHallSpawn.push_back( ActionCombatRoomSpawnDef{ _listHallMonster[index], _listHallPosition[index] } );
        }
        for ( size_t index = 0; index < _listBossMonster.size(); ++index )
        {
            outLayout._listBossSpawn.push_back( ActionCombatRoomSpawnDef{ _listBossMonster[index], _listBossPosition[index] } );
        }
        return true;
    }

    bool ActionCombatGameModeComponent::startGame()
    {
        ActionCombatRoomLayout layout;
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

    ActionCombatRoomFrameInput ActionCombatGameModeComponent::consumeInput( float32 deltaTime )
    {
        _playerPosition._x += _commandMove._x * _playerSpeed * deltaTime;
        _playerPosition._y += _commandMove._y * _playerSpeed * deltaTime;
        if ( _commandMove._x > ActionCombatGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Right;
        else if ( _commandMove._x < -ActionCombatGameModeComponentInternal::kFacingDeadZone )
            _facing = FacingDirection::Left;

        ActionCombatRoomFrameInput input;
        input._playerPos      = _playerPosition;
        input._facing         = _facing;
        input._bAttackPressed = _bAttackQueued;
        input._bDashPressed   = _bDashQueued;
        _bAttackQueued        = SW_FALSE;
        _bDashQueued          = SW_FALSE;
        return input;
    }

    void ActionCombatGameModeComponent::applyResult( const ActionCombatRoomFrameResult& result )
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

    void ActionCombatGameModeComponent::updateMatch( GameSeconds frameTime )
    {
        const float32                    deltaTime = frameTime.count();
        const ActionCombatRoomFrameInput input     = consumeInput( deltaTime );
        if ( _phase == ActionCombatPhase::BossWait )
        {
            if ( _bossDelay.update( frameTime ) )
            {
                _room.beginBoss();
                _phase = ActionCombatPhase::Boss;
                SW_LOG_INFO_ID( "ActionCombat.BossStarted", "[ActionCombat] boss fight with %# enemies", _room.getAliveEnemyCount() );
            }
            return;
        }
        applyResult( _room.update( frameTime, input ) );
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
        const bool bRoom = io.valueOpened( _room, [this]( ActionCombatRoom& room )
        {
            ActionCombatRoomLayout layout;
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

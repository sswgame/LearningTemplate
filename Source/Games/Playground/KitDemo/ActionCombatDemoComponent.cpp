#include "pch.h"

#include "Games/Playground/KitDemo/ActionCombatDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Combat/Health/RuleHealthViewComponent.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        struct ActionCombatDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDAC" );
            static constexpr uint32 kStateVersion = 3; ///< 2: 탄의 꿰뚫은 대상이 상한 없는 목록이다(탄 한 줄 길이가 바뀌었다). 3: 결과 보여 주기 시간(`_resultHold`)

            static constexpr const utf8* kAttackAction = "KitDemo.Attack";
            static constexpr const utf8* kDashAction   = "KitDemo.Dash";
            static constexpr const utf8* kHealValue    = "heal"; ///< 승리 결과 값 이름 — 허브 캐릭터의 보상 표(`MinigameRewardComponent`)가 같은 이름을 적는다
            static constexpr const utf8* kWinResult    = "win";
            static constexpr const utf8* kLoseResult   = "lose";

            /** @brief 축 입력이 이보다 작으면 바라보는 쪽을 바꾸지 않는다(패드 스틱의 떨림). */
            static constexpr float32 kFacingDeadZone = 0.2f;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoCombatPhase, "KitDemo.Combat.Phase", "Action combat demo phase (0 hall, 1 boss wait, 2 boss, 3 victory, 4 defeat)", ActionCombatDemoComponent,
                                &ActionCombatDemoComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCombatAlive, "KitDemo.Combat.Alive", "Living enemies in the action combat demo room", ActionCombatDemoComponent,
                                &ActionCombatDemoComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCombatPlayerHP, "KitDemo.Combat.PlayerHP", "Player HP in the action combat demo", ActionCombatDemoComponent, &ActionCombatDemoComponent::getPlayerHP );
    SW_AUTOMATION_GETTER_WATCH( kitDemoCombatPlayerX, "KitDemo.Combat.PlayerX", "Player room-plane X (m) in the action combat demo", ActionCombatDemoComponent,
                                &ActionCombatDemoComponent::getPlayerX );

    ActionCombatDemoComponent::ActionCombatDemoComponent()
        : _listHallMonster{}
        , _listHallPosition{}
        , _listBossMonster{}
        , _listBossPosition{}
        , _playerMaxHP{ 100 }
        , _playerSpeed{ 4.0f }
        , _bossDelaySeconds{ 1.0f }
        , _playerView{}
        , _resultHoldSeconds{ 3.0f }
        , _victoryHeal{ 25 }
        , _room{}
        , _bossDelay{}
        , _resultHold{}
        , _playerPosition{ 0.0f, 0.0f }
        , _facing{ FacingDir::Right }
        , _playerHP{ 100 }
        , _phase{ ActionCombatDemoPhase::Hall }
        , _bPlayerViewMissing{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    ActionCombatDemoComponent::~ActionCombatDemoComponent() = default;

    bool ActionCombatDemoComponent::makeLayout( ActionRoomLayout& outLayout ) const
    {
        const bool bHallMatches = _listHallMonster.size() == _listHallPosition.size();
        const bool bBossMatches = _listBossMonster.size() == _listBossPosition.size();
        if ( bHallMatches == false || bBossMatches == false )
        {
            SW_LOG_ERROR_ID( "KitDemo.CombatLayoutMismatch", "Action combat demo: monster and position lists differ in length (hall %# / %#, boss %# / %#) - the room does not open",
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

    bool ActionCombatDemoComponent::startGame()
    {
        ActionRoomLayout layout;
        if ( makeLayout( layout ) == false )
            return false;
        _room.setLayout( layout );
        _room.beginHall(); // 종이 카탈로그에 없으면 룸이 오류를 남기고 그 적을 세우지 않는다 — 시나리오의 첫 Alive 판정이 진다
        _playerPosition = float2{ 0.0f, 0.0f };
        _facing         = FacingDir::Right;
        _playerHP       = _playerMaxHP;
        _phase          = ActionCombatDemoPhase::Hall;
        _bossDelay.clear();
        _resultHold.clear();
        if ( RuleHealthViewComponent* const pView = findOn<RuleHealthViewComponent>( _playerView ) )
            pView->resetSnapshot(); // 새 판의 HP 는 앞 판에서 이어지지 않는다 — 연출 없이 다시 둔다
        return true;
    }

    void ActionCombatDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.CombatStarted", "[KitDemo] action combat: hall with %# enemies - Move to walk, KitDemo.Attack to strike, KitDemo.Dash to dash", _room.getAliveEnemyCount() );
    }

    ActionRoomFrameInput ActionCombatDemoComponent::readInput( float32 deltaTime )
    {
        // 이름은 한 번만 만든다(프레임마다 intern 하지 않는다 — 린트 `common-shapes` 의 `frameIntern` 까닭).
        static const hashed_string s_moveAction( CommonInputActions::kMove );
        static const hashed_string s_attackAction( ActionCombatDemoComponentInternal::kAttackAction );
        static const hashed_string s_dashAction( ActionCombatDemoComponentInternal::kDashAction );
        const float2               move = getActionVector2D( s_moveAction );
        _playerPosition._x += move._x * _playerSpeed * deltaTime;
        _playerPosition._y += move._y * _playerSpeed * deltaTime;
        if ( move._x > ActionCombatDemoComponentInternal::kFacingDeadZone )
            _facing = FacingDir::Right;
        else if ( move._x < -ActionCombatDemoComponentInternal::kFacingDeadZone )
            _facing = FacingDir::Left;

        ActionRoomFrameInput input;
        input._playerPos      = _playerPosition;
        input._facing         = _facing;
        input._bAttackPressed = wasActionTriggered( s_attackAction ) ? SW_TRUE : SW_FALSE;
        input._bDashPressed   = wasActionTriggered( s_dashAction ) ? SW_TRUE : SW_FALSE;
        return input;
    }

    void ActionCombatDemoComponent::applyResult( const ActionRoomFrameResult& result )
    {
        if ( DepletingValue::take( _playerHP, result._damageToPlayer )._bDepleted == SW_TRUE )
        {
            _room.onPlayerDefeated();
            _phase = ActionCombatDemoPhase::Defeat;
            _resultHold.start( _resultHoldSeconds );
            SW_LOG_INFO_ID( "KitDemo.CombatDefeat", "[KitDemo] action combat: the player fell" );
            return;
        }
        if ( result._bClearedThisFrame == SW_FALSE )
            return;
        if ( _phase == ActionCombatDemoPhase::Hall )
        {
            _phase = ActionCombatDemoPhase::BossWait;
            _bossDelay.start( _bossDelaySeconds );
            SW_LOG_INFO_ID( "KitDemo.CombatHallCleared", "[KitDemo] action combat: hall cleared, boss in %# s", _bossDelaySeconds );
            return;
        }
        _phase = ActionCombatDemoPhase::Victory;
        _resultHold.start( _resultHoldSeconds );
        SW_LOG_INFO_ID( "KitDemo.CombatVictory", "[KitDemo] action combat: boss defeated (%#) with %# HP left", result._bBossDefeated == SW_TRUE ? "boss" : "room", _playerHP );
    }

    void ActionCombatDemoComponent::pushPlayerHealthView()
    {
        if ( _playerView.isValid() == false )
            return;
        RuleHealthViewComponent* const pView = findOn<RuleHealthViewComponent>( _playerView );
        if ( pView == nullptr )
        {
            if ( _bPlayerViewMissing == SW_FALSE )
                SW_LOG_ERROR_ID( "KitDemo.CombatPlayerViewMissing", "Action combat demo: the Player View object has no RuleHealthViewComponent - the player HP bar does not move" );
            _bPlayerViewMissing = SW_TRUE;
            return;
        }
        pView->applySnapshot( static_cast<float32>( _playerHP ), static_cast<float32>( _playerMaxHP ) );
    }

    void ActionCombatDemoComponent::updateGame( float32 deltaTime )
    {
        stepRoom( deltaTime );
        pushPlayerHealthView(); // 규칙 걸음 뒤 — 이 걸음의 피해가 같은 프레임에 HP 바로 간다(시간이 멈춘 프레임 · 복원 직후에도 넣는다)
    }

    void ActionCombatDemoComponent::stepRoom( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        const bool bFinished = _phase == ActionCombatDemoPhase::Victory || _phase == ActionCombatDemoPhase::Defeat;
        if ( bFinished )
        {
            drawPlayer();
            if ( _resultHold.update( deltaTime ) )
            {
                const bool bWon = _phase == ActionCombatDemoPhase::Victory;
                (void)finishMinigame( hashed_string( bWon ? ActionCombatDemoComponentInternal::kWinResult : ActionCombatDemoComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
            }
            return;
        }
        const ActionRoomFrameInput input = readInput( deltaTime );
        if ( _phase == ActionCombatDemoPhase::BossWait )
        {
            if ( _bossDelay.update( deltaTime ) )
            {
                _room.beginBoss();
                _phase = ActionCombatDemoPhase::Boss;
                SW_LOG_INFO_ID( "KitDemo.CombatBossStarted", "[KitDemo] action combat: boss fight with %# enemies", _room.getAliveEnemyCount() );
            }
            drawPlayer();
            return;
        }
        applyResult( _room.update( deltaTime, input ) );
        _room.drawDebug();
        drawPlayer();
    }

    void ActionCombatDemoComponent::drawPlayer() const
    {
        DebugDrawQueue* pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        const float4 color = _room.isPlayerInvulnerable() ? float4( 0.6f, 0.9f, 1.0f, 1.0f ) : float4( 0.2f, 0.6f, 1.0f, 1.0f );
        pDraw->drawSphere( float3( _playerPosition._x, 0.5f, _playerPosition._y ), 0.28f, color );
    }

    void ActionCombatDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 디버그 그리기뿐이라 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void ActionCombatDemoComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase != ActionCombatDemoPhase::Victory || _victoryHeal <= 0 )
            return;
        outListValue.push_back( MinigameResultValue{ hashed_string( ActionCombatDemoComponentInternal::kHealValue ), _victoryHeal } );
    }

    void ActionCombatDemoComponent::serializeGameState( StateIO& io )
    {
        io.header( ActionCombatDemoComponentInternal::kStateTag, ActionCombatDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        ActionRoom restoredRoom;
        if ( io.isReading() )
        {
            ActionRoomLayout layout;
            if ( io.check( makeLayout( layout ) ) == false )
                return;
            restoredRoom.setLayout( layout );
        }
        io.value( io.isWriting() ? _room : restoredRoom );
        // 자리 · HP · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 손 본문은 룸 · 보스 지연 · 결과 보여 주기(`Countdown`) · 방향(리플렉션 열거가 아니다)만.
        Countdown bossDelay  = _bossDelay;
        Countdown resultHold = _resultHold;
        FacingDir facing     = _facing;
        io.value( bossDelay );
        io.value( resultHold );
        io.enumValue( facing, FacingDir::Up );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _room       = std::move( restoredRoom );
        _bossDelay  = bossDelay;
        _resultHold = resultHold;
        _facing     = facing;
    }
} // namespace sw

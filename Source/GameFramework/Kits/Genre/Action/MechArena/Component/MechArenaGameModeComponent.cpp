#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Component/MechArenaGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct MechArenaGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "MAGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";
            static constexpr const utf8* kDrawResult = "draw";

            static constexpr uint32  kBotSalt         = 1; ///< 판 씨앗에서 컴퓨터 손놀림의 수열을 뽑는 소금
            static constexpr float32 kEvenChance      = 0.5f;
            static constexpr float32 kMinDirection    = 1.0e-3f;
            static constexpr uint32  kMinBotHandBytes = 8; ///< 손놀림 하나: 생각(4) + 옆걸음(4)

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventHit         = "MechArenaHit";
            static constexpr const utf8* kEventDowned      = "MechArenaDowned";
            static constexpr const utf8* kEventDestroyed   = "MechArenaDestroyed";
            static constexpr const utf8* kEventRespawned   = "MechArenaRespawned";
            static constexpr const utf8* kEventOverheated  = "MechArenaOverheated";
            static constexpr const utf8* kEventTransformed = "MechArenaTransformed";
            static constexpr const utf8* kEventSkill       = "MechArenaSkill";
            static constexpr const utf8* kEventWon         = "MechArenaWon";
            static constexpr const utf8* kEventLost        = "MechArenaLost";
            static constexpr const utf8* kEventWokeUp      = "MechArenaWokeUp";
            static constexpr const utf8* kEventSkillEnded  = "MechArenaSkillEnded";

            /** @brief 판의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(판 끝은 이긴 팀이 정한다). */
            static const utf8* findSound( MechArenaEvent::Kind kind )
            {
                switch ( kind )
                {
                    case MechArenaEvent::Kind::Hit:
                    {
                        return kEventHit;
                    }
                    case MechArenaEvent::Kind::Downed:
                    {
                        return kEventDowned;
                    }
                    case MechArenaEvent::Kind::Destroyed:
                    {
                        return kEventDestroyed;
                    }
                    case MechArenaEvent::Kind::Respawned:
                    case MechArenaEvent::Kind::MechSwapped:
                    {
                        return kEventRespawned;
                    }
                    case MechArenaEvent::Kind::Overheated:
                    {
                        return kEventOverheated;
                    }
                    case MechArenaEvent::Kind::Transformed:
                    {
                        return kEventTransformed;
                    }
                    case MechArenaEvent::Kind::SkillStarted:
                    {
                        return kEventSkill;
                    }
                    case MechArenaEvent::Kind::WokeUp:
                    {
                        return kEventWokeUp;
                    }
                    case MechArenaEvent::Kind::SkillEnded:
                    {
                        return kEventSkillEnded;
                    }
                    case MechArenaEvent::Kind::Fired: // 무기 · 기술 표의 소리다(`findEventSound`)
                    case MechArenaEvent::Kind::ComboAdvanced:
                    case MechArenaEvent::Kind::MatchEnded: // 이긴 팀이 정한다(`settle`)
                    {
                        break;
                    }
                }
                return nullptr;
            }

            /** @brief 두 입력의 누름을 합칩니다(@p latest 의 이동 · 사격 · 부유는 그대로 이긴다). */
            static MechInput mergePresses( const MechInput& queued, const MechInput& latest )
            {
                MechInput merged   = latest;
                merged._bDash      = static_cast<uint8>( latest._bDash | queued._bDash );
                merged._bJump      = static_cast<uint8>( latest._bJump | queued._bJump );
                merged._bMelee     = static_cast<uint8>( latest._bMelee | queued._bMelee );
                merged._bSpecial   = static_cast<uint8>( latest._bSpecial | queued._bSpecial );
                merged._bTransform = static_cast<uint8>( latest._bTransform | queued._bTransform );
                merged._bLockOn    = static_cast<uint8>( latest._bLockOn | queued._bLockOn );
                if ( latest._skillSlot < 0 )
                    merged._skillSlot = queued._skillSlot;
                return merged;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( mechArenaEnded, "MechArena.Ended", "1 when the mech arena match is over", MechArenaGameModeComponent, &MechArenaGameModeComponent::getEnded );
    SW_AUTOMATION_GETTER_WATCH( mechArenaPlayerState, "MechArena.PlayerState", "Player pilot state (0 waiting, 1 active, 2 down, 3 destroyed, 4 retired)", MechArenaGameModeComponent,
                                &MechArenaGameModeComponent::getPlayerState );
    SW_AUTOMATION_GETTER_WATCH( mechArenaPlayerAmmo, "MechArena.PlayerAmmo", "Magazine of the player's main weapon", MechArenaGameModeComponent, &MechArenaGameModeComponent::getPlayerAmmo );
    SW_AUTOMATION_GETTER_WATCH( mechArenaEnemyGauge, "MechArena.EnemyGauge", "Cost gauge left for the enemy team", MechArenaGameModeComponent, &MechArenaGameModeComponent::getEnemyGauge );

    MechArenaGameModeComponent::MechArenaGameModeComponent()
        : _rules{}
        , _moveDataPath{}
        , _listPlayerMech{}
        , _listPlayerSkill{}
        , _playerSpawn{ 0.0f, 0.0f, 0.0f }
        , _listAllyMech{}
        , _listAllySpawn{}
        , _listEnemyMech{}
        , _listEnemySpawn{}
        , _botThinkSeconds{ 0.6f }
        , _botKeepDistance{ 12.0f }
        , _botFireRange{ 30.0f }
        , _botMeleeRange{ 4.0f }
        , _botDashChance{ 0.25f }
        , _botSpecialChance{ 0.3f }
        , _resultHoldSeconds{ 3.0f }
        , _victoryMoney{ 50 }
        , _pMechs{}
        , _pWeapons{}
        , _pProjectiles{}
        , _pMoves{}
        , _world{}
        , _timer{}
        , _random{}
        , _resultHold{}
        , _listBotHand{}
        , _listEvent{}
        , _command{}
    {
    }

    MechArenaGameModeComponent::~MechArenaGameModeComponent() = default;

    bool MechArenaGameModeComponent::addPilot( MechArenaWorld& inoutWorld, const MechPilotConfig& config )
    {
        const expected<int32> added = inoutWorld.addPilot( config );
        if ( added.has_value() )
            return true;
        SW_LOG_ERROR_ID( "MechArena.PilotRefused", "Mech arena game mode: a pilot of team %# with mech '%#' was refused (%#) - the match does not open", config._team,
                         config._listMechID.empty() ? "" : config._listMechID.front().c_str(), added.error().message() );
        return false;
    }

    bool MechArenaGameModeComponent::openWorld( MechArenaWorld& outWorld ) const
    {
        const bool bAllyMatch  = _listAllyMech.size() == _listAllySpawn.size();
        const bool bEnemyMatch = _listEnemyMech.size() == _listEnemySpawn.size();
        if ( bAllyMatch == false || bEnemyMatch == false || _listEnemyMech.empty() || _listPlayerMech.empty() )
        {
            SW_LOG_ERROR_ID( "MechArena.LayoutMismatch", "Mech arena game mode: the player deck (%#) and the enemies must not be empty and mech and spawn lists must be of equal length (ally %# / %#, enemy %# / %#) - the match does not open",
                             _listPlayerMech.size(), _listAllyMech.size(), _listAllySpawn.size(), _listEnemyMech.size(), _listEnemySpawn.size() );
            return false;
        }
        if ( outWorld.initialize( _rules, *_pMechs, *_pWeapons, *_pProjectiles, *_pMoves ) == false )
            return false;                                    // 규칙이 범위 밖이면 판이 오류를 남긴다
        (void)outWorld.addTeam( hashed_string( "Player" ) ); // 팀 번호는 더한 순서다(kPlayerTeam · kEnemyTeam)
        (void)outWorld.addTeam( hashed_string( "Enemy" ) );
        MechPilotConfig player;
        for ( const CatalogKey<MechDef>& mech : _listPlayerMech )
        {
            player._listMechID.push_back( mech.key() );
        }
        for ( const CatalogKey<MechSkillDef>& skill : _listPlayerSkill )
        {
            player._listSkillID.push_back( skill.key() );
        }
        player._spawnPosition = _playerSpawn;
        player._team          = kPlayerTeam;
        if ( addPilot( outWorld, player ) == false )
            return false;
        for ( size_t index = 0; index < _listAllyMech.size() + _listEnemyMech.size(); ++index )
        {
            const bool      bAlly = index < _listAllyMech.size();
            const size_t    slot  = bAlly ? index : index - _listAllyMech.size();
            MechPilotConfig config;
            config._listMechID    = { bAlly ? _listAllyMech[slot].key() : _listEnemyMech[slot].key() };
            config._spawnPosition = bAlly ? _listAllySpawn[slot] : _listEnemySpawn[slot];
            config._team          = bAlly ? kPlayerTeam : kEnemyTeam;
            if ( addPilot( outWorld, config ) == false )
                return false;
        }
        return true;
    }

    bool MechArenaGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pMechs ) == false || acquireBoundCatalog( _pWeapons ) == false || acquireBoundCatalog( _pProjectiles ) == false || acquireRequired( _moveDataPath, _pMoves ) == false )
            return false;
        if ( openWorld( _world ) == false )
            return false;
        _world.start();
        _timer  = FixedStepTimer{ _rules._fixedStep, constant::time::kDefaultMaxFrameSeconds };
        _random = GameRandom( GameRandom::deriveSeed( _rules._seed, MechArenaGameModeComponentInternal::kBotSalt ) );
        _resultHold.clear();
        _listBotHand.assign( static_cast<size_t>( _world.getPilotCount() ), BotHand{} );
        _command = MechInput{};
        return true;
    }

    void MechArenaGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "MechArena.Started", "[MechArena] %# pilots, gauge %# against %# - the player input arrives as commands", _world.getPilotCount(), getPlayerGauge(), getEnemyGauge() );
    }

    bool MechArenaGameModeComponent::submitPlayerInput( const MechInput& input )
    {
        if ( isAcceptingInput() == false )
            return false;
        _command = MechArenaGameModeComponentInternal::mergePresses( _command, input );
        return true;
    }

    bool MechArenaGameModeComponent::requestNextMech()
    {
        const MechPilot* const pPilot = isAcceptingInput() ? _world.findPilot( kPlayer ) : nullptr;
        if ( pPilot == nullptr || pPilot->_listDeckMech.size() <= 1 )
            return false;
        const int32 next = ( pPilot->_nextDeckIndex + 1 ) % static_cast<int32>( pPilot->_listDeckMech.size() );
        return _world.requestMechSwap( kPlayer, next ) == MechSwapResult::Ok;
    }

    AbsolutePosition MechArenaGameModeComponent::getPilotPosition( int32 pilot ) const
    {
        const MechPilot* const pPilot = _world.findPilot( pilot );
        return pPilot != nullptr ? AbsolutePosition{ pPilot->_position } : AbsolutePosition{};
    }

    float32 MechArenaGameModeComponent::computeHealthRatio( int32 pilot ) const
    {
        const MechPilot* const pPilot = _world.findPilot( pilot );
        if ( pPilot == nullptr || ( pPilot->_state != MechPilotState::Active && pPilot->_state != MechPilotState::Down ) )
            return 0.0f;
        return pPilot->_healthMachine.getHealth() / pPilot->_healthMachine.getSettings()._maxHealth;
    }

    int32 MechArenaGameModeComponent::getPlayerAmmo() const
    {
        const MechPilot* const   pPilot = _world.findPilot( kPlayer );
        const MechModeDef* const pMode  = pPilot != nullptr && pPilot->getMech() != nullptr ? pPilot->getMode() : nullptr;
        if ( pMode == nullptr )
            return 0;
        const int32 offset = pPilot->getMech()->computeSlotOffset( pPilot->_mode );
        for ( size_t slot = 0; slot < pMode->_listWeapon.size(); ++slot )
        {
            if ( pMode->_listWeapon[slot]._kind == MechWeaponKind::Shot )
                return pPilot->_listWeaponState[static_cast<size_t>( offset ) + slot].getMagazineAmmo();
        }
        return 0;
    }

    int32 MechArenaGameModeComponent::getPlayerState() const
    {
        const MechPilot* const pPilot = _world.findPilot( kPlayer );
        return pPilot != nullptr ? static_cast<int32>( pPilot->_state ) : 0;
    }

    bool MechArenaGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPilotPosition( kPlayer )._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( outFocusPosition );
        return isStarted();
    }

    MechInput MechArenaGameModeComponent::consumePlayerInput()
    {
        const MechInput input = _command;
        MechInput       held;
        held._moveX  = _command._moveX;
        held._moveZ  = _command._moveZ;
        held._bFire  = _command._bFire;
        held._bHover = _command._bHover;
        _command     = held;
        return input;
    }

    optional<int32> MechArenaGameModeComponent::findNearestEnemy( int32 pilot ) const
    {
        const MechPilot& self        = *_world.findPilot( pilot );
        float32          bestSquared = 0.0f;
        optional<int32>  best{};
        for ( int32 other = 0; other < _world.getPilotCount(); ++other )
        {
            const MechPilot& candidate = *_world.findPilot( other );
            const bool       bTarget   = candidate._team != self._team && ( candidate._state == MechPilotState::Active || candidate._state == MechPilotState::Down );
            if ( bTarget == false )
                continue;
            const float32 distanceSquared = float3::getDistanceSquared( self._position, candidate._position );
            if ( best.has_value() && distanceSquared >= bestSquared )
                continue;
            bestSquared = distanceSquared;
            best        = other;
        }
        return best;
    }

    MechInput MechArenaGameModeComponent::makeBotInput( int32 pilot, float32 deltaTime )
    {
        using Internal                    = MechArenaGameModeComponentInternal;
        BotHand&              hand        = _listBotHand[static_cast<size_t>( pilot )];
        const bool            bDecision   = hand._think.updateRepeat( deltaTime, _botThinkSeconds, true );
        const optional<int32> target      = findNearestEnemy( pilot );
        const MechPilot&      self        = *_world.findPilot( pilot );
        const bool            bCannotPlay = self._state != MechPilotState::Active || target.has_value() == false;
        MechInput             input;
        if ( bCannotPlay )
            return input;
        const float3  offset   = _world.findPilot( *target )->_position - self._position;
        const float3  toTarget = float3( offset._x, 0.0f, offset._z );
        const float32 distance = toTarget.getLength();
        const float3  forward  = toTarget.normalizeOr( self._forward, Internal::kMinDirection );
        if ( bDecision )
        {
            hand._strafeSign = _random.nextChance( Internal::kEvenChance ) ? 1.0f : -1.0f;
            input._bDash     = _random.nextChance( _botDashChance ) ? SW_TRUE : SW_FALSE;
            input._bSpecial  = _random.nextChance( _botSpecialChance ) ? SW_TRUE : SW_FALSE;
            input._bMelee    = distance <= _botMeleeRange ? SW_TRUE : SW_FALSE;
        }
        const bool   bCloseIn = distance > _botKeepDistance;
        const float3 move     = bCloseIn ? forward : float3( forward._z, 0.0f, -forward._x ) * hand._strafeSign;
        input._moveX          = move._x;
        input._moveZ          = move._z;
        input._bFire          = distance <= _botFireRange ? SW_TRUE : SW_FALSE;
        return input;
    }

    hashed_string MechArenaGameModeComponent::findEventSound( const MechArenaEvent& event ) const
    {
        const bool bOwnOnly = event._kind == MechArenaEvent::Kind::Overheated || event._kind == MechArenaEvent::Kind::WokeUp || event._kind == MechArenaEvent::Kind::SkillEnded;
        if ( bOwnOnly && event._pilot != kPlayer )
            return hashed_string{}; // 남의 과열 · 기상 · 스킬 끝은 알리지 않는다
        if ( event._kind == MechArenaEvent::Kind::Fired )
        {
            // 주무기는 무기 표의 소리를 `queueWeaponSound` 가 낸다 — 여기는 특수기뿐이다.
            const MechPilot* const         pPilot = event._pilot.has_value() ? _world.findPilot( *event._pilot ) : nullptr;
            const MechModeDef* const       pMode  = pPilot != nullptr && pPilot->getMech() != nullptr ? pPilot->getMode() : nullptr;
            const MechWeaponSlotDef* const pSlot  = pMode != nullptr ? VectorUtil::findBy<&MechWeaponSlotDef::_id>( pMode->_listWeapon, event._id ) : nullptr;
            return pSlot != nullptr && pSlot->_kind == MechWeaponKind::Special ? pSlot->_fireSound : hashed_string{};
        }
        // 근접의 시작 · 맞음은 기술 표의 소리다.
        const bool                 bMelee = event._kind == MechArenaEvent::Kind::ComboAdvanced || ( event._kind == MechArenaEvent::Kind::Hit && event._id.empty() == false );
        const MoveFrameData* const pMove  = bMelee ? _pMoves->find( event._id ) : nullptr;
        if ( pMove != nullptr )
            return event._kind == MechArenaEvent::Kind::ComboAdvanced ? pMove->_startSound : pMove->_hitSound;
        const utf8* const pSound = MechArenaGameModeComponentInternal::findSound( event._kind );
        return pSound != nullptr ? hashed_string( pSound ) : hashed_string{};
    }

    void MechArenaGameModeComponent::settle()
    {
        _world.queueWeaponSound( getSoundQueue() );
        _listEvent.clear();
        _world.pollEvents( _listEvent );
        for ( const MechArenaEvent& event : _listEvent )
        {
            const hashed_string sound = findEventSound( event );
            if ( sound.empty() == false )
                getSoundQueue().queueEvent( sound.c_str() ); // 이름 표의 글은 사라지지 않는다
            if ( event._kind == MechArenaEvent::Kind::Destroyed )
                SW_LOG_INFO_ID( "MechArena.Destroyed", "[MechArena] pilot %# destroyed, gauge %# against %#", event._pilot.value(), getPlayerGauge(), getEnemyGauge() );
            else if ( event._kind == MechArenaEvent::Kind::Respawned )
                SW_LOG_INFO_ID( "MechArena.Respawned", "[MechArena] pilot %# launches again with '%#'", event._pilot.value(), event._id.c_str() );
            else if ( event._kind == MechArenaEvent::Kind::MatchEnded )
            {
                _resultHold.start( _resultHoldSeconds );
                getSoundQueue().queueEvent( hasWon() ? MechArenaGameModeComponentInternal::kEventWon : MechArenaGameModeComponentInternal::kEventLost );
                SW_LOG_INFO_ID( "MechArena.MatchEnd", "[MechArena] the match ends - winning team %# (-1 = draw)", _world.getMatch().getWinningTeam() );
            }
        }
    }

    void MechArenaGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _world.isEnded() )
        {
            if ( _resultHold.update( deltaTime ) == false )
                return;
            const utf8* pResult = MechArenaGameModeComponentInternal::kDrawResult;
            if ( hasWon() )
                pResult = MechArenaGameModeComponentInternal::kWinResult;
            else if ( _world.getMatch().getWinningTeam() == kEnemyTeam )
                pResult = MechArenaGameModeComponentInternal::kLoseResult;
            (void)finishMinigame( hashed_string( pResult ) ); // 흐름이 없으면 베이스가 알린다
            return;
        }
        const bool  bAutoPlay = isAutoPlayOn();
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 step = 0; step < stepCount && _world.isEnded() == false; ++step )
        {
            for ( int32 pilot = 0; pilot < _world.getPilotCount(); ++pilot )
            {
                const bool bHuman = pilot == kPlayer && bAutoPlay == false;
                _world.setInput( pilot, bHuman ? consumePlayerInput() : makeBotInput( pilot, _timer.getStep() ) );
            }
            _world.update( _timer.getStep() );
        }
        settle();
    }

    void MechArenaGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`MechArenaViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void MechArenaGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( hasWon() )
            addMoneyResult( outListValue, _victoryMoney );
    }

    void MechArenaGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( MechArenaGameModeComponentInternal::kStateTag, MechArenaGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 판은 같은 설정 · 카탈로그로 연 뒤에 읽는다(키트 계약). 입력 명령은 싣지 않는다.
        MechArenaWorld  restoredWorld;
        FixedStepTimer  timer       = _timer;
        GameRandom      random      = _random;
        Countdown       resultHold  = _resultHold;
        vector<BotHand> listBotHand = _listBotHand;
        if ( io.isReading() && io.check( _pMechs != nullptr && _pMoves != nullptr && openWorld( restoredWorld ) ) == false )
            return;
        io.value( io.isWriting() ? _world : restoredWorld );
        io.value( timer );
        io.value( random );
        io.value( resultHold );
        io.list( listBotHand, MechArenaGameModeComponentInternal::kMinBotHandBytes, [&]( BotHand& hand )
        {
            io.value( hand._think );
            io.value( hand._strafeSign );
        } );
        io.check( io.isWriting() || static_cast<int32>( listBotHand.size() ) == restoredWorld.getPilotCount() );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _world       = std::move( restoredWorld );
        _timer       = timer;
        _random      = random;
        _resultHold  = resultHold;
        _listBotHand = std::move( listBotHand );
    }
} // namespace sw

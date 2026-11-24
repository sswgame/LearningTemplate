#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/World/Query/WalkSweep.h"

namespace sw
{
    namespace
    {
        struct JRPGGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "JRGM" );
            static constexpr uint32 kStateVersion = 2; ///< 2: 가방 · 타이밍

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventEncounter     = "JRPGEncounter";
            static constexpr const utf8* kEventHit           = "JRPGHit";
            static constexpr const utf8* kEventHurt          = "JRPGHurt";
            static constexpr const utf8* kEventTimed         = "JRPGTimed";
            static constexpr const utf8* kEventHeal          = "JRPGHeal";
            static constexpr const utf8* kEventLevelUp       = "JRPGLevelUp";
            static constexpr const utf8* kEventEquip         = "JRPGEquip";
            static constexpr const utf8* kEventLearned       = "JRPGLearned";
            static constexpr const utf8* kEventClassChange   = "JRPGClassChange";
            static constexpr const utf8* kEventRest          = "JRPGRest";
            static constexpr const utf8* kEventWon           = "JRPGWon";
            static constexpr const utf8* kEventLost          = "JRPGLost";
            static constexpr const utf8* kEventRefused       = "JRPGRefused";
            static constexpr const utf8* kEventDefend        = "JRPGDefend";
            static constexpr const utf8* kEventCast          = "JRPGCast";
            static constexpr const utf8* kEventLockBroken    = "JRPGLockBroken";
            static constexpr const utf8* kEventCastCancelled = "JRPGCastCancelled";
            static constexpr const utf8* kEventCombo         = "JRPGCombo";
            static constexpr const utf8* kEventDefeated      = "JRPGDefeated";
            static constexpr const utf8* kEventFled          = "JRPGFled";

            /** @brief 전투 알림 @p event 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( const JRPGBattleEvent& event )
            {
                switch ( event._kind )
                {
                    case JRPGBattleEvent::Kind::Damage:
                    {
                        return event._bEnemyActor ? kEventHurt : kEventHit;
                    }
                    case JRPGBattleEvent::Kind::TimedHit:
                    case JRPGBattleEvent::Kind::TimedBlock:
                    {
                        return kEventTimed;
                    }
                    case JRPGBattleEvent::Kind::Healed:
                    case JRPGBattleEvent::Kind::Revived:
                    {
                        return kEventHeal;
                    }
                    case JRPGBattleEvent::Kind::NotEnoughMP:
                    case JRPGBattleEvent::Kind::NotEnoughInner:
                    case JRPGBattleEvent::Kind::CannotUse:
                    case JRPGBattleEvent::Kind::FleeFailed:
                    {
                        return kEventRefused;
                    }
                    case JRPGBattleEvent::Kind::Defending:
                    {
                        return kEventDefend;
                    }
                    case JRPGBattleEvent::Kind::CastStarted:
                    {
                        return kEventCast;
                    }
                    case JRPGBattleEvent::Kind::LockBroken:
                    {
                        return kEventLockBroken;
                    }
                    case JRPGBattleEvent::Kind::CastCancelled:
                    {
                        return kEventCastCancelled;
                    }
                    case JRPGBattleEvent::Kind::ComboUsed:
                    {
                        return kEventCombo;
                    }
                    case JRPGBattleEvent::Kind::Defeated:
                    {
                        return kEventDefeated;
                    }
                    case JRPGBattleEvent::Kind::Fled:
                    {
                        return kEventFled;
                    }
                    case JRPGBattleEvent::Kind::Attack: // 직업 · 주문 표의 소리다(`queueBattleSound`)
                    case JRPGBattleEvent::Kind::SpellCast:
                    case JRPGBattleEvent::Kind::CastReleased: // 뒤따르는 피해 · 회복이 낸다
                    case JRPGBattleEvent::Kind::ComboPoints:  // 쌓이는 값이다 — 쓰거나 열릴 때(`ComboUsed` · `TechniqueUnlocked`) 낸다
                    case JRPGBattleEvent::Kind::ProficiencyGained:
                    {
                        break;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( jrpgPhase, "JRPG.Phase", "JRPG phase (0 explore, 1 battle, 2 victory, 3 defeat, 4 timing)", JRPGGameModeComponent, &JRPGGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( jrpgCommandMember, "JRPG.CommandMember", "Party member whose battle command is awaited (-1 = none)", JRPGGameModeComponent,
                                &JRPGGameModeComponent::getCommandMember );
    SW_AUTOMATION_GETTER_WATCH( jrpgEnemies, "JRPG.EnemiesAlive", "Living enemies of the current battle (0 outside a battle)", JRPGGameModeComponent, &JRPGGameModeComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleCount, "JRPG.BattleCount", "Battles opened in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleRound, "JRPG.BattleRound", "Rounds resolved in the current or last battle", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleRound );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimingStep, "JRPG.TimingStep", "Timed swing awaited (member index, member count = block, -1 = none)", JRPGGameModeComponent, &JRPGGameModeComponent::getTimingStep );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimingElapsed, "JRPG.TimingElapsed", "Seconds since the awaited swing started", JRPGGameModeComponent, &JRPGGameModeComponent::getTimingElapsed );
    SW_AUTOMATION_GETTER_WATCH( jrpgTimedCount, "JRPG.TimedCount", "Timed hits and blocks landed in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getTimedCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderAttack, "JRPG.LeaderAttack", "Attack of the first party member (strength and gear)", JRPGGameModeComponent,
                                &JRPGGameModeComponent::getLeaderAttack );
    SW_AUTOMATION_GETTER_WATCH( jrpgClassChanges, "JRPG.ClassChanges", "Class changes in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getClassChangeCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgGold, "JRPG.Gold", "Gold held by the party", JRPGGameModeComponent, &JRPGGameModeComponent::getGold );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderHP, "JRPG.LeaderHP", "HP of the first party member", JRPGGameModeComponent, &JRPGGameModeComponent::getLeaderHP );
    SW_AUTOMATION_GETTER_WATCH( jrpgLeaderXP, "JRPG.LeaderXP", "Experience of the first party member", JRPGGameModeComponent, &JRPGGameModeComponent::getLeaderXP );
    SW_AUTOMATION_GETTER_WATCH( jrpgPlayerX, "JRPG.PlayerX", "Absolute ground-plane X (m) of the party", JRPGGameModeComponent, &JRPGGameModeComponent::getPlayerX );

    JRPGGameModeComponent::JRPGGameModeComponent()
        : _listHero{}
        , _area{}
        , _listBoss{}
        , _startPosition{ 0.0f, 0.0f }
        , _goalPosition{ 12.0f, 0.0f }
        , _restPosition{ 0.0f, 2.0f }
        , _trailMin{ -2.0f, -3.0f }
        , _trailMax{ 14.0f, 3.0f }
        , _useRadius{ 1.0f }
        , _moveSpeed{ 4.0f }
        , _bodyRadius{ 0.3f }
        , _stepLength{ 1.0f }
        , _battleSettings{}
        , _listTimingWindow{}
        , _timingTarget{ 0.4f }
        , _timingDuration{ 0.7f }
        , _seed{ 7 }
        , _startGold{ 0 }
        , _innPrice{ 4 }
        , _revivePrice{ 10 }
        , _listStartItem{}
        , _bagSlotCount{ 8 }
        , _restClass{}
        , _classChangeLevel{ JRPGParty::kClassChangeMinLevel }
        , _resultHoldSeconds{ 3.0f }
        , _pCatalog{}
        , _pItemCatalog{}
        , _bag{}
        , _judge{}
        , _party{}
        , _wallet{}
        , _walker{}
        , _battle{}
        , _listBattleEvent{}
        , _listPartyEvent{}
        , _resultHold{}
        , _playerPosition{ 0.0f, 0.0f }
        , _commandMove{ 0.0f, 0.0f }
        , _strideProgress{ 0.0f }
        , _commandMember{ 0 }
        , _battleCount{ 0 }
        , _listTimingOffset{}
        , _listTimingPressed{}
        , _timingElapsed{ 0.0f }
        , _timingStep{ 0 }
        , _timedCount{ 0 }
        , _classChangeCount{ 0 }
        , _phase{ JRPGGamePhase::Explore }
        , _bBossBattle{ SW_FALSE }
    {
    }

    JRPGGameModeComponent::~JRPGGameModeComponent() = default;

    class JRPGGameModeComponent::TimingInput final : public IJRPGTimingInput
    {
    public:
        explicit TimingInput( const JRPGGameModeComponent& owner )
            : _owner{ owner }
        {
        }

        bool findPressOffset( JRPGTimingKind kind, int32 memberIndex, float32& outOffset ) const override
        {
            const size_t step = static_cast<size_t>( kind == JRPGTimingKind::Attack ? memberIndex : _owner._party.getMemberCount() ); // 막기는 라운드에 한 번 누른다
            if ( step >= _owner._listTimingPressed.size() || _owner._listTimingPressed[step] == SW_FALSE )
                return false;
            outOffset = _owner._listTimingOffset[step];
            return true;
        }

    private:
        const JRPGGameModeComponent& _owner;
    };

    bool JRPGGameModeComponent::validateSetup() const
    {
        if ( _listHero.empty() || _listBoss.empty() || _pCatalog->findArea( _area.key() ) == nullptr )
        {
            SW_LOG_ERROR_ID( "JRPG.SetupIncomplete", "JRPG game mode: heroes (%#), boss enemies (%#) and a known encounter area ('%#') are all required - the trail does not open",
                             _listHero.size(), _listBoss.size(), _area.c_str() );
            return false;
        }
        if ( _restClass.empty() == false && _pCatalog->findClass( _restClass.key() ) == nullptr )
        {
            SW_LOG_ERROR_ID( "JRPG.UnknownRestClass", "JRPG game mode: the catalog has no class '%#' to teach at the inn - the trail does not open", _restClass.c_str() );
            return false;
        }
        for ( const CatalogKey<JRPGEnemyDef>& boss : _listBoss )
        {
            if ( _pCatalog->findEnemy( boss.key() ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "JRPG.UnknownBoss", "JRPG game mode: the catalog has no enemy '%#' - the trail does not open", boss.c_str() );
            return false;
        }
        return true;
    }

    bool JRPGGameModeComponent::openParty()
    {
        if ( _bag.initialize( *_pItemCatalog, _bagSlotCount ) == false )
            return false; // 까닭은 가방이 남겼다
        for ( const CatalogKey<ItemDef>& item : _listStartItem )
        {
            if ( _pItemCatalog->find( item.key() ) != nullptr && _bag.addItem( item.key(), 1 ) == 1 )
                continue;
            SW_LOG_ERROR_ID( "JRPG.StartItemNotPacked", "JRPG game mode: start item '%#' is unknown or does not fit a bag of %# slots - the trail does not open", item.c_str(), _bagSlotCount );
            return false;
        }
        _party.initialize( *_pCatalog, *_pItemCatalog, _bag, _wallet );
        for ( const CatalogKey<JRPGClassDef>& hero : _listHero )
        {
            const JRPGClassDef* pClass = _pCatalog->findClass( hero.key() );
            if ( pClass == nullptr || _party.addMember( hero.key(), pClass->_name, hero.key() ).has_value() == false )
            {
                SW_LOG_ERROR_ID( "JRPG.HeroNotAdded", "JRPG game mode: hero '%#' cannot join - unknown class, listed twice or more than %# heroes; the trail does not open", hero.c_str(),
                                 JRPGParty::kMaxMembers );
                return false;
            }
        }
        return true;
    }

    bool JRPGGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireBoundCatalog( _pItemCatalog ) == false )
            return false;
        if ( validateSetup() == false || openParty() == false )
            return false;
        _judge.setWindows( _listTimingWindow );
        _wallet.clear();
        if ( _startGold > 0 )
            _wallet.add( Wallet::getDefaultCurrency(), _startGold );
        _walker.initialize( *_pCatalog, _seed );
        _walker.restartGrace(); // 첫 걸음에 조우하지 않는다
        _battle = JRPGBattle{};
        _resultHold.clear();
        _playerPosition = _startPosition;
        _commandMove    = float2{ 0.0f, 0.0f };
        return true;
    }

    void JRPGGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "JRPG.Started", "[JRPG] a party of %# sets out - the input arrives as commands (move, attack, spell, defend, flee, rest)", _party.getMemberCount() );
    }

    bool JRPGGameModeComponent::isNear( const float2& target ) const
    {
        const float32 deltaX = target._x - _playerPosition._x;
        const float32 deltaY = target._y - _playerPosition._y;
        return deltaX * deltaX + deltaY * deltaY <= _useRadius * _useRadius;
    }

    optional<int32> JRPGGameModeComponent::findLivingMember( int32 firstMember ) const
    {
        for ( int32 memberIndex = firstMember; memberIndex < _party.getMemberCount(); ++memberIndex )
        {
            if ( _party.getMember( memberIndex ).isAlive() )
                return memberIndex;
        }
        return nullopt;
    }

    optional<int32> JRPGGameModeComponent::findCommandMember() const
    {
        if ( isStarted() == false || _phase != JRPGGamePhase::Battle )
            return nullopt;
        return _commandMember;
    }

    int32 JRPGGameModeComponent::getCommandMember() const
    {
        return findCommandMember().value_or( constant::invalid_index::kInt32 );
    }

    int32 JRPGGameModeComponent::getEnemiesAlive() const
    {
        int32 aliveCount = 0;
        for ( const JRPGEnemyState& enemy : _battle.getEnemies() )
        {
            aliveCount += isInBattle() && enemy.isAlive() ? 1 : 0;
        }
        return aliveCount;
    }

    int32 JRPGGameModeComponent::getGold() const
    {
        return static_cast<int32>( _wallet.getBalance( Wallet::getDefaultCurrency() ) );
    }

    int32 JRPGGameModeComponent::getLeaderHP() const
    {
        return _party.getMemberCount() > 0 ? _party.getMember( 0 )._hp : 0;
    }

    int32 JRPGGameModeComponent::getLeaderXP() const
    {
        return _party.getMemberCount() > 0 ? static_cast<int32>( _party.getMember( 0 )._level.getXP() ) : 0;
    }

    bool JRPGGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPlayerPosition()._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( getPlayerPosition() );
        return isStarted();
    }

    bool JRPGGameModeComponent::submitMove( float2 move )
    {
        if ( isStarted() == false || _phase != JRPGGamePhase::Explore )
            return false;
        _commandMove = move;
        return true;
    }

    bool JRPGGameModeComponent::submitCommand( const JRPGCommand& command )
    {
        const optional<int32> member = findCommandMember();
        if ( member.has_value() == false || _battle.setCommand( *member, command ) == false )
            return false;
        const optional<int32> next = findLivingMember( *member + 1 );
        if ( next.has_value() )
        {
            _commandMember = *next;
            return true;
        }
        if ( _listTimingWindow.empty() )
        {
            resolveRound();
            return true;
        }
        const size_t stepCount = static_cast<size_t>( _party.getMemberCount() ) + 1; // 멤버마다의 공격과 막기 하나
        _listTimingOffset.assign( stepCount, 0.0f );
        _listTimingPressed.assign( stepCount, SW_FALSE );
        _phase = JRPGGamePhase::Timing;
        advanceTiming( 0 );
        return true;
    }

    bool JRPGGameModeComponent::isTimingStep( int32 step ) const
    {
        if ( step >= _party.getMemberCount() )
            return step == _party.getMemberCount();
        return _party.getMember( step ).isAlive() && _battle.getCommands()[static_cast<size_t>( step )]._kind == JRPGCommandKind::Attack;
    }

    void JRPGGameModeComponent::advanceTiming( int32 firstStep )
    {
        _timingElapsed = 0.0f;
        for ( int32 step = firstStep; step <= _party.getMemberCount(); ++step )
        {
            if ( isTimingStep( step ) == false )
                continue;
            _timingStep = step;
            return;
        }
        _phase = JRPGGamePhase::Battle;
        resolveRound();
    }

    void JRPGGameModeComponent::resolveRound()
    {
        const TimingInput timingInput( *this );
        _battle.setTimingInput( &timingInput );
        _battle.resolveRound();
        _battle.setTimingInput( nullptr ); // 누름은 이 라운드의 것이다(도망친 라운드에는 없다)
        _listTimingOffset.clear();
        _listTimingPressed.clear();
        settleBattle();
    }

    bool JRPGGameModeComponent::submitTiming()
    {
        if ( isStarted() == false || _phase != JRPGGamePhase::Timing )
            return false;
        _listTimingOffset[static_cast<size_t>( _timingStep )]  = _timingElapsed - _timingTarget;
        _listTimingPressed[static_cast<size_t>( _timingStep )] = SW_TRUE;
        advanceTiming( _timingStep + 1 );
        return true;
    }

    void JRPGGameModeComponent::updateTiming( float32 deltaTime )
    {
        _timingElapsed += deltaTime;
        if ( _timingDuration <= _timingElapsed )
            advanceTiming( _timingStep + 1 ); // 누르지 않았다
    }

    int32 JRPGGameModeComponent::getTimingStep() const
    {
        return _phase == JRPGGamePhase::Timing ? _timingStep : constant::invalid_index::kInt32;
    }

    float32 JRPGGameModeComponent::computeTimingRatio() const
    {
        return _timingTarget > 0.0f ? MathUtil::saturate( _timingElapsed / _timingTarget ) : 1.0f;
    }

    bool JRPGGameModeComponent::findNextGear( int32& outMemberIndex, int32& outBagSlot ) const
    {
        for ( int32 memberIndex = 0; memberIndex < _party.getMemberCount(); ++memberIndex )
        {
            const Equipment& equipment = _party.getMember( memberIndex )._equipment;
            for ( int32 bagSlot = 0; bagSlot < _bag.getSlotCount(); ++bagSlot )
            {
                const InventorySlot& item = _bag.getSlot( bagSlot );
                if ( item.isEmpty() )
                    continue;
                const hashed_string equipSlot = equipment.findSlotFor( item._itemID );
                const bool          bOpen     = equipSlot.empty() == false && equipment.findEquipped( equipSlot ) == nullptr;
                if ( bOpen == false || equipment.canEquip( equipSlot, item._itemID ) == false )
                    continue;
                outMemberIndex = memberIndex;
                outBagSlot     = bagSlot;
                return true;
            }
        }
        return false;
    }

    bool JRPGGameModeComponent::submitEquip()
    {
        int32 memberIndex = 0;
        int32 bagSlot     = 0;
        if ( isStarted() == false || _phase != JRPGGamePhase::Explore || findNextGear( memberIndex, bagSlot ) == false )
            return false;
        const hashed_string itemID = _bag.getSlot( bagSlot )._itemID;
        JRPGMember&         member = _party.getMember( memberIndex );
        if ( member._equipment.equipFromInventory( _bag, bagSlot ) != EquipResult::Ok )
            return false;
        getSoundQueue().queueEvent( JRPGGameModeComponentInternal::kEventEquip );
        SW_LOG_INFO_ID( "JRPG.Equipped", "[JRPG] '%#' wears '%#'", member._id.c_str(), itemID.c_str() );
        return true;
    }

    bool JRPGGameModeComponent::submitClassChange( int32 memberIndex )
    {
        const bool bCannotChange = isStarted() == false || _phase != JRPGGamePhase::Explore || isNearRest() == false || _restClass.empty();
        if ( bCannotChange || _party.changeClass( memberIndex, _restClass.key(), _classChangeLevel ) != JRPGClassChangeResult::Ok )
            return false;
        ++_classChangeCount;
        reportPartyEvents();
        return true;
    }

    bool JRPGGameModeComponent::submitFlee()
    {
        if ( findCommandMember().has_value() == false || _bBossBattle == SW_TRUE )
            return false;
        if ( _battle.tryFlee() == false )
            SW_LOG_INFO_ID( "JRPG.FleeFailed", "[JRPG] the party could not get away" );
        settleBattle();
        return true;
    }

    bool JRPGGameModeComponent::submitRest()
    {
        const bool bCannotRest = isStarted() == false || _phase != JRPGGamePhase::Explore || isNearRest() == false;
        if ( bCannotRest )
            return false;
        bool bRevived = false;
        for ( int32 memberIndex = 0; memberIndex < _party.getMemberCount(); ++memberIndex )
        {
            if ( _party.getMember( memberIndex ).isAlive() == false )
                bRevived = _party.reviveAtChurch( memberIndex, _revivePrice ) || bRevived;
        }
        const bool bRested = _party.restAtInn( _innPrice );
        if ( bRevived == false && bRested == false )
            return false;
        _walker.restartGrace();
        reportPartyEvents();
        return true;
    }

    void JRPGGameModeComponent::updateExplore( float32 deltaTime )
    {
        const float2 before = _playerPosition;
        const float2 step   = WalkSweep::walkOnGround( getObjectManager(), getPlayerPosition(), _commandMove * ( _moveSpeed * deltaTime ), _bodyRadius );
        _playerPosition._x  = MathUtil::clamp( before._x + step._x, _trailMin._x, _trailMax._x );
        _playerPosition._y  = MathUtil::clamp( before._y + step._y, _trailMin._y, _trailMax._y );

        const float32 walkedX = _playerPosition._x - before._x;
        const float32 walkedY = _playerPosition._y - before._y;
        _strideProgress += MathUtil::sqrt( walkedX * walkedX + walkedY * walkedY );
        while ( _stepLength <= _strideProgress )
        {
            _strideProgress -= _stepLength;
            const JRPGEncounterGroup* pGroup = _walker.step( _area.key() );
            if ( pGroup == nullptr )
                continue;
            openBattle( pGroup->_listEnemyID, false );
            return;
        }
        if ( isNear( _goalPosition ) == false )
            return;
        vector<hashed_string> listEnemyID;
        for ( const CatalogKey<JRPGEnemyDef>& boss : _listBoss )
        {
            listEnemyID.push_back( boss.key() );
        }
        openBattle( listEnemyID, true );
    }

    void JRPGGameModeComponent::openBattle( const vector<hashed_string>& listEnemyID, bool bBoss )
    {
        _commandMove    = float2{ 0.0f, 0.0f };
        _strideProgress = 0.0f;
        ++_battleCount;
        _battle.initialize( *_pCatalog, _judge, _battleSettings, GameRandom::deriveSeed( _seed, static_cast<uint32>( _battleCount ) ) );
        if ( _battle.start( _party, listEnemyID ) == false )
            return; // 까닭은 전투가 남겼다
        _bBossBattle   = bBoss ? SW_TRUE : SW_FALSE;
        _commandMember = findLivingMember( 0 ).value(); // 전투를 열었으니 산 멤버가 있다
        _phase         = JRPGGamePhase::Battle;
        getSoundQueue().queueEvent( JRPGGameModeComponentInternal::kEventEncounter );
        SW_LOG_INFO_ID( "JRPG.BattleStarted", "[JRPG] battle %# against %# enemies (boss %#)", _battleCount, _battle.getEnemies().size(), static_cast<int32>( _bBossBattle ) );
    }

    void JRPGGameModeComponent::queueBattleSound( const JRPGBattleEvent& event )
    {
        if ( const utf8* const pSound = JRPGGameModeComponentInternal::findSound( event ) )
            getSoundQueue().queueEvent( pSound );
        hashed_string techniqueSound;
        if ( event._kind == JRPGBattleEvent::Kind::Attack && event._bEnemyActor == false && event._actor.has_value() )
        {
            const JRPGClassDef* const pClass = _pCatalog->findClass( _party.getMember( *event._actor )._classID );
            techniqueSound                   = pClass != nullptr ? pClass->_attackSound : hashed_string{};
        }
        else if ( event._kind == JRPGBattleEvent::Kind::SpellCast )
        {
            const JRPGSpellDef* const pSpell = _pCatalog->findSpell( event._id );
            techniqueSound                   = pSpell != nullptr ? pSpell->_castSound : hashed_string{};
        }
        if ( techniqueSound.empty() == false )
            getSoundQueue().queueEvent( techniqueSound.c_str() ); // 이름 표의 글은 사라지지 않는다
    }

    void JRPGGameModeComponent::reportPartyEvents()
    {
        using Internal = JRPGGameModeComponentInternal;
        _listPartyEvent.clear();
        _party.pollEvents( _listPartyEvent );
        for ( const JRPGPartyEvent& event : _listPartyEvent )
        {
            const utf8* const pMemberID = event._memberIndex.has_value() ? _party.getMember( *event._memberIndex )._id.c_str() : "the party";
            switch ( event._kind )
            {
                case JRPGPartyEvent::Kind::LevelUp:
                {
                    getSoundQueue().queueEvent( Internal::kEventLevelUp );
                    SW_LOG_INFO_ID( "JRPG.LevelUp", "[JRPG] '%#' reached level %#", pMemberID, event._value );
                    break;
                }
                case JRPGPartyEvent::Kind::LearnedSpell:
                case JRPGPartyEvent::Kind::TechniqueUnlocked:
                {
                    getSoundQueue().queueEvent( Internal::kEventLearned );
                    SW_LOG_INFO_ID( "JRPG.Learned", "[JRPG] '%#' learned '%#'", pMemberID, event._id.c_str() );
                    break;
                }
                case JRPGPartyEvent::Kind::ClassChanged:
                {
                    getSoundQueue().queueEvent( Internal::kEventClassChange );
                    SW_LOG_INFO_ID( "JRPG.ClassChanged", "[JRPG] '%#' becomes '%#'", pMemberID, event._id.c_str() );
                    break;
                }
                case JRPGPartyEvent::Kind::ExpGained:
                {
                    SW_LOG_INFO_ID( "JRPG.ExpGained", "[JRPG] '%#' gains %# experience", pMemberID, event._value );
                    break;
                }
                case JRPGPartyEvent::Kind::Rested:
                {
                    getSoundQueue().queueEvent( Internal::kEventRest );
                    SW_LOG_INFO_ID( "JRPG.Rested", "[JRPG] rested at the inn for %#, %# gold left", event._value, getGold() );
                    break;
                }
                case JRPGPartyEvent::Kind::Revived:
                {
                    getSoundQueue().queueEvent( Internal::kEventHeal );
                    SW_LOG_INFO_ID( "JRPG.Revived", "[JRPG] '%#' stands again for %#", pMemberID, event._value );
                    break;
                }
            }
        }
    }

    void JRPGGameModeComponent::settleBattle()
    {
        _listBattleEvent.clear();
        _battle.pollEvents( _listBattleEvent );
        for ( const JRPGBattleEvent& event : _listBattleEvent )
        {
            queueBattleSound( event );
            _timedCount += event._kind == JRPGBattleEvent::Kind::TimedHit || event._kind == JRPGBattleEvent::Kind::TimedBlock ? 1 : 0;
            if ( event._kind == JRPGBattleEvent::Kind::Defeated )
                SW_LOG_INFO_ID( "JRPG.Defeated", "[JRPG] '%#' is down", event._id.c_str() );
        }
        reportPartyEvents();
        switch ( _battle.getResult() )
        {
            case JRPGBattleResult::Ongoing:
            {
                _commandMember = findLivingMember( 0 ).value(); // 모두 쓰러졌으면 패배다
                return;
            }
            case JRPGBattleResult::Victory:
            {
                if ( _bBossBattle == SW_TRUE )
                {
                    finishGame( JRPGGamePhase::Victory );
                    return;
                }
                SW_LOG_INFO_ID( "JRPG.BattleWon", "[JRPG] won in %# rounds - %# experience, %# gold", _battle.getRound(), _battle.getRewardExp(), _battle.getRewardGold() );
                _phase = JRPGGamePhase::Explore;
                return;
            }
            case JRPGBattleResult::Defeat:
            {
                finishGame( JRPGGamePhase::Defeat );
                return;
            }
            case JRPGBattleResult::Fled:
            {
                _phase = JRPGGamePhase::Explore;
                return;
            }
        }
    }

    void JRPGGameModeComponent::finishGame( JRPGGamePhase phase )
    {
        _phase = phase;
        _resultHold.start( _resultHoldSeconds );
        getSoundQueue().queueEvent( phase == JRPGGamePhase::Victory ? JRPGGameModeComponentInternal::kEventWon : JRPGGameModeComponentInternal::kEventLost );
        if ( phase == JRPGGamePhase::Victory )
            SW_LOG_INFO_ID( "JRPG.Victory", "[JRPG] the boss fell in %# rounds", _battle.getRound() );
        else
            SW_LOG_INFO_ID( "JRPG.Defeat", "[JRPG] the party fell" );
    }

    void JRPGGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( isAutoPlayOn() )
            updateAutoPlay();
        if ( _phase == JRPGGamePhase::Battle )
            return; // 전투는 명령이 진행한다
        if ( _phase == JRPGGamePhase::Timing )
        {
            updateTiming( deltaTime );
            return;
        }
        if ( _phase == JRPGGamePhase::Explore )
        {
            updateExplore( deltaTime );
            return;
        }
        if ( _resultHold.update( deltaTime ) )
        {
            const bool bWon = _phase == JRPGGamePhase::Victory;
            (void)finishMinigame( hashed_string( bWon ? JRPGGameModeComponentInternal::kWinResult : JRPGGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
        }
    }

    void JRPGGameModeComponent::updateAutoPlay()
    {
        // 거절(돈 부족 · 낄 것 없음)은 할 일이 없다는 뜻이라 버린다.
        if ( _phase == JRPGGamePhase::Explore )
        {
            (void)submitEquip();
            bool bHurt = false;
            for ( int32 memberIndex = 0; memberIndex < _party.getMemberCount(); ++memberIndex )
            {
                const JRPGMember& member = _party.getMember( memberIndex );
                bHurt                    = bHurt || member._hp < member.getStat( JRPGStat::MaxHP ) || member._mp < member.getStat( JRPGStat::MaxMP );
            }
            if ( bHurt )
                (void)submitRest();
            const float2 toGoal = _goalPosition - _playerPosition;
            (void)submitMove( toGoal.normalizeOr( float2{ 0.0f, 0.0f }, MathUtil::kEpsilon ) );
        }
        else if ( _phase == JRPGGamePhase::Timing )
        {
            if ( _timingTarget <= _timingElapsed )
                (void)submitTiming();
        }
        else if ( _phase == JRPGGamePhase::Battle )
        {
            const vector<JRPGEnemyState>& listEnemy = _battle.getEnemies();
            for ( size_t enemyIndex = 0; enemyIndex < listEnemy.size(); ++enemyIndex )
            {
                if ( listEnemy[enemyIndex].isAlive() && submitCommand( JRPGCommand::makeAttack( static_cast<int32>( enemyIndex ) ) ) )
                    return;
            }
        }
    }

    void JRPGGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`JRPGAvatarComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void JRPGGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _phase != JRPGGamePhase::Defeat )
            addMoneyResult( outListValue, _wallet.getBalance( Wallet::getDefaultCurrency() ) - _startGold );
    }

    void JRPGGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( JRPGGameModeComponentInternal::kStateTag, JRPGGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 자리 · 걸음 · 명령 멤버 · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 규칙 객체는 같은 카탈로그로 연 뒤에 읽는다(키트 계약).
        // 전투는 전투 중일 때만 싣는다 — 다음 전투는 시작할 때 새로 열고 시드는 연 전투 수가 정한다.
        Wallet              restoredWallet;
        Inventory           restoredBag;
        JRPGParty           restoredParty;
        JRPGEncounterWalker restoredWalker;
        JRPGBattle          restoredBattle;
        Countdown           resultHold = _resultHold;
        const bool          bInBattle  = isInBattle();
        if ( io.isReading() )
        {
            if ( _pCatalog == nullptr || _pItemCatalog == nullptr )
            {
                SW_LOG_ERROR_ID( "JRPG.StateNeedsCatalog", "JRPG game mode: state read before the JRPG and item catalogs are loaded - refused" );
                io.fail();
                return;
            }
            if ( io.check( restoredBag.initialize( *_pItemCatalog, _bagSlotCount ) ) == false )
                return;
            restoredParty.initialize( *_pCatalog, *_pItemCatalog, _bag, _wallet );
            restoredWalker.initialize( *_pCatalog, _seed );
            restoredBattle.initialize( *_pCatalog, _judge, _battleSettings, _seed );
            restoredBattle.bindParty( restoredParty );
        }
        io.value( io.isWriting() ? _wallet : restoredWallet );
        io.value( io.isWriting() ? _bag : restoredBag );
        io.value( io.isWriting() ? _party : restoredParty );
        io.value( io.isWriting() ? _walker : restoredWalker );
        if ( bInBattle )
            io.value( io.isWriting() ? _battle : restoredBattle );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        const bool bCommandValid = _commandMember < restoredParty.getMemberCount() && restoredParty.getMember( _commandMember ).isAlive();
        const bool bBattleValid  = restoredBattle.getResult() == JRPGBattleResult::Ongoing && restoredBattle.getEnemies().empty() == false && bCommandValid;
        if ( io.check( restoredParty.getMemberCount() > 0 && ( bInBattle == false || bBattleValid ) ) == false )
            return;
        const size_t stepCount    = static_cast<size_t>( restoredParty.getMemberCount() ) + 1;
        const bool   bTimingValid = _listTimingOffset.size() == stepCount && _listTimingPressed.size() == stepCount && static_cast<size_t>( _timingStep ) < stepCount;
        if ( io.check( _phase != JRPGGamePhase::Timing || bTimingValid ) == false )
            return;
        _wallet     = std::move( restoredWallet );
        _bag        = std::move( restoredBag );
        _party      = std::move( restoredParty ); // 빌린 가방 · 지갑의 주소는 그대로다
        _walker     = std::move( restoredWalker );
        _battle     = std::move( restoredBattle );
        _resultHold = resultHold;
        _battle.bindParty( _party );
    }
} // namespace sw

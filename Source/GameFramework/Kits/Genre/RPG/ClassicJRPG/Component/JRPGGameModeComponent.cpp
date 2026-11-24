#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/Component/JRPGGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"

namespace sw
{
    namespace
    {
        struct JRPGGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "JRGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( jrpgPhase, "JRPG.Phase", "JRPG phase (0 explore, 1 battle, 2 victory, 3 defeat)", JRPGGameModeComponent, &JRPGGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( jrpgCommandMember, "JRPG.CommandMember", "Party member whose battle command is awaited (-1 = none)", JRPGGameModeComponent,
                                &JRPGGameModeComponent::getCommandMember );
    SW_AUTOMATION_GETTER_WATCH( jrpgEnemies, "JRPG.EnemiesAlive", "Living enemies of the current battle (0 outside a battle)", JRPGGameModeComponent, &JRPGGameModeComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleCount, "JRPG.BattleCount", "Battles opened in this run", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleCount );
    SW_AUTOMATION_GETTER_WATCH( jrpgBattleRound, "JRPG.BattleRound", "Rounds resolved in the current or last battle", JRPGGameModeComponent, &JRPGGameModeComponent::getBattleRound );
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
        , _stepLength{ 1.0f }
        , _battleSettings{}
        , _seed{ 7 }
        , _startGold{ 0 }
        , _innPrice{ 4 }
        , _revivePrice{ 10 }
        , _resultHoldSeconds{ 3.0f }
        , _pCatalog{}
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
        , _phase{ JRPGGamePhase::Explore }
        , _bBossBattle{ SW_FALSE }
    {
    }

    JRPGGameModeComponent::~JRPGGameModeComponent() = default;

    bool JRPGGameModeComponent::validateSetup() const
    {
        if ( _listHero.empty() || _listBoss.empty() || _pCatalog->findArea( _area.key() ) == nullptr )
        {
            SW_LOG_ERROR_ID( "JRPG.SetupIncomplete", "JRPG game mode: heroes (%#), boss enemies (%#) and a known encounter area ('%#') are all required - the trail does not open",
                             _listHero.size(), _listBoss.size(), _area.c_str() );
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
        // 장비 · 전직을 쓰지 않는 판이라 아이템 카탈로그 · 가방은 넘기지 않고 돈을 받을 지갑만 빌려 준다.
        _party.initialize( _pCatalog.get(), nullptr, nullptr, &_wallet );
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
        if ( acquireBoundCatalog( _pCatalog ) == false || validateSetup() == false || openParty() == false )
            return false;
        _wallet.clear();
        if ( _startGold > 0 )
            _wallet.add( Wallet::getDefaultCurrency(), _startGold );
        _walker.initialize( _pCatalog.get(), _seed );
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
            aliveCount += _phase == JRPGGamePhase::Battle && enemy.isAlive() ? 1 : 0;
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
        _battle.resolveRound();
        settleBattle();
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
        SW_LOG_INFO_ID( "JRPG.Rested", "[JRPG] rested at the inn, %# gold left", getGold() );
        return true;
    }

    void JRPGGameModeComponent::updateExplore( float32 deltaTime )
    {
        const float2 before = _playerPosition;
        _playerPosition._x  = MathUtil::clamp( before._x + _commandMove._x * _moveSpeed * deltaTime, _trailMin._x, _trailMax._x );
        _playerPosition._y  = MathUtil::clamp( before._y + _commandMove._y * _moveSpeed * deltaTime, _trailMin._y, _trailMax._y );

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
        // 타이밍 판정이 없으면 타이밍 공격 · 방어는 실패다(공식대로 한 번 친다).
        _battle.initialize( _pCatalog.get(), nullptr, _battleSettings, GameRandom::deriveSeed( _seed, static_cast<uint32>( _battleCount ) ) );
        if ( _battle.start( &_party, listEnemyID ) == false )
            return; // 까닭은 전투가 남겼다
        _bBossBattle   = bBoss ? SW_TRUE : SW_FALSE;
        _commandMember = findLivingMember( 0 ).value(); // 전투를 열었으니 산 멤버가 있다
        _phase         = JRPGGamePhase::Battle;
        SW_LOG_INFO_ID( "JRPG.BattleStarted", "[JRPG] battle %# against %# enemies (boss %#)", _battleCount, _battle.getEnemies().size(), static_cast<int32>( _bBossBattle ) );
    }

    void JRPGGameModeComponent::settleBattle()
    {
        _listBattleEvent.clear();
        _battle.pollEvents( _listBattleEvent );
        for ( const JRPGBattleEvent& event : _listBattleEvent )
        {
            if ( event._kind == JRPGBattleEvent::Kind::Defeated )
                SW_LOG_INFO_ID( "JRPG.Defeated", "[JRPG] '%#' is down", event._id.c_str() );
        }
        _listPartyEvent.clear();
        _party.pollEvents( _listPartyEvent );
        for ( const JRPGPartyEvent& event : _listPartyEvent )
        {
            if ( event._kind == JRPGPartyEvent::Kind::LevelUp && event._memberIndex.has_value() )
                SW_LOG_INFO_ID( "JRPG.LevelUp", "[JRPG] '%#' reached level %#", _party.getMember( *event._memberIndex )._id.c_str(), event._value );
        }
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
        if ( phase == JRPGGamePhase::Victory )
            SW_LOG_INFO_ID( "JRPG.Victory", "[JRPG] the boss fell in %# rounds", _battle.getRound() );
        else
            SW_LOG_INFO_ID( "JRPG.Defeat", "[JRPG] the party fell" );
    }

    void JRPGGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _phase == JRPGGamePhase::Battle )
            return; // 전투는 명령이 진행한다
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
        JRPGParty           restoredParty;
        JRPGEncounterWalker restoredWalker;
        JRPGBattle          restoredBattle;
        Countdown           resultHold = _resultHold;
        const bool          bInBattle  = _phase == JRPGGamePhase::Battle;
        if ( io.isReading() )
        {
            if ( _pCatalog == nullptr )
            {
                SW_LOG_ERROR_ID( "JRPG.StateNeedsCatalog", "JRPG game mode: state read before the JRPG catalog is loaded - refused" );
                io.fail();
                return;
            }
            restoredParty.initialize( _pCatalog.get(), nullptr, nullptr, &_wallet );
            restoredWalker.initialize( _pCatalog.get(), _seed );
            restoredBattle.initialize( _pCatalog.get(), nullptr, _battleSettings, _seed );
            restoredBattle.bindParty( &restoredParty );
        }
        io.value( io.isWriting() ? _wallet : restoredWallet );
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
        _wallet     = std::move( restoredWallet );
        _party      = std::move( restoredParty ); // 빌린 지갑의 주소는 그대로다
        _walker     = std::move( restoredWalker );
        _battle     = std::move( restoredBattle );
        _resultHold = resultHold;
        _battle.bindParty( &_party );
    }
} // namespace sw

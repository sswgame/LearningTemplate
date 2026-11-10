#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGBattle.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"
#include "GameFramework/Runtime/Character/Input/TimingJudge.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        struct JRPGBattleInternal
        {
            static int32 applyVariance( int32 value, int32 minPermille, int32 maxPermille, GameRandom& random )
            {
                return static_cast<int32>( static_cast<int64>( value ) * random.nextInt( minPermille, maxPermille ) / 1000 );
            }

            static int32 applyReduction( int32 damage, float32 reduction )
            {
                return static_cast<int32>( MathUtil::floor( static_cast<float32>( damage ) * MathUtil::clamp( 1.0f - reduction, 0.0f, 1.0f ) ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    JRPGBattle::JRPGBattle()
        : _listEnemy{}
        , _listCommand{}
        , _listDefending{}
        , _eventBuffer{}
        , _turnOrder{}
        , _random{}
        , _settings{}
        , _pCatalog{ nullptr }
        , _pJudge{ nullptr }
        , _pTimingInput{ nullptr }
        , _pParty{ nullptr }
        , _rewardExp{ 0 }
        , _rewardGold{ 0 }
        , _comboPoints{ 0 }
        , _round{ 0 }
        , _fleeAttempts{ 0 }
        , _result{ JRPGBattleResult::Ongoing }
    {
    }

    void JRPGBattle::initialize( const JRPGCatalog* pCatalog, const TimingJudge* pJudge, const JRPGBattleSettings& settings, uint32 seed )
    {
        _pCatalog = pCatalog;
        _pJudge   = pJudge;
        _settings = settings;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, seed ^ 0x27d4eb2fu );
    }

    bool JRPGBattle::start( JRPGParty* pParty, const vector<hashed_string>& listEnemyID )
    {
        _pParty = pParty;
        _listEnemy.clear();
        _eventBuffer.clear();
        _turnOrder.initialize( TurnOrderMode::Rounds, _random.nextUint() );
        _comboPoints                  = 0;
        _round                        = 0;
        _fleeAttempts                 = 0;
        _rewardExp                    = 0;
        _rewardGold                   = 0;
        _result                       = JRPGBattleResult::Ongoing;
        const bool bCannotStartBattle = _pCatalog == nullptr || pParty == nullptr || pParty->countAlive() == 0;
        if ( bCannotStartBattle )
            return false;
        for ( const hashed_string& enemyID : listEnemyID )
        {
            const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemyID );
            if ( pEnemy == nullptr )
            {
                // 빼고 시작하면 오타 하나가 쉬운 전투가 된다.
                SW_LOG_ERROR_ID( "JRPGBattle.UnknownEnemy", "JRPGBattle: refused to start - unknown enemy '%#'", enemyID.c_str() );
                _listEnemy.clear();
                return false;
            }
            JRPGEnemyState state;
            state._enemyID = enemyID;
            state._hp      = pEnemy->_arrStat[static_cast<size_t>( JRPGStat::MaxHP )];
            _listEnemy.push_back( state );
        }
        _listCommand.assign( static_cast<size_t>( pParty->getMemberCount() ), JRPGCommand{} );
        _listDefending.assign( static_cast<size_t>( pParty->getMemberCount() ), 0 );
        return _listEnemy.empty() == false;
    }

    bool JRPGBattle::setCommand( int32 memberIndex, const JRPGCommand& command )
    {
        const bool bCannotSetCommand = isInProgress() == false || memberIndex < 0 || memberIndex >= _pParty->getMemberCount();
        if ( bCannotSetCommand )
            return false;
        const JRPGMember& member = _pParty->getMember( memberIndex );
        if ( member.isAlive() == false )
            return false;
        if ( command._kind == JRPGCommandKind::Spell )
        {
            const JRPGSpellDef* pSpell         = _pCatalog->findSpell( command._id );
            const bool          bSpellUnusable = pSpell == nullptr || _pParty->canUseSpell( memberIndex, command._id ) == false || member._mp < pSpell->_mpCost;
            if ( bSpellUnusable )
                return false;
            if ( _settings._bWuxia && member._inner < pSpell->_innerCost )
                return false;
            // 없는 멤버를 고르면 MP 만 쓰고 아무 일도 없다.
            const bool bNoAllyTarget = pSpell->_kind != JRPGSpellKind::Damage && pSpell->_target == JRPGTargetKind::One && ( command._target < 0 || command._target >= _pParty->getMemberCount() );
            if ( bNoAllyTarget )
                return false;
        }
        else if ( command._kind == JRPGCommandKind::Combo )
        {
            const JRPGComboDef* pCombo = _pCatalog->findCombo( command._id );
            if ( pCombo == nullptr || _comboPoints < pCombo->_points )
                return false;
            bool bParticipant = false;
            for ( const hashed_string& participantID : pCombo->_listMemberID )
            {
                const optional<int32> participant = _pParty->findMemberIndex( participantID );
                if ( participant.has_value() == false || _pParty->getMember( *participant ).isAlive() == false )
                    return false;
                bParticipant = bParticipant || *participant == memberIndex;
            }
            if ( bParticipant == false )
                return false;
        }
        _listCommand[static_cast<size_t>( memberIndex )] = command;
        return true;
    }

    bool JRPGBattle::isInProgress() const
    {
        return _result == JRPGBattleResult::Ongoing && _pParty != nullptr && _pCatalog != nullptr && _listEnemy.empty() == false &&
               static_cast<int32>( _listCommand.size() ) == _pParty->getMemberCount();
    }

    void JRPGBattle::resolveRound()
    {
        if ( isInProgress() == false )
            return;
        ++_round;

        // 합동기는 참여 멤버의 이번 라운드 행동을 함께 쓴다.
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            if ( _listCommand[memberIndex]._kind != JRPGCommandKind::Combo )
                continue;
            const JRPGComboDef* pCombo = _pCatalog->findCombo( _listCommand[memberIndex]._id );
            if ( pCombo == nullptr )
                continue;
            for ( const hashed_string& participantID : pCombo->_listMemberID )
            {
                const optional<int32> participant = _pParty->findMemberIndex( participantID );
                if ( participant.has_value() && static_cast<size_t>( *participant ) != memberIndex )
                    _listCommand[static_cast<size_t>( *participant )] = JRPGCommand{};
            }
        }
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            _listDefending[memberIndex] = _listCommand[memberIndex]._kind == JRPGCommandKind::Defend ? SW_TRUE : SW_FALSE;
        }

        syncActors();
        _turnOrder.restartRound();
        const int32 roundBefore = _turnOrder.getRound();
        while ( _result == JRPGBattleResult::Ongoing )
        {
            const int32 actor = _turnOrder.next();
            if ( actor < 0 || _turnOrder.getRound() != roundBefore + 1 )
                break; // 이번 라운드의 차례가 다 돌았다(남은 큐는 다음 라운드의 restartRound 가 버린다)
            if ( actor >= kEnemyActorBase )
                executeEnemy( actor - kEnemyActorBase );
            else
                executeMember( actor );
            finishIfDecided();
        }
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            _listCommand[memberIndex]   = JRPGCommand{};
            _listDefending[memberIndex] = SW_FALSE;
        }
    }

    bool JRPGBattle::tryFlee()
    {
        if ( isInProgress() == false )
            return false;
        const float32 chance = computeFleeChance();
        ++_fleeAttempts;
        if ( _random.nextFloat() < chance )
        {
            pushEvent( JRPGBattleEvent::Kind::Fled, false, nullopt, nullopt, _fleeAttempts );
            _result = JRPGBattleResult::Fled;
            return true;
        }
        pushEvent( JRPGBattleEvent::Kind::FleeFailed, false, nullopt, nullopt, _fleeAttempts );
        for ( JRPGCommand& command : _listCommand )
        {
            command = JRPGCommand{};
        }
        resolveRound();
        return false;
    }

    float32 JRPGBattle::computeFleeChance() const
    {
        if ( _pParty == nullptr || _pCatalog == nullptr )
            return 0.0f;
        float32 enemyAgility = 0.0f;
        int32   enemyCount   = 0;
        for ( const JRPGEnemyState& enemy : _listEnemy )
        {
            const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
            if ( enemy.isAlive() == false || pEnemy == nullptr )
                continue;
            if ( pEnemy->_bBoss )
                return 0.0f;
            enemyAgility += static_cast<float32>( pEnemy->_arrStat[static_cast<size_t>( JRPGStat::Agility )] );
            ++enemyCount;
        }
        float32 partyAgility = 0.0f;
        int32   partyCount   = 0;
        for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
        {
            const JRPGMember& member = _pParty->getMember( memberIndex );
            if ( member.isAlive() == false )
                continue;
            partyAgility += static_cast<float32>( member.getStat( JRPGStat::Agility ) );
            ++partyCount;
        }
        const float32 averageGap = ( partyCount > 0 ? partyAgility / static_cast<float32>( partyCount ) : 0.0f ) -
                                   ( enemyCount > 0 ? enemyAgility / static_cast<float32>( enemyCount ) : 0.0f );
        const float32 chance = _settings._fleeBase + averageGap * _settings._fleePerAgility + static_cast<float32>( _fleeAttempts ) * _settings._fleePerAttempt;
        return MathUtil::clamp( chance, 0.05f, 0.95f );
    }

    int32 JRPGBattle::computePhysicalDamage( int32 attack, int32 defense, GameRandom& random )
    {
        const int32 base = attack / 2 - defense / 4;
        if ( base < 1 )
            return random.nextInt( 0, 1 );
        return MathUtil::max( 1, JRPGBattleInternal::applyVariance( base, 875, 1125, random ) );
    }

    void JRPGBattle::syncActors()
    {
        for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
        {
            const JRPGMember& member = _pParty->getMember( memberIndex );
            if ( member.isAlive() == false )
            {
                _turnOrder.removeActor( memberIndex );
                continue;
            }
            const float32 agility = static_cast<float32>( member.getStat( JRPGStat::Agility ) );
            _turnOrder.addActor( memberIndex, agility );
            _turnOrder.setSpeed( memberIndex, agility );
            _turnOrder.setPriority( memberIndex, _listDefending[static_cast<size_t>( memberIndex )] != SW_FALSE ? kDefendPriority : 0 );
        }
        for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
        {
            const int32 actor = kEnemyActorBase + enemyIndex;
            if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() == false )
            {
                _turnOrder.removeActor( actor );
                continue;
            }
            const JRPGEnemyDef* pEnemy  = _pCatalog->findEnemy( _listEnemy[static_cast<size_t>( enemyIndex )]._enemyID );
            const float32       agility = pEnemy != nullptr ? static_cast<float32>( pEnemy->_arrStat[static_cast<size_t>( JRPGStat::Agility )] ) : 1.0f;
            _turnOrder.addActor( actor, agility );
            _turnOrder.setSpeed( actor, agility );
            _turnOrder.setPriority( actor, 0 );
        }
    }

    void JRPGBattle::executeMember( int32 memberIndex )
    {
        const bool bCannotExecuteMember = memberIndex < 0 || memberIndex >= static_cast<int32>( _listCommand.size() ) || _pParty->getMember( memberIndex ).isAlive() == false;
        if ( bCannotExecuteMember )
            return;
        const JRPGCommand command = _listCommand[static_cast<size_t>( memberIndex )];
        switch ( command._kind )
        {
            case JRPGCommandKind::None:
            {
                return;
            }
            case JRPGCommandKind::Attack:
            {
                executeAttack( memberIndex, command._target );
                return;
            }
            case JRPGCommandKind::Spell:
            {
                executeSpell( memberIndex, command );
                return;
            }
            case JRPGCommandKind::Combo:
            {
                executeCombo( memberIndex, command );
                return;
            }
            case JRPGCommandKind::Defend:
            {
                pushEvent( JRPGBattleEvent::Kind::Defending, false, memberIndex, memberIndex );
                return;
            }
        }
    }

    void JRPGBattle::executeAttack( int32 memberIndex, int32 enemyIndex )
    {
        const optional<int32> foundTarget = findLivingEnemy( enemyIndex );
        if ( foundTarget.has_value() == false )
            return;
        const int32           target  = *foundTarget;
        const JRPGMember&     member  = _pParty->getMember( memberIndex );
        const JRPGClassDef*   pClass  = _pCatalog->findClass( member._classID );
        const JRPGEnemyDef*   pEnemy  = _pCatalog->findEnemy( _listEnemy[static_cast<size_t>( target )]._enemyID );
        const int32           defense = pEnemy != nullptr ? pEnemy->_arrStat[static_cast<size_t>( JRPGStat::HealthStateMachine )] / 2 : 0;
        vector<hashed_string> listType;
        if ( pClass != nullptr && pClass->_attackType.empty() == false )
            listType.push_back( pClass->_attackType );

        pushEvent( JRPGBattleEvent::Kind::Attack, false, memberIndex, target );
        const int32 damage = computePhysicalDamage( _pParty->computeAttack( memberIndex ), defense, _random );
        hitEnemy( memberIndex, target, damage, listType );
        addComboPoints( _settings._comboPerHit );
        addInner( memberIndex, _settings._innerPerAttack );

        // 타이밍 공격(씨 오브 스타즈)이다. 맞으면 추가 타격이 하나 들어가고, 추가 타격도 잠금을 깨며 콤보 포인트를 준다.
        if ( isTimed( JRPGTimingKind::Attack, memberIndex ) && _listEnemy[static_cast<size_t>( target )].isAlive() )
        {
            pushEvent( JRPGBattleEvent::Kind::TimedHit, false, memberIndex, target );
            hitEnemy( memberIndex, target, MathUtil::max( 1, JRPGBattleInternal::applyReduction( damage, 1.0f - _settings._timedHitBonus ) ), listType );
            addComboPoints( _settings._comboPerHit );
            addInner( memberIndex, _settings._innerPerAttack );
        }
    }

    void JRPGBattle::executeSpell( int32 memberIndex, const JRPGCommand& command )
    {
        const JRPGSpellDef* pSpell = _pCatalog->findSpell( command._id );
        JRPGMember&         member = _pParty->getMember( memberIndex );
        if ( pSpell == nullptr || _pParty->canUseSpell( memberIndex, command._id ) == false )
        {
            pushEvent( JRPGBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
            return;
        }
        if ( member._mp < pSpell->_mpCost )
        {
            pushEvent( JRPGBattleEvent::Kind::NotEnoughMP, false, memberIndex, command._target, 0, command._id );
            return;
        }
        const bool bUsesInner = _settings._bWuxia && pSpell->_innerCost > 0;
        if ( bUsesInner && member._inner < pSpell->_innerCost )
        {
            pushEvent( JRPGBattleEvent::Kind::NotEnoughInner, false, memberIndex, command._target, 0, command._id );
            return;
        }
        member._mp -= pSpell->_mpCost;
        if ( bUsesInner )
            member._inner -= pSpell->_innerCost;
        pushEvent( JRPGBattleEvent::Kind::SpellCast, false, memberIndex, command._target, 0, command._id );

        if ( pSpell->_kind == JRPGSpellKind::Damage )
        {
            // 주문은 지능으로, 초식(비급)은 힘으로 위력이 커진다.
            const bool            bTechnique = pSpell->_manualID.empty() == false;
            const int32           scale      = bTechnique ? _pParty->computeAttack( memberIndex ) : member.getStat( JRPGStat::Intellect );
            const int32           base       = pSpell->_power * ( 100 + scale ) / 100;
            vector<hashed_string> listType;
            if ( pSpell->_damageType.empty() == false )
                listType.push_back( pSpell->_damageType );
            if ( pSpell->_target == JRPGTargetKind::All )
            {
                for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
                {
                    if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                        hitEnemy( memberIndex, enemyIndex, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), listType );
                }
            }
            else
            {
                const optional<int32> target = findLivingEnemy( command._target );
                if ( target.has_value() )
                    hitEnemy( memberIndex, *target, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), listType );
            }
        }
        else
        {
            const int32 firstTarget = pSpell->_target == JRPGTargetKind::All ? 0 : command._target;
            const int32 lastTarget  = pSpell->_target == JRPGTargetKind::All ? _pParty->getMemberCount() - 1 : command._target;
            for ( int32 target = firstTarget; target <= lastTarget; ++target )
            {
                if ( target < 0 || target >= _pParty->getMemberCount() )
                    continue;
                JRPGMember& ally = _pParty->getMember( target );
                if ( pSpell->_kind == JRPGSpellKind::Heal && ally.isAlive() )
                {
                    const int32 before = ally._hp;
                    ally._hp           = MathUtil::min( ally.getStat( JRPGStat::MaxHP ), ally._hp + pSpell->_power + member.getStat( JRPGStat::Intellect ) / 2 );
                    pushEvent( JRPGBattleEvent::Kind::Healed, false, memberIndex, target, ally._hp - before, pSpell->_id );
                }
                else if ( pSpell->_kind == JRPGSpellKind::Revive && ally.isAlive() == false )
                {
                    ally._hp = MathUtil::max( 1, ally.getStat( JRPGStat::MaxHP ) / 2 );
                    pushEvent( JRPGBattleEvent::Kind::Revived, false, memberIndex, target, ally._hp, pSpell->_id );
                }
            }
        }

        // 초식은 하는 일과 상관없이 쓸 때마다 숙련이 오른다.
        if ( pSpell->_manualID.empty() == false && pSpell->_proficiencyGain > 0 )
        {
            _pParty->addProficiency( memberIndex, pSpell->_manualID, pSpell->_proficiencyGain );
            pushEvent( JRPGBattleEvent::Kind::ProficiencyGained, false, memberIndex, nullopt, pSpell->_proficiencyGain, pSpell->_manualID );
        }
    }

    void JRPGBattle::executeCombo( int32 memberIndex, const JRPGCommand& command )
    {
        const JRPGComboDef* pCombo = _pCatalog->findCombo( command._id );
        if ( pCombo == nullptr || _comboPoints < pCombo->_points )
        {
            pushEvent( JRPGBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
            return;
        }
        int32 attackSum = 0;
        for ( const hashed_string& participantID : pCombo->_listMemberID )
        {
            const optional<int32> foundParticipant = _pParty->findMemberIndex( participantID );
            if ( foundParticipant.has_value() == false || _pParty->getMember( *foundParticipant ).isAlive() == false )
            {
                pushEvent( JRPGBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
                return;
            }
            const int32 participant = *foundParticipant;
            attackSum += _pParty->computeAttack( participant );
        }
        _comboPoints -= pCombo->_points;
        pushEvent( JRPGBattleEvent::Kind::ComboUsed, false, memberIndex, command._target, _comboPoints, command._id );
        const int32 base = pCombo->_power + attackSum / 2;
        if ( pCombo->_target == JRPGTargetKind::All )
        {
            for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
            {
                if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                    hitEnemy( memberIndex, enemyIndex, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), pCombo->_listDamageType );
            }
            return;
        }
        const optional<int32> target = findLivingEnemy( command._target );
        if ( target.has_value() )
            hitEnemy( memberIndex, *target, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), pCombo->_listDamageType );
    }

    void JRPGBattle::executeEnemy( int32 enemyIndex )
    {
        if ( enemyIndex < 0 || enemyIndex >= static_cast<int32>( _listEnemy.size() ) )
            return;
        JRPGEnemyState&     enemy  = _listEnemy[static_cast<size_t>( enemyIndex )];
        const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
        if ( enemy.isAlive() == false || pEnemy == nullptr )
            return;
        ++enemy._turnsTaken;

        if ( enemy.isCasting() )
        {
            if ( --enemy._castTurnsLeft > 0 )
                return; // 아직 시전 중이다
            const JRPGSpellDef* pSpell    = _pCatalog->findSpell( pEnemy->_castSpellID );
            const int32         remaining = static_cast<int32>( enemy._listLock.size() );
            const int32         basePower = pSpell != nullptr ? pSpell->_power : 0;
            const int32         power     = basePower * ( remaining + 1 ) / ( enemy._lockTotal + 1 );
            enemy._listLock.clear();
            pushEvent( JRPGBattleEvent::Kind::CastReleased, true, enemyIndex, nullopt, power, pEnemy->_castSpellID );
            const int32 scaled = power * ( 100 + pEnemy->_arrStat[static_cast<size_t>( JRPGStat::Intellect )] ) / 100;
            for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
            {
                if ( _pParty->getMember( memberIndex ).isAlive() )
                    hitMember( enemyIndex, memberIndex, JRPGBattleInternal::applyVariance( scaled, 900, 1100, _random ) );
            }
            return;
        }

        if ( pEnemy->_castSpellID.empty() == false && enemy._turnsTaken % pEnemy->_castEvery == 0 )
        {
            enemy._castTurnsLeft = pEnemy->_castTurns;
            enemy._listLock      = pEnemy->_listLock;
            enemy._lockTotal     = static_cast<int32>( enemy._listLock.size() );
            pushEvent( JRPGBattleEvent::Kind::CastStarted, true, enemyIndex, nullopt, enemy._lockTotal, pEnemy->_castSpellID );
            return;
        }

        // 살아 있는 멤버 중 하나를 시드 난수로 고른다.
        vector<int32> listAlive;
        for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
        {
            if ( _pParty->getMember( memberIndex ).isAlive() )
                listAlive.push_back( memberIndex );
        }
        if ( listAlive.empty() )
            return;
        const int32 target = listAlive[static_cast<size_t>( _random.nextInt( 0, static_cast<int32>( listAlive.size() ) - 1 ) )];
        pushEvent( JRPGBattleEvent::Kind::Attack, true, enemyIndex, target );
        hitMember( enemyIndex, target, computePhysicalDamage( pEnemy->_arrStat[static_cast<size_t>( JRPGStat::Strength )], _pParty->computeDefense( target ), _random ) );
    }

    void JRPGBattle::hitEnemy( int32 memberIndex, int32 enemyIndex, int32 damage, const vector<hashed_string>& listDamageType )
    {
        JRPGEnemyState&     enemy  = _listEnemy[static_cast<size_t>( enemyIndex )];
        const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
        if ( enemy.isAlive() == false )
            return;
        bool bWeak = false;
        if ( pEnemy != nullptr )
        {
            for ( const hashed_string& type : listDamageType )
            {
                for ( const hashed_string& weakness : pEnemy->_listWeakness )
                {
                    bWeak = bWeak || type == weakness;
                }
            }
        }
        const int32 dealt = DepletingValue::take( enemy._hp, bWeak ? static_cast<int32>( static_cast<float32>( damage ) * _settings._weaknessMultiplier ) : damage )._taken;
        pushEvent( JRPGBattleEvent::Kind::Damage, false, memberIndex, enemyIndex, dealt );

        // 잠금은 한 타의 유형 하나마다 같은 유형의 잠금을 하나 깬다. 다 깨면 시전이 취소된다.
        if ( enemy.isCasting() && enemy._listLock.empty() == false )
        {
            for ( const hashed_string& type : listDamageType )
            {
                for ( size_t lockIndex = 0; lockIndex < enemy._listLock.size(); ++lockIndex )
                {
                    if ( enemy._listLock[lockIndex] != type )
                        continue;
                    enemy._listLock.erase( enemy._listLock.begin() + static_cast<ptrdiff_t>( lockIndex ) );
                    pushEvent( JRPGBattleEvent::Kind::LockBroken, false, memberIndex, enemyIndex, static_cast<int32>( enemy._listLock.size() ), type );
                    break;
                }
            }
            if ( enemy._listLock.empty() )
            {
                enemy._castTurnsLeft = 0;
                pushEvent( JRPGBattleEvent::Kind::CastCancelled, false, memberIndex, enemyIndex, 0, pEnemy != nullptr ? pEnemy->_castSpellID : hashed_string{} );
            }
        }
        if ( enemy.isAlive() == false )
        {
            enemy._castTurnsLeft = 0;
            enemy._listLock.clear();
            _turnOrder.removeActor( kEnemyActorBase + enemyIndex );
            pushEvent( JRPGBattleEvent::Kind::Defeated, false, memberIndex, enemyIndex, 0, enemy._enemyID );
        }
    }

    void JRPGBattle::hitMember( int32 enemyIndex, int32 memberIndex, int32 damage )
    {
        JRPGMember& member = _pParty->getMember( memberIndex );
        if ( member.isAlive() == false )
            return;
        int32 reduced = damage;
        if ( _listDefending[static_cast<size_t>( memberIndex )] != SW_FALSE )
            reduced = JRPGBattleInternal::applyReduction( reduced, _settings._defendReduction );
        if ( isTimed( JRPGTimingKind::Block, memberIndex ) )
        {
            pushEvent( JRPGBattleEvent::Kind::TimedBlock, true, enemyIndex, memberIndex );
            reduced = JRPGBattleInternal::applyReduction( reduced, _settings._timedBlockReduction );
        }
        const int32 dealt = DepletingValue::take( member._hp, reduced )._taken;
        pushEvent( JRPGBattleEvent::Kind::Damage, true, enemyIndex, memberIndex, dealt );
        if ( member.isAlive() == false )
        {
            _turnOrder.removeActor( memberIndex );
            pushEvent( JRPGBattleEvent::Kind::Defeated, true, enemyIndex, memberIndex, 0, member._id );
            return; // 쓰러지는 타격에는 내공이 차지 않는다
        }
        addInner( memberIndex, _settings._innerPerHitTaken );
    }

    void JRPGBattle::addComboPoints( int32 amount )
    {
        if ( amount <= 0 )
            return;
        _comboPoints = MathUtil::min( _settings._comboMax, _comboPoints + amount );
        pushEvent( JRPGBattleEvent::Kind::ComboPoints, false, nullopt, nullopt, _comboPoints );
    }

    void JRPGBattle::addInner( int32 memberIndex, int32 amount )
    {
        if ( _settings._bWuxia == false || amount <= 0 )
            return;
        JRPGMember& member = _pParty->getMember( memberIndex );
        member._inner      = MathUtil::min( JRPGParty::kInnerMax, member._inner + amount );
    }

    bool JRPGBattle::isTimed( JRPGTimingKind kind, int32 memberIndex ) const
    {
        float32    offset       = 0.0f;
        const bool bNoTimingHit = _pJudge == nullptr || _pTimingInput == nullptr || _pTimingInput->findPressOffset( kind, memberIndex, offset ) == false;
        if ( bNoTimingHit )
            return false;
        return _pJudge->judge( 0.0f, offset ).isHit();
    }

    optional<int32> JRPGBattle::findLivingEnemy( int32 preferred ) const
    {
        const bool bPreferredEnemyAlive = preferred >= 0 && preferred < static_cast<int32>( _listEnemy.size() ) && _listEnemy[static_cast<size_t>( preferred )].isAlive();
        if ( bPreferredEnemyAlive )
            return preferred;
        for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
        {
            if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                return enemyIndex;
        }
        return nullopt;
    }

    void JRPGBattle::finishIfDecided()
    {
        if ( _result != JRPGBattleResult::Ongoing )
            return;
        if ( _pParty->countAlive() == 0 )
        {
            _result = JRPGBattleResult::Defeat;
            pushEvent( JRPGBattleEvent::Kind::Defeat, false, nullopt, nullopt );
            return;
        }
        if ( findLivingEnemy( 0 ).has_value() )
            return;
        for ( const JRPGEnemyState& enemy : _listEnemy )
        {
            const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
            if ( pEnemy == nullptr )
                continue;
            _rewardExp += pEnemy->_exp;
            _rewardGold += pEnemy->_gold;
        }
        _result           = JRPGBattleResult::Victory;
        const int64 share = _pParty->distributeRewards( _rewardExp, _rewardGold );
        pushEvent( JRPGBattleEvent::Kind::Victory, false, nullopt, nullopt, static_cast<int32>( share ) );
    }

    void JRPGBattle::pushEvent( JRPGBattleEvent::Kind kind, bool bEnemyActor, optional<int32> actor, optional<int32> target, int32 value, const hashed_string& id )
    {
        JRPGBattleEvent event;
        event._kind        = kind;
        event._bEnemyActor = bEnemyActor;
        event._actor       = actor;
        event._target      = target;
        event._value       = value;
        event._id          = id;
        _eventBuffer.push( event );
    }

    void JRPGBattle::pollEvents( vector<JRPGBattleEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void JRPGBattle::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        // 적마다 이름(4) + 잠금 수(4) + HP, 시전, 잠금 전체, 차례(16) 바이트 이상
        io.list( _listEnemy, 24, [&]( JRPGEnemyState& enemy )
        {
            io.value( enemy._enemyID );
            if ( io.isReading() )
            {
                io.check( _pCatalog->findEnemy( enemy._enemyID ) != nullptr );
            }
            io.list( enemy._listLock );
            io.value( enemy._hp );
            io.value( enemy._castTurnsLeft );
            io.value( enemy._lockTotal );
            io.value( enemy._turnsTaken );
            if ( io.isReading() && io.isOk() )
            {
                io.check( isEnemyStateValid( enemy ) );
            }
        } );
        // 명령마다 이름(4) + 대상(4) + 종류(1) + 방어(1) 바이트
        uint32 commandCount = static_cast<uint32>( _listCommand.size() );
        io.count( 10, commandCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            const uint32 memberCount = _pParty != nullptr ? static_cast<uint32>( _pParty->getMemberCount() ) : 0;
            if ( io.check( commandCount == memberCount ) == false )
                return;
            _listCommand.assign( commandCount, JRPGCommand{} );
            _listDefending.assign( commandCount, SW_FALSE );
        }
        for ( uint32 memberIndex = 0; memberIndex < commandCount; ++memberIndex )
        {
            JRPGCommand& command = _listCommand[memberIndex];
            io.value( command._id );
            io.value( command._target );
            io.enumValue( command._kind, JRPGCommandKind::Defend );
            io.flag( _listDefending[memberIndex] );
            if ( io.isReading() && ( io.isOk() == false || io.check( isCommandStateValid( command ) ) == false ) )
                return;
        }
        io.value( _turnOrder );
        io.value( _random );
        io.value( _rewardExp );
        io.value( _rewardGold );
        io.value( _comboPoints );
        io.value( _round );
        io.value( _fleeAttempts );
        io.enumValue( _result, JRPGBattleResult::Fled );
        io.check( 0 <= _comboPoints && _comboPoints <= _settings._comboMax && 0 <= _round && 0 <= _fleeAttempts && 0 <= _rewardExp && 0 <= _rewardGold );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

    bool JRPGBattle::isEnemyStateValid( const JRPGEnemyState& enemy ) const
    {
        const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
        if ( pEnemy == nullptr )
            return false;
        // 잠금 전체가 음수이면 시전을 터뜨릴 때 0 으로 나눈다(`_lockTotal + 1`). 시전 중이 아니면 남은 잠금이 없다(터뜨리기 · 취소 · 쓰러짐이 모두 비운다).
        const int32 lockCount = static_cast<int32>( enemy._listLock.size() );
        const int32 lockLimit = static_cast<int32>( pEnemy->_listLock.size() );
        return 0 <= enemy._hp && enemy._hp <= pEnemy->_arrStat[static_cast<size_t>( JRPGStat::MaxHP )] && 0 <= enemy._castTurnsLeft && enemy._castTurnsLeft <= pEnemy->_castTurns &&
               lockCount <= enemy._lockTotal && enemy._lockTotal <= lockLimit && ( lockCount == 0 || enemy.isCasting() ) && 0 <= enemy._turnsTaken;
    }

    bool JRPGBattle::isCommandStateValid( const JRPGCommand& command ) const
    {
        if ( command._kind == JRPGCommandKind::Spell )
            return _pCatalog->findSpell( command._id ) != nullptr;
        if ( command._kind == JRPGCommandKind::Combo )
            return _pCatalog->findCombo( command._id ) != nullptr;
        return true;
    }

} // namespace sw

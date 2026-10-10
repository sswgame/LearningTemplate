#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGBattle.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Input/TimingJudge.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGParty.h"

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
        , _outcome{ JRPGBattleOutcome::Ongoing }
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
        _comboPoints  = 0;
        _round        = 0;
        _fleeAttempts = 0;
        _rewardExp    = 0;
        _rewardGold   = 0;
        _outcome      = JRPGBattleOutcome::Ongoing;
        if ( _pCatalog == nullptr || pParty == nullptr || pParty->countAlive() == 0 )
            return false;
        for ( const hashed_string& enemyID : listEnemyID )
        {
            const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemyID );
            if ( pEnemy == nullptr )
                continue;
            JRPGEnemyState state;
            state._enemyID = enemyID;
            state._hp      = pEnemy->_arrStat[static_cast<size_t>( JRPGStat::MaxHp )];
            _listEnemy.push_back( state );
        }
        _listCommand.assign( static_cast<size_t>( pParty->getMemberCount() ), JRPGCommand{} );
        _listDefending.assign( static_cast<size_t>( pParty->getMemberCount() ), 0 );
        return _listEnemy.empty() == false;
    }

    bool JRPGBattle::setCommand( int32 memberIndex, const JRPGCommand& command )
    {
        if ( _outcome != JRPGBattleOutcome::Ongoing || _pParty == nullptr || memberIndex < 0 || memberIndex >= _pParty->getMemberCount() )
            return false;
        const JRPGMember& member = _pParty->getMember( memberIndex );
        if ( member.isAlive() == false )
            return false;
        if ( command._kind == JRPGCommandKind::Spell )
        {
            const JRPGSpellDef* pSpell = _pCatalog->findSpell( command._id );
            if ( pSpell == nullptr || _pParty->canUseSpell( memberIndex, command._id ) == false || member._mp < pSpell->_mpCost )
                return false;
            if ( _settings._bWuxia && member._inner < pSpell->_innerCost )
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
                const int32 participant = _pParty->findMemberIndex( participantID );
                if ( participant < 0 || _pParty->getMember( participant ).isAlive() == false )
                    return false;
                bParticipant = bParticipant || participant == memberIndex;
            }
            if ( bParticipant == false )
                return false;
        }
        _listCommand[static_cast<size_t>( memberIndex )] = command;
        return true;
    }

    void JRPGBattle::resolveRound()
    {
        if ( _outcome != JRPGBattleOutcome::Ongoing || _pParty == nullptr )
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
                const int32 participant = _pParty->findMemberIndex( participantID );
                if ( participant >= 0 && static_cast<size_t>( participant ) != memberIndex )
                    _listCommand[static_cast<size_t>( participant )] = JRPGCommand{};
            }
        }
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            _listDefending[memberIndex] = _listCommand[memberIndex]._kind == JRPGCommandKind::Defend ? SW_TRUE : SW_FALSE;
        }

        syncActors();
        _turnOrder.restartRound();
        const int32 roundBefore = _turnOrder.getRound();
        while ( _outcome == JRPGBattleOutcome::Ongoing )
        {
            const int32 actor = _turnOrder.next();
            if ( actor < 0 || _turnOrder.getRound() != roundBefore + 1 )
                break; // 이번 라운드 차례가 다 돌았다(다음 큐는 다음 라운드의 restartRound 가 버린다)
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
        if ( _outcome != JRPGBattleOutcome::Ongoing || _pParty == nullptr )
            return false;
        const float32 chance = computeFleeChance();
        ++_fleeAttempts;
        if ( _random.nextFloat() < chance )
        {
            pushEvent( JRPGBattleEvent::Kind::Fled, false, -1, -1, _fleeAttempts );
            _outcome = JRPGBattleOutcome::Fled;
            return true;
        }
        pushEvent( JRPGBattleEvent::Kind::FleeFailed, false, -1, -1, _fleeAttempts );
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
        if ( memberIndex < 0 || memberIndex >= static_cast<int32>( _listCommand.size() ) || _pParty->getMember( memberIndex ).isAlive() == false )
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
        const int32 target = findLivingEnemy( enemyIndex );
        if ( target < 0 )
            return;
        const JRPGMember&     member  = _pParty->getMember( memberIndex );
        const JRPGClassDef*   pClass  = _pCatalog->findClass( member._classID );
        const JRPGEnemyDef*   pEnemy  = _pCatalog->findEnemy( _listEnemy[static_cast<size_t>( target )]._enemyID );
        const int32           defense = pEnemy != nullptr ? pEnemy->_arrStat[static_cast<size_t>( JRPGStat::Vitality )] / 2 : 0;
        vector<hashed_string> listType;
        if ( pClass != nullptr && pClass->_attackType.empty() == false )
            listType.push_back( pClass->_attackType );

        pushEvent( JRPGBattleEvent::Kind::Attack, false, memberIndex, target );
        const int32 damage = computePhysicalDamage( _pParty->computeAttack( memberIndex ), defense, _random );
        hitEnemy( memberIndex, target, damage, listType );
        addComboPoints( _settings._comboPerHit );
        addInner( memberIndex, _settings._innerPerAttack );

        // 타이밍 공격(씨 오브 스타즈) — 맞으면 추가 타격 하나. 추가 타격도 잠금을 깨고 콤보 포인트를 준다.
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
            pushEvent( JRPGBattleEvent::Kind::NotEnoughMp, false, memberIndex, command._target, 0, command._id );
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
            // 주문은 지능, 초식(비급)은 힘으로 키운다.
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
                const int32 target = findLivingEnemy( command._target );
                if ( target >= 0 )
                    hitEnemy( memberIndex, target, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), listType );
            }
            if ( bTechnique && pSpell->_proficiencyGain > 0 )
            {
                _pParty->addProficiency( memberIndex, pSpell->_manualID, pSpell->_proficiencyGain );
                pushEvent( JRPGBattleEvent::Kind::ProficiencyGained, false, memberIndex, -1, pSpell->_proficiencyGain, pSpell->_manualID );
            }
            return;
        }

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
                ally._hp           = MathUtil::min( ally.getStat( JRPGStat::MaxHp ), ally._hp + pSpell->_power + member.getStat( JRPGStat::Intellect ) / 2 );
                pushEvent( JRPGBattleEvent::Kind::Healed, false, memberIndex, target, ally._hp - before, pSpell->_id );
            }
            else if ( pSpell->_kind == JRPGSpellKind::Revive && ally.isAlive() == false )
            {
                ally._hp = MathUtil::max( 1, ally.getStat( JRPGStat::MaxHp ) / 2 );
                pushEvent( JRPGBattleEvent::Kind::Revived, false, memberIndex, target, ally._hp, pSpell->_id );
            }
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
            const int32 participant = _pParty->findMemberIndex( participantID );
            if ( participant < 0 || _pParty->getMember( participant ).isAlive() == false )
            {
                pushEvent( JRPGBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
                return;
            }
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
        const int32 target = findLivingEnemy( command._target );
        if ( target >= 0 )
            hitEnemy( memberIndex, target, JRPGBattleInternal::applyVariance( base, 900, 1100, _random ), pCombo->_listDamageType );
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
                return; // 아직 시전 중
            const JRPGSpellDef* pSpell    = _pCatalog->findSpell( pEnemy->_castSpellID );
            const int32         remaining = static_cast<int32>( enemy._listLock.size() );
            const int32         basePower = pSpell != nullptr ? pSpell->_power : 0;
            const int32         power     = basePower * ( remaining + 1 ) / ( enemy._lockTotal + 1 );
            enemy._listLock.clear();
            pushEvent( JRPGBattleEvent::Kind::CastReleased, true, enemyIndex, -1, power, pEnemy->_castSpellID );
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
            pushEvent( JRPGBattleEvent::Kind::CastStarted, true, enemyIndex, -1, enemy._lockTotal, pEnemy->_castSpellID );
            return;
        }

        // 살아 있는 멤버 중 하나를 씨앗 난수로 고른다.
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
        const int32 dealt = MathUtil::min( enemy._hp, bWeak ? static_cast<int32>( static_cast<float32>( damage ) * _settings._weaknessMultiplier ) : damage );
        enemy._hp -= MathUtil::max( 0, dealt );
        pushEvent( JRPGBattleEvent::Kind::Damage, false, memberIndex, enemyIndex, dealt );

        // 잠금: 한 타의 유형 하나마다 같은 유형 잠금을 하나 깬다. 다 깨면 시전이 취소된다.
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
        const int32 dealt = MathUtil::min( member._hp, MathUtil::max( 0, reduced ) );
        member._hp -= dealt;
        pushEvent( JRPGBattleEvent::Kind::Damage, true, enemyIndex, memberIndex, dealt );
        addInner( memberIndex, _settings._innerPerHitTaken );
        if ( member.isAlive() == false )
        {
            _turnOrder.removeActor( memberIndex );
            pushEvent( JRPGBattleEvent::Kind::Defeated, true, enemyIndex, memberIndex, 0, member._id );
        }
    }

    void JRPGBattle::addComboPoints( int32 amount )
    {
        if ( amount <= 0 )
            return;
        _comboPoints = MathUtil::min( _settings._comboMax, _comboPoints + amount );
        pushEvent( JRPGBattleEvent::Kind::ComboPoints, false, -1, -1, _comboPoints );
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
        float32 offset = 0.0f;
        if ( _pJudge == nullptr || _pTimingInput == nullptr || _pTimingInput->findPressOffset( kind, memberIndex, offset ) == false )
            return false;
        return _pJudge->judge( 0.0f, offset ).isHit();
    }

    int32 JRPGBattle::findLivingEnemy( int32 preferred ) const
    {
        if ( preferred >= 0 && preferred < static_cast<int32>( _listEnemy.size() ) && _listEnemy[static_cast<size_t>( preferred )].isAlive() )
            return preferred;
        for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
        {
            if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                return enemyIndex;
        }
        return -1;
    }

    void JRPGBattle::finishIfDecided()
    {
        if ( _outcome != JRPGBattleOutcome::Ongoing )
            return;
        if ( _pParty->countAlive() == 0 )
        {
            _outcome = JRPGBattleOutcome::Defeat;
            pushEvent( JRPGBattleEvent::Kind::Defeat, false, -1, -1 );
            return;
        }
        if ( findLivingEnemy( 0 ) >= 0 )
            return;
        for ( const JRPGEnemyState& enemy : _listEnemy )
        {
            const JRPGEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyID );
            if ( pEnemy == nullptr )
                continue;
            _rewardExp += pEnemy->_exp;
            _rewardGold += pEnemy->_gold;
        }
        _outcome          = JRPGBattleOutcome::Victory;
        const int64 share = _pParty->distributeRewards( _rewardExp, _rewardGold );
        pushEvent( JRPGBattleEvent::Kind::Victory, false, -1, -1, static_cast<int32>( share ) );
    }

    void JRPGBattle::pushEvent( JRPGBattleEvent::Kind kind, bool bEnemyActor, int32 actor, int32 target, int32 value, const hashed_string& id )
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

    void JRPGBattle::drainEvents( vector<JRPGBattleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void JRPGBattle::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listEnemy.size() );
        for ( const JRPGEnemyState& enemy : _listEnemy )
        {
            StateArchiveUtil::writeName( outArchive, enemy._enemyID );
            outArchive << static_cast<uint32>( enemy._listLock.size() );
            for ( const hashed_string& lock : enemy._listLock )
            {
                StateArchiveUtil::writeName( outArchive, lock );
            }
            outArchive << enemy._hp;
            outArchive << enemy._castTurnsLeft;
            outArchive << enemy._lockTotal;
            outArchive << enemy._turnsTaken;
        }
        outArchive << static_cast<uint32>( _listCommand.size() );
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            const JRPGCommand& command = _listCommand[memberIndex];
            StateArchiveUtil::writeName( outArchive, command._id );
            outArchive << command._target;
            outArchive << static_cast<uint8>( command._kind );
            outArchive << _listDefending[memberIndex];
        }
        _turnOrder.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _rewardExp;
        outArchive << _rewardGold;
        outArchive << _comboPoints;
        outArchive << _round;
        outArchive << _fleeAttempts;
        outArchive << static_cast<uint8>( _outcome );
    }

    bool JRPGBattle::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 판정 · 타이밍 입력 · 설정은 사본이 그대로 든다.
        JRPGBattle restored = *this;
        uint32     count    = 0;
        // 적마다 이름(4) + 잠금 수(4) + HP · 시전 · 잠금 전체 · 차례(16) 이상
        if ( _pCatalog == nullptr || StateArchiveUtil::readCount( archive, 24, count ) == false )
            return false;
        restored._listEnemy.assign( count, JRPGEnemyState{} );
        for ( JRPGEnemyState& enemy : restored._listEnemy )
        {
            uint32 lockCount = 0;
            if ( StateArchiveUtil::readName( archive, enemy._enemyID ) == false || _pCatalog->findEnemy( enemy._enemyID ) == nullptr )
                return false;
            if ( StateArchiveUtil::readCount( archive, 4, lockCount ) == false )
                return false;
            enemy._listLock.resize( lockCount );
            for ( hashed_string& lock : enemy._listLock )
            {
                if ( StateArchiveUtil::readName( archive, lock ) == false )
                    return false;
            }
            archive >> enemy._hp;
            archive >> enemy._castTurnsLeft;
            archive >> enemy._lockTotal;
            archive >> enemy._turnsTaken;
        }

        // 명령마다 이름(4) + 대상(4) + 종류(1) + 방어(1)
        if ( StateArchiveUtil::readCount( archive, 10, count ) == false )
            return false;
        const uint32 memberCount = _pParty != nullptr ? static_cast<uint32>( _pParty->getMemberCount() ) : 0;
        if ( count != memberCount )
            return false;
        restored._listCommand.assign( count, JRPGCommand{} );
        restored._listDefending.assign( count, SW_FALSE );
        for ( uint32 memberIndex = 0; memberIndex < count; ++memberIndex )
        {
            JRPGCommand& command = restored._listCommand[memberIndex];
            uint8        kind    = 0;
            uint8        bDefend = SW_FALSE;
            if ( StateArchiveUtil::readName( archive, command._id ) == false )
                return false;
            archive >> command._target;
            archive >> kind;
            archive >> bDefend;
            const bool bValid = archive.isOk() && kind <= static_cast<uint8>( JRPGCommandKind::Defend ) && bDefend <= SW_TRUE;
            if ( bValid == false )
                return false;
            command._kind                        = static_cast<JRPGCommandKind>( kind );
            restored._listDefending[memberIndex] = bDefend;
        }

        uint8 outcome = 0;
        if ( restored._turnOrder.readState( archive ) == false || StateArchiveUtil::readRandom( archive, restored._random ) == false )
            return false;
        archive >> restored._rewardExp;
        archive >> restored._rewardGold;
        archive >> restored._comboPoints;
        archive >> restored._round;
        archive >> restored._fleeAttempts;
        archive >> outcome;
        const bool bValid = archive.isOk() && outcome <= static_cast<uint8>( JRPGBattleOutcome::Fled ) && 0 <= restored._comboPoints && 0 <= restored._round;
        if ( bValid == false )
            return false;
        restored._outcome = static_cast<JRPGBattleOutcome>( outcome );
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }
} // namespace sw

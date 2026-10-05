#include "pch.h"

#include "GameFramework/Kits/Rpg/ClassicJrpg/JrpgBattle.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Input/TimingJudge.h"
#include "GameFramework/Kits/Rpg/ClassicJrpg/JrpgParty.h"

namespace sw
{
    namespace
    {
        struct JrpgBattleInternal
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
    JrpgBattle::JrpgBattle()
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
        , _outcome{ JrpgBattleOutcome::Ongoing }
    {
    }

    void JrpgBattle::initialize( const JrpgCatalog* pCatalog, const TimingJudge* pJudge, const JrpgBattleSettings& settings, uint32 seed )
    {
        _pCatalog = pCatalog;
        _pJudge   = pJudge;
        _settings = settings;
        _random.setSeed( seed );
        _turnOrder.initialize( TurnOrderMode::Rounds, seed ^ 0x27d4eb2fu );
    }

    bool JrpgBattle::start( JrpgParty* pParty, const vector<hashed_string>& listEnemyId )
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
        _outcome      = JrpgBattleOutcome::Ongoing;
        if ( _pCatalog == nullptr || pParty == nullptr || pParty->countAlive() == 0 )
            return false;
        for ( const hashed_string& enemyId : listEnemyId )
        {
            const JrpgEnemyDef* pEnemy = _pCatalog->findEnemy( enemyId );
            if ( pEnemy == nullptr )
                continue;
            JrpgEnemyState state;
            state._enemyId = enemyId;
            state._hp      = pEnemy->_arrStat[static_cast<size_t>( JrpgStat::MaxHp )];
            _listEnemy.push_back( state );
        }
        _listCommand.assign( static_cast<size_t>( pParty->getMemberCount() ), JrpgCommand{} );
        _listDefending.assign( static_cast<size_t>( pParty->getMemberCount() ), 0 );
        return _listEnemy.empty() == false;
    }

    bool JrpgBattle::setCommand( int32 memberIndex, const JrpgCommand& command )
    {
        if ( _outcome != JrpgBattleOutcome::Ongoing || _pParty == nullptr || memberIndex < 0 || memberIndex >= _pParty->getMemberCount() )
            return false;
        const JrpgMember& member = _pParty->getMember( memberIndex );
        if ( member.isAlive() == false )
            return false;
        if ( command._kind == JrpgCommandKind::Spell )
        {
            const JrpgSpellDef* pSpell = _pCatalog->findSpell( command._id );
            if ( pSpell == nullptr || _pParty->canUseSpell( memberIndex, command._id ) == false || member._mp < pSpell->_mpCost )
                return false;
            if ( _settings._bWuxia && member._inner < pSpell->_innerCost )
                return false;
        }
        else if ( command._kind == JrpgCommandKind::Combo )
        {
            const JrpgComboDef* pCombo = _pCatalog->findCombo( command._id );
            if ( pCombo == nullptr || _comboPoints < pCombo->_points )
                return false;
            bool bParticipant = false;
            for ( const hashed_string& participantId : pCombo->_listMemberId )
            {
                const int32 participant = _pParty->findMemberIndex( participantId );
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

    void JrpgBattle::resolveRound()
    {
        if ( _outcome != JrpgBattleOutcome::Ongoing || _pParty == nullptr )
            return;
        ++_round;

        // 합동기는 참여 멤버의 이번 라운드 행동을 함께 쓴다.
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
        {
            if ( _listCommand[memberIndex]._kind != JrpgCommandKind::Combo )
                continue;
            const JrpgComboDef* pCombo = _pCatalog->findCombo( _listCommand[memberIndex]._id );
            if ( pCombo == nullptr )
                continue;
            for ( const hashed_string& participantId : pCombo->_listMemberId )
            {
                const int32 participant = _pParty->findMemberIndex( participantId );
                if ( participant >= 0 && static_cast<size_t>( participant ) != memberIndex )
                    _listCommand[static_cast<size_t>( participant )] = JrpgCommand{};
            }
        }
        for ( size_t memberIndex = 0; memberIndex < _listCommand.size(); ++memberIndex )
            _listDefending[memberIndex] = _listCommand[memberIndex]._kind == JrpgCommandKind::Defend ? SW_TRUE : SW_FALSE;

        syncActors();
        _turnOrder.restartRound();
        const int32 roundBefore = _turnOrder.getRound();
        while ( _outcome == JrpgBattleOutcome::Ongoing )
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
            _listCommand[memberIndex]   = JrpgCommand{};
            _listDefending[memberIndex] = SW_FALSE;
        }
    }

    bool JrpgBattle::tryFlee()
    {
        if ( _outcome != JrpgBattleOutcome::Ongoing || _pParty == nullptr )
            return false;
        const float32 chance = computeFleeChance();
        ++_fleeAttempts;
        if ( _random.nextFloat() < chance )
        {
            pushEvent( JrpgBattleEvent::Kind::Fled, false, -1, -1, _fleeAttempts );
            _outcome = JrpgBattleOutcome::Fled;
            return true;
        }
        pushEvent( JrpgBattleEvent::Kind::FleeFailed, false, -1, -1, _fleeAttempts );
        for ( JrpgCommand& command : _listCommand )
            command = JrpgCommand{};
        resolveRound();
        return false;
    }

    float32 JrpgBattle::computeFleeChance() const
    {
        if ( _pParty == nullptr || _pCatalog == nullptr )
            return 0.0f;
        float32 enemyAgility = 0.0f;
        int32   enemyCount   = 0;
        for ( const JrpgEnemyState& enemy : _listEnemy )
        {
            const JrpgEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyId );
            if ( enemy.isAlive() == false || pEnemy == nullptr )
                continue;
            if ( pEnemy->_bBoss )
                return 0.0f;
            enemyAgility += static_cast<float32>( pEnemy->_arrStat[static_cast<size_t>( JrpgStat::Agility )] );
            ++enemyCount;
        }
        float32 partyAgility = 0.0f;
        int32   partyCount   = 0;
        for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
        {
            const JrpgMember& member = _pParty->getMember( memberIndex );
            if ( member.isAlive() == false )
                continue;
            partyAgility += static_cast<float32>( member.getStat( JrpgStat::Agility ) );
            ++partyCount;
        }
        const float32 averageGap = ( partyCount > 0 ? partyAgility / static_cast<float32>( partyCount ) : 0.0f ) -
                                   ( enemyCount > 0 ? enemyAgility / static_cast<float32>( enemyCount ) : 0.0f );
        const float32 chance = _settings._fleeBase + averageGap * _settings._fleePerAgility + static_cast<float32>( _fleeAttempts ) * _settings._fleePerAttempt;
        return MathUtil::clamp( chance, 0.05f, 0.95f );
    }

    int32 JrpgBattle::computePhysicalDamage( int32 attack, int32 defense, GameRandom& random )
    {
        const int32 base = attack / 2 - defense / 4;
        if ( base < 1 )
            return random.nextInt( 0, 1 );
        return MathUtil::max( 1, JrpgBattleInternal::applyVariance( base, 875, 1125, random ) );
    }

    void JrpgBattle::syncActors()
    {
        for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
        {
            const JrpgMember& member = _pParty->getMember( memberIndex );
            if ( member.isAlive() == false )
            {
                _turnOrder.removeActor( memberIndex );
                continue;
            }
            const float32 agility = static_cast<float32>( member.getStat( JrpgStat::Agility ) );
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
            const JrpgEnemyDef* pEnemy  = _pCatalog->findEnemy( _listEnemy[static_cast<size_t>( enemyIndex )]._enemyId );
            const float32       agility = pEnemy != nullptr ? static_cast<float32>( pEnemy->_arrStat[static_cast<size_t>( JrpgStat::Agility )] ) : 1.0f;
            _turnOrder.addActor( actor, agility );
            _turnOrder.setSpeed( actor, agility );
            _turnOrder.setPriority( actor, 0 );
        }
    }

    void JrpgBattle::executeMember( int32 memberIndex )
    {
        if ( memberIndex < 0 || memberIndex >= static_cast<int32>( _listCommand.size() ) || _pParty->getMember( memberIndex ).isAlive() == false )
            return;
        const JrpgCommand command = _listCommand[static_cast<size_t>( memberIndex )];
        switch ( command._kind )
        {
            case JrpgCommandKind::None:
            {
                return;
            }
            case JrpgCommandKind::Attack:
            {
                executeAttack( memberIndex, command._target );
                return;
            }
            case JrpgCommandKind::Spell:
            {
                executeSpell( memberIndex, command );
                return;
            }
            case JrpgCommandKind::Combo:
            {
                executeCombo( memberIndex, command );
                return;
            }
            case JrpgCommandKind::Defend:
            {
                pushEvent( JrpgBattleEvent::Kind::Defending, false, memberIndex, memberIndex );
                return;
            }
        }
    }

    void JrpgBattle::executeAttack( int32 memberIndex, int32 enemyIndex )
    {
        const int32 target = findLivingEnemy( enemyIndex );
        if ( target < 0 )
            return;
        const JrpgMember&     member  = _pParty->getMember( memberIndex );
        const JrpgClassDef*   pClass  = _pCatalog->findClass( member._classId );
        const JrpgEnemyDef*   pEnemy  = _pCatalog->findEnemy( _listEnemy[static_cast<size_t>( target )]._enemyId );
        const int32           defense = pEnemy != nullptr ? pEnemy->_arrStat[static_cast<size_t>( JrpgStat::Vitality )] / 2 : 0;
        vector<hashed_string> listType;
        if ( pClass != nullptr && pClass->_attackType.empty() == false )
            listType.push_back( pClass->_attackType );

        pushEvent( JrpgBattleEvent::Kind::Attack, false, memberIndex, target );
        const int32 damage = computePhysicalDamage( _pParty->computeAttack( memberIndex ), defense, _random );
        hitEnemy( memberIndex, target, damage, listType );
        addComboPoints( _settings._comboPerHit );
        addInner( memberIndex, _settings._innerPerAttack );

        // 타이밍 공격(씨 오브 스타즈) — 맞으면 추가 타격 하나. 추가 타격도 잠금을 깨고 콤보 포인트를 준다.
        if ( isTimed( JrpgTimingKind::Attack, memberIndex ) && _listEnemy[static_cast<size_t>( target )].isAlive() )
        {
            pushEvent( JrpgBattleEvent::Kind::TimedHit, false, memberIndex, target );
            hitEnemy( memberIndex, target, MathUtil::max( 1, JrpgBattleInternal::applyReduction( damage, 1.0f - _settings._timedHitBonus ) ), listType );
            addComboPoints( _settings._comboPerHit );
            addInner( memberIndex, _settings._innerPerAttack );
        }
    }

    void JrpgBattle::executeSpell( int32 memberIndex, const JrpgCommand& command )
    {
        const JrpgSpellDef* pSpell = _pCatalog->findSpell( command._id );
        JrpgMember&         member = _pParty->getMember( memberIndex );
        if ( pSpell == nullptr || _pParty->canUseSpell( memberIndex, command._id ) == false )
        {
            pushEvent( JrpgBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
            return;
        }
        if ( member._mp < pSpell->_mpCost )
        {
            pushEvent( JrpgBattleEvent::Kind::NotEnoughMp, false, memberIndex, command._target, 0, command._id );
            return;
        }
        const bool bUsesInner = _settings._bWuxia && pSpell->_innerCost > 0;
        if ( bUsesInner && member._inner < pSpell->_innerCost )
        {
            pushEvent( JrpgBattleEvent::Kind::NotEnoughInner, false, memberIndex, command._target, 0, command._id );
            return;
        }
        member._mp -= pSpell->_mpCost;
        if ( bUsesInner )
            member._inner -= pSpell->_innerCost;
        pushEvent( JrpgBattleEvent::Kind::SpellCast, false, memberIndex, command._target, 0, command._id );

        if ( pSpell->_kind == JrpgSpellKind::Damage )
        {
            // 주문은 지능, 초식(비급)은 힘으로 키운다.
            const bool            bTechnique = pSpell->_manualId.empty() == false;
            const int32           scale      = bTechnique ? _pParty->computeAttack( memberIndex ) : member.getStat( JrpgStat::Intellect );
            const int32           base       = pSpell->_power * ( 100 + scale ) / 100;
            vector<hashed_string> listType;
            if ( pSpell->_damageType.empty() == false )
                listType.push_back( pSpell->_damageType );
            if ( pSpell->_target == JrpgTargetKind::All )
            {
                for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
                {
                    if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                        hitEnemy( memberIndex, enemyIndex, JrpgBattleInternal::applyVariance( base, 900, 1100, _random ), listType );
                }
            }
            else
            {
                const int32 target = findLivingEnemy( command._target );
                if ( target >= 0 )
                    hitEnemy( memberIndex, target, JrpgBattleInternal::applyVariance( base, 900, 1100, _random ), listType );
            }
            if ( bTechnique && pSpell->_proficiencyGain > 0 )
            {
                _pParty->addProficiency( memberIndex, pSpell->_manualId, pSpell->_proficiencyGain );
                pushEvent( JrpgBattleEvent::Kind::ProficiencyGained, false, memberIndex, -1, pSpell->_proficiencyGain, pSpell->_manualId );
            }
            return;
        }

        const int32 firstTarget = pSpell->_target == JrpgTargetKind::All ? 0 : command._target;
        const int32 lastTarget  = pSpell->_target == JrpgTargetKind::All ? _pParty->getMemberCount() - 1 : command._target;
        for ( int32 target = firstTarget; target <= lastTarget; ++target )
        {
            if ( target < 0 || target >= _pParty->getMemberCount() )
                continue;
            JrpgMember& ally = _pParty->getMember( target );
            if ( pSpell->_kind == JrpgSpellKind::Heal && ally.isAlive() )
            {
                const int32 before = ally._hp;
                ally._hp           = MathUtil::min( ally.getStat( JrpgStat::MaxHp ), ally._hp + pSpell->_power + member.getStat( JrpgStat::Intellect ) / 2 );
                pushEvent( JrpgBattleEvent::Kind::Healed, false, memberIndex, target, ally._hp - before, pSpell->_id );
            }
            else if ( pSpell->_kind == JrpgSpellKind::Revive && ally.isAlive() == false )
            {
                ally._hp = MathUtil::max( 1, ally.getStat( JrpgStat::MaxHp ) / 2 );
                pushEvent( JrpgBattleEvent::Kind::Revived, false, memberIndex, target, ally._hp, pSpell->_id );
            }
        }
    }

    void JrpgBattle::executeCombo( int32 memberIndex, const JrpgCommand& command )
    {
        const JrpgComboDef* pCombo = _pCatalog->findCombo( command._id );
        if ( pCombo == nullptr || _comboPoints < pCombo->_points )
        {
            pushEvent( JrpgBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
            return;
        }
        int32 attackSum = 0;
        for ( const hashed_string& participantId : pCombo->_listMemberId )
        {
            const int32 participant = _pParty->findMemberIndex( participantId );
            if ( participant < 0 || _pParty->getMember( participant ).isAlive() == false )
            {
                pushEvent( JrpgBattleEvent::Kind::CannotUse, false, memberIndex, command._target, 0, command._id );
                return;
            }
            attackSum += _pParty->computeAttack( participant );
        }
        _comboPoints -= pCombo->_points;
        pushEvent( JrpgBattleEvent::Kind::ComboUsed, false, memberIndex, command._target, _comboPoints, command._id );
        const int32 base = pCombo->_power + attackSum / 2;
        if ( pCombo->_target == JrpgTargetKind::All )
        {
            for ( int32 enemyIndex = 0; enemyIndex < static_cast<int32>( _listEnemy.size() ); ++enemyIndex )
            {
                if ( _listEnemy[static_cast<size_t>( enemyIndex )].isAlive() )
                    hitEnemy( memberIndex, enemyIndex, JrpgBattleInternal::applyVariance( base, 900, 1100, _random ), pCombo->_listDamageType );
            }
            return;
        }
        const int32 target = findLivingEnemy( command._target );
        if ( target >= 0 )
            hitEnemy( memberIndex, target, JrpgBattleInternal::applyVariance( base, 900, 1100, _random ), pCombo->_listDamageType );
    }

    void JrpgBattle::executeEnemy( int32 enemyIndex )
    {
        if ( enemyIndex < 0 || enemyIndex >= static_cast<int32>( _listEnemy.size() ) )
            return;
        JrpgEnemyState&     enemy  = _listEnemy[static_cast<size_t>( enemyIndex )];
        const JrpgEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyId );
        if ( enemy.isAlive() == false || pEnemy == nullptr )
            return;
        ++enemy._turnsTaken;

        if ( enemy.isCasting() )
        {
            if ( --enemy._castTurnsLeft > 0 )
                return; // 아직 시전 중
            const JrpgSpellDef* pSpell    = _pCatalog->findSpell( pEnemy->_castSpellId );
            const int32         remaining = static_cast<int32>( enemy._listLock.size() );
            const int32         basePower = pSpell != nullptr ? pSpell->_power : 0;
            const int32         power     = basePower * ( remaining + 1 ) / ( enemy._lockTotal + 1 );
            enemy._listLock.clear();
            pushEvent( JrpgBattleEvent::Kind::CastReleased, true, enemyIndex, -1, power, pEnemy->_castSpellId );
            const int32 scaled = power * ( 100 + pEnemy->_arrStat[static_cast<size_t>( JrpgStat::Intellect )] ) / 100;
            for ( int32 memberIndex = 0; memberIndex < _pParty->getMemberCount(); ++memberIndex )
            {
                if ( _pParty->getMember( memberIndex ).isAlive() )
                    hitMember( enemyIndex, memberIndex, JrpgBattleInternal::applyVariance( scaled, 900, 1100, _random ) );
            }
            return;
        }

        if ( pEnemy->_castSpellId.empty() == false && enemy._turnsTaken % pEnemy->_castEvery == 0 )
        {
            enemy._castTurnsLeft = pEnemy->_castTurns;
            enemy._listLock      = pEnemy->_listLock;
            enemy._lockTotal     = static_cast<int32>( enemy._listLock.size() );
            pushEvent( JrpgBattleEvent::Kind::CastStarted, true, enemyIndex, -1, enemy._lockTotal, pEnemy->_castSpellId );
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
        pushEvent( JrpgBattleEvent::Kind::Attack, true, enemyIndex, target );
        hitMember( enemyIndex, target, computePhysicalDamage( pEnemy->_arrStat[static_cast<size_t>( JrpgStat::Strength )], _pParty->computeDefense( target ), _random ) );
    }

    void JrpgBattle::hitEnemy( int32 memberIndex, int32 enemyIndex, int32 damage, const vector<hashed_string>& listDamageType )
    {
        JrpgEnemyState&     enemy  = _listEnemy[static_cast<size_t>( enemyIndex )];
        const JrpgEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyId );
        if ( enemy.isAlive() == false )
            return;
        bool bWeak = false;
        if ( pEnemy != nullptr )
        {
            for ( const hashed_string& type : listDamageType )
            {
                for ( const hashed_string& weakness : pEnemy->_listWeakness )
                    bWeak = bWeak || type == weakness;
            }
        }
        const int32 dealt = MathUtil::min( enemy._hp, bWeak ? static_cast<int32>( static_cast<float32>( damage ) * _settings._weaknessMultiplier ) : damage );
        enemy._hp -= MathUtil::max( 0, dealt );
        pushEvent( JrpgBattleEvent::Kind::Damage, false, memberIndex, enemyIndex, dealt );

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
                    pushEvent( JrpgBattleEvent::Kind::LockBroken, false, memberIndex, enemyIndex, static_cast<int32>( enemy._listLock.size() ), type );
                    break;
                }
            }
            if ( enemy._listLock.empty() )
            {
                enemy._castTurnsLeft = 0;
                pushEvent( JrpgBattleEvent::Kind::CastCancelled, false, memberIndex, enemyIndex, 0, pEnemy != nullptr ? pEnemy->_castSpellId : hashed_string{} );
            }
        }
        if ( enemy.isAlive() == false )
        {
            enemy._castTurnsLeft = 0;
            enemy._listLock.clear();
            _turnOrder.removeActor( kEnemyActorBase + enemyIndex );
            pushEvent( JrpgBattleEvent::Kind::Defeated, false, memberIndex, enemyIndex, 0, enemy._enemyId );
        }
    }

    void JrpgBattle::hitMember( int32 enemyIndex, int32 memberIndex, int32 damage )
    {
        JrpgMember& member = _pParty->getMember( memberIndex );
        if ( member.isAlive() == false )
            return;
        int32 reduced = damage;
        if ( _listDefending[static_cast<size_t>( memberIndex )] != SW_FALSE )
            reduced = JrpgBattleInternal::applyReduction( reduced, _settings._defendReduction );
        if ( isTimed( JrpgTimingKind::Block, memberIndex ) )
        {
            pushEvent( JrpgBattleEvent::Kind::TimedBlock, true, enemyIndex, memberIndex );
            reduced = JrpgBattleInternal::applyReduction( reduced, _settings._timedBlockReduction );
        }
        const int32 dealt = MathUtil::min( member._hp, MathUtil::max( 0, reduced ) );
        member._hp -= dealt;
        pushEvent( JrpgBattleEvent::Kind::Damage, true, enemyIndex, memberIndex, dealt );
        addInner( memberIndex, _settings._innerPerHitTaken );
        if ( member.isAlive() == false )
        {
            _turnOrder.removeActor( memberIndex );
            pushEvent( JrpgBattleEvent::Kind::Defeated, true, enemyIndex, memberIndex, 0, member._id );
        }
    }

    void JrpgBattle::addComboPoints( int32 amount )
    {
        if ( amount <= 0 )
            return;
        _comboPoints = MathUtil::min( _settings._comboMax, _comboPoints + amount );
        pushEvent( JrpgBattleEvent::Kind::ComboPoints, false, -1, -1, _comboPoints );
    }

    void JrpgBattle::addInner( int32 memberIndex, int32 amount )
    {
        if ( _settings._bWuxia == false || amount <= 0 )
            return;
        JrpgMember& member = _pParty->getMember( memberIndex );
        member._inner      = MathUtil::min( JrpgParty::kInnerMax, member._inner + amount );
    }

    bool JrpgBattle::isTimed( JrpgTimingKind kind, int32 memberIndex ) const
    {
        float32 offset = 0.0f;
        if ( _pJudge == nullptr || _pTimingInput == nullptr || _pTimingInput->findPressOffset( kind, memberIndex, offset ) == false )
            return false;
        return _pJudge->judge( 0.0f, offset ).isHit();
    }

    int32 JrpgBattle::findLivingEnemy( int32 preferred ) const
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

    void JrpgBattle::finishIfDecided()
    {
        if ( _outcome != JrpgBattleOutcome::Ongoing )
            return;
        if ( _pParty->countAlive() == 0 )
        {
            _outcome = JrpgBattleOutcome::Defeat;
            pushEvent( JrpgBattleEvent::Kind::Defeat, false, -1, -1 );
            return;
        }
        if ( findLivingEnemy( 0 ) >= 0 )
            return;
        for ( const JrpgEnemyState& enemy : _listEnemy )
        {
            const JrpgEnemyDef* pEnemy = _pCatalog->findEnemy( enemy._enemyId );
            if ( pEnemy == nullptr )
                continue;
            _rewardExp += pEnemy->_exp;
            _rewardGold += pEnemy->_gold;
        }
        _outcome          = JrpgBattleOutcome::Victory;
        const int64 share = _pParty->distributeRewards( _rewardExp, _rewardGold );
        pushEvent( JrpgBattleEvent::Kind::Victory, false, -1, -1, static_cast<int32>( share ) );
    }

    void JrpgBattle::pushEvent( JrpgBattleEvent::Kind kind, bool bEnemyActor, int32 actor, int32 target, int32 value, const hashed_string& id )
    {
        JrpgBattleEvent event;
        event._kind        = kind;
        event._bEnemyActor = bEnemyActor;
        event._actor       = actor;
        event._target      = target;
        event._value       = value;
        event._id          = id;
        _eventBuffer.push( event );
    }

    void JrpgBattle::drainEvents( vector<JrpgBattleEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }
} // namespace sw

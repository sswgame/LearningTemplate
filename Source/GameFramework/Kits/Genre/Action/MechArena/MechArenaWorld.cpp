#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"
#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"

namespace sw
{
    namespace
    {
        uint32 mixMechArenaSeed( uint32 seed, int32 pilot, int32 slot )
        {
            return GameHash::mix32( seed ^ GameHash::mix32( static_cast<uint32>( pilot ) * 0x9E3779B1u + static_cast<uint32>( slot ) ) );
        }

        /** @brief 기체 정의에서 기반 `Vitality` 설정을 만듭니다(범위는 `Vitality::initialize` 가 본다). */
        VitalitySettings makeMechVitality( const MechDef& mech )
        {
            VitalitySettings vitality;
            vitality._maxHealth          = mech._maxHealth;
            vitality._poiseMax           = mech._downMax;
            vitality._poiseRegenDelay    = mech._downRecoveryDelay;
            vitality._poiseRegenRate     = mech._downRecovery;
            vitality._poiseBreakDuration = mech._downTime;
            vitality._bDownedEnabled     = SW_FALSE;
            return vitality;
        }

        /** @brief 기체 정의에서 부스트(과열형 `ResourceGauge`) 설정을 만듭니다(범위는 `ResourceGauge::initialize` 가 본다). */
        ResourceGaugeSettings makeMechBoost( const MechDef& mech )
        {
            ResourceGaugeSettings boost;
            boost._max                  = mech._boostMax;
            boost._regenRate            = mech._boostRegen;
            boost._regenDelay           = mech._boostRegenDelay;
            boost._drainPerSecond       = mech._hoverPerSecond;
            boost._overheatCooldown     = mech._overheatPenalty;
            boost._overheatRecoverLevel = mech._overheatRecover;
            boost._bOverheatMode        = SW_TRUE;
            return boost;
        }
    } // namespace

    const MechModeDef* MechPilot::getMode() const
    {
        const MechDef* pMech = getMech();
        if ( pMech == nullptr || pMech->_listMode.empty() )
            return nullptr;
        return &pMech->_listMode[static_cast<size_t>( MathUtil::clamp( _mode, 0, static_cast<int32>( pMech->_listMode.size() ) - 1 ) )];
    }

    MechArenaWorld::MechArenaWorld()
        : _settings{}
        , _listPilot{}
        , _listProjectile{}
        , _listTeamName{}
        , _eventBuffer{}
        , _listVitalityScratch{}
        , _listMatchScratch{}
        , _listCandidateScratch{}
        , _match{}
        , _timer{}
        , _pMechCatalog{ nullptr }
        , _pWeaponCatalog{ nullptr }
        , _pMoveCatalog{ nullptr }
        , _tick{ 0 }
        , _bStarted{ SW_FALSE }
        , _bEndReported{ SW_FALSE }
    {
    }

    bool MechArenaWorld::initialize( const MechArenaSettings& settings, const MechCatalog* pMechCatalog, const WeaponCatalog* pWeaponCatalog,
                                     const MoveCatalog* pMoveCatalog )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "MechArenaWorld: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings       = settings;
        _pMechCatalog   = pMechCatalog;
        _pWeaponCatalog = pWeaponCatalog;
        _pMoveCatalog   = pMoveCatalog;
        _listPilot.clear();
        _listProjectile.clear();
        _listTeamName.clear();
        _eventBuffer.clear();
        _match.initialize( MatchSettings{} );
        _timer        = FixedStepTimer( _settings._fixedStep, 0.25f );
        _tick         = 0;
        _bStarted     = SW_FALSE;
        _bEndReported = SW_FALSE;
        return true;
    }

    int32 MechArenaWorld::addTeam( const hashed_string& name )
    {
        _listTeamName.push_back( name );
        return static_cast<int32>( _listTeamName.size() ) - 1;
    }

    int32 MechArenaWorld::addPilot( const MechPilotConfig& config )
    {
        const bool bPilotRejected = _bStarted == SW_TRUE || _pMechCatalog == nullptr || config._listMechID.empty() || config._team < 0 ||
                                    config._team >= static_cast<int32>( _listTeamName.size() );
        if ( bPilotRejected )
            return -1;
        MechPilot pilot;
        int32     deckCost = 0;
        for ( const hashed_string& mechID : config._listMechID )
        {
            const MechDef* pMech = _pMechCatalog->findMech( mechID );
            if ( pMech == nullptr || pMech->_listMode.empty() )
                return -1;
            // 기체를 바꿀 때마다 체력과 부스트를 다시 짜므로(equipMech) 덱의 모든 기체가 지금 범위 안이어야 한다(거절 사유는 각 initialize 가 남긴다).
            Vitality      probeVitality;
            ResourceGauge probeBoost;
            if ( probeVitality.initialize( makeMechVitality( *pMech ) ) == false || probeBoost.initialize( makeMechBoost( *pMech ) ) == false )
                return -1;
            deckCost += pMech->_cost;
            pilot._listDeckMech.push_back( pMech );
        }
        if ( _pMechCatalog->getDeckCostLimit() > 0 && deckCost > _pMechCatalog->getDeckCostLimit() )
            return -1;
        for ( const hashed_string& skillID : config._listSkillID )
        {
            const MechSkillDef* pSkill = _pMechCatalog->findSkill( skillID );
            if ( pSkill != nullptr )
                pilot._listSkill.push_back( pSkill );
        }
        pilot._listSkillRemaining.resize( pilot._listSkill.size() );
        pilot._listSkillCooldown.resize( pilot._listSkill.size() );
        pilot._listSkillSpent.resize( pilot._listSkill.size(), SW_FALSE );
        pilot._team          = config._team;
        pilot._spawnPosition = config._spawnPosition;
        pilot._position      = config._spawnPosition;
        _listPilot.push_back( pilot );
        const int32 index = static_cast<int32>( _listPilot.size() ) - 1;
        if ( equipMech( _listPilot.back(), index ) == false )
        {
            _listPilot.pop_back();
            return -1;
        }
        return index;
    }

    void MechArenaWorld::start()
    {
        if ( _bStarted == SW_TRUE )
            return;
        _match.initialize( makeMatchSettings() );
        for ( size_t team = 0; team < _listTeamName.size(); ++team )
        {
            int32 gauge = _settings._teamGauge;
            if ( gauge <= 0 )
            {
                int32 costSum = 0;
                for ( const MechPilot& pilot : _listPilot )
                {
                    costSum += pilot._team == static_cast<int32>( team ) ? pilot._listDeckMech.front()->_cost : 0;
                }
                gauge = static_cast<int32>( MathUtil::round( static_cast<float32>( costSum ) * _settings._gaugeScale ) );
            }
            (void)_match.addTeam( _listTeamName[team], gauge );
        }
        for ( MechPilot& pilot : _listPilot )
        {
            pilot._listParticipant.clear();
            for ( const MechDef* pMech : pilot._listDeckMech )
            {
                pilot._listParticipant.push_back( _match.addParticipant( pilot._team, pMech->_id, pMech->_cost ) );
            }
        }
        _match.start();
        _bStarted = SW_TRUE;
        for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
        {
            spawnPilot( pilot, false );
        }
    }

    void MechArenaWorld::update( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( _timer.getStep() );
        }
    }

    void MechArenaWorld::setInput( int32 pilot, const MechInput& input )
    {
        if ( isValidPilot( pilot ) )
            _listPilot[static_cast<size_t>( pilot )]._input = input;
    }

    MechSwapResult MechArenaWorld::requestMechSwap( int32 pilot, int32 deckIndex )
    {
        if ( isValidPilot( pilot ) == false )
            return MechSwapResult::InvalidSlot;
        MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        if ( target._state != MechPilotState::Destroyed )
            return MechSwapResult::NotDestroyed;
        if ( deckIndex < 0 || deckIndex >= static_cast<int32>( target._listDeckMech.size() ) )
            return MechSwapResult::InvalidSlot;
        const MatchTeam* pTeam         = _match.findTeam( target._team );
        const bool       bSwapOverCost = pTeam != nullptr && pTeam->_bIgnoreCost == SW_FALSE && target._listDeckMech[static_cast<size_t>( deckIndex )]->_cost > pTeam->_costPool;
        if ( bSwapOverCost )
            return MechSwapResult::OverCost;
        target._deckIndex = deckIndex;
        pushEvent( MechArenaEvent::Kind::MechSwapped, pilot, -1, static_cast<float32>( deckIndex ), target._listDeckMech[static_cast<size_t>( deckIndex )]->_id );
        return MechSwapResult::Ok;
    }

    void MechArenaWorld::applyDamage( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 staggerTime )
    {
        resolveHit( attacker, victim, damage, downValue, 0.0f, staggerTime, false, MechWeaponKind::Shot );
    }

    void MechArenaWorld::teleport( int32 pilot, const float3& position )
    {
        if ( isValidPilot( pilot ) == false )
            return;
        MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        target._position  = position;
        target._velocity  = float3{};
        target._bGrounded = position._y <= 0.0f ? SW_TRUE : SW_FALSE;
    }

    int32 MechArenaWorld::getTeamGauge( int32 team ) const
    {
        const MatchTeam* pTeam = _match.findTeam( team );
        if ( pTeam == nullptr )
            return 0;
        return pTeam->_bIgnoreCost == SW_TRUE ? -1 : pTeam->_costPool;
    }

    float32 MechArenaWorld::computeModifier( int32 pilot, const hashed_string& name ) const
    {
        if ( isValidPilot( pilot ) == false )
            return 1.0f;
        const MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
        float32          scale  = 1.0f;
        const int32      count  = countActiveSkills( target );
        for ( int32 index = 0; index < count; ++index )
        {
            if ( target._listSkillRemaining[static_cast<size_t>( index )].isActive() )
                scale *= target._listSkill[static_cast<size_t>( index )]->_modifier.getScale( name );
        }
        return scale;
    }

    void MechArenaWorld::drainEvents( vector<MechArenaEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    void MechArenaWorld::step( float32 deltaTime )
    {
        ++_tick;
        if ( isEnded() == false )
        {
            for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
            {
                stepPilot( pilot, deltaTime );
            }
            stepProjectiles( deltaTime );
        }
        stepMatch( deltaTime );
    }

    void MechArenaWorld::stepMatch( float32 deltaTime )
    {
        _match.update( deltaTime );
        _listMatchScratch.clear();
        _match.drainEvents( _listMatchScratch );
        for ( const MatchEvent& event : _listMatchScratch )
        {
            if ( event._kind == MatchEvent::Kind::Respawned )
            {
                for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
                {
                    const MechPilot& target = _listPilot[static_cast<size_t>( pilot )];
                    const bool       bWaitingOnThis =
                        target._state == MechPilotState::Destroyed && target._deathSlot >= 0 && target._listParticipant[static_cast<size_t>( target._deathSlot )] == event._participant;
                    if ( bWaitingOnThis )
                        spawnPilot( pilot, true );
                }
            }
        }
        if ( isEnded() && _bEndReported == SW_FALSE )
        {
            _bEndReported = SW_TRUE;
            for ( MechPilot& pilot : _listPilot )
            {
                pilot._state = MechPilotState::Retired;
                pilot._melee.cancel();
                pilot._comboStage = -1;
            }
            _listProjectile.clear();
            pushEvent( MechArenaEvent::Kind::MatchEnded, -1, _match.getWinningTeam(), 0.0f, hashed_string{} );
        }
    }

    bool MechArenaWorld::equipMech( MechPilot& pilot, int32 pilotIndex )
    {
        const MechDef* pMech = pilot.getMech();
        if ( pMech == nullptr )
            return true;
        if ( pilot._vitality.initialize( makeMechVitality( *pMech ) ) == false )
            return false;

        if ( pilot._boost.initialize( makeMechBoost( *pMech ) ) == false )
            return false;
        pilot._bWasOverheated = SW_FALSE;

        LockOnSettings lockOn;
        lockOn._maxDistance   = pMech->_lockOnRange;
        lockOn._breakDistance = pMech->_lockOnRange * 1.2f;
        lockOn._maxAngle      = MathUtil::toDegree( pMech->_lockOnAngle ); // 기반 LockOnSelector 는 도 단위다
        pilot._lockOn.setSettings( lockOn );
        pilot._lockOn.release();

        const int32 slotCount = pMech->computeSlotCount();
        pilot._listWeaponState.clear();
        pilot._listWeaponState.resize( static_cast<size_t>( slotCount ) );
        pilot._listSpecialCooldown.assign( static_cast<size_t>( slotCount ), Countdown{} );
        int32 slotIndex = 0;
        for ( const MechModeDef& mode : pMech->_listMode )
        {
            for ( const MechWeaponSlotDef& slot : mode._listWeapon )
            {
                const WeaponDef* pWeapon = _pWeaponCatalog != nullptr && slot._kind == MechWeaponKind::Shot ? _pWeaponCatalog->find( slot._weaponID ) : nullptr;
                if ( pWeapon != nullptr )
                    pilot._listWeaponState[static_cast<size_t>( slotIndex )].equip( *pWeapon, pWeapon->_maxReserveAmmo,
                                                                                    mixMechArenaSeed( _settings._seed, pilotIndex, slotIndex ) );
                ++slotIndex;
            }
        }
        pilot._mode = 0;
        for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
        {
            pilot._listSkillRemaining[index].clear();
            pilot._listSkillSpent[index] = SW_FALSE;
        }
        return true;
    }

    void MechArenaWorld::spawnPilot( int32 pilotIndex, bool bRespawn )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( equipMech( pilot, pilotIndex ) == false )
        {
            // 덱의 모든 기체를 `addPilot` 이 범위 안인지 봤으므로 여기에 닿지 않는다. 닿으면 그 조종사는 출격시키지 않는다(옛 기체로 대신 뜨지 않는다).
            pilot._state = MechPilotState::Retired;
            return;
        }
        pilot._position = pilot._spawnPosition;
        pilot._velocity = float3{};
        pilot._forward  = float3{ 0.0f, 0.0f, 1.0f };
        pilot._dashRemaining.clear();
        pilot._staggerRemaining.clear();
        pilot._transformCooldown.clear();
        pilot._comboStage   = -1;
        pilot._meleeSlot    = -1;
        pilot._deathSlot    = -1;
        pilot._bMeleeQueued = SW_FALSE;
        pilot._bGrounded    = pilot._position._y <= 0.0f ? SW_TRUE : SW_FALSE;
        pilot._melee.cancel();
        pilot._state = MechPilotState::Active;
        if ( bRespawn == false )
            return;
        pilot._vitality.setInvulnerable( _settings._respawnInvulnerable );
        pushEvent( MechArenaEvent::Kind::Respawned, pilotIndex, -1, static_cast<float32>( pilot._deckIndex ), pilot.getMech()->_id );
        triggerSkills( pilotIndex, MechSkillTrigger::OnRespawn );
    }

    // --- 조회 도우미 ------------------------------------------------------------------------------

    void MechArenaWorld::collectCandidates( int32 pilotIndex, vector<LockOnCandidate>& outListCandidate ) const
    {
        outListCandidate.clear();
        const int32 team = _listPilot[static_cast<size_t>( pilotIndex )]._team;
        for ( int32 other = 0; other < getPilotCount(); ++other )
        {
            const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
            if ( isEnemyTarget( team, other ) == false )
                continue;
            LockOnCandidate entry;
            entry._position = computeCenter( candidate );
            entry._id       = static_cast<uint64>( other + 1 );
            outListCandidate.push_back( entry );
        }
    }

    int32 MechArenaWorld::findLockTarget( int32 pilotIndex )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( pilot._lockOn.hasTarget() == false )
        {
            // 록온 없이 쏘면 앞쪽의 가장 좋은 대상을 잡는다(캡슐파이터의 자동 록온).
            collectCandidates( pilotIndex, _listCandidateScratch );
            (void)pilot._lockOn.pickBest( computeCenter( pilot ), pilot._forward, _listCandidateScratch );
        }
        if ( pilot._lockOn.hasTarget() == false )
            return -1;
        const int32 target = static_cast<int32>( pilot._lockOn.getTarget() ) - 1;
        if ( isTargetable( target ) == false )
        {
            pilot._lockOn.release();
            return -1;
        }
        return target;
    }

    int32 MechArenaWorld::countActiveSkills( const MechPilot& pilot ) const
    {
        const MechDef* pMech = pilot.getMech();
        return pMech != nullptr ? MathUtil::min( static_cast<int32>( pilot._listSkill.size() ), pMech->_skillSlots ) : 0;
    }

    int32 MechArenaWorld::findSlot( const MechPilot& pilot, MechWeaponKind kind ) const
    {
        const MechModeDef* pMode = pilot.getMode();
        if ( pMode == nullptr )
            return -1;
        const int32 offset = pilot.getMech()->computeSlotOffset( pilot._mode );
        for ( size_t index = 0; index < pMode->_listWeapon.size(); ++index )
        {
            if ( pMode->_listWeapon[index]._kind == kind )
                return offset + static_cast<int32>( index );
        }
        return -1;
    }

    bool MechArenaWorld::isTargetable( int32 pilot ) const
    {
        if ( isValidPilot( pilot ) == false )
            return false;
        const MechPilotState state = _listPilot[static_cast<size_t>( pilot )]._state;
        return state == MechPilotState::Active || state == MechPilotState::Down;
    }

    bool MechArenaWorld::isEnemyTarget( int32 team, int32 other ) const
    {
        return isTargetable( other ) && TeamAttitudeUtil::isHostile( team, _listPilot[static_cast<size_t>( other )]._team );
    }

    float3 MechArenaWorld::computeCenter( const MechPilot& pilot ) const
    {
        const MechDef* pMech = pilot.getMech();
        return pilot._position + float3{ 0.0f, pMech != nullptr ? pMech->_radius : 1.0f, 0.0f };
    }

    void MechArenaWorld::pushEvent( MechArenaEvent::Kind kind, int32 pilot, int32 other, float32 value, const hashed_string& id )
    {
        MechArenaEvent event;
        event._kind  = kind;
        event._pilot = pilot;
        event._other = other;
        event._value = value;
        event._id    = id;
        _eventBuffer.push( event );
    }
} // namespace sw

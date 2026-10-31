#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

#include "Core/Math/DeterministicMathUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Net/NetMessageChannel.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
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
            return HashUtil::mix32( seed ^ HashUtil::mix32( static_cast<uint32>( pilot ) * 0x9E3779B1u + static_cast<uint32>( slot ) ) );
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

        struct MechArenaWorldInternal
        {
            static constexpr float32 kTiny = 1.0e-5f;

            /** @brief XZ 평면에서 길이 1로 만듭니다. 너무 짧으면 @p fallback 을 반환합니다. */
            static float3 flattenDirection( const float3& direction, const float3& fallback )
            {
                const float3  flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                return length > kTiny ? flat / length : fallback;
            }

            static float3 normalizeOr( const float3& direction, const float3& fallback )
            {
                const float32 length = direction.getLength();
                return length > kTiny ? direction / length : fallback;
            }

            static const hashed_string& getMeleeName()
            {
                static const hashed_string name( "melee" );
                return name;
            }

            static const hashed_string& getShotName()
            {
                static const hashed_string name( "shot" );
                return name;
            }

            static const hashed_string& getDownName()
            {
                static const hashed_string name( "down" );
                return name;
            }
            static const hashed_string& getAttackName()
            {
                static const hashed_string name( "attack" );
                return name;
            }
            static const hashed_string& getDefenseName()
            {
                static const hashed_string name( "defense" );
                return name;
            }
            static const hashed_string& getDownResistName()
            {
                static const hashed_string name( "downResist" );
                return name;
            }

            static void writeInput( Archive& outArchive, const MechInput& input )
            {
                outArchive << input._moveX;
                outArchive << input._moveZ;
                outArchive << input._skillSlot;
                outArchive << input._bDash;
                outArchive << input._bJump;
                outArchive << input._bHover;
                outArchive << input._bFire;
                outArchive << input._bMelee;
                outArchive << input._bSpecial;
                outArchive << input._bTransform;
                outArchive << input._bLockOn;
            }

            [[nodiscard]] static bool readInput( Archive& archive, MechInput& outInput )
            {
                archive >> outInput._moveX;
                archive >> outInput._moveZ;
                archive >> outInput._skillSlot;
                archive >> outInput._bDash;
                archive >> outInput._bJump;
                archive >> outInput._bHover;
                archive >> outInput._bFire;
                archive >> outInput._bMelee;
                archive >> outInput._bSpecial;
                archive >> outInput._bTransform;
                archive >> outInput._bLockOn;
                const uint8 combined = static_cast<uint8>( outInput._bDash | outInput._bJump | outInput._bHover | outInput._bFire | outInput._bMelee |
                                                           outInput._bSpecial | outInput._bTransform | outInput._bLockOn );
                return archive.isOk() && combined <= SW_TRUE;
            }

            /**
             * @brief 조종사의 덱(기체 id)과 스킬 id 를 카탈로그에서 찾아 채우고, 스킬 수만큼 타이머 슬롯을 잡습니다.
             * @return 덱이 비었거나 카탈로그에 없는 기체 · 스킬이면 false 입니다.
             */
            [[nodiscard]] static bool readLoadout( Archive& archive, const MechCatalog& catalog, MechPilot& outPilot )
            {
                uint32 deckCount = 0;
                if ( StateArchiveUtil::readCount( archive, 4, deckCount ) == false || deckCount == 0 )
                    return false;
                for ( uint32 index = 0; index < deckCount; ++index )
                {
                    hashed_string mechID;
                    if ( StateArchiveUtil::readName( archive, mechID ) == false )
                        return false;
                    const MechDef* pMech = catalog.findMech( mechID );
                    if ( pMech == nullptr || pMech->_listMode.empty() )
                        return false;
                    outPilot._listDeckMech.push_back( pMech );
                }
                uint32 skillCount = 0;
                if ( StateArchiveUtil::readCount( archive, 4, skillCount ) == false )
                    return false;
                for ( uint32 index = 0; index < skillCount; ++index )
                {
                    hashed_string skillID;
                    if ( StateArchiveUtil::readName( archive, skillID ) == false )
                        return false;
                    const MechSkillDef* pSkill = catalog.findSkill( skillID );
                    if ( pSkill == nullptr )
                        return false;
                    outPilot._listSkill.push_back( pSkill );
                }
                outPilot._listSkillRemaining.resize( skillCount );
                outPilot._listSkillCooldown.resize( skillCount );
                outPilot._listSkillSpent.resize( skillCount, SW_FALSE );
                return true;
            }

            /** @brief 스킬마다 남은 시간 · 쿨다운 · 쓴 표시를 읽습니다. 슬롯은 `readLoadout` 이 스킬 수만큼 잡아 두었습니다. */
            [[nodiscard]] static bool readSkillTimers( Archive& archive, MechPilot& outPilot )
            {
                for ( size_t index = 0; index < outPilot._listSkill.size(); ++index )
                {
                    const bool bSkillRead = StateArchiveUtil::readCountdown( archive, outPilot._listSkillRemaining[index] ) &&
                                            StateArchiveUtil::readCountdown( archive, outPilot._listSkillCooldown[index] );
                    archive >> outPilot._listSkillSpent[index];
                    const bool bPilotSkillInvalid = bSkillRead == false || archive.isError() || outPilot._listSkillSpent[index] > SW_TRUE;
                    if ( bPilotSkillInvalid )
                        return false;
                }
                return true;
            }

            static void writePilot( Archive& outArchive, const MechPilot& pilot )
            {
                outArchive << static_cast<uint32>( pilot._listDeckMech.size() );
                for ( const MechDef* pMech : pilot._listDeckMech )
                {
                    StateArchiveUtil::writeName( outArchive, pMech->_id );
                }
                outArchive << static_cast<uint32>( pilot._listSkill.size() );
                for ( const MechSkillDef* pSkill : pilot._listSkill )
                {
                    StateArchiveUtil::writeName( outArchive, pSkill->_id );
                }
                outArchive << pilot._spawnPosition;
                outArchive << pilot._team;
                outArchive << pilot._deckIndex;
                outArchive << pilot._deathSlot;
                outArchive << pilot._mode;
                outArchive << pilot._comboStage;
                outArchive << pilot._meleeSlot;
                outArchive << pilot._kills;
                outArchive << pilot._deaths;
                outArchive << static_cast<uint8>( pilot._state );
                outArchive << pilot._bGrounded;
                outArchive << pilot._bMeleeHit;
                outArchive << pilot._bMeleeQueued;
                outArchive << pilot._bWasOverheated;
                outArchive << static_cast<uint32>( pilot._listParticipant.size() );
                for ( const int32 participant : pilot._listParticipant )
                {
                    outArchive << participant;
                }
                outArchive << pilot._position;
                outArchive << pilot._velocity;
                outArchive << pilot._forward;
                outArchive << pilot._dashDirection;
                StateArchiveUtil::writeCountdown( outArchive, pilot._dashRemaining );
                StateArchiveUtil::writeCountdown( outArchive, pilot._staggerRemaining );
                StateArchiveUtil::writeCountdown( outArchive, pilot._transformCooldown );
                for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
                {
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSkillRemaining[index] );
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSkillCooldown[index] );
                    outArchive << pilot._listSkillSpent[index];
                }
                writeInput( outArchive, pilot._input );
                writeInput( outArchive, pilot._previousInput );
                pilot._vitality.writeState( outArchive );
                pilot._boost.writeState( outArchive );
                pilot._lockOn.writeState( outArchive );
                pilot._melee.writeState( outArchive );
                outArchive << static_cast<uint32>( pilot._listWeaponState.size() );
                for ( size_t index = 0; index < pilot._listWeaponState.size(); ++index )
                {
                    pilot._listWeaponState[index].writeState( outArchive );
                    StateArchiveUtil::writeCountdown( outArchive, pilot._listSpecialCooldown[index] );
                }
            }
            static constexpr float32 kTiny = 1.0e-5f;

            /** @brief XZ 평면에서 길이 1로 만듭니다. 너무 짧으면 @p fallback 을 반환합니다. */
            static float3 flattenDirection( const float3& direction, const float3& fallback )
            {
                const float3  flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                return length > kTiny ? flat / length : fallback;
            }

            static float3 normalizeOr( const float3& direction, const float3& fallback )
            {
                const float32 length = direction.getLength();
                return length > kTiny ? direction / length : fallback;
            }

            /** @brief @p direction 을 @p desired 쪽으로 최대 @p maxRadian 만큼 돌립니다(구면 보간). 둘 다 단위 벡터여야 합니다. */
            static float3 rotateToward( const float3& direction, const float3& desired, float32 maxRadian )
            {
                const float32 cosAngle = MathUtil::clamp( direction.dot( desired ), -1.0f, 1.0f );
                // 결정적 시뮬레이션 TU 라 libm 대신 `DeterministicMathUtil` 이다(`sw_markDeterministicSources`).
                const float32 angle = DeterministicMathUtil::acos( cosAngle );
                if ( angle <= maxRadian || angle < kTiny )
                    return desired;
                const float32 sinAngle = DeterministicMathUtil::sin( angle );
                if ( sinAngle < kTiny )
                    return direction; // 정반대 방향이라 돌 축이 정해지지 않는다
                const float32 ratio    = maxRadian / angle;
                const float32 fromPart = DeterministicMathUtil::sin( ( 1.0f - ratio ) * angle ) / sinAngle;
                const float32 toPart   = DeterministicMathUtil::sin( ratio * angle ) / sinAngle;
                return normalizeOr( direction * fromPart + desired * toPart, direction );
            }

            static bool isPressed( uint8 current, uint8 previous ) { return current == SW_TRUE && previous == SW_FALSE; }

            static const hashed_string& getBoostRegenName()
            {
                static const hashed_string name( "boostRegen" );
                return name;
            }
            static const hashed_string& getSpeedName()
            {
                static const hashed_string name( "speed" );
                return name;
            }
        };
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

    // --- 무기와 피해 ------------------------------------------------------------------------------

    void MechArenaWorld::fireShot( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot, bool bPressed )
    {
        MechPilot&   pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        WeaponState& weapon = pilot._listWeaponState[static_cast<size_t>( slotIndex )];
        if ( weapon.getDef()._id.empty() )
            return;
        const int32  target    = findLockTarget( pilotIndex );
        const float3 origin    = computeCenter( pilot );
        const float3 direction = target >= 0 ? MechArenaWorldInternal::normalizeOr( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin, pilot._forward )
                                             : pilot._forward;
        WeaponShot   shot;
        if ( weapon.pullTrigger( GameRay{ origin, direction }, bPressed, shot ) != WeaponFireResult::Fired )
            return;
        pushEvent( MechArenaEvent::Kind::Fired, pilotIndex, target, 0.0f, slot._id );
        const WeaponDef& def = weapon.getDef();
        for ( const GameRay& ray : shot._listRay )
        {
            launch( pilotIndex, target, ray._direction, def._projectileSpeed, def._range, def._damage, slot );
        }
    }

    void MechArenaWorld::fireSpecial( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot )
    {
        MechPilot& pilot    = _listPilot[static_cast<size_t>( pilotIndex )];
        Countdown& cooldown = pilot._listSpecialCooldown[static_cast<size_t>( slotIndex )];
        if ( cooldown.isActive() )
            return;
        cooldown.start( slot._cooldown );
        const int32  target    = findLockTarget( pilotIndex );
        const float3 origin    = computeCenter( pilot );
        const float3 direction = target >= 0 ? MechArenaWorldInternal::normalizeOr( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin, pilot._forward )
                                             : pilot._forward;
        pushEvent( MechArenaEvent::Kind::Fired, pilotIndex, target, 0.0f, slot._id );
        launch( pilotIndex, target, direction, slot._projectileSpeed, slot._range, slot._damage, slot );
    }

    void MechArenaWorld::launch( int32 pilotIndex, int32 target, const float3& direction, float32 speed, float32 range, float32 damage,
                                 const MechWeaponSlotDef& slot )
    {
        const MechPilot& pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        const float3     origin = computeCenter( pilot );
        if ( speed <= 0.0f )
        {
            // 즉시 탄(광선)은 사거리 안에서 가장 가까운 적을 맞힌다.
            const GameRay ray{ origin, direction };
            int32         hitPilot    = -1;
            float32       hitDistance = range;
            for ( int32 other = 0; other < getPilotCount(); ++other )
            {
                const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
                float32          distance  = 0.0f;
                if ( isEnemyTarget( pilot._team, other ) == false )
                    continue;
                if ( RayMath::intersectSphere( ray, computeCenter( candidate ), candidate.getMech()->_radius, hitDistance, distance ) && distance <= hitDistance )
                {
                    hitDistance = distance;
                    hitPilot    = other;
                }
            }
            if ( hitPilot >= 0 )
                resolveHit( pilotIndex, hitPilot, damage, slot._downValue, slot._knockback, slot._staggerTime, false, slot._kind );
            return;
        }
        MechProjectile projectile;
        projectile._position    = origin;
        projectile._direction   = direction;
        projectile._speed       = speed;
        projectile._homing      = slot._homing;
        projectile._rangeLeft   = range;
        projectile._damage      = damage;
        projectile._downValue   = slot._downValue;
        projectile._knockback   = slot._knockback;
        projectile._staggerTime = slot._staggerTime;
        projectile._owner       = pilotIndex;
        projectile._target      = target;
        projectile._kind        = slot._kind;
        _listProjectile.push_back( projectile );
    }

    void MechArenaWorld::resolveHit( int32 attacker, int32 victim, float32 damage, float32 downValue, float32 knockback, float32 staggerTime, bool bKnockdown,
                                     MechWeaponKind kind )
    {
        using Internal = MechArenaWorldInternal;
        if ( isTargetable( victim ) == false || _pMechCatalog == nullptr )
            return;
        MechPilot&     target      = _listPilot[static_cast<size_t>( victim )];
        const MechDef* pVictimMech = target.getMech();
        const bool     bAttacker   = isValidPilot( attacker );
        float32        scale       = 1.0f;
        if ( bAttacker )
        {
            const MechDef*       pAttackerMech = _listPilot[static_cast<size_t>( attacker )].getMech();
            const hashed_string& kindName      = kind == MechWeaponKind::Melee ? Internal::getMeleeName() : Internal::getShotName();
            scale                              = _pMechCatalog->getClassModifier( pAttackerMech->_rangeClass ).getScale( kindName );
            scale *= computeModifier( attacker, MechArenaWorldInternal::getAttackName() );
        }
        const float32 finalDamage = damage * scale / MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldInternal::getDefenseName() ) );
        float32       finalDown   = downValue * _pMechCatalog->getClassModifier( pVictimMech->_rangeClass ).getScale( Internal::getDownName() ) /
                            MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldInternal::getDownResistName() ) );
        if ( bKnockdown )
            finalDown = MathUtil::max( finalDown, pVictimMech->_downMax ); // 눕히는 기술은 다운치를 한 번에 채운다

        const VitalityDamageResult result = target._vitality.applyDamage( finalDamage, finalDown, attacker );
        _listVitalityScratch.clear();
        target._vitality.drainEvents( _listVitalityScratch );
        if ( result._bIgnored == SW_TRUE )
            return;
        const int32 victimParticipant   = target._listParticipant.empty() ? -1 : target._listParticipant[static_cast<size_t>( target._deckIndex )];
        const int32 attackerParticipant = bAttacker && _listPilot[static_cast<size_t>( attacker )]._listParticipant.empty() == false
                                            ? _listPilot[static_cast<size_t>( attacker )]._listParticipant[static_cast<size_t>( _listPilot[static_cast<size_t>( attacker )]._deckIndex )]
                                            : -1;
        _match.reportDamage( attackerParticipant, victimParticipant, result._healthDamage );
        pushEvent( MechArenaEvent::Kind::Hit, victim, attacker, result._healthDamage, hashed_string{} );

        if ( result._bDied == SW_TRUE )
        {
            target._state        = MechPilotState::Destroyed;
            target._deathSlot    = target._deckIndex;
            target._comboStage   = -1;
            target._meleeSlot    = -1;
            target._bMeleeQueued = SW_FALSE;
            target._velocity     = float3{};
            target._dashRemaining.clear();
            target._staggerRemaining.clear();
            target._melee.cancel();
            target._lockOn.release();
            for ( Countdown& remaining : target._listSkillRemaining )
            {
                remaining.clear();
            }
            ++target._deaths;
            if ( bAttacker && TeamAttitudeUtil::isHostile( _listPilot[static_cast<size_t>( attacker )]._team, target._team ) )
                ++_listPilot[static_cast<size_t>( attacker )]._kills;
            pushEvent( MechArenaEvent::Kind::Destroyed, victim, attacker, 0.0f, pVictimMech->_id );
            _match.reportKill( victimParticipant, attackerParticipant );
            return;
        }

        if ( target._state == MechPilotState::Active )
        {
            if ( staggerTime > 0.0f )
            {
                // 경직: 하던 근접과 대시가 끊긴다.
                target._staggerRemaining.extendTo( staggerTime );
                target._dashRemaining.clear();
                target._melee.cancel();
                target._comboStage   = -1;
                target._meleeSlot    = -1;
                target._bMeleeQueued = SW_FALSE;
            }
            if ( knockback > 0.0f )
            {
                const float3 away = bAttacker ? Internal::flattenDirection( target._position - _listPilot[static_cast<size_t>( attacker )]._position, -target._forward )
                                              : -target._forward;
                target._position += away * knockback;
                target._position._x = MathUtil::clamp( target._position._x, -_settings._arenaHalfSize, _settings._arenaHalfSize );
                target._position._z = MathUtil::clamp( target._position._z, -_settings._arenaHalfSize, _settings._arenaHalfSize );
            }
            if ( result._bPoiseBroken == SW_TRUE )
            {
                target._state = MechPilotState::Down;
                target._dashRemaining.clear();
                target._melee.cancel();
                target._comboStage   = -1;
                target._meleeSlot    = -1;
                target._bMeleeQueued = SW_FALSE;
                pushEvent( MechArenaEvent::Kind::Downed, victim, attacker, 0.0f, hashed_string{} );
                triggerSkills( victim, MechSkillTrigger::OnDown );
            }
        }
        triggerSkills( victim, MechSkillTrigger::HealthBelow );
    }

    // --- 스킬과 장비 ------------------------------------------------------------------------------

    void MechArenaWorld::activateSkill( int32 pilotIndex, int32 skillIndex )
    {
        MechPilot&          pilot             = _listPilot[static_cast<size_t>( pilotIndex )];
        const size_t        index             = static_cast<size_t>( skillIndex );
        const MechSkillDef* pSkill            = pilot._listSkill[index];
        const bool          bSkillUnavailable = pilot._listSkillRemaining[index].isActive() || pilot._listSkillCooldown[index].isActive() || pSkill->_duration <= 0.0f;
        if ( bSkillUnavailable )
            return;
        pilot._listSkillRemaining[index].start( pSkill->_duration );
        pilot._listSkillCooldown[index].start( pSkill->_cooldown );
        pilot._listSkillSpent[index] = SW_TRUE;
        pushEvent( MechArenaEvent::Kind::SkillStarted, pilotIndex, -1, pSkill->_duration, pSkill->_id );
    }

    void MechArenaWorld::triggerSkills( int32 pilotIndex, MechSkillTrigger trigger )
    {
        MechPilot&  pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const int32 count = countActiveSkills( pilot );
        for ( int32 index = 0; index < count; ++index )
        {
            const MechSkillDef* pSkill = pilot._listSkill[static_cast<size_t>( index )];
            if ( pSkill->_trigger != trigger )
                continue;
            if ( trigger == MechSkillTrigger::HealthBelow )
            {
                const bool bBelow = pilot._vitality.isAlive() && pilot._vitality.getHealthRatio() < pSkill->_threshold;
                if ( bBelow == false || pilot._listSkillSpent[static_cast<size_t>( index )] == SW_TRUE )
                    continue;
            }
            activateSkill( pilotIndex, index );
        }
    }

    // --- 직렬화 -----------------------------------------------------------------------------------

    void MechArenaWorld::makeSnapshot( MechArenaSnapshot& outSnapshot ) const
    {
        outSnapshot                = MechArenaSnapshot{};
        outSnapshot._tick          = _tick;
        outSnapshot._phase         = _match.getPhase();
        outSnapshot._winningTeam   = _match.getWinningTeam();
        outSnapshot._remainingTime = _match.getRemaining();
        outSnapshot._arenaHalfSize = _settings._arenaHalfSize;
        outSnapshot._ceiling       = _settings._ceiling;
        for ( int32 team = 0; team < static_cast<int32>( _listTeamName.size() ); ++team )
        {
            outSnapshot._listTeamGauge.push_back( getTeamGauge( team ) );
        }
        for ( int32 index = 0; index < getPilotCount(); ++index )
        {
            const MechPilot&  pilot = _listPilot[static_cast<size_t>( index )];
            const MechDef*    pMech = pilot.getMech();
            MechPilotSnapshot entry;
            entry._position      = pilot._position;
            entry._health        = pilot._vitality.getHealth();
            entry._boostHeat     = pilot._boost.getRatio();
            entry._downRatio     = pMech->_downMax > 0.0f ? MathUtil::saturate( 1.0f - pilot._vitality.getPoise() / pMech->_downMax ) : 0.0f;
            entry._team          = pilot._team;
            entry._mechIndex     = _pMechCatalog != nullptr ? _pMechCatalog->findMechIndex( pMech->_id ) : -1;
            entry._mode          = pilot._mode;
            entry._lockTarget    = pilot._lockOn.hasTarget() ? static_cast<int32>( pilot._lockOn.getTarget() ) - 1 : -1;
            entry._comboStage    = pilot._comboStage;
            entry._state         = pilot._state;
            entry._bOverheated   = pilot._boost.isOverheated() ? SW_TRUE : SW_FALSE;
            entry._bInvulnerable = pilot._vitality.isInvulnerable() ? SW_TRUE : SW_FALSE;
            const int32 shot     = findSlot( pilot, MechWeaponKind::Shot );
            if ( shot >= 0 )
            {
                entry._magazineAmmo = pilot._listWeaponState[static_cast<size_t>( shot )].getMagazineAmmo();
                entry._bReloading   = pilot._listWeaponState[static_cast<size_t>( shot )].isReloading() ? SW_TRUE : SW_FALSE;
            }
            const int32 skillCount = MathUtil::min( countActiveSkills( pilot ), 32 );
            for ( int32 skill = 0; skill < skillCount; ++skill )
            {
                entry._activeSkillMask |= pilot._listSkillRemaining[static_cast<size_t>( skill )].isActive() ? ( 1u << static_cast<uint32>( skill ) ) : 0u;
            }
            outSnapshot._listPilot.push_back( entry );
        }
        for ( const MechProjectile& projectile : _listProjectile )
        {
            MechProjectileSnapshot entry;
            entry._position = projectile._position;
            entry._owner    = projectile._owner;
            outSnapshot._listProjectile.push_back( entry );
        }
    }

    bool MechArenaWorld::registerSnapshotMessage( NetMessageChannel& inoutChannel, uint8 kind )
    {
        return inoutChannel.registerMessage<MechArenaSnapshot>( kind );
    }

    bool MechArenaWorld::writeSnapshotMessage( const NetMessageChannel& channel, uint8 kind, NetMessageWriter& outWriter ) const
    {
        MechArenaSnapshot snapshot;
        makeSnapshot( snapshot );
        return channel.writeMessage( kind, snapshot, outWriter );
    }

    void MechArenaWorld::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listTeamName.size() );
        for ( const hashed_string& name : _listTeamName )
        {
            StateArchiveUtil::writeName( outArchive, name );
        }
        outArchive << _bStarted;
        outArchive << _bEndReported;
        outArchive << _tick;
        StateArchiveUtil::writeStepTimer( outArchive, _timer );
        _match.writeState( outArchive );
        outArchive << static_cast<uint32>( _listPilot.size() );
        for ( const MechPilot& pilot : _listPilot )
        {
            MechArenaWorldInternal::writePilot( outArchive, pilot );
        }
        outArchive << static_cast<uint32>( _listProjectile.size() );
        for ( const MechProjectile& projectile : _listProjectile )
        {
            outArchive << projectile._position;
            outArchive << projectile._direction;
            outArchive << projectile._speed;
            outArchive << projectile._homing;
            outArchive << projectile._rangeLeft;
            outArchive << projectile._damage;
            outArchive << projectile._downValue;
            outArchive << projectile._knockback;
            outArchive << projectile._staggerTime;
            outArchive << projectile._owner;
            outArchive << projectile._target;
            outArchive << static_cast<uint8>( projectile._kind );
        }
    }

    bool MechArenaWorld::readState( Archive& archive )
    {
        if ( _pMechCatalog == nullptr || _pMoveCatalog == nullptr )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다. 설정과 카탈로그는 사본이 그대로 가지고 있다.
        MechArenaWorld restored  = *this;
        uint32         teamCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, teamCount ) == false )
            return false;
        restored._listTeamName.assign( teamCount, hashed_string{} );
        for ( hashed_string& name : restored._listTeamName )
        {
            if ( StateArchiveUtil::readName( archive, name ) == false )
                return false;
        }
        archive >> restored._bStarted;
        archive >> restored._bEndReported;
        archive >> restored._tick;
        const bool bHeadValid = archive.isOk() && restored._bStarted <= SW_TRUE && restored._bEndReported <= SW_TRUE;
        if ( bHeadValid == false || StateArchiveUtil::readStepTimer( archive, restored._timer ) == false )
            return false;
        restored._match.initialize( restored._bStarted == SW_TRUE ? makeMatchSettings() : MatchSettings{} );
        if ( restored._match.readState( archive ) == false )
            return false;

        uint32 pilotCount = 0;
        // 조종사마다 적어도 덱 개수(4) + 기체 이름(4) + 스킬 개수(4) + 위치, 정수, 상태 슬롯(60) 바이트
        if ( StateArchiveUtil::readCount( archive, 72, pilotCount ) == false )
            return false;
        restored._listPilot.assign( pilotCount, MechPilot{} );
        for ( uint32 index = 0; index < pilotCount; ++index )
        {
            if ( restored.readPilot( archive, static_cast<int32>( index ), restored._listPilot[index] ) == false )
                return false;
        }

        uint32 projectileCount = 0;
        // 탄마다 위치(12) + 방향(12) + 실수 7개(28) + 쏜 쪽과 대상(8) + 종류(1) 바이트
        if ( StateArchiveUtil::readCount( archive, 61, projectileCount ) == false )
            return false;
        restored._listProjectile.assign( projectileCount, MechProjectile{} );
        for ( MechProjectile& projectile : restored._listProjectile )
        {
            uint8 kind = 0;
            archive >> projectile._position;
            archive >> projectile._direction;
            archive >> projectile._speed;
            archive >> projectile._homing;
            archive >> projectile._rangeLeft;
            archive >> projectile._damage;
            archive >> projectile._downValue;
            archive >> projectile._knockback;
            archive >> projectile._staggerTime;
            archive >> projectile._owner;
            archive >> projectile._target;
            archive >> kind;
            const bool bValid = archive.isOk() && -1 <= projectile._owner && projectile._owner < static_cast<int32>( pilotCount ) && -1 <= projectile._target &&
                                projectile._target < static_cast<int32>( pilotCount ) && kind <= static_cast<uint8>( MechWeaponKind::Special );
            if ( bValid == false )
                return false;
            projectile._kind = static_cast<MechWeaponKind>( kind );
        }
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    MatchSettings MechArenaWorld::makeMatchSettings() const
    {
        MatchSettings matchSettings;
        matchSettings._respawnDelay = _settings._respawnDelay;
        matchSettings._timeLimit    = _settings._timeLimit;
        matchSettings._scorePerKill = 1;
        return matchSettings;
    }

    bool MechArenaWorld::readPilot( Archive& archive, int32 pilotIndex, MechPilot& outPilot )
    {
        if ( MechArenaWorldInternal::readLoadout( archive, *_pMechCatalog, outPilot ) == false )
            return false;
        const uint32 deckCount = static_cast<uint32>( outPilot._listDeckMech.size() );

        int32 mode           = 0;
        uint8 state          = 0;
        uint8 bWasOverheated = SW_FALSE;
        archive >> outPilot._spawnPosition;
        archive >> outPilot._team;
        archive >> outPilot._deckIndex;
        archive >> outPilot._deathSlot;
        archive >> mode;
        archive >> outPilot._comboStage;
        archive >> outPilot._meleeSlot;
        archive >> outPilot._kills;
        archive >> outPilot._deaths;
        archive >> state;
        archive >> outPilot._bGrounded;
        archive >> outPilot._bMeleeHit;
        archive >> outPilot._bMeleeQueued;
        archive >> bWasOverheated;
        const int32 deckSize   = static_cast<int32>( deckCount );
        const uint8 flags      = static_cast<uint8>( outPilot._bGrounded | outPilot._bMeleeHit | outPilot._bMeleeQueued | bWasOverheated );
        const bool  bHeadValid = archive.isOk() && 0 <= outPilot._team && outPilot._team < static_cast<int32>( _listTeamName.size() ) && 0 <= outPilot._deckIndex &&
                                outPilot._deckIndex < deckSize && -1 <= outPilot._deathSlot && outPilot._deathSlot < deckSize && -1 <= outPilot._comboStage &&
                                state <= static_cast<uint8>( MechPilotState::Retired ) && flags <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        outPilot._state = static_cast<MechPilotState>( state );

        // 무기, 체력, 부스트 설정은 지금 장착한 기체가 정한다. 격추된 동안은 격추된 슬롯의 기체를 쓴다(교체한 슬롯은 부활할 때 장착된다).
        const int32 deckIndex = outPilot._deckIndex;
        outPilot._deckIndex   = outPilot._deathSlot >= 0 ? outPilot._deathSlot : deckIndex;
        const MechDef* pMech  = outPilot.getMech();
        if ( equipMech( outPilot, pilotIndex ) == false )
            return false;
        outPilot._deckIndex      = deckIndex;
        outPilot._bWasOverheated = bWasOverheated;
        outPilot._mode           = mode;
        if ( mode < 0 || mode >= static_cast<int32>( pMech->_listMode.size() ) )
            return false;

        uint32 participantCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, participantCount ) == false || ( participantCount != 0 && participantCount != deckCount ) )
            return false;
        outPilot._listParticipant.assign( participantCount, -1 );
        for ( int32& participant : outPilot._listParticipant )
        {
            archive >> participant;
        }
        archive >> outPilot._position;
        archive >> outPilot._velocity;
        archive >> outPilot._forward;
        archive >> outPilot._dashDirection;
        const bool bTimerRead = StateArchiveUtil::readCountdown( archive, outPilot._dashRemaining ) &&
                                StateArchiveUtil::readCountdown( archive, outPilot._staggerRemaining ) &&
                                StateArchiveUtil::readCountdown( archive, outPilot._transformCooldown );
        if ( bTimerRead == false || MechArenaWorldInternal::readSkillTimers( archive, outPilot ) == false )
            return false;
        const bool bBodyRead = MechArenaWorldInternal::readInput( archive, outPilot._input ) && MechArenaWorldInternal::readInput( archive, outPilot._previousInput ) &&
                               outPilot._vitality.readState( archive ) && outPilot._boost.readState( archive ) && outPilot._lockOn.readState( archive ) &&
                               outPilot._melee.readState( archive, *_pMoveCatalog );
        if ( bBodyRead == false )
            return false;

        uint32 weaponCount = 0;
        archive >> weaponCount;
        const bool bWeaponStateInvalid = archive.isError() || weaponCount != static_cast<uint32>( outPilot._listWeaponState.size() ) || outPilot._meleeSlot >= static_cast<int32>( weaponCount ) ||
                                         outPilot._meleeSlot < -1;
        if ( bWeaponStateInvalid )
            return false;
        for ( uint32 index = 0; index < weaponCount; ++index )
        {
            if ( outPilot._listWeaponState[index].readState( archive ) == false ||
                 StateArchiveUtil::readCountdown( archive, outPilot._listSpecialCooldown[index] ) == false )
                return false;
        }
        return true;
    }

    void MechArenaWorld::stepPilot( int32 pilotIndex, float32 deltaTime )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( pilot._state != MechPilotState::Active && pilot._state != MechPilotState::Down )
        {
            pilot._previousInput = pilot._input;
            return;
        }
        const MechInput& input    = pilot._input;
        const MechInput& previous = pilot._previousInput;
        const MechDef*   pMech    = pilot.getMech();

        pilot._vitality.update( deltaTime );
        _listVitalityScratch.clear();
        pilot._vitality.drainEvents( _listVitalityScratch );
        if ( pilot._state == MechPilotState::Down && pilot._vitality.isPoiseBroken() == false )
        {
            // 기상: 다운치는 가득 찬 채로 일어나고 잠시 무적이다.
            pilot._state = MechPilotState::Active;
            pilot._vitality.setInvulnerable( pMech->_wakeInvulnerable );
            pushEvent( MechArenaEvent::Kind::WokeUp, pilotIndex, -1, pMech->_wakeInvulnerable, hashed_string{} );
        }
        pilot._staggerRemaining.update( deltaTime );
        pilot._transformCooldown.update( deltaTime );
        for ( WeaponState& weapon : pilot._listWeaponState )
        {
            weapon.update( deltaTime );
        }
        for ( Countdown& cooldown : pilot._listSpecialCooldown )
        {
            cooldown.update( deltaTime );
        }
        stepSkills( pilotIndex, deltaTime );

        bool bControl = false;
        if ( pilot._state == MechPilotState::Active )
        {
            stepLockOn( pilotIndex, MechArenaWorldInternal::isPressed( input._bLockOn, previous._bLockOn ), deltaTime );
            bControl = pilot.canAct();
            if ( bControl )
            {
                const bool bPressedTransform = MechArenaWorldInternal::isPressed( input._bTransform, previous._bTransform );
                const bool bCanTransform     = bPressedTransform && pilot._transformCooldown.isActive() == false && pilot._comboStage < 0 && pMech->_listMode.size() > 1;
                if ( bCanTransform )
                {
                    pilot._mode = ( pilot._mode + 1 ) % static_cast<int32>( pMech->_listMode.size() );
                    pilot._transformCooldown.start( pMech->_transformTime );
                    pilot._dashRemaining.clear();
                    pushEvent( MechArenaEvent::Kind::Transformed, pilotIndex, -1, static_cast<float32>( pilot._mode ), pilot.getMode()->_id );
                }
                stepMelee( pilotIndex, MechArenaWorldInternal::isPressed( input._bMelee, previous._bMelee ) );
                if ( pilot._comboStage < 0 )
                    stepWeapons( pilotIndex, deltaTime );
            }
        }
        stepMovement( pilotIndex, bControl, MechArenaWorldInternal::isPressed( input._bDash, previous._bDash ),
                      MechArenaWorldInternal::isPressed( input._bJump, previous._bJump ), deltaTime );

        pilot._boost.setRegenScale( computeModifier( pilotIndex, MechArenaWorldInternal::getBoostRegenName() ) );
        pilot._boost.update( deltaTime );
        const bool bOverheated = pilot._boost.isOverheated();
        if ( bOverheated && pilot._bWasOverheated == SW_FALSE )
            pushEvent( MechArenaEvent::Kind::Overheated, pilotIndex, -1, 0.0f, hashed_string{} );
        pilot._bWasOverheated = bOverheated ? SW_TRUE : SW_FALSE;
        pilot._previousInput  = input;
    }

    void MechArenaWorld::stepMovement( int32 pilotIndex, bool bControl, bool bPressedDash, bool bPressedJump, float32 deltaTime )
    {
        using Internal             = MechArenaWorldInternal;
        MechPilot&         pilot   = _listPilot[static_cast<size_t>( pilotIndex )];
        const MechDef*     pMech   = pilot.getMech();
        const MechModeDef* pMode   = pilot.getMode();
        const bool         bRooted = pilot._comboStage >= 0;
        float3             horizontal{};
        bool               bHovering = false;
        if ( bControl && bRooted == false )
        {
            const float32 costScale = pMode->_boostCostScale;
            float3        move{ pilot._input._moveX, 0.0f, pilot._input._moveZ };
            const float32 moveLength = move.getLength();
            if ( moveLength > 1.0f )
                move = move / moveLength;
            if ( bPressedDash && pilot._boost.trySpend( pMech->_dashCost * costScale ) )
            {
                pilot._dashRemaining.start( pMech->_dashTime );
                pilot._dashDirection = Internal::flattenDirection( move, pilot._forward );
            }
            const bool bCanJump = bPressedJump && pilot._bGrounded == SW_TRUE && pilot._boost.trySpend( pMech->_jumpCost * costScale );
            if ( bCanJump )
            {
                pilot._velocity._y = pMech->_jumpSpeed;
                pilot._bGrounded   = SW_FALSE;
            }
            if ( pilot._dashRemaining.isActive() )
            {
                horizontal = pilot._dashDirection * pMech->_dashSpeed;
                pilot._dashRemaining.update( deltaTime );
            }
            else
            {
                horizontal = move * ( pMode->_speed * computeModifier( pilotIndex, MechArenaWorldInternal::getSpeedName() ) );
            }
            if ( pilot._lockOn.hasTarget() == false && moveLength > Internal::kTiny )
                pilot._forward = Internal::flattenDirection( move, pilot._forward );
            const bool bWantsHover = pilot._input._bHover == SW_TRUE && pilot._bGrounded == SW_FALSE && pilot._velocity._y <= 0.0f;
            if ( bWantsHover && pilot._boost.drain( pMech->_hoverPerSecond * costScale, deltaTime ) )
                bHovering = true;
        }
        else
        {
            pilot._dashRemaining.clear();
        }

        pilot._velocity._x = horizontal._x;
        pilot._velocity._z = horizontal._z;
        if ( bHovering )
            pilot._velocity._y = 0.0f;
        else if ( pilot._bGrounded == SW_FALSE )
            pilot._velocity._y -= _settings._gravity * deltaTime;
        pilot._position += pilot._velocity * deltaTime;
        pilot._position._x = MathUtil::clamp( pilot._position._x, -_settings._arenaHalfSize, _settings._arenaHalfSize );
        pilot._position._z = MathUtil::clamp( pilot._position._z, -_settings._arenaHalfSize, _settings._arenaHalfSize );
        if ( pilot._position._y >= _settings._ceiling )
        {
            pilot._position._y = _settings._ceiling;
            pilot._velocity._y = MathUtil::min( 0.0f, pilot._velocity._y );
        }
        if ( pilot._position._y <= 0.0f )
        {
            pilot._position._y = 0.0f;
            pilot._velocity._y = 0.0f;
            pilot._bGrounded   = SW_TRUE;
        }
        else
        {
            pilot._bGrounded = SW_FALSE;
        }
    }

    void MechArenaWorld::stepLockOn( int32 pilotIndex, bool bPressedLockOn, float32 deltaTime )
    {
        MechPilot&   pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const float3 eye   = computeCenter( pilot );
        collectCandidates( pilotIndex, _listCandidateScratch );
        if ( bPressedLockOn )
        {
            if ( pilot._lockOn.hasTarget() )
                (void)pilot._lockOn.cycle( eye, pilot._forward, _listCandidateScratch, 1 );
            else
                (void)pilot._lockOn.pickBest( eye, pilot._forward, _listCandidateScratch );
        }
        (void)pilot._lockOn.update( eye, _listCandidateScratch, deltaTime );
        if ( pilot._lockOn.hasTarget() )
        {
            const MechPilot& target = _listPilot[static_cast<size_t>( pilot._lockOn.getTarget() - 1 )];
            pilot._forward          = MechArenaWorldInternal::flattenDirection( target._position - pilot._position, pilot._forward );
        }
    }

    void MechArenaWorld::stepWeapons( int32 pilotIndex, float32 deltaTime )
    {
        (void)deltaTime;
        MechPilot&         pilot   = _listPilot[static_cast<size_t>( pilotIndex )];
        const MechModeDef* pMode   = pilot.getMode();
        const int32        offset  = pilot.getMech()->computeSlotOffset( pilot._mode );
        const int32        shot    = findSlot( pilot, MechWeaponKind::Shot );
        const int32        special = findSlot( pilot, MechWeaponKind::Special );
        if ( shot >= 0 && pilot._input._bFire == SW_TRUE )
            fireShot( pilotIndex, shot, pMode->_listWeapon[static_cast<size_t>( shot - offset )],
                      MechArenaWorldInternal::isPressed( pilot._input._bFire, pilot._previousInput._bFire ) );
        if ( special >= 0 && MechArenaWorldInternal::isPressed( pilot._input._bSpecial, pilot._previousInput._bSpecial ) )
            fireSpecial( pilotIndex, special, pMode->_listWeapon[static_cast<size_t>( special - offset )] );
    }

    void MechArenaWorld::stepMelee( int32 pilotIndex, bool bPressedMelee )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        if ( _pMoveCatalog == nullptr )
            return;
        if ( pilot._comboStage < 0 )
        {
            const int32 slotIndex = findSlot( pilot, MechWeaponKind::Melee );
            if ( bPressedMelee == false || slotIndex < 0 )
                return;
            const MechWeaponSlotDef& slot  = pilot.getMode()->_listWeapon[static_cast<size_t>( slotIndex - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
            const MoveFrameData*     pMove = slot._listMoveID.empty() ? nullptr : _pMoveCatalog->find( slot._listMoveID.front() );
            if ( pMove == nullptr )
                return;
            (void)findLockTarget( pilotIndex ); // 고르는 부수 효과만 쓴다 — 대상이 없으면 앞으로 친다
            if ( pilot._melee.start( *pMove ) == false )
                return;
            pilot._comboStage   = 0;
            pilot._meleeSlot    = slotIndex;
            pilot._bMeleeHit    = SW_FALSE;
            pilot._bMeleeQueued = SW_FALSE;
            pushEvent( MechArenaEvent::Kind::ComboAdvanced, pilotIndex, -1, 0.0f, pMove->_id );
        }
        else
        {
            const MechWeaponSlotDef& slot = pilot.getMode()->_listWeapon[static_cast<size_t>( pilot._meleeSlot - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
            if ( bPressedMelee )
                pilot._bMeleeQueued = SW_TRUE;
            const int32          nextStage          = pilot._comboStage + 1;
            const bool           bHasNext           = nextStage < static_cast<int32>( slot._listMoveID.size() );
            const MoveFrameData* pNext              = bHasNext ? _pMoveCatalog->find( slot._listMoveID[static_cast<size_t>( nextStage )] ) : nullptr;
            const bool           bCanCancelIntoNext = pilot._bMeleeQueued == SW_TRUE && pNext != nullptr && pilot._melee.canCancelInto( pNext->_id );
            if ( bCanCancelIntoNext )
            {
                // 캔슬 창이 열려서 다음 단이 이 프레임에 나간다.
                if ( pilot._melee.start( *pNext ) == false )
                    return;
                pilot._comboStage   = nextStage;
                pilot._bMeleeHit    = SW_FALSE;
                pilot._bMeleeQueued = SW_FALSE;
                pushEvent( MechArenaEvent::Kind::ComboAdvanced, pilotIndex, -1, static_cast<float32>( nextStage ), pNext->_id );
            }
            else
            {
                (void)pilot._melee.advanceFrame();
                if ( pilot._melee.getPhase() == MovePhase::Finished )
                {
                    pilot._melee.cancel();
                    pilot._comboStage   = -1;
                    pilot._meleeSlot    = -1;
                    pilot._bMeleeQueued = SW_FALSE;
                    return;
                }
            }
        }

        if ( pilot._melee.getPhase() != MovePhase::Active || pilot._bMeleeHit == SW_TRUE )
            return;
        const MechWeaponSlotDef& slot   = pilot.getMode()->_listWeapon[static_cast<size_t>( pilot._meleeSlot - pilot.getMech()->computeSlotOffset( pilot._mode ) )];
        const MechDef*           pMech  = pilot.getMech();
        int32                    target = -1;
        float32                  best   = MathUtil::kMaxFloat;
        for ( int32 other = 0; other < getPilotCount(); ++other )
        {
            const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
            if ( isEnemyTarget( pilot._team, other ) == false )
                continue;
            const float32 reach    = slot._range + pMech->_radius + candidate.getMech()->_radius;
            const float32 distance = float3::getDistance( computeCenter( pilot ), computeCenter( candidate ) );
            // 록온 대상이 닿으면 그쪽을, 아니면 가장 가까운 적을 고른다.
            const bool    bLocked = pilot._lockOn.getTarget() == static_cast<uint64>( other + 1 );
            const float32 score   = bLocked ? -1.0f : distance;
            if ( distance <= reach && score < best )
            {
                best   = score;
                target = other;
            }
        }
        if ( target < 0 )
            return;
        const MoveFrameData& move = pilot._melee.getMove();
        pilot._melee.registerContact( false );
        pilot._bMeleeHit = SW_TRUE;
        resolveHit( pilotIndex, target, move._damage, slot._downValue, slot._knockback, static_cast<float32>( move._hitstun ) * _settings._fixedStep,
                    move._bKnockdown == SW_TRUE, MechWeaponKind::Melee );
    }

    void MechArenaWorld::stepSkills( int32 pilotIndex, float32 deltaTime )
    {
        MechPilot&  pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        const int32 count = countActiveSkills( pilot );
        for ( int32 index = 0; index < count; ++index )
        {
            Countdown& remaining = pilot._listSkillRemaining[static_cast<size_t>( index )];
            pilot._listSkillCooldown[static_cast<size_t>( index )].update( deltaTime );
            if ( remaining.update( deltaTime ) )
                pushEvent( MechArenaEvent::Kind::SkillEnded, pilotIndex, -1, 0.0f, pilot._listSkill[static_cast<size_t>( index )]->_id );
        }
        const int32 slot                       = pilot._input._skillSlot;
        const bool  bPressedManual             = slot >= 0 && slot < count && slot != pilot._previousInput._skillSlot;
        const bool  bShouldActivateManualSkill = bPressedManual && pilot.canAct() && pilot._listSkill[static_cast<size_t>( slot )]->_trigger == MechSkillTrigger::Manual;
        if ( bShouldActivateManualSkill )
            activateSkill( pilotIndex, slot );
    }

    void MechArenaWorld::stepProjectiles( float32 deltaTime )
    {
        using Internal    = MechArenaWorldInternal;
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < _listProjectile.size(); ++readIndex )
        {
            MechProjectile projectile = _listProjectile[readIndex];
            const bool     bCanHome   = projectile._target >= 0 && projectile._homing > 0.0f && isTargetable( projectile._target );
            if ( bCanHome )
            {
                const float3 targetCenter = computeCenter( _listPilot[static_cast<size_t>( projectile._target )] );
                const float3 desired      = Internal::normalizeOr( targetCenter - projectile._position, projectile._direction );
                projectile._direction     = Internal::rotateToward( projectile._direction, desired, MathUtil::toRadian( projectile._homing ) * deltaTime );
            }
            const float32 stepLength = MathUtil::min( projectile._speed * deltaTime, projectile._rangeLeft );
            const int32   ownerTeam  = isValidPilot( projectile._owner ) ? _listPilot[static_cast<size_t>( projectile._owner )]._team : TeamAttitudeUtil::kNoTeam;
            const GameRay ray{ projectile._position, projectile._direction };
            int32         hitPilot    = -1;
            float32       hitDistance = stepLength;
            for ( int32 other = 0; other < getPilotCount(); ++other )
            {
                const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
                float32          distance  = 0.0f;
                if ( isEnemyTarget( ownerTeam, other ) == false )
                    continue;
                if ( RayMath::intersectSphere( ray, computeCenter( candidate ), candidate.getMech()->_radius, hitDistance, distance ) && distance <= hitDistance )
                {
                    hitDistance = distance;
                    hitPilot    = other;
                }
            }
            if ( hitPilot >= 0 )
            {
                resolveHit( projectile._owner, hitPilot, projectile._damage, projectile._downValue, projectile._knockback, projectile._staggerTime, false,
                            projectile._kind );
                continue;
            }
            projectile._position += projectile._direction * stepLength;
            projectile._rangeLeft -= stepLength;
            const bool bOutside = MathUtil::abs( projectile._position._x ) > _settings._arenaHalfSize ||
                                  MathUtil::abs( projectile._position._z ) > _settings._arenaHalfSize || projectile._position._y < 0.0f ||
                                  projectile._position._y > _settings._ceiling;
            if ( projectile._rangeLeft <= 0.0f || bOutside )
                continue;
            _listProjectile[writeIndex++] = projectile;
        }
        _listProjectile.resize( writeIndex );
    }
} // namespace sw

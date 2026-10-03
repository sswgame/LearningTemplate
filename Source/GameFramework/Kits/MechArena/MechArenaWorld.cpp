#include "pch.h"

#include "GameFramework/Kits/MechArena/MechArenaWorld.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/GameRandom.h"
#include "GameFramework/Base/RayMath.h"
#include "GameFramework/Kits/MechArena/MechArenaSnapshot.h"

namespace sw
{
    namespace
    {
        struct MechArenaWorldInternal
        {
            static constexpr float32 kTiny = 1.0e-5f;

            /** @brief XZ 평면에서 길이 1 로 만듭니다. 너무 짧으면 @p fallback 입니다. */
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

            /** @brief @p direction 을 @p desired 쪽으로 최대 @p maxRadian 만큼 돌립니다(구면 보간). 둘 다 단위 벡터입니다. */
            static float3 rotateToward( const float3& direction, const float3& desired, float32 maxRadian )
            {
                const float32 cosAngle = MathUtil::clamp( direction.dot( desired ), -1.0f, 1.0f );
                const float32 angle    = MathUtil::acos( cosAngle );
                if ( angle <= maxRadian || angle < kTiny )
                    return desired;
                const float32 sinAngle = MathUtil::sin( angle );
                if ( sinAngle < kTiny )
                    return direction; // 정반대 — 돌 축이 정해지지 않는다
                const float32 ratio    = maxRadian / angle;
                const float32 fromPart = MathUtil::sin( ( 1.0f - ratio ) * angle ) / sinAngle;
                const float32 toPart   = MathUtil::sin( ratio * angle ) / sinAngle;
                return normalizeOr( direction * fromPart + desired * toPart, direction );
            }

            static uint32 mixSeed( uint32 seed, int32 pilot, int32 slot )
            {
                return GameHash::mix32( seed ^ GameHash::mix32( static_cast<uint32>( pilot ) * 0x9E3779B1u + static_cast<uint32>( slot ) ) );
            }

            static bool isPressed( uint8 current, uint8 previous ) { return current == SW_TRUE && previous == SW_FALSE; }

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
        };
    } // namespace
} // namespace sw

namespace sw
{
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
        , _listEvent{}
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

    void MechArenaWorld::initialize( const MechArenaSettings& settings, const MechCatalog* pMechCatalog, const WeaponCatalog* pWeaponCatalog,
                                     const MoveCatalog* pMoveCatalog )
    {
        _settings                = settings;
        _settings._fixedStep     = MathUtil::max( 1.0e-3f, _settings._fixedStep );
        _settings._arenaHalfSize = MathUtil::max( 1.0f, _settings._arenaHalfSize );
        _settings._ceiling       = MathUtil::max( 1.0f, _settings._ceiling );
        _pMechCatalog            = pMechCatalog;
        _pWeaponCatalog          = pWeaponCatalog;
        _pMoveCatalog            = pMoveCatalog;
        _listPilot.clear();
        _listProjectile.clear();
        _listTeamName.clear();
        _listEvent.clear();
        _match.initialize( MatchSettings{} );
        _timer        = FixedStepTimer( _settings._fixedStep, 0.25f );
        _tick         = 0;
        _bStarted     = SW_FALSE;
        _bEndReported = SW_FALSE;
    }

    int32 MechArenaWorld::addTeam( const hashed_string& name )
    {
        _listTeamName.push_back( name );
        return static_cast<int32>( _listTeamName.size() ) - 1;
    }

    int32 MechArenaWorld::addPilot( const MechPilotConfig& config )
    {
        if ( _bStarted == SW_TRUE || _pMechCatalog == nullptr || config._listMechId.empty() || config._team < 0 ||
             config._team >= static_cast<int32>( _listTeamName.size() ) )
            return -1;
        MechPilot pilot;
        int32     deckCost = 0;
        for ( const hashed_string& mechId : config._listMechId )
        {
            const MechDef* pMech = _pMechCatalog->findMech( mechId );
            if ( pMech == nullptr || pMech->_listMode.empty() )
                return -1;
            deckCost += pMech->_cost;
            pilot._listDeckMech.push_back( pMech );
        }
        if ( _pMechCatalog->getDeckCostLimit() > 0 && deckCost > _pMechCatalog->getDeckCostLimit() )
            return -1;
        for ( const hashed_string& skillId : config._listSkillId )
        {
            const MechSkillDef* pSkill = _pMechCatalog->findSkill( skillId );
            if ( pSkill != nullptr )
                pilot._listSkill.push_back( pSkill );
        }
        pilot._listSkillRemaining.resize( pilot._listSkill.size(), 0.0f );
        pilot._listSkillCooldown.resize( pilot._listSkill.size(), 0.0f );
        pilot._listSkillSpent.resize( pilot._listSkill.size(), SW_FALSE );
        pilot._team          = config._team;
        pilot._spawnPosition = config._spawnPosition;
        pilot._position      = config._spawnPosition;
        _listPilot.push_back( pilot );
        const int32 index = static_cast<int32>( _listPilot.size() ) - 1;
        equipMech( _listPilot.back(), index );
        return index;
    }

    void MechArenaWorld::start()
    {
        if ( _bStarted == SW_TRUE )
            return;
        MatchSettings matchSettings;
        matchSettings._respawnDelay = _settings._respawnDelay;
        matchSettings._timeLimit    = _settings._timeLimit;
        matchSettings._scorePerKill = 1;
        _match.initialize( matchSettings );
        for ( size_t team = 0; team < _listTeamName.size(); ++team )
        {
            int32 gauge = _settings._teamGauge;
            if ( gauge <= 0 )
            {
                int32 costSum = 0;
                for ( const MechPilot& pilot : _listPilot )
                    costSum += pilot._team == static_cast<int32>( team ) ? pilot._listDeckMech.front()->_cost : 0;
                gauge = static_cast<int32>( MathUtil::round( static_cast<float32>( costSum ) * _settings._gaugeScale ) );
            }
            (void)_match.addTeam( _listTeamName[team], gauge );
        }
        for ( MechPilot& pilot : _listPilot )
        {
            pilot._listParticipant.clear();
            for ( const MechDef* pMech : pilot._listDeckMech )
                pilot._listParticipant.push_back( _match.addParticipant( pilot._team, pMech->_id, pMech->_cost ) );
        }
        _match.start();
        _bStarted = SW_TRUE;
        for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
            spawnPilot( pilot, false );
    }

    void MechArenaWorld::update( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            step( _timer.getStep() );
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
        const MatchTeam* pTeam = _match.findTeam( target._team );
        if ( pTeam != nullptr && pTeam->_bUnlimitedCost == SW_FALSE && target._listDeckMech[static_cast<size_t>( deckIndex )]->_cost > pTeam->_costPool )
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
        return pTeam->_bUnlimitedCost == SW_TRUE ? -1 : pTeam->_costPool;
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
            if ( target._listSkillRemaining[static_cast<size_t>( index )] > 0.0f )
                scale *= target._listSkill[static_cast<size_t>( index )]->_modifier.getValue( name, 1.0f );
        }
        return scale;
    }

    void MechArenaWorld::drainEvents( vector<MechArenaEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    void MechArenaWorld::step( float32 deltaTime )
    {
        ++_tick;
        if ( isEnded() == false )
        {
            for ( int32 pilot = 0; pilot < getPilotCount(); ++pilot )
                stepPilot( pilot, deltaTime );
            stepProjectiles( deltaTime );
        }
        stepMatch( deltaTime );
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
            // 기상 — 다운치는 가득 찬 채로 일어나고 잠시 무적이다.
            pilot._state = MechPilotState::Active;
            pilot._vitality.setInvulnerable( pMech->_wakeInvulnerable );
            pushEvent( MechArenaEvent::Kind::WokeUp, pilotIndex, -1, pMech->_wakeInvulnerable, hashed_string{} );
        }
        pilot._staggerRemaining  = MathUtil::max( 0.0f, pilot._staggerRemaining - deltaTime );
        pilot._transformCooldown = MathUtil::max( 0.0f, pilot._transformCooldown - deltaTime );
        for ( WeaponState& weapon : pilot._listWeaponState )
            weapon.update( deltaTime );
        for ( float32& cooldown : pilot._listSpecialCooldown )
            cooldown = MathUtil::max( 0.0f, cooldown - deltaTime );
        stepSkills( pilotIndex, deltaTime );

        bool bControl = false;
        if ( pilot._state == MechPilotState::Active )
        {
            stepLockOn( pilotIndex, MechArenaWorldInternal::isPressed( input._bLockOn, previous._bLockOn ), deltaTime );
            bControl = pilot.canAct();
            if ( bControl )
            {
                const bool bPressedTransform = MechArenaWorldInternal::isPressed( input._bTransform, previous._bTransform );
                if ( bPressedTransform && pilot._transformCooldown <= 0.0f && pilot._comboStage < 0 && pMech->_listMode.size() > 1 )
                {
                    pilot._mode              = ( pilot._mode + 1 ) % static_cast<int32>( pMech->_listMode.size() );
                    pilot._transformCooldown = pMech->_transformTime;
                    pilot._dashRemaining     = 0.0f;
                    pushEvent( MechArenaEvent::Kind::Transformed, pilotIndex, -1, static_cast<float32>( pilot._mode ), pilot.getMode()->_id );
                }
                stepMelee( pilotIndex, MechArenaWorldInternal::isPressed( input._bMelee, previous._bMelee ) );
                if ( pilot._comboStage < 0 )
                    stepWeapons( pilotIndex, deltaTime );
            }
        }
        stepMovement( pilotIndex, bControl, MechArenaWorldInternal::isPressed( input._bDash, previous._bDash ),
                      MechArenaWorldInternal::isPressed( input._bJump, previous._bJump ), deltaTime );

        pilot._boost.update( deltaTime * computeModifier( pilotIndex, hashed_string( "boostRegen" ) ) );
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
                pilot._dashRemaining = pMech->_dashTime;
                pilot._dashDirection = Internal::flattenDirection( move, pilot._forward );
            }
            if ( bPressedJump && pilot._bGrounded == SW_TRUE && pilot._boost.trySpend( pMech->_jumpCost * costScale ) )
            {
                pilot._velocity._y = pMech->_jumpSpeed;
                pilot._bGrounded   = SW_FALSE;
            }
            if ( pilot._dashRemaining > 0.0f )
            {
                horizontal = pilot._dashDirection * pMech->_dashSpeed;
                pilot._dashRemaining -= deltaTime;
            }
            else
            {
                horizontal = move * ( pMode->_speed * computeModifier( pilotIndex, hashed_string( "speed" ) ) );
            }
            if ( pilot._lockOn.hasTarget() == false && moveLength > Internal::kTiny )
                pilot._forward = Internal::flattenDirection( move, pilot._forward );
            const bool bWantsHover = pilot._input._bHover == SW_TRUE && pilot._bGrounded == SW_FALSE && pilot._velocity._y <= 0.0f;
            if ( bWantsHover && pilot._boost.drain( pMech->_hoverPerSecond * costScale, deltaTime ) )
                bHovering = true;
        }
        else
        {
            pilot._dashRemaining = 0.0f;
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
            const MoveFrameData*     pMove = slot._listMoveId.empty() ? nullptr : _pMoveCatalog->findMove( slot._listMoveId.front() );
            if ( pMove == nullptr )
                return;
            (void)findLockTarget( pilotIndex );
            pilot._melee.start( *pMove );
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
            const int32          nextStage = pilot._comboStage + 1;
            const bool           bHasNext  = nextStage < static_cast<int32>( slot._listMoveId.size() );
            const MoveFrameData* pNext     = bHasNext ? _pMoveCatalog->findMove( slot._listMoveId[static_cast<size_t>( nextStage )] ) : nullptr;
            if ( pilot._bMeleeQueued == SW_TRUE && pNext != nullptr && pilot._melee.canCancelInto( pNext->_id ) )
            {
                // 캔슬 창 — 다음 단이 이 프레임에 나간다.
                pilot._melee.start( *pNext );
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
        float32                  best   = MathUtil::MaxFloat;
        for ( int32 other = 0; other < getPilotCount(); ++other )
        {
            const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
            if ( candidate._team == pilot._team || isTargetable( other ) == false )
                continue;
            const float32 reach    = slot._range + pMech->_radius + candidate.getMech()->_radius;
            const float32 distance = float3::getDistance( computeCenter( pilot ), computeCenter( candidate ) );
            // 록온 대상이 닿으면 그쪽, 아니면 가장 가까운 적.
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
            float32& remaining = pilot._listSkillRemaining[static_cast<size_t>( index )];
            float32& cooldown  = pilot._listSkillCooldown[static_cast<size_t>( index )];
            cooldown           = MathUtil::max( 0.0f, cooldown - deltaTime );
            if ( remaining > 0.0f )
            {
                remaining -= deltaTime;
                if ( remaining <= 0.0f )
                {
                    remaining = 0.0f;
                    pushEvent( MechArenaEvent::Kind::SkillEnded, pilotIndex, -1, 0.0f, pilot._listSkill[static_cast<size_t>( index )]->_id );
                }
            }
        }
        const int32 slot           = pilot._input._skillSlot;
        const bool  bPressedManual = slot >= 0 && slot < count && slot != pilot._previousInput._skillSlot;
        if ( bPressedManual && pilot.canAct() && pilot._listSkill[static_cast<size_t>( slot )]->_trigger == MechSkillTrigger::Manual )
            activateSkill( pilotIndex, slot );
    }

    void MechArenaWorld::stepProjectiles( float32 deltaTime )
    {
        using Internal    = MechArenaWorldInternal;
        size_t writeIndex = 0;
        for ( size_t readIndex = 0; readIndex < _listProjectile.size(); ++readIndex )
        {
            MechProjectile projectile = _listProjectile[readIndex];
            if ( projectile._target >= 0 && projectile._homing > 0.0f && isTargetable( projectile._target ) )
            {
                const float3 targetCenter = computeCenter( _listPilot[static_cast<size_t>( projectile._target )] );
                const float3 desired      = Internal::normalizeOr( targetCenter - projectile._position, projectile._direction );
                projectile._direction     = Internal::rotateToward( projectile._direction, desired, MathUtil::toRadian( projectile._homing ) * deltaTime );
            }
            const float32 stepLength = MathUtil::min( projectile._speed * deltaTime, projectile._rangeLeft );
            const int32   ownerTeam  = isValidPilot( projectile._owner ) ? _listPilot[static_cast<size_t>( projectile._owner )]._team : -1;
            const GameRay ray{ projectile._position, projectile._direction };
            int32         hitPilot    = -1;
            float32       hitDistance = stepLength;
            for ( int32 other = 0; other < getPilotCount(); ++other )
            {
                const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
                float32          distance  = 0.0f;
                if ( candidate._team == ownerTeam || isTargetable( other ) == false )
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

    // --- 무기 · 피해 ------------------------------------------------------------------------------

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
            launch( pilotIndex, target, ray._direction, def._projectileSpeed, def._range, def._damage, slot );
    }

    void MechArenaWorld::fireSpecial( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot )
    {
        MechPilot& pilot    = _listPilot[static_cast<size_t>( pilotIndex )];
        float32&   cooldown = pilot._listSpecialCooldown[static_cast<size_t>( slotIndex )];
        if ( cooldown > 0.0f )
            return;
        cooldown               = slot._cooldown;
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
            // 즉시 탄(광선) — 사거리 안의 가장 가까운 적.
            const GameRay ray{ origin, direction };
            int32         hitPilot    = -1;
            float32       hitDistance = range;
            for ( int32 other = 0; other < getPilotCount(); ++other )
            {
                const MechPilot& candidate = _listPilot[static_cast<size_t>( other )];
                float32          distance  = 0.0f;
                if ( candidate._team == pilot._team || isTargetable( other ) == false )
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
            scale                              = _pMechCatalog->getClassModifier( pAttackerMech->_rangeClass ).getValue( kindName, 1.0f );
            scale *= computeModifier( attacker, hashed_string( "attack" ) );
        }
        const float32 finalDamage = damage * scale / MathUtil::max( 0.01f, computeModifier( victim, hashed_string( "defense" ) ) );
        float32       finalDown   = downValue * _pMechCatalog->getClassModifier( pVictimMech->_rangeClass ).getValue( Internal::getDownName(), 1.0f ) /
                            MathUtil::max( 0.01f, computeModifier( victim, hashed_string( "downResist" ) ) );
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
            target._state            = MechPilotState::Destroyed;
            target._deathSlot        = target._deckIndex;
            target._comboStage       = -1;
            target._meleeSlot        = -1;
            target._bMeleeQueued     = SW_FALSE;
            target._velocity         = float3{};
            target._dashRemaining    = 0.0f;
            target._staggerRemaining = 0.0f;
            target._melee.cancel();
            target._lockOn.release();
            for ( float32& remaining : target._listSkillRemaining )
                remaining = 0.0f;
            ++target._deaths;
            if ( bAttacker && _listPilot[static_cast<size_t>( attacker )]._team != target._team )
                ++_listPilot[static_cast<size_t>( attacker )]._kills;
            pushEvent( MechArenaEvent::Kind::Destroyed, victim, attacker, 0.0f, pVictimMech->_id );
            _match.reportKill( victimParticipant, attackerParticipant );
            return;
        }

        if ( target._state == MechPilotState::Active )
        {
            if ( staggerTime > 0.0f )
            {
                // 경직 — 하던 근접 · 대시가 끊긴다.
                target._staggerRemaining = MathUtil::max( target._staggerRemaining, staggerTime );
                target._dashRemaining    = 0.0f;
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
                target._state         = MechPilotState::Down;
                target._dashRemaining = 0.0f;
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

    // --- 스킬 · 장비 ------------------------------------------------------------------------------

    void MechArenaWorld::activateSkill( int32 pilotIndex, int32 skillIndex )
    {
        MechPilot&          pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        const size_t        index  = static_cast<size_t>( skillIndex );
        const MechSkillDef* pSkill = pilot._listSkill[index];
        if ( pilot._listSkillRemaining[index] > 0.0f || pilot._listSkillCooldown[index] > 0.0f || pSkill->_duration <= 0.0f )
            return;
        pilot._listSkillRemaining[index] = pSkill->_duration;
        pilot._listSkillCooldown[index]  = pSkill->_cooldown;
        pilot._listSkillSpent[index]     = SW_TRUE;
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

    void MechArenaWorld::equipMech( MechPilot& pilot, int32 pilotIndex )
    {
        const MechDef* pMech = pilot.getMech();
        if ( pMech == nullptr )
            return;
        VitalitySettings vitality;
        vitality._maxHealth          = pMech->_maxHealth;
        vitality._poiseMax           = pMech->_downMax;
        vitality._poiseRegenDelay    = pMech->_downRecoveryDelay;
        vitality._poiseRegenRate     = pMech->_downRecovery;
        vitality._poiseBreakDuration = pMech->_downTime;
        vitality._bDownedEnabled     = SW_FALSE;
        pilot._vitality.initialize( vitality );

        ResourceGaugeSettings boost;
        boost._max                  = pMech->_boostMax;
        boost._regenRate            = pMech->_boostRegen;
        boost._regenDelay           = pMech->_boostRegenDelay;
        boost._drainPerSecond       = pMech->_hoverPerSecond;
        boost._overheatCooldown     = pMech->_overheatPenalty;
        boost._overheatRecoverLevel = pMech->_overheatRecover;
        boost._bOverheatMode        = SW_TRUE;
        pilot._boost.initialize( boost );
        pilot._bWasOverheated = SW_FALSE;

        LockOnSettings lockOn;
        lockOn._maxDistance   = pMech->_lockOnRange;
        lockOn._breakDistance = pMech->_lockOnRange * 1.2f;
        lockOn._maxAngle      = pMech->_lockOnAngle;
        pilot._lockOn.setSettings( lockOn );
        pilot._lockOn.release();

        const int32 slotCount = pMech->computeSlotCount();
        pilot._listWeaponState.clear();
        pilot._listWeaponState.resize( static_cast<size_t>( slotCount ) );
        pilot._listSpecialCooldown.assign( static_cast<size_t>( slotCount ), 0.0f );
        int32 slotIndex = 0;
        for ( const MechModeDef& mode : pMech->_listMode )
        {
            for ( const MechWeaponSlotDef& slot : mode._listWeapon )
            {
                const WeaponDef* pWeapon = _pWeaponCatalog != nullptr && slot._kind == MechWeaponKind::Shot ? _pWeaponCatalog->findWeapon( slot._weaponId ) : nullptr;
                if ( pWeapon != nullptr )
                    pilot._listWeaponState[static_cast<size_t>( slotIndex )].equip( *pWeapon, pWeapon->_maxReserveAmmo,
                                                                                    MechArenaWorldInternal::mixSeed( _settings._seed, pilotIndex, slotIndex ) );
                ++slotIndex;
            }
        }
        pilot._mode = 0;
        for ( size_t index = 0; index < pilot._listSkill.size(); ++index )
        {
            pilot._listSkillRemaining[index] = 0.0f;
            pilot._listSkillSpent[index]     = SW_FALSE;
        }
    }

    void MechArenaWorld::spawnPilot( int32 pilotIndex, bool bRespawn )
    {
        MechPilot& pilot = _listPilot[static_cast<size_t>( pilotIndex )];
        equipMech( pilot, pilotIndex );
        pilot._position          = pilot._spawnPosition;
        pilot._velocity          = float3{};
        pilot._forward           = float3{ 0.0f, 0.0f, 1.0f };
        pilot._dashRemaining     = 0.0f;
        pilot._staggerRemaining  = 0.0f;
        pilot._transformCooldown = 0.0f;
        pilot._comboStage        = -1;
        pilot._meleeSlot         = -1;
        pilot._deathSlot         = -1;
        pilot._bMeleeQueued      = SW_FALSE;
        pilot._bGrounded         = pilot._position._y <= 0.0f ? SW_TRUE : SW_FALSE;
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
            if ( candidate._team == team || isTargetable( other ) == false )
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
            // 록온 없이 쏘면 앞의 가장 좋은 대상을 잡는다(캡슐파이터의 자동 록온).
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
        _listEvent.push_back( event );
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
            outSnapshot._listTeamGauge.push_back( getTeamGauge( team ) );
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
                entry._activeSkillMask |= pilot._listSkillRemaining[static_cast<size_t>( skill )] > 0.0f ? ( 1u << static_cast<uint32>( skill ) ) : 0u;
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

    void MechArenaWorld::writeState( BitWriter& outWriter ) const
    {
        MechArenaSnapshot snapshot;
        makeSnapshot( snapshot );
        MechArenaSnapshotCodec::write( snapshot, outWriter );
    }
} // namespace sw

#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
#include "GameFramework/Base/Foundation/Utility/Random/GameRandom.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/Gameplay/Match/TeamAttitude.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaSnapshot.h"
#include "GameFramework/Kits/Genre/Action/MechArena/MechArenaWorld.h"

namespace sw
{
    namespace
    {
        struct MechArenaWorldStepInternal
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

            /** @brief @p direction 을 @p desired 쪽으로 최대 @p maxRadian 만큼 돌립니다(구면 보간). 둘 다 단위 벡터여야 합니다. */
            static float3 rotateToward( const float3& direction, const float3& desired, float32 maxRadian )
            {
                const float32 cosAngle = MathUtil::clamp( direction.dot( desired ), -1.0f, 1.0f );
                const float32 angle    = MathUtil::acos( cosAngle );
                if ( angle <= maxRadian || angle < kTiny )
                    return desired;
                const float32 sinAngle = MathUtil::sin( angle );
                if ( sinAngle < kTiny )
                    return direction; // 정반대 방향이라 돌 축이 정해지지 않는다
                const float32 ratio    = maxRadian / angle;
                const float32 fromPart = MathUtil::sin( ( 1.0f - ratio ) * angle ) / sinAngle;
                const float32 toPart   = MathUtil::sin( ratio * angle ) / sinAngle;
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
            stepLockOn( pilotIndex, MechArenaWorldStepInternal::isPressed( input._bLockOn, previous._bLockOn ), deltaTime );
            bControl = pilot.canAct();
            if ( bControl )
            {
                const bool bPressedTransform = MechArenaWorldStepInternal::isPressed( input._bTransform, previous._bTransform );
                const bool bCanTransform     = bPressedTransform && pilot._transformCooldown.isActive() == false && pilot._comboStage < 0 && pMech->_listMode.size() > 1;
                if ( bCanTransform )
                {
                    pilot._mode = ( pilot._mode + 1 ) % static_cast<int32>( pMech->_listMode.size() );
                    pilot._transformCooldown.start( pMech->_transformTime );
                    pilot._dashRemaining.clear();
                    pushEvent( MechArenaEvent::Kind::Transformed, pilotIndex, -1, static_cast<float32>( pilot._mode ), pilot.getMode()->_id );
                }
                stepMelee( pilotIndex, MechArenaWorldStepInternal::isPressed( input._bMelee, previous._bMelee ) );
                if ( pilot._comboStage < 0 )
                    stepWeapons( pilotIndex, deltaTime );
            }
        }
        stepMovement( pilotIndex, bControl, MechArenaWorldStepInternal::isPressed( input._bDash, previous._bDash ),
                      MechArenaWorldStepInternal::isPressed( input._bJump, previous._bJump ), deltaTime );

        pilot._boost.setRegenScale( computeModifier( pilotIndex, MechArenaWorldStepInternal::getBoostRegenName() ) );
        pilot._boost.update( deltaTime );
        const bool bOverheated = pilot._boost.isOverheated();
        if ( bOverheated && pilot._bWasOverheated == SW_FALSE )
            pushEvent( MechArenaEvent::Kind::Overheated, pilotIndex, -1, 0.0f, hashed_string{} );
        pilot._bWasOverheated = bOverheated ? SW_TRUE : SW_FALSE;
        pilot._previousInput  = input;
    }

    void MechArenaWorld::stepMovement( int32 pilotIndex, bool bControl, bool bPressedDash, bool bPressedJump, float32 deltaTime )
    {
        using Internal             = MechArenaWorldStepInternal;
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
                horizontal = move * ( pMode->_speed * computeModifier( pilotIndex, MechArenaWorldStepInternal::getSpeedName() ) );
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
            pilot._forward          = MechArenaWorldStepInternal::flattenDirection( target._position - pilot._position, pilot._forward );
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
                      MechArenaWorldStepInternal::isPressed( pilot._input._bFire, pilot._previousInput._bFire ) );
        if ( special >= 0 && MechArenaWorldStepInternal::isPressed( pilot._input._bSpecial, pilot._previousInput._bSpecial ) )
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
            const int32          nextStage          = pilot._comboStage + 1;
            const bool           bHasNext           = nextStage < static_cast<int32>( slot._listMoveID.size() );
            const MoveFrameData* pNext              = bHasNext ? _pMoveCatalog->find( slot._listMoveID[static_cast<size_t>( nextStage )] ) : nullptr;
            const bool           bCanCancelIntoNext = pilot._bMeleeQueued == SW_TRUE && pNext != nullptr && pilot._melee.canCancelInto( pNext->_id );
            if ( bCanCancelIntoNext )
            {
                // 캔슬 창이 열려서 다음 단이 이 프레임에 나간다.
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
        using Internal    = MechArenaWorldStepInternal;
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

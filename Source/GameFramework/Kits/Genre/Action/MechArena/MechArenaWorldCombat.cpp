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
        struct MechArenaWorldCombatInternal
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
        };
    } // namespace

    // --- 무기와 피해 ------------------------------------------------------------------------------

    void MechArenaWorld::fireShot( int32 pilotIndex, int32 slotIndex, const MechWeaponSlotDef& slot, bool bPressed )
    {
        MechPilot&   pilot  = _listPilot[static_cast<size_t>( pilotIndex )];
        WeaponState& weapon = pilot._listWeaponState[static_cast<size_t>( slotIndex )];
        if ( weapon.getDef()._id.empty() )
            return;
        const int32  target    = findLockTarget( pilotIndex );
        const float3 origin    = computeCenter( pilot );
        const float3 direction = target >= 0 ? MechArenaWorldCombatInternal::normalizeOr( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin, pilot._forward )
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
        const float3 direction = target >= 0 ? MechArenaWorldCombatInternal::normalizeOr( computeCenter( _listPilot[static_cast<size_t>( target )] ) - origin, pilot._forward )
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
        using Internal = MechArenaWorldCombatInternal;
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
            scale *= computeModifier( attacker, MechArenaWorldCombatInternal::getAttackName() );
        }
        const float32 finalDamage = damage * scale / MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldCombatInternal::getDefenseName() ) );
        float32       finalDown   = downValue * _pMechCatalog->getClassModifier( pVictimMech->_rangeClass ).getScale( Internal::getDownName() ) /
                            MathUtil::max( 0.01f, computeModifier( victim, MechArenaWorldCombatInternal::getDownResistName() ) );
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
} // namespace sw

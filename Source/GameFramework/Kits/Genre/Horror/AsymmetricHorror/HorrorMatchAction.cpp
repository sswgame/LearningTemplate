#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Character/AI/AIPerception.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorSnapshot.h"

namespace sw
{
    namespace
    {
        struct HorrorMatchActionInternal
        {
            static constexpr float32 kTiny = 1.0e-5f;

            /** @brief XZ 평면에서 길이를 1 이하로 자른 방향을 반환합니다. */
            static float3 clampDirection( const float3& direction )
            {
                float3        flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                if ( length > 1.0f )
                    flat = flat / length;
                return flat;
            }

            /** @brief 창틀 건너편을 반환합니다. 창틀을 사이에 두고 마주 보는 위치입니다. */
            static float3 computeVaultExit( const float3& from, const float3& window )
            {
                return float3{ window._x * 2.0f - from._x, 0.0f, window._z * 2.0f - from._z };
            }
        };
    } // namespace

    // --- 생존자 행동 ------------------------------------------------------------------------------

    void HorrorMatch::moveSurvivor( int32 survivor, const float3& direction, float32 deltaTime )
    {
        if ( isValidSurvivor( survivor ) == false || isEnded() )
            return;
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        target._lastMoveSpeed  = 0.0f;
        const bool bCrawling   = target._state == SurvivorState::Dying;
        if ( target.canAct() == false && bCrawling == false )
            return;
        if ( target._activity == SurvivorActivity::InLocker )
            return;
        const float3  move   = HorrorMatchActionInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= HorrorMatchActionInternal::kTiny )
            return;
        leaveActivity( survivor );
        const float32 speed = computeSurvivorSpeed( survivor );
        target._position += move * ( speed * deltaTime );
        target._lastMoveSpeed = length * speed;
    }

    bool HorrorMatch::startRepair( int32 survivor, int32 generator )
    {
        const bool bInvalidRepairRequest = isValidSurvivor( survivor ) == false || generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || isEnded();
        if ( bInvalidRepairRequest )
            return false;
        HorrorSurvivor&  target        = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorGenerator& gen           = _listGenerator[static_cast<size_t>( generator )];
        const bool       bCannotRepair = target.canAct() == false || gen._bBlocked == SW_TRUE || gen._progress.isCompleted() ||
                                   isNear( target._position, gen._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotRepair )
            return false;
        if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == generator )
            return true;
        leaveActivity( survivor );
        if ( gen._bKicked == SW_TRUE && configureGenerator( generator, false ) == false ) // 누가 다시 붙으면 퇴행이 멈춘다
            return false;
        if ( gen._progress.join( static_cast<uint32>( survivor ) ) == false )
            return false;
        target._activity       = SurvivorActivity::Repairing;
        target._activityTarget = generator;
        return true;
    }

    bool HorrorMatch::startHeal( int32 healer, int32 patient )
    {
        const bool bInvalidHealRequest = isValidSurvivor( healer ) == false || isValidSurvivor( patient ) == false || healer == patient || isEnded();
        if ( bInvalidHealRequest )
            return false;
        HorrorSurvivor& doctor      = _listSurvivor[static_cast<size_t>( healer )];
        HorrorSurvivor& target      = _listSurvivor[static_cast<size_t>( patient )];
        const bool      bHurt       = target._state == SurvivorState::Injured || target._state == SurvivorState::Dying;
        const bool      bCannotHeal = doctor.canAct() == false || bHurt == false || isNear( doctor._position, target._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotHeal )
            return false;
        if ( doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient )
            return true;
        leaveActivity( healer );
        if ( target._healing.join( static_cast<uint32>( healer ) ) == false )
            return false;
        if ( target._state == SurvivorState::Dying )
            (void)target._vitality.startRevive( healer, 0.0f ); // 치료받는 동안 출혈이 멈춘다
        doctor._activity       = SurvivorActivity::Healing;
        doctor._activityTarget = patient;
        return true;
    }

    bool HorrorMatch::startOpenGate( int32 survivor, int32 gate )
    {
        const bool bInvalidGateRequest = isValidSurvivor( survivor ) == false || gate < 0 || gate >= static_cast<int32>( _listGate.size() ) || _bGatesPowered == SW_FALSE || isEnded();
        if ( bInvalidGateRequest )
            return false;
        HorrorSurvivor& target          = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorGate&     exit            = _listGate[static_cast<size_t>( gate )];
        const bool      bCannotOpenGate = target.canAct() == false || exit._progress.isCompleted() || isNear( target._position, exit._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotOpenGate )
            return false;
        leaveActivity( survivor );
        if ( exit._progress.join( static_cast<uint32>( survivor ) ) == false )
            return false;
        target._activity       = SurvivorActivity::OpeningGate;
        target._activityTarget = gate;
        return true;
    }

    void HorrorMatch::stopActivity( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false )
            return;
        const SurvivorActivity activity               = _listSurvivor[static_cast<size_t>( survivor )]._activity;
        const bool             bInterruptibleActivity = activity == SurvivorActivity::Repairing || activity == SurvivorActivity::Healing || activity == SurvivorActivity::OpeningGate;
        if ( bInterruptibleActivity )
            leaveActivity( survivor );
    }

    bool HorrorMatch::respondSkillCheck( int32 survivor, float32 pressTime )
    {
        InteractionProgress* pProgress = findActivityProgressMutable( survivor );
        return pProgress != nullptr && pProgress->respondSkillCheck( static_cast<uint32>( survivor ), pressTime );
    }

    bool HorrorMatch::rescue( int32 rescuer, int32 hooked )
    {
        const bool bInvalidRescueRequest = isValidSurvivor( rescuer ) == false || isValidSurvivor( hooked ) == false || rescuer == hooked || isEnded();
        if ( bInvalidRescueRequest )
            return false;
        HorrorSurvivor& helper        = _listSurvivor[static_cast<size_t>( rescuer )];
        HorrorSurvivor& target        = _listSurvivor[static_cast<size_t>( hooked )];
        const bool      bCannotRescue = helper.canAct() == false || target._state != SurvivorState::Hooked ||
                                   isNear( helper._position, target._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotRescue )
            return false;
        leaveActivity( rescuer );
        restoreInjured( target );
        target._hookTimer    = 0.0f;
        target._struggleIdle = 0.0f;
        target._bStruggling  = SW_FALSE;
        pushEvent( AsymmetricHorrorEvent::Kind::Rescued, rescuer, hooked, 0.0f, target._position );
        awardScore( helper._score, "Unhook", 1.0f );
        return true;
    }

    bool HorrorMatch::dropPallet( int32 survivor, int32 pallet )
    {
        const bool bInvalidPalletRequest = isValidSurvivor( survivor ) == false || pallet < 0 || pallet >= static_cast<int32>( _listPallet.size() ) || isEnded();
        if ( bInvalidPalletRequest )
            return false;
        HorrorSurvivor&              target            = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules& rules             = _pCatalog->getRules();
        const float3&                position          = _listPallet[static_cast<size_t>( pallet )];
        const bool                   bCannotDropPallet = target.canAct() == false || _listPalletState[static_cast<size_t>( pallet )] != PalletState::Upright ||
                                       isNear( target._position, position, rules._interactRange ) == false;
        if ( bCannotDropPallet )
            return false;
        leaveActivity( survivor );
        _listPalletState[static_cast<size_t>( pallet )] = PalletState::Dropped;
        pushEvent( AsymmetricHorrorEvent::Kind::PalletDropped, survivor, pallet, 0.0f, position );
        if ( _killer._stunRemaining.isActive() == false && isNear( _killer._position, position, rules._pallet._stunRange ) )
        {
            // 판자에 맞은 살인마는 기절하고, 들고 있던 생존자를 떨어뜨린다.
            if ( _killer._carrying >= 0 )
                dropCarried( false, 0.0f );
            stunKiller( rules._pallet._stunTime );
            awardScore( target._score, "PalletSTUN", 1.0f );
        }
        return true;
    }

    bool HorrorMatch::vault( int32 survivor, int32 window )
    {
        const bool bInvalidVaultRequest = isValidSurvivor( survivor ) == false || window < 0 || window >= static_cast<int32>( _listWindow.size() ) || isEnded();
        if ( bInvalidVaultRequest )
            return false;
        HorrorSurvivor&              target       = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorWindow&                frame        = _listWindow[static_cast<size_t>( window )];
        const AsymmetricHorrorRules& rules        = _pCatalog->getRules();
        const bool                   bCannotVault = target.canAct() == false || frame._blockedRemaining > 0.0f || isNear( target._position, frame._position, rules._interactRange ) == false;
        if ( bCannotVault )
            return false;
        leaveActivity( survivor );
        const bool bFast       = target._lastMoveSpeed >= rules._survivorSpeed * rules._window._fastSpeedRatio;
        target._activity       = SurvivorActivity::Vaulting;
        target._activityTarget = window;
        target._vaultRemaining.start( bFast ? rules._window._fastTime : rules._window._mediumTime );
        target._vaultExit = HorrorMatchActionInternal::computeVaultExit( target._position, frame._position );
        pushEvent( AsymmetricHorrorEvent::Kind::Vaulted, survivor, window, bFast ? 1.0f : 0.0f, frame._position );
        awardScore( target._score, "Vault", 1.0f );
        if ( bFast )
            makeNoise( survivor, rules._window._noiseRadius, frame._position );

        // 추격 중(위협 반경 안) 같은 창을 여러 번 넘으면 창이 막힌다.
        if ( isNear( _killer._position, frame._position, _killer._pDef->_terrorRadius ) )
        {
            ++frame._chaseVaultCount;
            if ( frame._chaseVaultCount >= rules._window._blockCount )
            {
                frame._chaseVaultCount  = 0;
                frame._blockedRemaining = rules._window._blockTime;
                pushEvent( AsymmetricHorrorEvent::Kind::WindowBlocked, survivor, window, rules._window._blockTime, frame._position );
            }
        }
        return true;
    }

    bool HorrorMatch::enterLocker( int32 survivor, int32 locker )
    {
        const bool bInvalidLockerRequest = isValidSurvivor( survivor ) == false || locker < 0 || locker >= static_cast<int32>( _listLocker.size() ) || isEnded();
        if ( bInvalidLockerRequest )
            return false;
        HorrorSurvivor& target             = _listSurvivor[static_cast<size_t>( survivor )];
        const float3&   position           = _listLocker[static_cast<size_t>( locker )];
        const bool      bCannotEnterLocker = target.canAct() == false || _listLockerOccupant[static_cast<size_t>( locker )] >= 0 ||
                                        isNear( target._position, position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotEnterLocker )
            return false;
        leaveActivity( survivor );
        _listLockerOccupant[static_cast<size_t>( locker )] = survivor;
        target._activity                                   = SurvivorActivity::InLocker;
        target._activityTarget                             = locker;
        target._position                                   = position;
        return true;
    }

    void HorrorMatch::exitLocker( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) && _listSurvivor[static_cast<size_t>( survivor )]._activity == SurvivorActivity::InLocker )
            leaveActivity( survivor );
    }

    bool HorrorMatch::escape( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false || isEnded() )
            return false;
        HorrorSurvivor&              target = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules& rules  = _pCatalog->getRules();
        if ( target.canAct() == false )
            return false;
        bool bThroughHatch = false;
        bool bCanLeave     = false;
        for ( const HorrorGate& gate : _listGate )
        {
            bCanLeave = bCanLeave || ( gate._progress.isCompleted() && isNear( target._position, gate._position, rules._interactRange ) );
        }
        const bool bCanLeaveThroughHatch = bCanLeave == false && _bHatchOpen == SW_TRUE && isNear( target._position, _hatchPosition, rules._interactRange );
        if ( bCanLeaveThroughHatch )
        {
            bCanLeave     = true;
            bThroughHatch = true;
        }
        if ( bCanLeave == false )
            return false;
        leaveActivity( survivor );
        releaseHealers( survivor, true );
        target._state = SurvivorState::Escaped;
        _match.eliminate( target._participant );
        _match.addScore( _survivorTeam, 1 );
        pushEvent( AsymmetricHorrorEvent::Kind::Escaped, survivor, -1, bThroughHatch ? 1.0f : 0.0f, target._position );
        awardScore( target._score, "Escape", 1.0f );
        resolveEnd();
        return true;
    }

    void HorrorMatch::setStruggling( int32 survivor, bool bStruggling )
    {
        if ( isValidSurvivor( survivor ) )
            _listSurvivor[static_cast<size_t>( survivor )]._bStruggling = bStruggling ? SW_TRUE : SW_FALSE;
    }

    void HorrorMatch::setWiggling( int32 survivor, bool bWiggling )
    {
        if ( isValidSurvivor( survivor ) )
            _listSurvivor[static_cast<size_t>( survivor )]._bWiggling = bWiggling ? SW_TRUE : SW_FALSE;
    }

    // --- 살인마 행동 ------------------------------------------------------------------------------

    void HorrorMatch::moveKiller( const float3& direction, float32 deltaTime )
    {
        const bool bKillerCannotMove = _killer._pDef == nullptr || _killer.canAct() == false || isEnded();
        if ( bKillerCannotMove )
            return;
        const float3  move   = HorrorMatchActionInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= HorrorMatchActionInternal::kTiny )
            return;
        _killer._forward = move / length;
        _killer._position += move * ( computeKillerSpeed() * deltaTime );
        if ( _killer._carrying >= 0 )
            _listSurvivor[static_cast<size_t>( _killer._carrying )]._position = _killer._position;
    }

    KillerAttackResult HorrorMatch::killerAttack()
    {
        const bool bKillerNotReady = _killer._pDef == nullptr || _killer.canAct() == false || _killer._carrying >= 0 || _killer._attackCooldown.isActive() || isEnded();
        if ( bKillerNotReady )
            return KillerAttackResult::NotReady;
        const HorrorKillerDef& def    = *_killer._pDef;
        const float32          minDot = MathUtil::cos( def._lungeAngle * 0.5f );
        int32                  victim = -1;
        float32                best   = MathUtil::kMaxFloat;
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const HorrorSurvivor& candidate = _listSurvivor[static_cast<size_t>( index )];
            const bool            bStanding = candidate._state == SurvivorState::Healthy || candidate._state == SurvivorState::Injured;
            if ( bStanding == false || candidate._activity == SurvivorActivity::InLocker )
                continue;
            const float32 distance = float3::getHorizontalDistance( _killer._position, candidate._position );
            if ( distance > def._lungeRange || distance >= best )
                continue;
            if ( distance > HorrorMatchActionInternal::kTiny )
            {
                const float3  toward = float3{ candidate._position._x - _killer._position._x, 0.0f, candidate._position._z - _killer._position._z } / distance;
                const float32 facing = toward._x * _killer._forward._x + toward._z * _killer._forward._z;
                if ( facing < minDot )
                    continue;
            }
            best   = distance;
            victim = index;
        }
        if ( victim < 0 )
        {
            _killer._attackCooldown.start( def._missCooldown );
            pushEvent( AsymmetricHorrorEvent::Kind::Missed, -1, -1, 0.0f, _killer._position );
            return KillerAttackResult::Missed;
        }

        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( victim )];
        leaveActivity( victim );
        _killer._attackCooldown.start( def._hitCooldown );
        _match.reportDamage( _killer._participant, target._participant, 1.0f );
        (void)target._vitality.applyDamage( 1.0f, 0.0f, -1 ); // 결과는 아래 drainEvents 가 이벤트로 읽는다
        _listVitalityScratch.clear();
        target._vitality.drainEvents( _listVitalityScratch );
        awardScore( _killer._score, "Hit", 1.0f );
        if ( target._vitality.isDowned() )
        {
            target._state = SurvivorState::Dying;
            releaseHealers( victim, true );
            pushEvent( AsymmetricHorrorEvent::Kind::Downed, victim, -1, 0.0f, target._position );
            return KillerAttackResult::Downed;
        }
        target._state = SurvivorState::Injured;
        target._hasteRemaining.start( _pCatalog->getRules()._hitHasteTime );
        pushEvent( AsymmetricHorrorEvent::Kind::Hit, victim, -1, 0.0f, target._position );
        return KillerAttackResult::Hit;
    }

    bool HorrorMatch::pickUp( int32 survivor )
    {
        const bool bCannotPickUp = isValidSurvivor( survivor ) == false || _killer.canAct() == false || _killer._carrying >= 0 || isEnded();
        if ( bCannotPickUp )
            return false;
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        if ( target._state != SurvivorState::Dying || isNear( _killer._position, target._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        releaseHealers( survivor, true );
        target._vitality.stopRevive();
        target._state          = SurvivorState::Carried;
        target._wiggleProgress = 0.0f;
        target._position       = _killer._position;
        _killer._carrying      = survivor;
        pushEvent( AsymmetricHorrorEvent::Kind::PickedUp, survivor, -1, 0.0f, target._position );
        return true;
    }

    bool HorrorMatch::hookCarried( int32 hook )
    {
        const bool bCannotHook = _killer._carrying < 0 || hook < 0 || hook >= static_cast<int32>( _listHook.size() ) || _killer.canAct() == false || isEnded();
        if ( bCannotHook )
            return false;
        const AsymmetricHorrorRules& rules    = _pCatalog->getRules();
        const float3&                position = _listHook[static_cast<size_t>( hook )];
        if ( isNear( _killer._position, position, rules._interactRange ) == false )
            return false;
        const int32     survivor = _killer._carrying;
        HorrorSurvivor& target   = _listSurvivor[static_cast<size_t>( survivor )];
        _killer._carrying        = -1;
        target._position         = position;
        target._hookStage += 1;
        target._hookTimer    = 0.0f;
        target._struggleIdle = 0.0f;
        awardScore( _killer._score, "Hook", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::Hooked, survivor, hook, static_cast<float32>( target._hookStage ), position );
        if ( target._hookStage >= rules._hook._stages )
        {
            sacrifice( survivor, false ); // 마지막 걸림이면 바로 희생된다
            return true;
        }
        target._state = SurvivorState::Hooked;
        return true;
    }

    bool HorrorMatch::kickGenerator( int32 generator )
    {
        const bool bCannotKickGenerator = generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || _killer.canAct() == false || _killer._carrying >= 0 || isEnded();
        if ( bCannotKickGenerator )
            return false;
        HorrorGenerator&             gen       = _listGenerator[static_cast<size_t>( generator )];
        const AsymmetricHorrorRules& rules     = _pCatalog->getRules();
        const bool                   bKickable = gen._progress.isCompleted() == false && gen._bBlocked == SW_FALSE && gen._bKicked == SW_FALSE && gen._progress.getProgress() > 0.0f &&
                               gen._progress.getParticipantCount() == 0;
        if ( bKickable == false || isNear( _killer._position, gen._position, rules._interactRange ) == false )
            return false;
        const float32 progress = MathUtil::max( 0.0f, gen._progress.getProgress() - rules._generator._kickPenalty );
        if ( configureGenerator( generator, true ) == false )
            return false;
        gen._progress.setProgress( progress );
        gen._bKicked = SW_TRUE;
        awardScore( _killer._score, "Kick", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::GeneratorKicked, -1, generator, progress, gen._position );
        return true;
    }

    bool HorrorMatch::breakPallet( int32 pallet )
    {
        const bool bCannotBreakPallet = pallet < 0 || pallet >= static_cast<int32>( _listPallet.size() ) || _killer.canAct() == false || isEnded();
        if ( bCannotBreakPallet )
            return false;
        if ( _listPalletState[static_cast<size_t>( pallet )] != PalletState::Dropped ||
             isNear( _killer._position, _listPallet[static_cast<size_t>( pallet )], _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining.start( _pCatalog->getRules()._pallet._breakTime );
        _killer._breakingPallet = pallet;
        return true;
    }

    bool HorrorMatch::killerVault( int32 window )
    {
        const bool bCannotKillerVault = window < 0 || window >= static_cast<int32>( _listWindow.size() ) || _killer.canAct() == false || isEnded();
        if ( bCannotKillerVault )
            return false;
        const HorrorWindow& frame = _listWindow[static_cast<size_t>( window )];
        if ( isNear( _killer._position, frame._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining.start( _pCatalog->getRules()._window._killerTime );
        _killer._busyExit  = HorrorMatchActionInternal::computeVaultExit( _killer._position, frame._position );
        _killer._bVaulting = SW_TRUE;
        pushEvent( AsymmetricHorrorEvent::Kind::Vaulted, -1, window, 0.0f, frame._position );
        return true;
    }

    bool HorrorMatch::searchLocker( int32 locker )
    {
        const bool bCannotSearchLocker = locker < 0 || locker >= static_cast<int32>( _listLocker.size() ) || _killer.canAct() == false || _killer._carrying >= 0 || isEnded();
        if ( bCannotSearchLocker )
            return false;
        const float3& position = _listLocker[static_cast<size_t>( locker )];
        if ( isNear( _killer._position, position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining.start( _pCatalog->getRules()._locker._searchTime );
        const int32 occupant = _listLockerOccupant[static_cast<size_t>( locker )];
        if ( occupant < 0 )
            return false;

        // 숨은 생존자는 체력과 상관없이 바로 들린다.
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( occupant )];
        leaveActivity( occupant );
        releaseHealers( occupant, true );
        while ( target._vitality.isAlive() )
        {
            (void)target._vitality.applyDamage( 1.0f, 0.0f, -1 ); // 결과는 아래 drainEvents 가 이벤트로 읽는다
        }
        _listVitalityScratch.clear();
        target._vitality.drainEvents( _listVitalityScratch );
        target._state          = SurvivorState::Carried;
        target._wiggleProgress = 0.0f;
        target._position       = _killer._position;
        _killer._carrying      = occupant;
        awardScore( _killer._score, "LockerGrab", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::LockerGrab, occupant, locker, 0.0f, position );
        pushEvent( AsymmetricHorrorEvent::Kind::PickedUp, occupant, -1, 0.0f, position );
        return true;
    }

    bool HorrorMatch::closeHatch()
    {
        const bool bCannotCloseHatch = _bHatchOpen == SW_FALSE || _killer.canAct() == false || isEnded() ||
                                       isNear( _killer._position, _hatchPosition, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotCloseHatch )
            return false;
        _bHatchOpen   = SW_FALSE;
        _bHatchClosed = SW_TRUE;
        pushEvent( AsymmetricHorrorEvent::Kind::HatchClosed, -1, -1, 0.0f, _hatchPosition );
        if ( _bGatesPowered == SW_FALSE )
        {
            // 해치를 닫으면 탈출구에 전원이 들어오고 붕괴가 시작된다.
            _bGatesPowered = SW_TRUE;
            pushEvent( AsymmetricHorrorEvent::Kind::GatesPowered, -1, -1, 0.0f, _hatchPosition );
        }
        startCollapse();
        return true;
    }

    bool HorrorMatch::useKillerAbility()
    {
        const bool bAbilityUnavailable = _killer._pDef == nullptr || _killer.canAct() == false || _killer._abilityCooldown.isActive() || isEnded();
        if ( bAbilityUnavailable )
            return false;
        _killer._abilityCooldown.start( _killer._pDef->_abilityCooldown );
        return true;
    }
} // namespace sw

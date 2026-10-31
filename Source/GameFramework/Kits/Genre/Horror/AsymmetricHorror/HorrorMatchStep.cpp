#include "pch.h"

#include "Core/Hash/HashUtil.h"
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
    void HorrorMatch::step( float32 deltaTime )
    {
        ++_tick;
        if ( isEnded() == false )
        {
            stepKiller( deltaTime );
            for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
            {
                stepSurvivor( survivor, deltaTime );
            }
            stepGenerators( deltaTime );
            stepGates( deltaTime );
            stepEndgame( deltaTime );
            resolveEnd();
        }
        _match.update( deltaTime );
    }

    void HorrorMatch::stepKiller( float32 deltaTime )
    {
        _killer._attackCooldown.update( deltaTime );
        _killer._abilityCooldown.update( deltaTime );
        _killer._stunRemaining.update( deltaTime );
        if ( _killer._busyRemaining.isActive() == false )
            return;
        if ( _killer._busyRemaining.update( deltaTime ) == false )
            return;
        if ( _killer._breakingPallet >= 0 )
        {
            const int32 pallet                              = _killer._breakingPallet;
            _listPalletState[static_cast<size_t>( pallet )] = PalletState::Broken;
            _killer._breakingPallet                         = -1;
            awardScore( _killer._score, "BreakPallet", 1.0f );
            pushEvent( AsymmetricHorrorEvent::Kind::PalletBroken, -1, pallet, 0.0f, _listPallet[static_cast<size_t>( pallet )] );
        }
        if ( _killer._bVaulting == SW_TRUE )
        {
            _killer._position  = _killer._busyExit;
            _killer._bVaulting = SW_FALSE;
            if ( _killer._carrying >= 0 )
                _listSurvivor[static_cast<size_t>( _killer._carrying )]._position = _killer._position;
        }
    }

    void HorrorMatch::stepSurvivor( int32 survivorIndex, float32 deltaTime )
    {
        HorrorSurvivor&              survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        const AsymmetricHorrorRules& rules    = _pCatalog->getRules();
        survivor._hasteRemaining.update( deltaTime );
        survivor._noiseRemaining.update( deltaTime );
        if ( survivor._noiseRemaining.isActive() == false )
            survivor._noiseRadius = 0.0f;

        if ( survivor._activity == SurvivorActivity::Vaulting )
        {
            survivor._vaultRemaining.update( deltaTime );
            if ( survivor._vaultRemaining.isActive() == false )
            {
                survivor._position       = survivor._vaultExit;
                survivor._activity       = SurvivorActivity::None;
                survivor._activityTarget = -1;
            }
        }

        switch ( survivor._state )
        {
            case SurvivorState::Healthy:
            case SurvivorState::Injured:
            case SurvivorState::Dying:
            {
                survivor._vitality.update( deltaTime );
                _listVitalityScratch.clear();
                survivor._vitality.drainEvents( _listVitalityScratch );
                if ( survivor._vitality.isDead() )
                {
                    sacrifice( survivorIndex, true ); // 출혈사
                    return;
                }
                syncHealthState( survivor );
                break;
            }
            case SurvivorState::Carried:
            {
                if ( survivor._bWiggling == SW_TRUE )
                    survivor._wiggleProgress += deltaTime / rules._hook._wiggleTime;
                if ( survivor._wiggleProgress >= 1.0f )
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::WiggledFree, survivorIndex, -1, 0.0f, survivor._position );
                    dropCarried( true, rules._hook._wiggleStunTime );
                }
                break;
            }
            case SurvivorState::Hooked:
            {
                survivor._hookTimer += deltaTime;
                if ( survivor._hookStage >= 2 )
                {
                    // 몸부림 단계에서는 멈추면 희생이 앞당겨진다.
                    survivor._struggleIdle = survivor._bStruggling == SW_TRUE ? 0.0f : survivor._struggleIdle + deltaTime;
                    if ( survivor._struggleIdle > rules._hook._struggleGrace )
                    {
                        sacrifice( survivorIndex, false );
                        return;
                    }
                }
                if ( survivor._hookTimer >= rules._hook._stageTime )
                {
                    survivor._hookTimer = 0.0f;
                    survivor._hookStage += 1;
                    if ( survivor._hookStage >= rules._hook._stages )
                    {
                        sacrifice( survivorIndex, false );
                        return;
                    }
                    survivor._struggleIdle = 0.0f;
                    pushEvent( AsymmetricHorrorEvent::Kind::HookStageAdvanced, survivorIndex, -1, static_cast<float32>( survivor._hookStage ), survivor._position );
                }
                break;
            }
            case SurvivorState::Sacrificed:
            case SurvivorState::Escaped:
            {
                return;
            }
        }

        // 이 생존자를 치료하는 진행이며, 치료하는 쪽들이 붙어 있다.
        if ( survivor._healing.getParticipantCount() > 0 )
        {
            survivor._healing.update( deltaTime );
            if ( drainProgress( survivor._healing, survivor._position ) )
                finishHeal( survivorIndex );
        }
        survivor._lastMoveSpeed = 0.0f; // 이동은 걸음 사이에 들어오므로, 다음 걸음까지만 빠른 넘기 판정에 쓴다
    }

    void HorrorMatch::stepGenerators( float32 deltaTime )
    {
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        for ( int32 index = 0; index < static_cast<int32>( _listGenerator.size() ); ++index )
        {
            HorrorGenerator& generator = _listGenerator[static_cast<size_t>( index )];
            if ( generator._progress.isCompleted() || generator._bBlocked == SW_TRUE )
                continue;
            const float32 before = generator._progress.getProgress();
            generator._progress.update( deltaTime );
            const float32 gained = generator._progress.getProgress() - before;
            if ( gained > 0.0f )
            {
                const int32 participantCount = generator._progress.getParticipantCount();
                for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
                {
                    HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
                    if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == index )
                        awardScore( target._score, "Repair", gained / static_cast<float32>( MathUtil::max( 1, participantCount ) ) );
                }
            }
            if ( generator._bKicked == SW_TRUE && generator._progress.getProgress() <= 0.0f )
                generator._bKicked = SW_FALSE;
            if ( drainProgress( generator._progress, generator._position ) == false )
                continue;

            // 다 고쳤다. 고치던 사람은 손을 뗀다.
            for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
            {
                HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
                if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == index )
                {
                    target._activity       = SurvivorActivity::None;
                    target._activityTarget = -1;
                }
            }
            generator._bKicked = SW_FALSE;
            ++_completedGeneratorCount;
            pushEvent( AsymmetricHorrorEvent::Kind::GeneratorCompleted, -1, index, static_cast<float32>( _completedGeneratorCount ), generator._position );
            if ( _completedGeneratorCount >= rules._generatorsRequired && _bGatesPowered == SW_FALSE )
            {
                _bGatesPowered = SW_TRUE;
                for ( int32 other = 0; other < static_cast<int32>( _listGenerator.size() ); ++other )
                {
                    HorrorGenerator& rest = _listGenerator[static_cast<size_t>( other )];
                    if ( rest._progress.isCompleted() == false )
                        rest._bBlocked = SW_TRUE;
                }
                for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
                {
                    if ( _listSurvivor[static_cast<size_t>( survivor )]._activity == SurvivorActivity::Repairing )
                        leaveActivity( survivor );
                }
                pushEvent( AsymmetricHorrorEvent::Kind::GatesPowered, -1, -1, 0.0f, generator._position );
            }
        }
    }

    void HorrorMatch::stepGates( float32 deltaTime )
    {
        for ( int32 index = 0; index < static_cast<int32>( _listGate.size() ); ++index )
        {
            HorrorGate& gate = _listGate[static_cast<size_t>( index )];
            if ( gate._progress.isCompleted() || gate._progress.getParticipantCount() == 0 )
                continue;
            gate._progress.update( deltaTime );
            if ( drainProgress( gate._progress, gate._position ) == false )
                continue;
            for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
            {
                HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
                if ( target._activity == SurvivorActivity::OpeningGate && target._activityTarget == index )
                {
                    target._activity       = SurvivorActivity::None;
                    target._activityTarget = -1;
                }
            }
            pushEvent( AsymmetricHorrorEvent::Kind::GateOpened, -1, index, 0.0f, gate._position );
            startCollapse();
        }
    }

    void HorrorMatch::stepEndgame( float32 deltaTime )
    {
        const AsymmetricHorrorRules& rules            = _pCatalog->getRules();
        const int32                  standing         = countStandingSurvivors();
        const bool                   bShouldOpenHatch = _bHatchPlaced == SW_TRUE && _bHatchOpen == SW_FALSE && _bHatchClosed == SW_FALSE && standing > 0 && standing <= rules._endgame._hatchRemaining;
        if ( bShouldOpenHatch )
        {
            _bHatchOpen = SW_TRUE;
            pushEvent( AsymmetricHorrorEvent::Kind::HatchOpened, -1, -1, 0.0f, _hatchPosition );
        }
        if ( _bCollapseStarted == SW_FALSE || _collapseRemaining.isActive() == false )
            return;
        bool bSlowed = false;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            const bool bHelpless = survivor._state == SurvivorState::Dying || survivor._state == SurvivorState::Carried || survivor._state == SurvivorState::Hooked;
            bSlowed              = bSlowed || bHelpless;
        }
        _collapseRemaining.update( deltaTime * ( bSlowed ? rules._endgame._slowScale : 1.0f ) );
        if ( _collapseRemaining.isActive() )
            return;
        // 붕괴가 끝났다. 아직 남은 생존자는 모두 희생된다.
        for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
        {
            if ( _listSurvivor[static_cast<size_t>( survivor )].isStanding() )
                sacrifice( survivor, false );
        }
    }

    bool HorrorMatch::drainProgress( InteractionProgress& progress, const float3& position )
    {
        _listInteractionScratch.clear();
        progress.drainEvents( _listInteractionScratch );
        bool bCompleted = false;
        for ( const InteractionEvent& event : _listInteractionScratch )
        {
            const int32 actor = static_cast<int32>( event._actorID );
            switch ( event._kind )
            {
                case InteractionEvent::Kind::SkillCheckStarted:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckStarted, actor, -1, event._value, position );
                    break;
                }
                case InteractionEvent::Kind::SkillCheckSucceeded:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckResult, actor, -1, 1.0f, position );
                    if ( isValidSurvivor( actor ) )
                        awardScore( _listSurvivor[static_cast<size_t>( actor )]._score, "SkillCheck", 1.0f );
                    break;
                }
                case InteractionEvent::Kind::SkillCheckFailed:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckResult, actor, -1, 0.0f, position );
                    if ( event._bNoise == SW_TRUE )
                        makeNoise( actor, _pCatalog->getRules()._generator._noiseRadius, position );
                    break;
                }
                case InteractionEvent::Kind::Completed:
                {
                    bCompleted = true;
                    break;
                }
                case InteractionEvent::Kind::Joined:
                case InteractionEvent::Kind::Left:
                case InteractionEvent::Kind::Interrupted:
                case InteractionEvent::Kind::RegressionStarted:
                {
                    break;
                }
            }
        }
        return bCompleted;
    }
} // namespace sw

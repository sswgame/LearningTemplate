#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/AI/AIPerception.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorMatch.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorSnapshot.h"

namespace sw
{
    namespace
    {
        struct HorrorMatchRulesInternal
        {
            static constexpr uint32 kGeneratorSeedMix = HashUtil::kGoldenRatio32;
        };
    } // namespace

    InteractionProgress* HorrorMatch::findActivityProgressMutable( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false )
            return nullptr;
        const HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        const int32           index  = target._activityTarget;
        switch ( target._activity )
        {
            case SurvivorActivity::Repairing:
                return index >= 0 && index < static_cast<int32>( _listGenerator.size() ) ? &_listGenerator[static_cast<size_t>( index )]._progress : nullptr;
            case SurvivorActivity::Healing:
                return isValidSurvivor( index ) ? &_listSurvivor[static_cast<size_t>( index )]._healing : nullptr;
            case SurvivorActivity::OpeningGate:
                return index >= 0 && index < static_cast<int32>( _listGate.size() ) ? &_listGate[static_cast<size_t>( index )]._progress : nullptr;
            case SurvivorActivity::None:
            case SurvivorActivity::Vaulting:
            case SurvivorActivity::InLocker:
                return nullptr;
        }
        return nullptr;
    }

    // --- 상태 도우미 ------------------------------------------------------------------------------

    void HorrorMatch::releaseHealers( int32 patient, bool bResetProgress )
    {
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        for ( int32 healer = 0; healer < getSurvivorCount(); ++healer )
        {
            HorrorSurvivor& doctor = _listSurvivor[static_cast<size_t>( healer )];
            if ( doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient )
            {
                (void)target._healing.leave( static_cast<uint32>( healer ) );
                doctor._activity       = SurvivorActivity::None;
                doctor._activityTarget = -1;
            }
        }
        if ( bResetProgress )
            target._healing.reset();
        _listInteractionScratch.clear();
        target._healing.drainEvents( _listInteractionScratch );
    }

    void HorrorMatch::finishHeal( int32 patient )
    {
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        for ( int32 healer = 0; healer < getSurvivorCount(); ++healer )
        {
            const HorrorSurvivor& doctor = _listSurvivor[static_cast<size_t>( healer )];
            if ( doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient )
                awardScore( _listSurvivor[static_cast<size_t>( healer )]._score, "Heal", 1.0f );
        }
        releaseHealers( patient, true );
        if ( target._state == SurvivorState::Dying )
        {
            restoreInjured( target ); // 빈사에서 부상으로 돌아간다
        }
        else if ( target._state == SurvivorState::Injured )
        {
            (void)target._vitality.heal( 1.0f );
            target._state = SurvivorState::Healthy;
        }
        _listVitalityScratch.clear();
        target._vitality.drainEvents( _listVitalityScratch );
        pushEvent( AsymmetricHorrorEvent::Kind::Healed, -1, patient, static_cast<float32>( target._state ), target._position );
    }

    void HorrorMatch::restoreInjured( HorrorSurvivor& survivor )
    {
        survivor._vitality.respawn();
        (void)survivor._vitality.applyDamage( 1.0f, 0.0f, -1 ); // 결과는 아래 drainEvents 가 이벤트로 읽는다
        _listVitalityScratch.clear();
        survivor._vitality.drainEvents( _listVitalityScratch );
        survivor._state          = SurvivorState::Injured;
        survivor._wiggleProgress = 0.0f;
        survivor._bWiggling      = SW_FALSE;
    }

    void HorrorMatch::sacrifice( int32 survivorIndex, bool bBledOut )
    {
        HorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        if ( survivor.isStanding() == false )
            return;
        leaveActivity( survivorIndex );
        releaseHealers( survivorIndex, true );
        if ( _killer._carrying == survivorIndex )
            _killer._carrying = -1;
        survivor._vitality.kill( -1 );
        _listVitalityScratch.clear();
        survivor._vitality.drainEvents( _listVitalityScratch );
        survivor._state    = SurvivorState::Sacrificed;
        survivor._bBledOut = bBledOut ? SW_TRUE : SW_FALSE;
        _match.reportKill( survivor._participant, _killer._participant );
        awardScore( _killer._score, "Sacrifice", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::Sacrificed, survivorIndex, -1, bBledOut ? 1.0f : 0.0f, survivor._position );
        resolveEnd();
    }

    void HorrorMatch::leaveActivity( int32 survivorIndex )
    {
        HorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        const int32     target   = survivor._activityTarget;
        switch ( survivor._activity )
        {
            case SurvivorActivity::Repairing:
            {
                if ( target >= 0 && target < static_cast<int32>( _listGenerator.size() ) )
                    (void)_listGenerator[static_cast<size_t>( target )]._progress.leave( static_cast<uint32>( survivorIndex ) );
                break;
            }
            case SurvivorActivity::Healing:
            {
                if ( isValidSurvivor( target ) )
                {
                    HorrorSurvivor& patient = _listSurvivor[static_cast<size_t>( target )];
                    (void)patient._healing.leave( static_cast<uint32>( survivorIndex ) );
                    if ( patient._healing.getParticipantCount() == 0 )
                        patient._vitality.stopRevive(); // 아무도 치료하지 않으면 다시 피를 흘린다
                }
                break;
            }
            case SurvivorActivity::OpeningGate:
            {
                if ( target >= 0 && target < static_cast<int32>( _listGate.size() ) )
                    (void)_listGate[static_cast<size_t>( target )]._progress.leave( static_cast<uint32>( survivorIndex ) );
                break;
            }
            case SurvivorActivity::InLocker:
            {
                if ( target >= 0 && target < static_cast<int32>( _listLockerOccupant.size() ) )
                    _listLockerOccupant[static_cast<size_t>( target )] = -1;
                break;
            }
            case SurvivorActivity::Vaulting:
            {
                survivor._vaultRemaining.clear();
                break;
            }
            case SurvivorActivity::None:
            {
                break;
            }
        }
        survivor._activity       = SurvivorActivity::None;
        survivor._activityTarget = -1;
    }

    void HorrorMatch::dropCarried( bool bStunKiller, float32 stunTime )
    {
        const int32 survivorIndex = _killer._carrying;
        if ( isValidSurvivor( survivorIndex ) == false )
            return;
        _killer._carrying        = -1;
        HorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        restoreInjured( survivor );
        survivor._position = _killer._position;
        if ( bStunKiller )
            stunKiller( stunTime );
    }

    void HorrorMatch::stunKiller( float32 seconds )
    {
        _killer._stunRemaining.extendTo( seconds );
        _killer._busyRemaining.clear();
        _killer._breakingPallet = -1;
        if ( _killer._bVaulting == SW_TRUE )
        {
            _killer._position  = _killer._busyExit;
            _killer._bVaulting = SW_FALSE;
        }
        pushEvent( AsymmetricHorrorEvent::Kind::KillerStunned, -1, -1, seconds, _killer._position );
    }

    void HorrorMatch::startCollapse()
    {
        if ( _bCollapseStarted == SW_TRUE )
            return;
        _bCollapseStarted = SW_TRUE;
        _collapseRemaining.start( _pCatalog->getRules()._endgame._collapseTime );
        pushEvent( AsymmetricHorrorEvent::Kind::CollapseStarted, -1, -1, _collapseRemaining.getRemaining(), float3{} );
    }

    void HorrorMatch::resolveEnd()
    {
        if ( _bEndReported == SW_TRUE || countStandingSurvivors() > 0 )
            return;
        int32 sacrificed = 0;
        int32 escaped    = 0;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            sacrificed += survivor._state == SurvivorState::Sacrificed ? 1 : 0;
            escaped += survivor._state == SurvivorState::Escaped ? 1 : 0;
        }
        // 희생이 많으면 살인마, 탈출이 많으면 생존자가 이기고, 같으면 무승부이다.
        const int32 winner = sacrificed > escaped ? _killerTeam : ( escaped > sacrificed ? _survivorTeam : -1 );
        _bEndReported      = SW_TRUE;
        _match.endMatch( winner );
        pushEvent( AsymmetricHorrorEvent::Kind::MatchEnded, -1, escaped, static_cast<float32>( sacrificed ), float3{} );
    }

    void HorrorMatch::syncHealthState( HorrorSurvivor& survivor )
    {
        const bool bNotSurvivorHealthState = survivor._state != SurvivorState::Healthy && survivor._state != SurvivorState::Injured && survivor._state != SurvivorState::Dying;
        if ( bNotSurvivorHealthState )
            return;
        if ( survivor._vitality.isDowned() )
            survivor._state = SurvivorState::Dying;
        else if ( survivor._vitality.getHealth() >= 2.0f )
            survivor._state = SurvivorState::Healthy;
        else
            survivor._state = SurvivorState::Injured;
    }

    void HorrorMatch::awardScore( StatBlock& outScore, const utf8* pAction, float32 amount )
    {
        if ( _pCatalog == nullptr )
            return;
        const HorrorScoreRule* pRule = _pCatalog->findScoreRule( hashed_string( pAction ) );
        if ( pRule == nullptr )
            return;
        const float32 cap   = _pCatalog->getCategoryCap( pRule->_category );
        float32       value = outScore.getValue( pRule->_category ) + pRule->_points * amount;
        if ( cap > 0.0f )
            value = MathUtil::min( value, cap );
        outScore.setValue( pRule->_category, value );
    }

    void HorrorMatch::configureGenerator( int32 generator, bool bRegressing )
    {
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        InteractionConfig            config;
        config._duration             = rules._generator._time;
        config._listParticipantScale = rules._generator._listScale;
        config._gradeBonus           = rules._skillCheck._bonus;
        config._regressionRate       = bRegressing ? rules._generator._kickRegression : 0.0f;
        config._skillCheckInterval   = rules._generator._skillCheckInterval;
        config._skillCheckLeadTime   = rules._skillCheck._leadTime;
        config._skillCheckPenalty    = rules._skillCheck._failPenalty;
        config._maxParticipants      = rules._generator._maxRepairers;

        // 다시 설정해도 진행량은 남긴다(걷어차기 퇴행을 켜고 끄는 데 쓴다).
        HorrorGenerator& target   = _listGenerator[static_cast<size_t>( generator )];
        const float32    progress = target._progress.getProgress();
        target._progress.initialize( config, &_pCatalog->getJudge(),
                                     _settings._seed ^ ( static_cast<uint32>( generator + 1 ) * HorrorMatchRulesInternal::kGeneratorSeedMix ) ^ _tick );
        target._progress.setProgress( progress );
        _listInteractionScratch.clear();
        target._progress.drainEvents( _listInteractionScratch );
        target._bKicked = bRegressing ? SW_TRUE : SW_FALSE;
    }

    void HorrorMatch::makeNoise( int32 survivor, float32 radius, const float3& position )
    {
        if ( isValidSurvivor( survivor ) )
        {
            HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
            target._noiseRadius    = MathUtil::max( target._noiseRadius, radius );
            target._noiseRemaining.start( _settings._fixedStep * 2.0f );
        }
        pushEvent( AsymmetricHorrorEvent::Kind::Noise, survivor, -1, radius, position );
    }
} // namespace sw

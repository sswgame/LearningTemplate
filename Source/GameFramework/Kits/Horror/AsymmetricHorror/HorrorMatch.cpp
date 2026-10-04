#include "pch.h"

#include "GameFramework/Kits/Horror/AsymmetricHorror/HorrorMatch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/AI/AiPerception.h"
#include "GameFramework/Kits/Horror/AsymmetricHorror/HorrorSnapshot.h"

namespace sw
{
    namespace
    {
        struct HorrorMatchInternal
        {
            static constexpr float32 kTiny             = 1.0e-5f;
            static constexpr float32 kNeverReviveTime  = 1.0e9f; ///< 빈사 회복은 치료 진행이 정한다 — Vitality 의 부활 시계는 멈춤용으로만 쓴다
            static constexpr uint32  kGeneratorSeedMix = 0x9E3779B9u;
            static constexpr uint32  kHealSeedMix      = 0x85EBCA6Bu;
            static constexpr uint32  kGateSeedMix      = 0xC2B2AE35u;

            /** @brief XZ 평면 거리입니다. */
            static float32 computeFlatDistance( const float3& from, const float3& to )
            {
                const float32 deltaX = to._x - from._x;
                const float32 deltaZ = to._z - from._z;
                return MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
            }

            /** @brief XZ 평면에서 길이 1 이하로 자른 방향입니다. */
            static float3 clampDirection( const float3& direction )
            {
                float3        flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                if ( length > 1.0f )
                    flat = flat / length;
                return flat;
            }

            /** @brief 창틀 건너편 — 창틀을 사이에 두고 마주 보는 자리입니다. */
            static float3 computeVaultExit( const float3& from, const float3& window )
            {
                return float3{ window._x * 2.0f - from._x, 0.0f, window._z * 2.0f - from._z };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "HorrorMatch" );

    HorrorMatch::HorrorMatch()
        : _settings{}
        , _killer{}
        , _listSurvivor{}
        , _listGenerator{}
        , _listGate{}
        , _listWindow{}
        , _listHook{}
        , _listPallet{}
        , _listPalletState{}
        , _listLocker{}
        , _listLockerOccupant{}
        , _listEvent{}
        , _listInteractionScratch{}
        , _listVitalityScratch{}
        , _match{}
        , _timer{}
        , _hatchPosition{}
        , _pCatalog{ nullptr }
        , _collapseRemaining{ 0.0f }
        , _completedGeneratorCount{ 0 }
        , _survivorTeam{ -1 }
        , _killerTeam{ -1 }
        , _tick{ 0 }
        , _bStarted{ SW_FALSE }
        , _bGatesPowered{ SW_FALSE }
        , _bHatchPlaced{ SW_FALSE }
        , _bHatchOpen{ SW_FALSE }
        , _bHatchClosed{ SW_FALSE }
        , _bCollapseStarted{ SW_FALSE }
        , _bEndReported{ SW_FALSE }
    {
    }

    bool HorrorMatch::initialize( const HorrorMatchSettings& settings, const AsymmetricHorrorRulesCatalog* pCatalog )
    {
        _settings            = settings;
        _settings._fixedStep = MathUtil::max( 1.0e-3f, _settings._fixedStep );
        _pCatalog            = pCatalog;
        _killer              = HorrorKiller{};
        _listSurvivor.clear();
        _listGenerator.clear();
        _listGate.clear();
        _listWindow.clear();
        _listHook.clear();
        _listPallet.clear();
        _listPalletState.clear();
        _listLocker.clear();
        _listLockerOccupant.clear();
        _listEvent.clear();
        _match.initialize( MatchSettings{} );
        _timer                   = FixedStepTimer( _settings._fixedStep, 0.25f );
        _collapseRemaining       = 0.0f;
        _completedGeneratorCount = 0;
        _survivorTeam            = -1;
        _killerTeam              = -1;
        _tick                    = 0;
        _bStarted                = SW_FALSE;
        _bGatesPowered           = SW_FALSE;
        _bHatchPlaced            = SW_FALSE;
        _bHatchOpen              = SW_FALSE;
        _bHatchClosed            = SW_FALSE;
        _bCollapseStarted        = SW_FALSE;
        _bEndReported            = SW_FALSE;
        if ( _pCatalog == nullptr )
            return false;
        _killer._pDef = _pCatalog->findKiller( _settings._killerId );
        if ( _killer._pDef == nullptr )
        {
            SW_LOG_WARNING( "unknown killer '%#'", _settings._killerId.c_str() );
            return false;
        }
        return true;
    }

    int32 HorrorMatch::addSurvivor( const float3& position )
    {
        if ( _pCatalog == nullptr || _bStarted == SW_TRUE )
            return -1;
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        const int32                  index = static_cast<int32>( _listSurvivor.size() );

        VitalitySettings vitality;
        vitality._maxHealth               = 2.0f;
        vitality._bDownedEnabled          = SW_TRUE;
        vitality._downedHealth            = rules._bleedoutTime;
        vitality._bleedoutRate            = 1.0f;
        vitality._reviveTime              = HorrorMatchInternal::kNeverReviveTime;
        vitality._invulnerableAfterRevive = 0.0f;
        vitality._bDamageInterruptsRevive = SW_FALSE;

        InteractionConfig heal;
        heal._duration             = rules._healTime;
        heal._listParticipantScale = rules._listHealScale;
        heal._gradeBonus           = rules._skillCheckBonus;
        heal._skillCheckInterval   = rules._healSkillCheckInterval;
        heal._skillCheckLeadTime   = rules._skillCheckLeadTime;
        heal._skillCheckPenalty    = rules._skillCheckFailPenalty;
        heal._maxParticipants      = rules._healMaxParticipants;

        HorrorSurvivor survivor;
        survivor._vitality.initialize( vitality );
        survivor._healing.initialize( heal, &_pCatalog->getJudge(), _settings._seed ^ ( static_cast<uint32>( index + 1 ) * HorrorMatchInternal::kHealSeedMix ) );
        survivor._position = float3{ position._x, 0.0f, position._z };
        _listSurvivor.push_back( survivor );
        return index;
    }

    void HorrorMatch::setKillerPosition( const float3& position ) { _killer._position = float3{ position._x, 0.0f, position._z }; }

    int32 HorrorMatch::addGenerator( const float3& position )
    {
        if ( _pCatalog == nullptr )
            return -1;
        HorrorGenerator generator;
        generator._position = float3{ position._x, 0.0f, position._z };
        _listGenerator.push_back( generator );
        const int32 index = static_cast<int32>( _listGenerator.size() ) - 1;
        configureGenerator( index, false );
        return index;
    }

    int32 HorrorMatch::addHook( const float3& position )
    {
        _listHook.push_back( float3{ position._x, 0.0f, position._z } );
        return static_cast<int32>( _listHook.size() ) - 1;
    }

    int32 HorrorMatch::addPallet( const float3& position )
    {
        _listPallet.push_back( float3{ position._x, 0.0f, position._z } );
        _listPalletState.push_back( PalletState::Upright );
        return static_cast<int32>( _listPallet.size() ) - 1;
    }

    int32 HorrorMatch::addWindow( const float3& position )
    {
        HorrorWindow window;
        window._position = float3{ position._x, 0.0f, position._z };
        _listWindow.push_back( window );
        return static_cast<int32>( _listWindow.size() ) - 1;
    }

    int32 HorrorMatch::addLocker( const float3& position )
    {
        _listLocker.push_back( float3{ position._x, 0.0f, position._z } );
        _listLockerOccupant.push_back( -1 );
        return static_cast<int32>( _listLocker.size() ) - 1;
    }

    int32 HorrorMatch::addGate( const float3& position )
    {
        if ( _pCatalog == nullptr )
            return -1;
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        InteractionConfig            config;
        config._duration        = rules._gateOpenTime;
        config._maxParticipants = 1;
        HorrorGate gate;
        gate._position    = float3{ position._x, 0.0f, position._z };
        const int32 index = static_cast<int32>( _listGate.size() );
        gate._progress.initialize( config, nullptr, _settings._seed ^ ( static_cast<uint32>( index + 1 ) * HorrorMatchInternal::kGateSeedMix ) );
        _listGate.push_back( gate );
        return index;
    }

    void HorrorMatch::setHatchPosition( const float3& position )
    {
        _hatchPosition = float3{ position._x, 0.0f, position._z };
        _bHatchPlaced  = SW_TRUE;
    }

    void HorrorMatch::start()
    {
        if ( _bStarted == SW_TRUE || _pCatalog == nullptr || _killer._pDef == nullptr )
            return;
        MatchSettings settings;
        settings._bRespawn = SW_FALSE;
        _match.initialize( settings );
        _survivorTeam = _match.addTeam( hashed_string( "Survivors" ) );
        _killerTeam   = _match.addTeam( hashed_string( "Killer" ) );
        for ( HorrorSurvivor& survivor : _listSurvivor )
            survivor._participant = _match.addParticipant( _survivorTeam, hashed_string( "Survivor" ) );
        _killer._participant = _match.addParticipant( _killerTeam, hashed_string( "Killer" ) );
        _match.start();
        _bStarted = SW_TRUE;
    }

    void HorrorMatch::update( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            step( _timer.getStep() );
    }

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
        const float3  move   = HorrorMatchInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= HorrorMatchInternal::kTiny )
            return;
        leaveActivity( survivor );
        const float32 speed = computeSurvivorSpeed( survivor );
        target._position += move * ( speed * deltaTime );
        target._lastMoveSpeed = length * speed;
    }

    bool HorrorMatch::startRepair( int32 survivor, int32 generator )
    {
        if ( isValidSurvivor( survivor ) == false || generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || isEnded() )
            return false;
        HorrorSurvivor&  target = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorGenerator& gen    = _listGenerator[static_cast<size_t>( generator )];
        if ( target.canAct() == false || gen._bBlocked == SW_TRUE || gen._progress.isCompleted() ||
             isNear( target._position, gen._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == generator )
            return true;
        leaveActivity( survivor );
        if ( gen._bKicked == SW_TRUE )
            configureGenerator( generator, false ); // 누가 다시 붙으면 퇴행이 멈춘다
        if ( gen._progress.join( static_cast<uint32>( survivor ) ) == false )
            return false;
        target._activity       = SurvivorActivity::Repairing;
        target._activityTarget = generator;
        return true;
    }

    bool HorrorMatch::startHeal( int32 healer, int32 patient )
    {
        if ( isValidSurvivor( healer ) == false || isValidSurvivor( patient ) == false || healer == patient || isEnded() )
            return false;
        HorrorSurvivor& doctor = _listSurvivor[static_cast<size_t>( healer )];
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        const bool      bHurt  = target._state == SurvivorState::Injured || target._state == SurvivorState::Dying;
        if ( doctor.canAct() == false || bHurt == false || isNear( doctor._position, target._position, _pCatalog->getRules()._interactRange ) == false )
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
        if ( isValidSurvivor( survivor ) == false || gate < 0 || gate >= static_cast<int32>( _listGate.size() ) || _bGatesPowered == SW_FALSE || isEnded() )
            return false;
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorGate&     exit   = _listGate[static_cast<size_t>( gate )];
        if ( target.canAct() == false || exit._progress.isCompleted() || isNear( target._position, exit._position, _pCatalog->getRules()._interactRange ) == false )
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
        const SurvivorActivity activity = _listSurvivor[static_cast<size_t>( survivor )]._activity;
        if ( activity == SurvivorActivity::Repairing || activity == SurvivorActivity::Healing || activity == SurvivorActivity::OpeningGate )
            leaveActivity( survivor );
    }

    bool HorrorMatch::respondSkillCheck( int32 survivor, float32 pressTime )
    {
        InteractionProgress* pProgress = findActivityProgressMutable( survivor );
        return pProgress != nullptr && pProgress->respondSkillCheck( static_cast<uint32>( survivor ), pressTime );
    }

    bool HorrorMatch::rescue( int32 rescuer, int32 hooked )
    {
        if ( isValidSurvivor( rescuer ) == false || isValidSurvivor( hooked ) == false || rescuer == hooked || isEnded() )
            return false;
        HorrorSurvivor& helper = _listSurvivor[static_cast<size_t>( rescuer )];
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( hooked )];
        if ( helper.canAct() == false || target._state != SurvivorState::Hooked ||
             isNear( helper._position, target._position, _pCatalog->getRules()._interactRange ) == false )
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
        if ( isValidSurvivor( survivor ) == false || pallet < 0 || pallet >= static_cast<int32>( _listPallet.size() ) || isEnded() )
            return false;
        HorrorSurvivor&              target   = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules& rules    = _pCatalog->getRules();
        const float3&                position = _listPallet[static_cast<size_t>( pallet )];
        if ( target.canAct() == false || _listPalletState[static_cast<size_t>( pallet )] != PalletState::Upright ||
             isNear( target._position, position, rules._interactRange ) == false )
            return false;
        leaveActivity( survivor );
        _listPalletState[static_cast<size_t>( pallet )] = PalletState::Dropped;
        pushEvent( AsymmetricHorrorEvent::Kind::PalletDropped, survivor, pallet, 0.0f, position );
        if ( _killer._stunRemaining <= 0.0f && isNear( _killer._position, position, rules._palletStunRange ) )
        {
            // 판자에 맞은 살인마는 기절하고, 들고 있던 생존자를 떨어뜨린다.
            if ( _killer._carrying >= 0 )
                dropCarried( false, 0.0f );
            stunKiller( rules._palletStunTime );
            awardScore( target._score, "PalletStun", 1.0f );
        }
        return true;
    }

    bool HorrorMatch::vault( int32 survivor, int32 window )
    {
        if ( isValidSurvivor( survivor ) == false || window < 0 || window >= static_cast<int32>( _listWindow.size() ) || isEnded() )
            return false;
        HorrorSurvivor&              target = _listSurvivor[static_cast<size_t>( survivor )];
        HorrorWindow&                frame  = _listWindow[static_cast<size_t>( window )];
        const AsymmetricHorrorRules& rules  = _pCatalog->getRules();
        if ( target.canAct() == false || frame._blockedRemaining > 0.0f || isNear( target._position, frame._position, rules._interactRange ) == false )
            return false;
        leaveActivity( survivor );
        const bool bFast       = target._lastMoveSpeed >= rules._survivorSpeed * rules._fastVaultSpeedRatio;
        target._activity       = SurvivorActivity::Vaulting;
        target._activityTarget = window;
        target._vaultRemaining = bFast ? rules._fastVaultTime : rules._mediumVaultTime;
        target._vaultExit      = HorrorMatchInternal::computeVaultExit( target._position, frame._position );
        pushEvent( AsymmetricHorrorEvent::Kind::Vaulted, survivor, window, bFast ? 1.0f : 0.0f, frame._position );
        awardScore( target._score, "Vault", 1.0f );
        if ( bFast )
            makeNoise( survivor, rules._vaultNoiseRadius, frame._position );

        // 추격 중(위협 반경 안) 같은 창을 여러 번 넘으면 창이 막힌다.
        if ( isNear( _killer._position, frame._position, _killer._pDef->_terrorRadius ) )
        {
            ++frame._chaseVaultCount;
            if ( frame._chaseVaultCount >= rules._windowBlockCount )
            {
                frame._chaseVaultCount  = 0;
                frame._blockedRemaining = rules._windowBlockTime;
                pushEvent( AsymmetricHorrorEvent::Kind::WindowBlocked, survivor, window, rules._windowBlockTime, frame._position );
            }
        }
        return true;
    }

    bool HorrorMatch::enterLocker( int32 survivor, int32 locker )
    {
        if ( isValidSurvivor( survivor ) == false || locker < 0 || locker >= static_cast<int32>( _listLocker.size() ) || isEnded() )
            return false;
        HorrorSurvivor& target   = _listSurvivor[static_cast<size_t>( survivor )];
        const float3&   position = _listLocker[static_cast<size_t>( locker )];
        if ( target.canAct() == false || _listLockerOccupant[static_cast<size_t>( locker )] >= 0 ||
             isNear( target._position, position, _pCatalog->getRules()._interactRange ) == false )
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
            bCanLeave = bCanLeave || ( gate._progress.isCompleted() && isNear( target._position, gate._position, rules._interactRange ) );
        if ( bCanLeave == false && _bHatchOpen == SW_TRUE && isNear( target._position, _hatchPosition, rules._interactRange ) )
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
        if ( _killer._pDef == nullptr || _killer.canAct() == false || isEnded() )
            return;
        const float3  move   = HorrorMatchInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= HorrorMatchInternal::kTiny )
            return;
        _killer._forward = move / length;
        _killer._position += move * ( computeKillerSpeed() * deltaTime );
        if ( _killer._carrying >= 0 )
            _listSurvivor[static_cast<size_t>( _killer._carrying )]._position = _killer._position;
    }

    KillerAttackResult HorrorMatch::killerAttack()
    {
        if ( _killer._pDef == nullptr || _killer.canAct() == false || _killer._carrying >= 0 || _killer._attackCooldown > 0.0f || isEnded() )
            return KillerAttackResult::NotReady;
        const HorrorKillerDef& def    = *_killer._pDef;
        const float32          minDot = MathUtil::cos( MathUtil::toRadian( def._lungeAngle * 0.5f ) );
        int32                  victim = -1;
        float32                best   = MathUtil::MaxFloat;
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const HorrorSurvivor& candidate = _listSurvivor[static_cast<size_t>( index )];
            const bool            bStanding = candidate._state == SurvivorState::Healthy || candidate._state == SurvivorState::Injured;
            if ( bStanding == false || candidate._activity == SurvivorActivity::InLocker )
                continue;
            const float32 distance = HorrorMatchInternal::computeFlatDistance( _killer._position, candidate._position );
            if ( distance > def._lungeRange || distance >= best )
                continue;
            if ( distance > HorrorMatchInternal::kTiny )
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
            _killer._attackCooldown = def._missCooldown;
            pushEvent( AsymmetricHorrorEvent::Kind::Missed, -1, -1, 0.0f, _killer._position );
            return KillerAttackResult::Missed;
        }

        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( victim )];
        leaveActivity( victim );
        _killer._attackCooldown = def._hitCooldown;
        _match.reportDamage( _killer._participant, target._participant, 1.0f );
        (void)target._vitality.applyDamage( 1.0f, 0.0f, -1 );
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
        target._state          = SurvivorState::Injured;
        target._hasteRemaining = _pCatalog->getRules()._hitHasteTime;
        pushEvent( AsymmetricHorrorEvent::Kind::Hit, victim, -1, 0.0f, target._position );
        return KillerAttackResult::Hit;
    }

    bool HorrorMatch::pickUp( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false || _killer.canAct() == false || _killer._carrying >= 0 || isEnded() )
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
        if ( _killer._carrying < 0 || hook < 0 || hook >= static_cast<int32>( _listHook.size() ) || _killer.canAct() == false || isEnded() )
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
        if ( target._hookStage >= rules._maxHookStage )
        {
            sacrifice( survivor, false ); // 마지막 걸림은 바로 희생
            return true;
        }
        target._state = SurvivorState::Hooked;
        return true;
    }

    bool HorrorMatch::kickGenerator( int32 generator )
    {
        if ( generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || _killer.canAct() == false || _killer._carrying >= 0 || isEnded() )
            return false;
        HorrorGenerator&             gen       = _listGenerator[static_cast<size_t>( generator )];
        const AsymmetricHorrorRules& rules     = _pCatalog->getRules();
        const bool                   bKickable = gen._progress.isCompleted() == false && gen._bBlocked == SW_FALSE && gen._bKicked == SW_FALSE && gen._progress.getProgress() > 0.0f &&
                               gen._progress.getParticipantCount() == 0;
        if ( bKickable == false || isNear( _killer._position, gen._position, rules._interactRange ) == false )
            return false;
        const float32 progress = MathUtil::max( 0.0f, gen._progress.getProgress() - rules._kickPenalty );
        configureGenerator( generator, true );
        gen._progress.setProgress( progress );
        gen._bKicked = SW_TRUE;
        awardScore( _killer._score, "Kick", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::GeneratorKicked, -1, generator, progress, gen._position );
        return true;
    }

    bool HorrorMatch::breakPallet( int32 pallet )
    {
        if ( pallet < 0 || pallet >= static_cast<int32>( _listPallet.size() ) || _killer.canAct() == false || isEnded() )
            return false;
        if ( _listPalletState[static_cast<size_t>( pallet )] != PalletState::Dropped ||
             isNear( _killer._position, _listPallet[static_cast<size_t>( pallet )], _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining  = _pCatalog->getRules()._palletBreakTime;
        _killer._breakingPallet = pallet;
        return true;
    }

    bool HorrorMatch::killerVault( int32 window )
    {
        if ( window < 0 || window >= static_cast<int32>( _listWindow.size() ) || _killer.canAct() == false || isEnded() )
            return false;
        const HorrorWindow& frame = _listWindow[static_cast<size_t>( window )];
        if ( isNear( _killer._position, frame._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining = _pCatalog->getRules()._killerVaultTime;
        _killer._busyExit      = HorrorMatchInternal::computeVaultExit( _killer._position, frame._position );
        _killer._bVaulting     = SW_TRUE;
        pushEvent( AsymmetricHorrorEvent::Kind::Vaulted, -1, window, 0.0f, frame._position );
        return true;
    }

    bool HorrorMatch::searchLocker( int32 locker )
    {
        if ( locker < 0 || locker >= static_cast<int32>( _listLocker.size() ) || _killer.canAct() == false || _killer._carrying >= 0 || isEnded() )
            return false;
        const float3& position = _listLocker[static_cast<size_t>( locker )];
        if ( isNear( _killer._position, position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining = _pCatalog->getRules()._lockerSearchTime;
        const int32 occupant   = _listLockerOccupant[static_cast<size_t>( locker )];
        if ( occupant < 0 )
            return false;

        // 숨은 생존자는 체력과 상관없이 바로 들린다.
        HorrorSurvivor& target = _listSurvivor[static_cast<size_t>( occupant )];
        leaveActivity( occupant );
        releaseHealers( occupant, true );
        while ( target._vitality.isAlive() )
            (void)target._vitality.applyDamage( 1.0f, 0.0f, -1 );
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
        if ( _bHatchOpen == SW_FALSE || _killer.canAct() == false || isEnded() ||
             isNear( _killer._position, _hatchPosition, _pCatalog->getRules()._interactRange ) == false )
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
        if ( _killer._pDef == nullptr || _killer.canAct() == false || _killer._abilityCooldown > 0.0f || isEnded() )
            return false;
        _killer._abilityCooldown = _killer._pDef->_abilityCooldown;
        return true;
    }

    // --- 읽기 ------------------------------------------------------------------------------------

    float32 HorrorMatch::computeHeartbeat( int32 survivor ) const
    {
        if ( isValidSurvivor( survivor ) == false || _killer._pDef == nullptr || _killer._pDef->_terrorRadius <= 0.0f )
            return 0.0f;
        const float32 distance = HorrorMatchInternal::computeFlatDistance( _killer._position, _listSurvivor[static_cast<size_t>( survivor )]._position );
        return MathUtil::saturate( 1.0f - distance / _killer._pDef->_terrorRadius );
    }

    float32 HorrorMatch::computeSurvivorSpeed( int32 survivor ) const
    {
        if ( isValidSurvivor( survivor ) == false || _pCatalog == nullptr )
            return 0.0f;
        const HorrorSurvivor&        target = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules& rules  = _pCatalog->getRules();
        if ( target._state == SurvivorState::Dying )
            return rules._crawlSpeed;
        if ( target.canAct() == false || target._activity == SurvivorActivity::InLocker )
            return 0.0f;
        return rules._survivorSpeed * ( target._hasteRemaining > 0.0f ? rules._hitHasteScale : 1.0f );
    }

    float32 HorrorMatch::computeKillerSpeed() const
    {
        if ( _killer._pDef == nullptr || _pCatalog == nullptr || _killer.canAct() == false )
            return 0.0f;
        float32 speed = _pCatalog->getRules()._survivorSpeed * _killer._pDef->_speedRatio;
        if ( _killer._attackCooldown > 0.0f )
            speed *= _killer._pDef->_cooldownSpeedScale;
        if ( _killer._carrying >= 0 )
            speed *= _killer._pDef->_carrySpeedScale;
        return speed;
    }

    void HorrorMatch::collectKillerStimuli( vector<AiStimulus>& outListStimulusEntry ) const
    {
        outListStimulusEntry.clear();
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const HorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( index )];
            const bool            bVisible = survivor.isStanding() && survivor._state != SurvivorState::Carried && survivor._activity != SurvivorActivity::InLocker;
            if ( bVisible == false )
                continue;
            AiStimulus stimulus;
            stimulus._position    = survivor._position;
            stimulus._id          = static_cast<uint64>( index + 1 );
            stimulus._noiseRadius = survivor._noiseRemaining > 0.0f ? survivor._noiseRadius : 0.0f;
            outListStimulusEntry.push_back( stimulus );
        }
    }

    const InteractionProgress* HorrorMatch::findActivityProgress( int32 survivor ) const
    {
        return const_cast<HorrorMatch*>( this )->findActivityProgressMutable( survivor );
    }

    const HorrorGenerator* HorrorMatch::findGenerator( int32 generator ) const
    {
        return generator >= 0 && generator < static_cast<int32>( _listGenerator.size() ) ? &_listGenerator[static_cast<size_t>( generator )] : nullptr;
    }

    const HorrorGate* HorrorMatch::findGate( int32 gate ) const
    {
        return gate >= 0 && gate < static_cast<int32>( _listGate.size() ) ? &_listGate[static_cast<size_t>( gate )] : nullptr;
    }

    const HorrorWindow* HorrorMatch::findWindow( int32 window ) const
    {
        return window >= 0 && window < static_cast<int32>( _listWindow.size() ) ? &_listWindow[static_cast<size_t>( window )] : nullptr;
    }

    PalletState HorrorMatch::getPalletState( int32 pallet ) const
    {
        return pallet >= 0 && pallet < static_cast<int32>( _listPalletState.size() ) ? _listPalletState[static_cast<size_t>( pallet )] : PalletState::Broken;
    }

    int32 HorrorMatch::countStandingSurvivors() const
    {
        int32 count = 0;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
            count += survivor.isStanding() ? 1 : 0;
        return count;
    }

    void HorrorMatch::makeSnapshot( HorrorSnapshot& outSnapshot ) const
    {
        outSnapshot                    = HorrorSnapshot{};
        outSnapshot._tick              = _tick;
        outSnapshot._phase             = _match.getPhase();
        outSnapshot._killerPosition    = _killer._position;
        outSnapshot._killerStun        = _killer._stunRemaining;
        outSnapshot._killerCooldown    = _killer._attackCooldown;
        outSnapshot._killerCarrying    = _killer._carrying;
        outSnapshot._collapseRemaining = _collapseRemaining;
        outSnapshot._bGatesPowered     = _bGatesPowered;
        outSnapshot._bHatchOpen        = _bHatchOpen;
        outSnapshot._bCollapseStarted  = _bCollapseStarted;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            HorrorSurvivorSnapshot entry;
            entry._position       = survivor._position;
            entry._hookTimer      = survivor._hookTimer;
            entry._healProgress   = survivor._healing.getProgress();
            entry._bleedout       = survivor._vitality.isDowned() ? survivor._vitality.getDownedHealth() : 0.0f;
            entry._wiggleProgress = survivor._wiggleProgress;
            entry._hookStage      = survivor._hookStage;
            entry._state          = survivor._state;
            entry._activity       = survivor._activity;
            outSnapshot._listSurvivor.push_back( entry );
        }
        for ( const HorrorGenerator& generator : _listGenerator )
        {
            uint8 flag = 0;
            flag |= generator._progress.isCompleted() ? 1u : 0u;
            flag |= generator._bKicked == SW_TRUE ? 2u : 0u;
            flag |= generator._bBlocked == SW_TRUE ? 4u : 0u;
            outSnapshot._listGeneratorProgress.push_back( generator._progress.getProgress() );
            outSnapshot._listGeneratorFlag.push_back( flag );
        }
        for ( const HorrorGate& gate : _listGate )
            outSnapshot._listGateProgress.push_back( gate._progress.getProgress() );
        outSnapshot._listPalletState = _listPalletState;
        for ( const HorrorWindow& window : _listWindow )
            outSnapshot._listWindowBlocked.push_back( window._blockedRemaining > 0.0f ? SW_TRUE : SW_FALSE );
    }

    void HorrorMatch::writeState( BitWriter& outWriter ) const
    {
        HorrorSnapshot snapshot;
        makeSnapshot( snapshot );
        HorrorSnapshotCodec::write( snapshot, outWriter );
    }

    void HorrorMatch::drainEvents( vector<AsymmetricHorrorEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    bool HorrorMatch::isNear( const float3& from, const float3& to, float32 range ) const
    {
        return HorrorMatchInternal::computeFlatDistance( from, to ) <= range;
    }

    void HorrorMatch::step( float32 deltaTime )
    {
        ++_tick;
        if ( isEnded() == false )
        {
            stepKiller( deltaTime );
            for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
                stepSurvivor( survivor, deltaTime );
            stepGenerators( deltaTime );
            stepGates( deltaTime );
            stepEndgame( deltaTime );
            resolveEnd();
        }
        _match.update( deltaTime );
    }

    void HorrorMatch::stepKiller( float32 deltaTime )
    {
        _killer._attackCooldown  = MathUtil::max( 0.0f, _killer._attackCooldown - deltaTime );
        _killer._abilityCooldown = MathUtil::max( 0.0f, _killer._abilityCooldown - deltaTime );
        _killer._stunRemaining   = MathUtil::max( 0.0f, _killer._stunRemaining - deltaTime );
        if ( _killer._busyRemaining <= 0.0f )
            return;
        _killer._busyRemaining -= deltaTime;
        if ( _killer._busyRemaining > 0.0f )
            return;
        _killer._busyRemaining = 0.0f;
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
        survivor._hasteRemaining              = MathUtil::max( 0.0f, survivor._hasteRemaining - deltaTime );
        survivor._noiseRemaining              = MathUtil::max( 0.0f, survivor._noiseRemaining - deltaTime );
        if ( survivor._noiseRemaining <= 0.0f )
            survivor._noiseRadius = 0.0f;

        if ( survivor._activity == SurvivorActivity::Vaulting )
        {
            survivor._vaultRemaining -= deltaTime;
            if ( survivor._vaultRemaining <= 0.0f )
            {
                survivor._vaultRemaining = 0.0f;
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
                    survivor._wiggleProgress += deltaTime / rules._wiggleTime;
                if ( survivor._wiggleProgress >= 1.0f )
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::WiggledFree, survivorIndex, -1, 0.0f, survivor._position );
                    dropCarried( true, rules._wiggleStunTime );
                }
                break;
            }
            case SurvivorState::Hooked:
            {
                survivor._hookTimer += deltaTime;
                if ( survivor._hookStage >= 2 )
                {
                    // 몸부림 단계 — 멈추면 희생이 앞당겨진다.
                    survivor._struggleIdle = survivor._bStruggling == SW_TRUE ? 0.0f : survivor._struggleIdle + deltaTime;
                    if ( survivor._struggleIdle > rules._struggleGrace )
                    {
                        sacrifice( survivorIndex, false );
                        return;
                    }
                }
                if ( survivor._hookTimer >= rules._hookStageTime )
                {
                    survivor._hookTimer = 0.0f;
                    survivor._hookStage += 1;
                    if ( survivor._hookStage >= rules._maxHookStage )
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

        // 이 생존자를 치료하는 진행 — 치료하는 쪽들이 붙어 있다.
        if ( survivor._healing.getParticipantCount() > 0 )
        {
            survivor._healing.update( deltaTime );
            if ( drainProgress( survivor._healing, survivor._position ) )
                finishHeal( survivorIndex );
        }
        survivor._lastMoveSpeed = 0.0f; // 이동은 걸음 사이에 들어온다 — 다음 걸음까지만 빠른 넘기 판정에 쓴다
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

            // 다 고쳤다 — 고치던 사람은 손을 뗀다.
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
        const AsymmetricHorrorRules& rules    = _pCatalog->getRules();
        const int32                  standing = countStandingSurvivors();
        if ( _bHatchPlaced == SW_TRUE && _bHatchOpen == SW_FALSE && _bHatchClosed == SW_FALSE && standing > 0 && standing <= rules._hatchSurvivorCount )
        {
            _bHatchOpen = SW_TRUE;
            pushEvent( AsymmetricHorrorEvent::Kind::HatchOpened, -1, -1, 0.0f, _hatchPosition );
        }
        if ( _bCollapseStarted == SW_FALSE || _collapseRemaining <= 0.0f )
            return;
        bool bSlowed = false;
        for ( const HorrorSurvivor& survivor : _listSurvivor )
        {
            const bool bHelpless = survivor._state == SurvivorState::Dying || survivor._state == SurvivorState::Carried || survivor._state == SurvivorState::Hooked;
            bSlowed              = bSlowed || bHelpless;
        }
        _collapseRemaining -= deltaTime * ( bSlowed ? rules._collapseSlowScale : 1.0f );
        if ( _collapseRemaining > 0.0f )
            return;
        // 붕괴가 끝났다 — 아직 남은 생존자는 모두 희생된다.
        _collapseRemaining = 0.0f;
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
            const int32 actor = static_cast<int32>( event._actorId );
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
                        makeNoise( actor, _pCatalog->getRules()._skillCheckNoiseRadius, position );
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
            restoreInjured( target ); // 빈사 → 부상
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
        (void)survivor._vitality.applyDamage( 1.0f, 0.0f, -1 );
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
                survivor._vaultRemaining = 0.0f;
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
        _killer._stunRemaining  = MathUtil::max( _killer._stunRemaining, seconds );
        _killer._busyRemaining  = 0.0f;
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
        _bCollapseStarted  = SW_TRUE;
        _collapseRemaining = _pCatalog->getRules()._collapseTime;
        pushEvent( AsymmetricHorrorEvent::Kind::CollapseStarted, -1, -1, _collapseRemaining, float3{} );
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
        // 희생이 많으면 살인마, 탈출이 많으면 생존자, 같으면 무승부입니다.
        const int32 winner = sacrificed > escaped ? _killerTeam : ( escaped > sacrificed ? _survivorTeam : -1 );
        _bEndReported      = SW_TRUE;
        _match.endMatch( winner );
        pushEvent( AsymmetricHorrorEvent::Kind::MatchEnded, -1, escaped, static_cast<float32>( sacrificed ), float3{} );
    }

    void HorrorMatch::syncHealthState( HorrorSurvivor& survivor )
    {
        if ( survivor._state != SurvivorState::Healthy && survivor._state != SurvivorState::Injured && survivor._state != SurvivorState::Dying )
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
        float32       value = outScore.getValue( pRule->_category, 0.0f ) + pRule->_points * amount;
        if ( cap > 0.0f )
            value = MathUtil::min( value, cap );
        outScore.setValue( pRule->_category, value );
    }

    void HorrorMatch::configureGenerator( int32 generator, bool bRegressing )
    {
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        InteractionConfig            config;
        config._duration             = rules._repairTime;
        config._listParticipantScale = rules._listRepairScale;
        config._gradeBonus           = rules._skillCheckBonus;
        config._regressionRate       = bRegressing ? rules._kickRegression : 0.0f;
        config._skillCheckInterval   = rules._repairSkillCheckInterval;
        config._skillCheckLeadTime   = rules._skillCheckLeadTime;
        config._skillCheckPenalty    = rules._skillCheckFailPenalty;
        config._maxParticipants      = rules._repairMaxParticipants;

        // 다시 설정해도 진행량은 남긴다(걷어차기 퇴행을 켜고 끄는 데 쓴다).
        HorrorGenerator& target   = _listGenerator[static_cast<size_t>( generator )];
        const float32    progress = target._progress.getProgress();
        target._progress.initialize( config, &_pCatalog->getJudge(),
                                     _settings._seed ^ ( static_cast<uint32>( generator + 1 ) * HorrorMatchInternal::kGeneratorSeedMix ) ^ _tick );
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
            target._noiseRemaining = _settings._fixedStep * 2.0f;
        }
        pushEvent( AsymmetricHorrorEvent::Kind::Noise, survivor, -1, radius, position );
    }

    void HorrorMatch::pushEvent( AsymmetricHorrorEvent::Kind kind, int32 actor, int32 target, float32 value, const float3& position )
    {
        AsymmetricHorrorEvent event;
        event._kind     = kind;
        event._actor    = actor;
        event._target   = target;
        event._value    = value;
        event._position = position;
        _listEvent.push_back( event );
    }
} // namespace sw

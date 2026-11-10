#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorMatch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Net/NetMessageChannel.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorSnapshot.h"
#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

namespace sw
{
    namespace
    {
        struct AsymmetricHorrorMatchInternal
        {
            static constexpr float32 kNeverReviveTime = 1.0e9f; ///< 빈사 회복은 치료 진행이 정하므로, HealthStateMachine 의 부활 시계는 멈춤용으로만 쓴다
            static constexpr uint32  kHealSeedMix     = 0x85EBCA6Bu;
            static constexpr uint32  kGateSeedMix     = 0xC2B2AE35u;
            static constexpr float32 kTiny            = 1.0e-5f;

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
            static constexpr uint32 kGeneratorSeedMix = HashUtil::kGoldenRatio32;
        };
    } // namespace

    AsymmetricHorrorMatch::AsymmetricHorrorMatch()
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
        , _eventBuffer{}
        , _listInteractionScratch{}
        , _match{}
        , _timer{}
        , _hatchPosition{}
        , _pCatalog{ nullptr }
        , _collapseRemaining{}
        , _completedGeneratorCount{ 0 }
        , _survivorTeam{ constant::invalid_index::kInt32 }
        , _killerTeam{ constant::invalid_index::kInt32 }
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

    bool AsymmetricHorrorMatch::initialize( const AsymmetricHorrorMatchSettings& settings, const AsymmetricHorrorRulesCatalog* pCatalog )
    {
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "AsymmetricHorrorMatch.AsymmetricHorrorMatchRefusedSettings", "AsymmetricHorrorMatch: refused settings - %#", rangeError.c_str() );
            return false;
        }
        _settings = settings;
        _pCatalog = pCatalog;
        _killer   = AsymmetricHorrorKiller{};
        _listSurvivor.clear();
        _listGenerator.clear();
        _listGate.clear();
        _listWindow.clear();
        _listHook.clear();
        _listPallet.clear();
        _listPalletState.clear();
        _listLocker.clear();
        _listLockerOccupant.clear();
        _eventBuffer.clear();
        _match.initialize( MatchSettings{} );
        _timer = FixedStepTimer( _settings._fixedStep, constant::time::kDefaultMaxFrameSeconds );
        _collapseRemaining.clear();
        _completedGeneratorCount = 0;
        _survivorTeam            = constant::invalid_index::kInt32;
        _killerTeam              = constant::invalid_index::kInt32;
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
        _killer._pDef = _pCatalog->findKiller( _settings._killerID );
        if ( _killer._pDef == nullptr )
        {
            SW_LOG_WARNING_ID( "AsymmetricHorrorMatch.UnknownKiller", "unknown killer '%#'", _settings._killerID.c_str() );
            return false;
        }
        return true;
    }

    expected<int32> AsymmetricHorrorMatch::addSurvivor( const float3& position )
    {
        if ( _pCatalog == nullptr || _bStarted == SW_TRUE )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no catalog or the match has started" } );
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        const int32                  index = static_cast<int32>( _listSurvivor.size() );

        HealthSettings healthMachine;
        healthMachine._maxHealth               = 2.0f;
        healthMachine._bDownedEnabled          = SW_TRUE;
        healthMachine._downedHealth            = rules._bleedoutTime;
        healthMachine._bleedoutRate            = 1.0f;
        healthMachine._reviveTime              = AsymmetricHorrorMatchInternal::kNeverReviveTime;
        healthMachine._invulnerableAfterRevive = 0.0f;
        healthMachine._bDamageInterruptsRevive = SW_FALSE;

        InteractionConfig heal;
        heal._duration             = rules._heal._time;
        heal._listParticipantScale = rules._heal._listScale;
        heal._gradeBonus           = rules._skillCheck._bonus;
        heal._skillCheckInterval   = rules._heal._skillCheckInterval;
        heal._skillCheckLeadTime   = rules._skillCheck._leadTime;
        heal._skillCheckPenalty    = rules._skillCheck._failPenalty;
        heal._maxParticipants      = rules._heal._maxHealers;

        AsymmetricHorrorSurvivor survivor;
        if ( survivor._healthMachine.initialize( healthMachine ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the survivor health settings are out of range" } );
        if ( survivor._healing.initialize( heal, &_pCatalog->getJudge(), GameRandom::deriveSeed( _settings._seed, static_cast<uint32>( index + 1 ) * AsymmetricHorrorMatchInternal::kHealSeedMix ) ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the heal settings are out of range" } );
        survivor._position = float3{ position._x, 0.0f, position._z };
        _listSurvivor.push_back( survivor );
        return index;
    }

    void AsymmetricHorrorMatch::setKillerPosition( const float3& position )
    {
        _killer._position = float3{ position._x, 0.0f, position._z };
    }

    expected<int32> AsymmetricHorrorMatch::addGenerator( const float3& position )
    {
        if ( _pCatalog == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no catalog" } );
        AsymmetricHorrorGenerator generator;
        generator._position = float3{ position._x, 0.0f, position._z };
        _listGenerator.push_back( generator );
        const int32 index = static_cast<int32>( _listGenerator.size() ) - 1;
        if ( configureGenerator( index, false ) == false )
        {
            _listGenerator.pop_back(); // 수리 규칙이 범위 밖이면 발전기를 세우지 않는다
            return unexpected( Error{ ErrorKind::InvalidArgument, "the repair settings are out of range" } );
        }
        return index;
    }

    int32 AsymmetricHorrorMatch::addHook( const float3& position )
    {
        _listHook.push_back( float3{ position._x, 0.0f, position._z } );
        return static_cast<int32>( _listHook.size() ) - 1;
    }

    int32 AsymmetricHorrorMatch::addPallet( const float3& position )
    {
        _listPallet.push_back( float3{ position._x, 0.0f, position._z } );
        _listPalletState.push_back( PalletState::Upright );
        return static_cast<int32>( _listPallet.size() ) - 1;
    }

    int32 AsymmetricHorrorMatch::addWindow( const float3& position )
    {
        AsymmetricHorrorWindow window;
        window._position = float3{ position._x, 0.0f, position._z };
        _listWindow.push_back( window );
        return static_cast<int32>( _listWindow.size() ) - 1;
    }

    int32 AsymmetricHorrorMatch::addLocker( const float3& position )
    {
        _listLocker.push_back( float3{ position._x, 0.0f, position._z } );
        _listLockerOccupant.push_back( constant::invalid_index::kInt32 );
        return static_cast<int32>( _listLocker.size() ) - 1;
    }

    expected<int32> AsymmetricHorrorMatch::addGate( const float3& position )
    {
        if ( _pCatalog == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "no catalog" } );
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        InteractionConfig            config;
        config._duration        = rules._endgame._gateTime;
        config._maxParticipants = 1;
        AsymmetricHorrorGate gate;
        gate._position    = float3{ position._x, 0.0f, position._z };
        const int32 index = static_cast<int32>( _listGate.size() );
        if ( gate._progress.initialize( config, nullptr, GameRandom::deriveSeed( _settings._seed, static_cast<uint32>( index + 1 ) * AsymmetricHorrorMatchInternal::kGateSeedMix ) ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the gate time is out of range" } ); // 규칙의 문 시간이 범위 밖이다 - initialize 가 이유를 남겼다
        _listGate.push_back( gate );
        return index;
    }

    void AsymmetricHorrorMatch::setHatchPosition( const float3& position )
    {
        _hatchPosition = float3{ position._x, 0.0f, position._z };
        _bHatchPlaced  = SW_TRUE;
    }

    void AsymmetricHorrorMatch::start()
    {
        const bool bCannotStart = _bStarted == SW_TRUE || _pCatalog == nullptr || _killer._pDef == nullptr;
        if ( bCannotStart )
            return;
        _match.initialize( makeMatchSettings() );
        _survivorTeam = _match.addTeam( hashed_string( "Survivors" ) );
        _killerTeam   = _match.addTeam( hashed_string( "Killer" ) );
        for ( AsymmetricHorrorSurvivor& survivor : _listSurvivor )
        {
            survivor._participant = _match.addParticipant( _survivorTeam, hashed_string( "Survivor" ) ).value(); // 팀은 바로 위에서 만들었다
        }
        _killer._participant = _match.addParticipant( _killerTeam, hashed_string( "Killer" ) ).value();
        _match.start();
        _bStarted = SW_TRUE;
    }

    void AsymmetricHorrorMatch::update( float32 deltaTime )
    {
        if ( _bStarted == SW_FALSE )
            return;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step( _timer.getStep() );
        }
    }

    // --- 읽기 ------------------------------------------------------------------------------------

    float32 AsymmetricHorrorMatch::computeHeartbeat( int32 survivor ) const
    {
        const bool bNoHeartbeat = isValidSurvivor( survivor ) == false || _killer._pDef == nullptr || _killer._pDef->_terrorRadius <= 0.0f;
        if ( bNoHeartbeat )
            return 0.0f;
        const float32 distance = float3::getHorizontalDistance( _killer._position, _listSurvivor[static_cast<size_t>( survivor )]._position );
        return MathUtil::saturate( 1.0f - distance / _killer._pDef->_terrorRadius );
    }

    float32 AsymmetricHorrorMatch::computeSurvivorSpeed( int32 survivor ) const
    {
        if ( isValidSurvivor( survivor ) == false || _pCatalog == nullptr )
            return 0.0f;
        const AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules&    rules  = _pCatalog->getRules();
        if ( target._state == SurvivorState::Dying )
            return rules._crawlSpeed;
        if ( target.canAct() == false || target._activity == SurvivorActivity::InLocker )
            return 0.0f;
        return rules._survivorSpeed * ( target._hasteRemaining.isActive() ? rules._hitHasteScale : 1.0f );
    }

    float32 AsymmetricHorrorMatch::computeKillerSpeed() const
    {
        const bool bKillerCannotMove = _killer._pDef == nullptr || _pCatalog == nullptr || _killer.canAct() == false;
        if ( bKillerCannotMove )
            return 0.0f;
        float32 speed = _pCatalog->getRules()._survivorSpeed * _killer._pDef->_speedRatio;
        if ( _killer._attackCooldown.isActive() )
            speed *= _killer._pDef->_cooldownSpeedScale;
        if ( _killer._carrying >= 0 )
            speed *= _killer._pDef->_carrySpeedScale;
        return speed;
    }

    void AsymmetricHorrorMatch::collectKillerStimuli( vector<AIStimulus>& outListStimulusEntry ) const
    {
        outListStimulusEntry.clear();
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( index )];
            const bool                      bVisible = survivor.isStanding() && survivor._state != SurvivorState::Carried && survivor._activity != SurvivorActivity::InLocker;
            if ( bVisible == false )
                continue;
            AIStimulus stimulus;
            stimulus._position    = survivor._position;
            stimulus._id          = static_cast<uint64>( index + 1 );
            stimulus._noiseRadius = survivor._noiseRemaining.isActive() ? survivor._noiseRadius : 0.0f;
            outListStimulusEntry.push_back( stimulus );
        }
    }

    const InteractionProgress* AsymmetricHorrorMatch::findActivityProgress( int32 survivor ) const
    {
        return const_cast<AsymmetricHorrorMatch*>( this )->findActivityProgressMutable( survivor );
    }

    const AsymmetricHorrorGenerator* AsymmetricHorrorMatch::findGenerator( int32 generator ) const
    {
        return generator >= 0 && generator < static_cast<int32>( _listGenerator.size() ) ? &_listGenerator[static_cast<size_t>( generator )] : nullptr;
    }

    const AsymmetricHorrorGate* AsymmetricHorrorMatch::findGate( int32 gate ) const
    {
        return gate >= 0 && gate < static_cast<int32>( _listGate.size() ) ? &_listGate[static_cast<size_t>( gate )] : nullptr;
    }

    const AsymmetricHorrorWindow* AsymmetricHorrorMatch::findWindow( int32 window ) const
    {
        return window >= 0 && window < static_cast<int32>( _listWindow.size() ) ? &_listWindow[static_cast<size_t>( window )] : nullptr;
    }

    PalletState AsymmetricHorrorMatch::getPalletState( int32 pallet ) const
    {
        return pallet >= 0 && pallet < static_cast<int32>( _listPalletState.size() ) ? _listPalletState[static_cast<size_t>( pallet )] : PalletState::Broken;
    }

    int32 AsymmetricHorrorMatch::countStandingSurvivors() const
    {
        int32 count = 0;
        for ( const AsymmetricHorrorSurvivor& survivor : _listSurvivor )
        {
            count += survivor.isStanding() ? 1 : 0;
        }
        return count;
    }

    void AsymmetricHorrorMatch::pollEvents( vector<AsymmetricHorrorEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    bool AsymmetricHorrorMatch::isNear( const float3& from, const float3& to, float32 range ) const
    {
        return float3::getHorizontalDistance( from, to ) <= range;
    }

    void AsymmetricHorrorMatch::pushEvent( AsymmetricHorrorEvent::Kind kind, optional<int32> actor, optional<int32> target, float32 value, const float3& position )
    {
        AsymmetricHorrorEvent event;
        event._kind     = kind;
        event._actor    = actor;
        event._target   = target;
        event._value    = value;
        event._position = position;
        _eventBuffer.push( event );
    }

    // --- 생존자 행동 ------------------------------------------------------------------------------

    void AsymmetricHorrorMatch::moveSurvivor( int32 survivor, const float3& direction, float32 deltaTime )
    {
        if ( isValidSurvivor( survivor ) == false || isEnded() )
            return;
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        target._lastMoveSpeed            = 0.0f;
        const bool bCrawling             = target._state == SurvivorState::Dying;
        if ( target.canAct() == false && bCrawling == false )
            return;
        if ( target._activity == SurvivorActivity::InLocker )
            return;
        const float3  move   = AsymmetricHorrorMatchInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= AsymmetricHorrorMatchInternal::kTiny )
            return;
        leaveActivity( survivor );
        const float32 speed = computeSurvivorSpeed( survivor );
        target._position += move * ( speed * deltaTime );
        target._lastMoveSpeed = length * speed;
        releaseFarHealers( survivor );
    }

    bool AsymmetricHorrorMatch::startRepair( int32 survivor, int32 generator )
    {
        const bool bInvalidRepairRequest = isValidSurvivor( survivor ) == false || generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || isEnded();
        if ( bInvalidRepairRequest )
            return false;
        AsymmetricHorrorSurvivor&  target        = _listSurvivor[static_cast<size_t>( survivor )];
        AsymmetricHorrorGenerator& gen           = _listGenerator[static_cast<size_t>( generator )];
        const bool                 bCannotRepair = target.canAct() == false || gen._bBlocked == SW_TRUE || gen._progress.isCompleted() ||
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

    bool AsymmetricHorrorMatch::startHeal( int32 healer, int32 patient )
    {
        const bool bInvalidHealRequest = isValidSurvivor( healer ) == false || isValidSurvivor( patient ) == false || healer == patient || isEnded();
        if ( bInvalidHealRequest )
            return false;
        AsymmetricHorrorSurvivor& doctor      = _listSurvivor[static_cast<size_t>( healer )];
        AsymmetricHorrorSurvivor& target      = _listSurvivor[static_cast<size_t>( patient )];
        const bool                bHurt       = target._state == SurvivorState::Injured || target._state == SurvivorState::Dying;
        const bool                bCannotHeal = doctor.canAct() == false || bHurt == false || isNear( doctor._position, target._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotHeal )
            return false;
        if ( doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient )
            return true;
        leaveActivity( healer );
        if ( target._healing.join( static_cast<uint32>( healer ) ) == false )
            return false;
        if ( target._state == SurvivorState::Dying )
            (void)target._healthMachine.startRevive( healer, 0.0f ); // 치료받는 동안 출혈이 멈춘다
        doctor._activity       = SurvivorActivity::Healing;
        doctor._activityTarget = patient;
        return true;
    }

    bool AsymmetricHorrorMatch::startOpenGate( int32 survivor, int32 gate )
    {
        const bool bInvalidGateRequest = isValidSurvivor( survivor ) == false || gate < 0 || gate >= static_cast<int32>( _listGate.size() ) || _bGatesPowered == SW_FALSE || isEnded();
        if ( bInvalidGateRequest )
            return false;
        AsymmetricHorrorSurvivor& target          = _listSurvivor[static_cast<size_t>( survivor )];
        AsymmetricHorrorGate&     exit            = _listGate[static_cast<size_t>( gate )];
        const bool                bCannotOpenGate = target.canAct() == false || exit._progress.isCompleted() || isNear( target._position, exit._position, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotOpenGate )
            return false;
        leaveActivity( survivor );
        if ( exit._progress.join( static_cast<uint32>( survivor ) ) == false )
            return false;
        target._activity       = SurvivorActivity::OpeningGate;
        target._activityTarget = gate;
        return true;
    }

    void AsymmetricHorrorMatch::stopActivity( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false )
            return;
        const SurvivorActivity activity               = _listSurvivor[static_cast<size_t>( survivor )]._activity;
        const bool             bInterruptibleActivity = activity == SurvivorActivity::Repairing || activity == SurvivorActivity::Healing || activity == SurvivorActivity::OpeningGate;
        if ( bInterruptibleActivity )
            leaveActivity( survivor );
    }

    bool AsymmetricHorrorMatch::respondSkillCheck( int32 survivor, float32 pressTime )
    {
        InteractionProgress* pProgress = findActivityProgressMutable( survivor );
        if ( pProgress == nullptr || pProgress->respondSkillCheck( static_cast<uint32>( survivor ), pressTime ) == false )
            return false;
        // 보너스로 끝날 수 있다. 다음 걸음으로 미루면 끝난 발전기를 건너뛰어 완료 수가 오르지 않았고, 그 사이의 세이브는 완료 이벤트를 잃는다. 탈출구에는 판정기가 없다.
        const AsymmetricHorrorSurvivor& actor = _listSurvivor[static_cast<size_t>( survivor )];
        if ( actor._activity == SurvivorActivity::Repairing )
            settleGenerator( actor._activityTarget );
        else if ( actor._activity == SurvivorActivity::Healing )
            settleHealing( actor._activityTarget );
        return true;
    }

    bool AsymmetricHorrorMatch::rescue( int32 rescuer, int32 hooked )
    {
        const bool bInvalidRescueRequest = isValidSurvivor( rescuer ) == false || isValidSurvivor( hooked ) == false || rescuer == hooked || isEnded();
        if ( bInvalidRescueRequest )
            return false;
        AsymmetricHorrorSurvivor& helper        = _listSurvivor[static_cast<size_t>( rescuer )];
        AsymmetricHorrorSurvivor& target        = _listSurvivor[static_cast<size_t>( hooked )];
        const bool                bCannotRescue = helper.canAct() == false || target._state != SurvivorState::Hooked ||
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

    bool AsymmetricHorrorMatch::dropPallet( int32 survivor, int32 pallet )
    {
        const bool bInvalidPalletRequest = isValidSurvivor( survivor ) == false || pallet < 0 || pallet >= static_cast<int32>( _listPallet.size() ) || isEnded();
        if ( bInvalidPalletRequest )
            return false;
        AsymmetricHorrorSurvivor&    target            = _listSurvivor[static_cast<size_t>( survivor )];
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
            awardScore( target._score, "PalletStun", 1.0f );
        }
        return true;
    }

    bool AsymmetricHorrorMatch::vault( int32 survivor, int32 window )
    {
        const bool bInvalidVaultRequest = isValidSurvivor( survivor ) == false || window < 0 || window >= static_cast<int32>( _listWindow.size() ) || isEnded();
        if ( bInvalidVaultRequest )
            return false;
        AsymmetricHorrorSurvivor&    target       = _listSurvivor[static_cast<size_t>( survivor )];
        AsymmetricHorrorWindow&      frame        = _listWindow[static_cast<size_t>( window )];
        const AsymmetricHorrorRules& rules        = _pCatalog->getRules();
        const bool                   bCannotVault = target.canAct() == false || frame._blockedRemaining > 0.0f || isNear( target._position, frame._position, rules._interactRange ) == false;
        if ( bCannotVault )
            return false;
        leaveActivity( survivor );
        const bool bFast       = target._lastMoveSpeed >= rules._survivorSpeed * rules._window._fastSpeedRatio;
        target._activity       = SurvivorActivity::Vaulting;
        target._activityTarget = window;
        target._vaultRemaining.start( bFast ? rules._window._fastTime : rules._window._mediumTime );
        target._vaultExit = AsymmetricHorrorMatchInternal::computeVaultExit( target._position, frame._position );
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

    bool AsymmetricHorrorMatch::enterLocker( int32 survivor, int32 locker )
    {
        const bool bInvalidLockerRequest = isValidSurvivor( survivor ) == false || locker < 0 || locker >= static_cast<int32>( _listLocker.size() ) || isEnded();
        if ( bInvalidLockerRequest )
            return false;
        AsymmetricHorrorSurvivor& target             = _listSurvivor[static_cast<size_t>( survivor )];
        const float3&             position           = _listLocker[static_cast<size_t>( locker )];
        const bool                bCannotEnterLocker = target.canAct() == false || _listLockerOccupant[static_cast<size_t>( locker )] >= 0 ||
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

    void AsymmetricHorrorMatch::exitLocker( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) && _listSurvivor[static_cast<size_t>( survivor )]._activity == SurvivorActivity::InLocker )
            leaveActivity( survivor );
    }

    bool AsymmetricHorrorMatch::escape( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false || isEnded() )
            return false;
        AsymmetricHorrorSurvivor&    target = _listSurvivor[static_cast<size_t>( survivor )];
        const AsymmetricHorrorRules& rules  = _pCatalog->getRules();
        if ( target.canAct() == false )
            return false;
        bool bThroughHatch = false;
        bool bCanLeave     = false;
        for ( const AsymmetricHorrorGate& gate : _listGate )
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
        pushEvent( AsymmetricHorrorEvent::Kind::Escaped, survivor, nullopt, bThroughHatch ? 1.0f : 0.0f, target._position );
        awardScore( target._score, "Escape", 1.0f );
        resolveEnd();
        return true;
    }

    void AsymmetricHorrorMatch::setStruggling( int32 survivor, bool bStruggling )
    {
        if ( isValidSurvivor( survivor ) )
            _listSurvivor[static_cast<size_t>( survivor )]._bStruggling = bStruggling ? SW_TRUE : SW_FALSE;
    }

    void AsymmetricHorrorMatch::setWiggling( int32 survivor, bool bWiggling )
    {
        if ( isValidSurvivor( survivor ) )
            _listSurvivor[static_cast<size_t>( survivor )]._bWiggling = bWiggling ? SW_TRUE : SW_FALSE;
    }

    // --- 살인마 행동 ------------------------------------------------------------------------------

    void AsymmetricHorrorMatch::moveKiller( const float3& direction, float32 deltaTime )
    {
        const bool bKillerCannotMove = _killer._pDef == nullptr || _killer.canAct() == false || isEnded();
        if ( bKillerCannotMove )
            return;
        const float3  move   = AsymmetricHorrorMatchInternal::clampDirection( direction );
        const float32 length = move.getLength();
        if ( length <= AsymmetricHorrorMatchInternal::kTiny )
            return;
        _killer._forward = move / length;
        _killer._position += move * ( computeKillerSpeed() * deltaTime );
        if ( _killer._carrying >= 0 )
            _listSurvivor[static_cast<size_t>( _killer._carrying )]._position = _killer._position;
    }

    KillerAttackResult AsymmetricHorrorMatch::killerAttack()
    {
        const bool bKillerNotReady = _killer._pDef == nullptr || _killer.canAct() == false || _killer._carrying >= 0 || _killer._attackCooldown.isActive() || isEnded();
        if ( bKillerNotReady )
            return KillerAttackResult::NotReady;
        const AsymmetricHorrorKillerDef& def    = *_killer._pDef;
        const float32                    minDot = MathUtil::cos( def._lungeAngle * 0.5f );
        int32                            victim = constant::invalid_index::kInt32;
        float32                          best   = MathUtil::kMaxFloat;
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& candidate = _listSurvivor[static_cast<size_t>( index )];
            const bool                      bStanding = candidate._state == SurvivorState::Healthy || candidate._state == SurvivorState::Injured;
            if ( bStanding == false || candidate._activity == SurvivorActivity::InLocker )
                continue;
            const float32 distance = float3::getHorizontalDistance( _killer._position, candidate._position );
            if ( distance > def._lungeRange || distance >= best )
                continue;
            if ( distance > AsymmetricHorrorMatchInternal::kTiny )
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
            pushEvent( AsymmetricHorrorEvent::Kind::Missed, nullopt, nullopt, 0.0f, _killer._position );
            return KillerAttackResult::Missed;
        }

        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( victim )];
        leaveActivity( victim );
        _killer._attackCooldown.start( def._hitCooldown );
        _match.reportDamage( _killer._participant, target._participant, 1.0f );
        (void)target._healthMachine.applyDamage( 1.0f, 0.0f, nullopt ); // 결과는 아래 pollEvents 가 이벤트로 읽는다
        target._healthMachine.discardEvents();
        awardScore( _killer._score, "Hit", 1.0f );
        if ( target._healthMachine.isDowned() )
        {
            target._state = SurvivorState::Dying;
            releaseHealers( victim, true );
            pushEvent( AsymmetricHorrorEvent::Kind::Downed, victim, nullopt, 0.0f, target._position );
            return KillerAttackResult::Downed;
        }
        target._state = SurvivorState::Injured;
        target._hasteRemaining.start( _pCatalog->getRules()._hitHasteTime );
        pushEvent( AsymmetricHorrorEvent::Kind::Hit, victim, nullopt, 0.0f, target._position );
        return KillerAttackResult::Hit;
    }

    bool AsymmetricHorrorMatch::pickUp( int32 survivor )
    {
        const bool bCannotPickUp = isValidSurvivor( survivor ) == false || _killer.canAct() == false || _killer._carrying >= 0 || isEnded();
        if ( bCannotPickUp )
            return false;
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        if ( target._state != SurvivorState::Dying || isNear( _killer._position, target._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        releaseHealers( survivor, true );
        target._healthMachine.stopRevive();
        target._state          = SurvivorState::Carried;
        target._wiggleProgress = 0.0f;
        target._position       = _killer._position;
        _killer._carrying      = survivor;
        pushEvent( AsymmetricHorrorEvent::Kind::PickedUp, survivor, nullopt, 0.0f, target._position );
        return true;
    }

    bool AsymmetricHorrorMatch::hookCarried( int32 hook )
    {
        const bool bCannotHook = _killer._carrying < 0 || hook < 0 || hook >= static_cast<int32>( _listHook.size() ) || _killer.canAct() == false || isEnded();
        if ( bCannotHook )
            return false;
        const AsymmetricHorrorRules& rules    = _pCatalog->getRules();
        const float3&                position = _listHook[static_cast<size_t>( hook )];
        if ( isNear( _killer._position, position, rules._interactRange ) == false )
            return false;
        const int32               survivor = _killer._carrying;
        AsymmetricHorrorSurvivor& target   = _listSurvivor[static_cast<size_t>( survivor )];
        _killer._carrying                  = constant::invalid_index::kInt32;
        target._position                   = position;
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

    bool AsymmetricHorrorMatch::kickGenerator( int32 generator )
    {
        const bool bCannotKickGenerator = generator < 0 || generator >= static_cast<int32>( _listGenerator.size() ) || _killer.canAct() == false || _killer._carrying >= 0 || isEnded();
        if ( bCannotKickGenerator )
            return false;
        AsymmetricHorrorGenerator&   gen       = _listGenerator[static_cast<size_t>( generator )];
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
        pushEvent( AsymmetricHorrorEvent::Kind::GeneratorKicked, nullopt, generator, progress, gen._position );
        return true;
    }

    bool AsymmetricHorrorMatch::breakPallet( int32 pallet )
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

    bool AsymmetricHorrorMatch::killerVault( int32 window )
    {
        const bool bCannotKillerVault = window < 0 || window >= static_cast<int32>( _listWindow.size() ) || _killer.canAct() == false || isEnded();
        if ( bCannotKillerVault )
            return false;
        const AsymmetricHorrorWindow& frame = _listWindow[static_cast<size_t>( window )];
        if ( isNear( _killer._position, frame._position, _pCatalog->getRules()._interactRange ) == false )
            return false;
        _killer._busyRemaining.start( _pCatalog->getRules()._window._killerTime );
        _killer._busyExit  = AsymmetricHorrorMatchInternal::computeVaultExit( _killer._position, frame._position );
        _killer._bVaulting = SW_TRUE;
        pushEvent( AsymmetricHorrorEvent::Kind::Vaulted, nullopt, window, 0.0f, frame._position );
        return true;
    }

    bool AsymmetricHorrorMatch::searchLocker( int32 locker )
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
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( occupant )];
        leaveActivity( occupant );
        releaseHealers( occupant, true );
        while ( target._healthMachine.isAlive() )
        {
            (void)target._healthMachine.applyDamage( 1.0f, 0.0f, nullopt ); // 결과는 아래 pollEvents 가 이벤트로 읽는다
        }
        target._healthMachine.discardEvents();
        target._state          = SurvivorState::Carried;
        target._wiggleProgress = 0.0f;
        target._position       = _killer._position;
        _killer._carrying      = occupant;
        awardScore( _killer._score, "LockerGrab", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::LockerGrab, occupant, locker, 0.0f, position );
        pushEvent( AsymmetricHorrorEvent::Kind::PickedUp, occupant, nullopt, 0.0f, position );
        return true;
    }

    bool AsymmetricHorrorMatch::closeHatch()
    {
        const bool bCannotCloseHatch = _bHatchOpen == SW_FALSE || _killer.canAct() == false || isEnded() ||
                                       isNear( _killer._position, _hatchPosition, _pCatalog->getRules()._interactRange ) == false;
        if ( bCannotCloseHatch )
            return false;
        _bHatchOpen   = SW_FALSE;
        _bHatchClosed = SW_TRUE;
        pushEvent( AsymmetricHorrorEvent::Kind::HatchClosed, nullopt, nullopt, 0.0f, _hatchPosition );
        if ( _bGatesPowered == SW_FALSE )
        {
            // 해치를 닫으면 탈출구에 전원이 들어오고 붕괴가 시작된다.
            _bGatesPowered = SW_TRUE;
            pushEvent( AsymmetricHorrorEvent::Kind::GatesPowered, nullopt, nullopt, 0.0f, _hatchPosition );
        }
        startCollapse();
        return true;
    }

    bool AsymmetricHorrorMatch::useKillerAbility()
    {
        const bool bAbilityUnavailable = _killer._pDef == nullptr || _killer.canAct() == false || _killer._abilityCooldown.isActive() || isEnded();
        if ( bAbilityUnavailable )
            return false;
        _killer._abilityCooldown.start( _killer._pDef->_abilityCooldown );
        return true;
    }

    InteractionProgress* AsymmetricHorrorMatch::findActivityProgressMutable( int32 survivor )
    {
        if ( isValidSurvivor( survivor ) == false )
            return nullptr;
        const AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
        const int32                     index  = target._activityTarget;
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

    void AsymmetricHorrorMatch::releaseHealers( int32 patient, bool bResetProgress )
    {
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        for ( int32 healer = 0; healer < getSurvivorCount(); ++healer )
        {
            AsymmetricHorrorSurvivor& doctor = _listSurvivor[static_cast<size_t>( healer )];
            if ( doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient )
            {
                (void)target._healing.leave( static_cast<uint32>( healer ) );
                doctor._activity       = SurvivorActivity::None;
                doctor._activityTarget = constant::invalid_index::kInt32;
            }
        }
        if ( bResetProgress )
            target._healing.reset();
        _listInteractionScratch.clear();
        target._healing.pollEvents( _listInteractionScratch );
    }

    void AsymmetricHorrorMatch::releaseFarHealers( int32 patient )
    {
        const float3& position = _listSurvivor[static_cast<size_t>( patient )]._position;
        for ( int32 healer = 0; healer < getSurvivorCount(); ++healer )
        {
            const AsymmetricHorrorSurvivor& doctor  = _listSurvivor[static_cast<size_t>( healer )];
            const bool                      bTooFar = doctor._activity == SurvivorActivity::Healing && doctor._activityTarget == patient && isNear( doctor._position, position, _pCatalog->getRules()._interactRange ) == false;
            if ( bTooFar )
                leaveActivity( healer );
        }
    }

    void AsymmetricHorrorMatch::finishHeal( int32 patient )
    {
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        for ( int32 healer = 0; healer < getSurvivorCount(); ++healer )
        {
            const AsymmetricHorrorSurvivor& doctor = _listSurvivor[static_cast<size_t>( healer )];
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
            (void)target._healthMachine.heal( 1.0f );
            target._state = SurvivorState::Healthy;
        }
        target._healthMachine.discardEvents();
        pushEvent( AsymmetricHorrorEvent::Kind::Healed, nullopt, patient, static_cast<float32>( target._state ), target._position );
    }

    void AsymmetricHorrorMatch::restoreInjured( AsymmetricHorrorSurvivor& survivor )
    {
        survivor._healthMachine.respawn();
        (void)survivor._healthMachine.applyDamage( 1.0f, 0.0f, nullopt ); // 결과는 아래 pollEvents 가 이벤트로 읽는다
        survivor._healthMachine.discardEvents();
        survivor._state          = SurvivorState::Injured;
        survivor._wiggleProgress = 0.0f;
        survivor._bWiggling      = SW_FALSE;
    }

    void AsymmetricHorrorMatch::sacrifice( int32 survivorIndex, bool bBledOut )
    {
        AsymmetricHorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        if ( survivor.isStanding() == false )
            return;
        leaveActivity( survivorIndex );
        releaseHealers( survivorIndex, true );
        if ( _killer._carrying == survivorIndex )
            _killer._carrying = constant::invalid_index::kInt32;
        survivor._healthMachine.kill( nullopt );
        survivor._healthMachine.discardEvents();
        survivor._state    = SurvivorState::Sacrificed;
        survivor._bBledOut = bBledOut ? SW_TRUE : SW_FALSE;
        _match.reportKill( survivor._participant, _killer._participant );
        awardScore( _killer._score, "Sacrifice", 1.0f );
        pushEvent( AsymmetricHorrorEvent::Kind::Sacrificed, survivorIndex, nullopt, bBledOut ? 1.0f : 0.0f, survivor._position );
        resolveEnd();
    }

    void AsymmetricHorrorMatch::leaveActivity( int32 survivorIndex )
    {
        AsymmetricHorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        const int32               target   = survivor._activityTarget;
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
                    AsymmetricHorrorSurvivor& patient = _listSurvivor[static_cast<size_t>( target )];
                    (void)patient._healing.leave( static_cast<uint32>( survivorIndex ) );
                    if ( patient._healing.getParticipantCount() == 0 )
                        patient._healthMachine.stopRevive(); // 아무도 치료하지 않으면 다시 피를 흘린다
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
                    _listLockerOccupant[static_cast<size_t>( target )] = constant::invalid_index::kInt32;
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
        survivor._activityTarget = constant::invalid_index::kInt32;
    }

    void AsymmetricHorrorMatch::dropCarried( bool bStunKiller, float32 stunTime )
    {
        const int32 survivorIndex = _killer._carrying;
        if ( isValidSurvivor( survivorIndex ) == false )
            return;
        _killer._carrying                  = constant::invalid_index::kInt32;
        AsymmetricHorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
        restoreInjured( survivor );
        survivor._position = _killer._position;
        if ( bStunKiller )
            stunKiller( stunTime );
    }

    void AsymmetricHorrorMatch::stunKiller( float32 seconds )
    {
        _killer._stunRemaining.extendTo( seconds );
        _killer._busyRemaining.clear();
        _killer._breakingPallet = constant::invalid_index::kInt32;
        if ( _killer._bVaulting == SW_TRUE )
        {
            _killer._position  = _killer._busyExit;
            _killer._bVaulting = SW_FALSE;
        }
        pushEvent( AsymmetricHorrorEvent::Kind::KillerStunned, nullopt, nullopt, seconds, _killer._position );
    }

    void AsymmetricHorrorMatch::startCollapse()
    {
        if ( _bCollapseStarted == SW_TRUE )
            return;
        _bCollapseStarted = SW_TRUE;
        _collapseRemaining.start( _pCatalog->getRules()._endgame._collapseTime );
        pushEvent( AsymmetricHorrorEvent::Kind::CollapseStarted, nullopt, nullopt, _collapseRemaining.getRemaining(), float3{} );
    }

    void AsymmetricHorrorMatch::resolveEnd()
    {
        if ( _bEndReported == SW_TRUE || countStandingSurvivors() > 0 )
            return;
        int32 sacrificed = 0;
        int32 escaped    = 0;
        for ( const AsymmetricHorrorSurvivor& survivor : _listSurvivor )
        {
            sacrificed += survivor._state == SurvivorState::Sacrificed ? 1 : 0;
            escaped += survivor._state == SurvivorState::Escaped ? 1 : 0;
        }
        // 희생이 많으면 살인마, 탈출이 많으면 생존자가 이기고, 같으면 무승부이다.
        const int32 winner = sacrificed > escaped ? _killerTeam : ( escaped > sacrificed ? _survivorTeam : constant::invalid_index::kInt32 );
        _bEndReported      = SW_TRUE;
        _match.endMatch( winner );
        pushEvent( AsymmetricHorrorEvent::Kind::MatchEnded, nullopt, escaped, static_cast<float32>( sacrificed ), float3{} );
    }

    void AsymmetricHorrorMatch::syncHealthState( AsymmetricHorrorSurvivor& survivor )
    {
        const bool bNotSurvivorHealthState = survivor._state != SurvivorState::Healthy && survivor._state != SurvivorState::Injured && survivor._state != SurvivorState::Dying;
        if ( bNotSurvivorHealthState )
            return;
        if ( survivor._healthMachine.isDowned() )
            survivor._state = SurvivorState::Dying;
        else if ( survivor._healthMachine.getHealth() >= 2.0f )
            survivor._state = SurvivorState::Healthy;
        else
            survivor._state = SurvivorState::Injured;
    }

    void AsymmetricHorrorMatch::awardScore( StatBlock& outScore, const utf8* pAction, float32 amount )
    {
        if ( _pCatalog == nullptr )
            return;
        const AsymmetricHorrorScoreRule* pRule = _pCatalog->findScoreRule( hashed_string( pAction ) );
        if ( pRule == nullptr )
            return;
        const float32 cap   = _pCatalog->getCategoryCap( pRule->_category );
        float32       value = outScore.getValue( pRule->_category ) + pRule->_points * amount;
        if ( cap > 0.0f )
            value = MathUtil::min( value, cap );
        outScore.setValue( pRule->_category, value );
    }

    bool AsymmetricHorrorMatch::configureGenerator( int32 generator, bool bRegressing )
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
        AsymmetricHorrorGenerator& target   = _listGenerator[static_cast<size_t>( generator )];
        const float32              progress = target._progress.getProgress();
        if ( target._progress.initialize( config, &_pCatalog->getJudge(),
                                          GameRandom::deriveSeed( _settings._seed, ( static_cast<uint32>( generator + 1 ) * AsymmetricHorrorMatchInternal::kGeneratorSeedMix ) ^ _tick ) ) == false )
            return false;
        target._progress.setProgress( progress );
        _listInteractionScratch.clear();
        target._progress.pollEvents( _listInteractionScratch );
        target._bKicked = bRegressing ? SW_TRUE : SW_FALSE;
        return true;
    }

    void AsymmetricHorrorMatch::makeNoise( int32 survivor, float32 radius, const float3& position )
    {
        if ( isValidSurvivor( survivor ) )
        {
            AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
            target._noiseRadius              = MathUtil::max( target._noiseRadius, radius );
            target._noiseRemaining.start( _settings._fixedStep * 2.0f );
        }
        pushEvent( AsymmetricHorrorEvent::Kind::Noise, survivor, nullopt, radius, position );
    }

    void AsymmetricHorrorMatch::makeSnapshot( AsymmetricHorrorSnapshot& outSnapshot ) const
    {
        outSnapshot                    = AsymmetricHorrorSnapshot{};
        outSnapshot._tick              = _tick;
        outSnapshot._phase             = _match.getPhase();
        outSnapshot._killerPosition    = _killer._position;
        outSnapshot._killerStun        = _killer._stunRemaining.getRemaining();
        outSnapshot._killerCooldown    = _killer._attackCooldown.getRemaining();
        outSnapshot._killerCarrying    = _killer._carrying;
        outSnapshot._collapseRemaining = _collapseRemaining.getRemaining();
        outSnapshot._bGatesPowered     = _bGatesPowered;
        outSnapshot._bHatchOpen        = _bHatchOpen;
        outSnapshot._bCollapseStarted  = _bCollapseStarted;
        for ( const AsymmetricHorrorSurvivor& survivor : _listSurvivor )
        {
            AsymmetricHorrorSurvivorSnapshot entry;
            entry._position       = survivor._position;
            entry._hookTimer      = survivor._hookTimer;
            entry._healProgress   = survivor._healing.getProgress();
            entry._bleedout       = survivor._healthMachine.isDowned() ? survivor._healthMachine.getDownedHealth() : 0.0f;
            entry._wiggleProgress = survivor._wiggleProgress;
            entry._hookStage      = survivor._hookStage;
            entry._state          = survivor._state;
            entry._activity       = survivor._activity;
            outSnapshot._listSurvivor.push_back( entry );
        }
        for ( const AsymmetricHorrorGenerator& generator : _listGenerator )
        {
            uint8 flag = 0;
            flag |= generator._progress.isCompleted() ? 1u : 0u;
            flag |= generator._bKicked == SW_TRUE ? 2u : 0u;
            flag |= generator._bBlocked == SW_TRUE ? 4u : 0u;
            AsymmetricHorrorGeneratorSnapshot entry;
            entry._progress = generator._progress.getProgress();
            entry._flag     = flag;
            outSnapshot._listGenerator.push_back( entry );
        }
        for ( const AsymmetricHorrorGate& gate : _listGate )
        {
            outSnapshot._listGateProgress.push_back( gate._progress.getProgress() );
        }
        outSnapshot._listPalletState = _listPalletState;
        for ( const AsymmetricHorrorWindow& window : _listWindow )
        {
            outSnapshot._listWindowBlocked.push_back( window._blockedRemaining > 0.0f ? SW_TRUE : SW_FALSE );
        }
    }

    bool AsymmetricHorrorMatch::registerSnapshotMessage( NetMessageChannel& inoutChannel, uint8 kind )
    {
        return inoutChannel.registerMessage<AsymmetricHorrorSnapshot>( kind );
    }

    bool AsymmetricHorrorMatch::writeSnapshotMessage( const NetMessageChannel& channel, uint8 kind, NetMessageWriter& outWriter ) const
    {
        AsymmetricHorrorSnapshot snapshot;
        makeSnapshot( snapshot );
        return channel.writeMessage( kind, snapshot, outWriter );
    }

    void AsymmetricHorrorMatch::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr && _killer._pDef != nullptr ) == false )
            return;
        io.expect( _settings._killerID );
        if ( io.isOk() == false )
            return;
        // 같은 설정과 카탈로그로 새 판을 열고, 무대와 생존자를 add... 로 다시 만든 뒤(규칙에서 오는 설정) 상태를 덮는다.
        if ( io.isReading() )
        {
            AsymmetricHorrorMatch restored;
            if ( io.check( restored.initialize( _settings, _pCatalog ) ) == false )
                return;
            *this = std::move( restored );
        }
        uint8 bStarted = _bStarted;
        io.flag( bStarted );
        io.flag( _bEndReported );
        io.value( _tick );
        io.value( _timer );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _match.initialize( bStarted == SW_TRUE ? makeMatchSettings() : MatchSettings{} );
        }
        io.value( _match );
        io.value( _survivorTeam );
        io.value( _killerTeam );
        io.value( _completedGeneratorCount );
        io.flag( _bGatesPowered );
        io.flag( _bHatchPlaced );
        io.flag( _bHatchOpen );
        io.flag( _bHatchClosed );
        io.flag( _bCollapseStarted );
        io.value( _collapseRemaining );
        io.value( _hatchPosition );
        if ( io.check( 0 <= _completedGeneratorCount ) == false )
            return;

        // 무대: 위치를 먼저 싣고(읽는 쪽이 add... 로 다시 만든다) 그 위에 상태를 덮는다.
        uint32 palletCount = 0;
        serializeStage( io, palletCount );
        if ( io.isOk() == false )
            return;

        uint32 survivorCount = static_cast<uint32>( _listSurvivor.size() );
        // 생존자마다 최소한 위치(12) + 실수, 타이머, 정수 필드(60) + 체력, 치료, 점수
        io.count( 72, survivorCount );
        for ( uint32 index = 0; index < survivorCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listSurvivor[index]._position : float3{};
            io.value( position );
            if ( io.isOk() == false )
                return;
            int32 survivor = static_cast<int32>( index );
            if ( io.isReading() )
            {
                const expected<int32> added = addSurvivor( position );
                if ( io.check( added.has_value() ) == false )
                    return;
                survivor = *added;
            }
            serializeSurvivor( io, _listSurvivor[static_cast<size_t>( survivor )] );
        }
        serializeKiller( io, survivorCount, palletCount );
        if ( io.isWriting() || io.isOk() == false )
            return;
        _bStarted = bStarted;
        io.check( isStateConsistent() );
        _eventBuffer.clear();
    }

    bool AsymmetricHorrorMatch::isStateConsistent() const
    {
        const AsymmetricHorrorRules& rules = _pCatalog->getRules();
        // 판. 시작 전이면 번호가 없고, 시작했으면 팀과 참가자가 맞으며, 결과를 알린 것과 판이 끝난 것이 같다.
        const bool              bStarted        = _bStarted == SW_TRUE;
        const MatchParticipant* pKillerEntry    = _match.findParticipant( _killer._participant );
        const bool              bMatchInvalid   = bStarted ? ( _match.findTeam( _survivorTeam ) == nullptr || _match.findTeam( _killerTeam ) == nullptr || _survivorTeam == _killerTeam || pKillerEntry == nullptr || pKillerEntry->_team != _killerTeam ||
                                                ( _bEndReported == SW_TRUE ) != isEnded() )
                                                           : ( _survivorTeam != constant::invalid_index::kInt32 || _killerTeam != constant::invalid_index::kInt32 || _killer._participant != constant::invalid_index::kInt32 || _bEndReported == SW_TRUE );
        const bool              bEndgameInvalid = ( _bHatchOpen == SW_TRUE && ( _bHatchPlaced == SW_FALSE || _bHatchClosed == SW_TRUE ) ) || ( _bCollapseStarted == SW_FALSE && _collapseRemaining.isActive() );
        if ( bMatchInvalid || bEndgameInvalid )
            return false;

        // 발전기. 완료 수는 끝난 발전기의 수이고, 그만큼 고쳤으면 전원이 들어와 있고, 막힘은 전원 뒤에 끝나지 않은 발전기에만 있다.
        int32 completedCount = 0;
        for ( const AsymmetricHorrorGenerator& generator : _listGenerator )
        {
            const bool bCompleted = generator._progress.isCompleted();
            completedCount += bCompleted ? 1 : 0;
            if ( generator._bBlocked == SW_TRUE && ( _bGatesPowered == SW_FALSE || bCompleted ) )
                return false;
        }
        if ( completedCount != _completedGeneratorCount || ( completedCount >= rules._generatorsRequired && _bGatesPowered == SW_FALSE ) )
            return false;

        for ( const AsymmetricHorrorWindow& window : _listWindow )
        {
            if ( window._blockedRemaining < 0.0f || window._chaseVaultCount < 0 || window._chaseVaultCount >= rules._window._blockCount )
                return false;
        }
        const bool bBreakingInvalid = _killer._breakingPallet >= 0 && _listPalletState[static_cast<size_t>( _killer._breakingPallet )] != PalletState::Dropped;
        if ( bBreakingInvalid )
            return false;

        // 생존자마다 상태 · 체력 · 갈고리 · 하던 일. 진행의 참가자는 그 일을 하는 사람이고, 들린 사람은 살인마가 든 사람이다.
        vector<int32> listRepairer( _listGenerator.size(), 0 );
        vector<int32> listOpener( _listGate.size(), 0 );
        vector<int32> listHealer( _listSurvivor.size(), 0 );
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const AsymmetricHorrorSurvivor& survivor      = _listSurvivor[static_cast<size_t>( index )];
            const HealthStateMachine&       healthMachine = survivor._healthMachine;
            bool                            bBodyValid    = false;
            switch ( survivor._state )
            {
                case SurvivorState::Healthy:
                    bBodyValid = healthMachine.isAlive() && healthMachine.getHealth() >= 2.0f;
                    break;
                case SurvivorState::Injured:
                    bBodyValid = healthMachine.isAlive() && healthMachine.getHealth() < 2.0f;
                    break;
                case SurvivorState::Dying:
                case SurvivorState::Carried:
                    bBodyValid = healthMachine.isDowned();
                    break;
                case SurvivorState::Hooked:
                    bBodyValid = healthMachine.isDowned() && 1 <= survivor._hookStage && survivor._hookStage < rules._hook._stages;
                    break;
                case SurvivorState::Sacrificed:
                    bBodyValid = healthMachine.isDead();
                    break;
                case SurvivorState::Escaped:
                    bBodyValid = healthMachine.isAlive();
                    break;
            }
            const MatchParticipant* pEntry        = _match.findParticipant( survivor._participant );
            const bool              bEntryInvalid = bStarted ? ( pEntry == nullptr || pEntry->_team != _survivorTeam ) : survivor._participant != constant::invalid_index::kInt32;
            const bool              bCarryInvalid = ( survivor._state == SurvivorState::Carried ) != ( _killer._carrying == index );
            if ( bBodyValid == false || bEntryInvalid || bCarryInvalid || survivor._hookStage > rules._hook._stages )
                return false;

            const int32 target = survivor._activityTarget;
            if ( survivor._activity == SurvivorActivity::None )
            {
                if ( target != constant::invalid_index::kInt32 )
                    return false;
                continue;
            }
            // 하던 일은 움직일 수 있는 상태에서만 있다. 맞거나 들리면 `leaveActivity` 가 뗀다.
            if ( survivor._state != SurvivorState::Healthy && survivor._state != SurvivorState::Injured )
                return false;
            const uint32 actor = static_cast<uint32>( index );
            switch ( survivor._activity )
            {
                case SurvivorActivity::Repairing:
                {
                    const bool bInvalid = target < 0 || target >= static_cast<int32>( _listGenerator.size() ) || _listGenerator[static_cast<size_t>( target )]._progress.isCompleted() || _listGenerator[static_cast<size_t>( target )]._progress.hasParticipant( actor ) == false;
                    if ( bInvalid )
                        return false;
                    ++listRepairer[static_cast<size_t>( target )];
                    break;
                }
                case SurvivorActivity::Healing:
                {
                    const bool bInvalid = isValidSurvivor( target ) == false || target == index || _listSurvivor[static_cast<size_t>( target )]._healing.hasParticipant( actor ) == false;
                    if ( bInvalid )
                        return false;
                    ++listHealer[static_cast<size_t>( target )];
                    break;
                }
                case SurvivorActivity::OpeningGate:
                {
                    const bool bInvalid = target < 0 || target >= static_cast<int32>( _listGate.size() ) || _listGate[static_cast<size_t>( target )]._progress.isCompleted() || _listGate[static_cast<size_t>( target )]._progress.hasParticipant( actor ) == false;
                    if ( bInvalid )
                        return false;
                    ++listOpener[static_cast<size_t>( target )];
                    break;
                }
                case SurvivorActivity::Vaulting:
                {
                    if ( target < 0 || target >= static_cast<int32>( _listWindow.size() ) )
                        return false;
                    break;
                }
                case SurvivorActivity::InLocker:
                {
                    if ( target < 0 || target >= static_cast<int32>( _listLocker.size() ) || _listLockerOccupant[static_cast<size_t>( target )] != index )
                        return false;
                    break;
                }
                case SurvivorActivity::None:
                {
                    break;
                }
            }
        }

        // 거꾸로도 본다. 끝난 진행에는 떠나지 않은 참가자가 남아 있고, 그 밖의 참가자는 위에서 센 사람뿐이다.
        for ( size_t index = 0; index < _listGenerator.size(); ++index )
        {
            const InteractionProgress& progress = _listGenerator[index]._progress;
            if ( progress.isCompleted() == false && progress.getParticipantCount() != listRepairer[index] )
                return false;
        }
        for ( size_t index = 0; index < _listGate.size(); ++index )
        {
            const InteractionProgress& progress = _listGate[index]._progress;
            if ( progress.isCompleted() == false && progress.getParticipantCount() != listOpener[index] )
                return false;
        }
        for ( size_t index = 0; index < _listSurvivor.size(); ++index )
        {
            if ( _listSurvivor[index]._healing.getParticipantCount() != listHealer[index] )
                return false;
        }
        for ( size_t index = 0; index < _listLockerOccupant.size(); ++index )
        {
            const int32 occupant = _listLockerOccupant[index];
            if ( occupant == constant::invalid_index::kInt32 )
                continue;
            const bool bInvalid = isValidSurvivor( occupant ) == false || _listSurvivor[static_cast<size_t>( occupant )]._activity != SurvivorActivity::InLocker || _listSurvivor[static_cast<size_t>( occupant )]._activityTarget != static_cast<int32>( index );
            if ( bInvalid )
                return false;
        }
        return true;
    }

    MatchSettings AsymmetricHorrorMatch::makeMatchSettings() const
    {
        MatchSettings settings;
        settings._bRespawn = SW_FALSE;
        return settings;
    }

    void AsymmetricHorrorMatch::serializeSurvivor( StateIO& io, AsymmetricHorrorSurvivor& survivor ) const
    {
        io.value( survivor._healthMachine );
        io.value( survivor._healing );
        io.value( survivor._score );
        io.value( survivor._lastMoveSpeed );
        io.value( survivor._hasteRemaining );
        io.value( survivor._hookTimer );
        io.value( survivor._struggleIdle );
        io.value( survivor._wiggleProgress );
        io.value( survivor._vaultRemaining );
        io.value( survivor._noiseRadius );
        io.value( survivor._noiseRemaining );
        io.value( survivor._vaultExit );
        io.value( survivor._hookStage );
        io.value( survivor._activityTarget );
        io.value( survivor._participant );
        io.enumValue( survivor._state, SurvivorState::Escaped );
        io.enumValue( survivor._activity, SurvivorActivity::InLocker );
        io.flag( survivor._bStruggling );
        io.flag( survivor._bWiggling );
        io.flag( survivor._bBledOut );
        io.check( 0.0f <= survivor._lastMoveSpeed && 0.0f <= survivor._hookTimer && 0.0f <= survivor._struggleIdle && 0.0f <= survivor._wiggleProgress && survivor._wiggleProgress < 1.0f && 0.0f <= survivor._noiseRadius && 0 <= survivor._hookStage );
    }

    void AsymmetricHorrorMatch::serializeStage( StateIO& io, uint32& inoutPalletCount )
    {
        uint32 hookCount = static_cast<uint32>( _listHook.size() );
        io.count( 12, hookCount );
        for ( uint32 index = 0; index < hookCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listHook[index] : float3{};
            io.value( position );
            if ( io.isReading() )
            {
                (void)addHook( position );
            }
        }
        inoutPalletCount = static_cast<uint32>( _listPallet.size() );
        io.count( 13, inoutPalletCount );
        for ( uint32 index = 0; index < inoutPalletCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listPallet[index] : float3{};
            uint8  state    = io.isWriting() ? static_cast<uint8>( _listPalletState[index] ) : uint8( 0 );
            io.value( position );
            io.value( state );
            if ( io.check( state <= static_cast<uint8>( PalletState::Broken ) ) == false )
                return;
            if ( io.isReading() )
            {
                _listPalletState[static_cast<size_t>( addPallet( position ) )] = static_cast<PalletState>( state );
            }
        }
        uint32 lockerCount = static_cast<uint32>( _listLocker.size() );
        io.count( 16, lockerCount );
        for ( uint32 index = 0; index < lockerCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listLocker[index] : float3{};
            int32  occupant = io.isWriting() ? _listLockerOccupant[index] : constant::invalid_index::kInt32;
            io.value( position );
            io.value( occupant );
            if ( io.isReading() )
            {
                _listLockerOccupant[static_cast<size_t>( addLocker( position ) )] = occupant;
            }
        }
        uint32 windowCount = static_cast<uint32>( _listWindow.size() );
        io.count( 20, windowCount );
        for ( uint32 index = 0; index < windowCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listWindow[index]._position : float3{};
            io.value( position );
            AsymmetricHorrorWindow& window = _listWindow[io.isWriting() ? static_cast<size_t>( index ) : static_cast<size_t>( addWindow( position ) )];
            io.value( window._blockedRemaining );
            io.value( window._chaseVaultCount );
        }
        uint32 generatorCount = static_cast<uint32>( _listGenerator.size() );
        io.count( 14, generatorCount );
        for ( uint32 index = 0; index < generatorCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listGenerator[index]._position : float3{};
            uint8  bKicked  = io.isWriting() ? _listGenerator[index]._bKicked : uint8( SW_FALSE );
            uint8  bBlocked = io.isWriting() ? _listGenerator[index]._bBlocked : uint8( SW_FALSE );
            io.value( position );
            io.flag( bKicked );
            io.flag( bBlocked );
            if ( io.isOk() == false )
                return;
            int32 generator = static_cast<int32>( index );
            if ( io.isReading() )
            {
                const expected<int32> added = addGenerator( position );
                if ( io.check( added.has_value() && configureGenerator( *added, bKicked == SW_TRUE ) ) == false ) // 걷어차인 발전기는 퇴행 규칙을 쓴다
                    return;
                generator                                                  = *added;
                _listGenerator[static_cast<size_t>( generator )]._bBlocked = bBlocked;
            }
            io.value( _listGenerator[static_cast<size_t>( generator )]._progress );
        }
        uint32 gateCount = static_cast<uint32>( _listGate.size() );
        io.count( 12, gateCount );
        for ( uint32 index = 0; index < gateCount && io.isOk(); ++index )
        {
            float3 position = io.isWriting() ? _listGate[index]._position : float3{};
            io.value( position );
            if ( io.isOk() == false )
                return;
            int32 gate = static_cast<int32>( index );
            if ( io.isReading() )
            {
                const expected<int32> added = addGate( position );
                if ( io.check( added.has_value() ) == false )
                    return;
                gate = *added;
            }
            io.value( _listGate[static_cast<size_t>( gate )]._progress );
        }
    }

    void AsymmetricHorrorMatch::serializeKiller( StateIO& io, uint32 survivorCount, uint32 palletCount )
    {
        AsymmetricHorrorKiller& killer = _killer;
        io.value( killer._score );
        io.value( killer._position );
        io.value( killer._forward );
        io.value( killer._busyExit );
        io.value( killer._attackCooldown );
        io.value( killer._stunRemaining );
        io.value( killer._busyRemaining );
        io.value( killer._abilityCooldown );
        io.value( killer._carrying );
        io.value( killer._breakingPallet );
        io.value( killer._participant );
        io.flag( killer._bVaulting );
        io.check( constant::invalid_index::kInt32 <= killer._carrying && killer._carrying < static_cast<int32>( survivorCount ) && constant::invalid_index::kInt32 <= killer._breakingPallet && killer._breakingPallet < static_cast<int32>( palletCount ) );
    }

    void AsymmetricHorrorMatch::step( float32 deltaTime )
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
            stepWindows( deltaTime );
            stepEndgame( deltaTime );
            resolveEnd();
        }
        _match.update( deltaTime );
    }

    void AsymmetricHorrorMatch::stepKiller( float32 deltaTime )
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
            _killer._breakingPallet                         = constant::invalid_index::kInt32;
            awardScore( _killer._score, "BreakPallet", 1.0f );
            pushEvent( AsymmetricHorrorEvent::Kind::PalletBroken, nullopt, pallet, 0.0f, _listPallet[static_cast<size_t>( pallet )] );
        }
        if ( _killer._bVaulting == SW_TRUE )
        {
            _killer._position  = _killer._busyExit;
            _killer._bVaulting = SW_FALSE;
            if ( _killer._carrying >= 0 )
                _listSurvivor[static_cast<size_t>( _killer._carrying )]._position = _killer._position;
        }
    }

    void AsymmetricHorrorMatch::stepSurvivor( int32 survivorIndex, float32 deltaTime )
    {
        AsymmetricHorrorSurvivor&    survivor = _listSurvivor[static_cast<size_t>( survivorIndex )];
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
                survivor._activityTarget = constant::invalid_index::kInt32;
                releaseFarHealers( survivorIndex );
            }
        }

        switch ( survivor._state )
        {
            case SurvivorState::Healthy:
            case SurvivorState::Injured:
            case SurvivorState::Dying:
            {
                survivor._healthMachine.update( deltaTime );
                survivor._healthMachine.discardEvents();
                if ( survivor._healthMachine.isDead() )
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
                    pushEvent( AsymmetricHorrorEvent::Kind::WiggledFree, survivorIndex, nullopt, 0.0f, survivor._position );
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
                    pushEvent( AsymmetricHorrorEvent::Kind::HookStageAdvanced, survivorIndex, nullopt, static_cast<float32>( survivor._hookStage ), survivor._position );
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
            settleHealing( survivorIndex );
        }
        survivor._lastMoveSpeed = 0.0f; // 이동은 걸음 사이에 들어오므로, 다음 걸음까지만 빠른 넘기 판정에 쓴다
    }

    void AsymmetricHorrorMatch::settleHealing( int32 patient )
    {
        AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( patient )];
        if ( drainProgress( target._healing, target._position ) )
            finishHeal( patient );
    }

    void AsymmetricHorrorMatch::stepGenerators( float32 deltaTime )
    {
        for ( int32 index = 0; index < static_cast<int32>( _listGenerator.size() ); ++index )
        {
            AsymmetricHorrorGenerator& generator = _listGenerator[static_cast<size_t>( index )];
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
                    AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
                    if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == index )
                        awardScore( target._score, "Repair", gained / static_cast<float32>( MathUtil::max( 1, participantCount ) ) );
                }
            }
            // 다 줄면 퇴행 규칙도 내린다. 표시만 내리면 다음에 고치다 손을 뗄 때 걷어차지 않아도 줄었다.
            if ( generator._bKicked == SW_TRUE && generator._progress.getProgress() <= 0.0f )
                (void)configureGenerator( index, false ); // 같은 규칙을 `addGenerator` 가 이미 받았다
            settleGenerator( index );
        }
    }

    void AsymmetricHorrorMatch::settleGenerator( int32 generatorIndex )
    {
        AsymmetricHorrorGenerator& generator = _listGenerator[static_cast<size_t>( generatorIndex )];
        if ( drainProgress( generator._progress, generator._position ) == false )
            return;

        // 다 고쳤다. 고치던 사람은 손을 뗀다.
        for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
        {
            AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
            if ( target._activity == SurvivorActivity::Repairing && target._activityTarget == generatorIndex )
            {
                target._activity       = SurvivorActivity::None;
                target._activityTarget = constant::invalid_index::kInt32;
            }
        }
        generator._bKicked = SW_FALSE;
        ++_completedGeneratorCount;
        pushEvent( AsymmetricHorrorEvent::Kind::GeneratorCompleted, nullopt, generatorIndex, static_cast<float32>( _completedGeneratorCount ), generator._position );
        if ( _completedGeneratorCount < _pCatalog->getRules()._generatorsRequired || _bGatesPowered == SW_TRUE )
            return;
        _bGatesPowered = SW_TRUE;
        for ( int32 other = 0; other < static_cast<int32>( _listGenerator.size() ); ++other )
        {
            AsymmetricHorrorGenerator& rest = _listGenerator[static_cast<size_t>( other )];
            if ( rest._progress.isCompleted() == false )
                rest._bBlocked = SW_TRUE;
        }
        for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
        {
            if ( _listSurvivor[static_cast<size_t>( survivor )]._activity == SurvivorActivity::Repairing )
                leaveActivity( survivor );
        }
        pushEvent( AsymmetricHorrorEvent::Kind::GatesPowered, nullopt, nullopt, 0.0f, generator._position );
    }

    void AsymmetricHorrorMatch::stepGates( float32 deltaTime )
    {
        for ( int32 index = 0; index < static_cast<int32>( _listGate.size() ); ++index )
        {
            AsymmetricHorrorGate& gate = _listGate[static_cast<size_t>( index )];
            if ( gate._progress.isCompleted() || gate._progress.getParticipantCount() == 0 )
                continue;
            gate._progress.update( deltaTime );
            if ( drainProgress( gate._progress, gate._position ) == false )
                continue;
            for ( int32 survivor = 0; survivor < getSurvivorCount(); ++survivor )
            {
                AsymmetricHorrorSurvivor& target = _listSurvivor[static_cast<size_t>( survivor )];
                if ( target._activity == SurvivorActivity::OpeningGate && target._activityTarget == index )
                {
                    target._activity       = SurvivorActivity::None;
                    target._activityTarget = constant::invalid_index::kInt32;
                }
            }
            pushEvent( AsymmetricHorrorEvent::Kind::GateOpened, nullopt, index, 0.0f, gate._position );
            startCollapse();
        }
    }

    void AsymmetricHorrorMatch::stepWindows( float32 deltaTime )
    {
        for ( AsymmetricHorrorWindow& window : _listWindow )
        {
            window._blockedRemaining = MathUtil::max( 0.0f, window._blockedRemaining - deltaTime );
        }
    }

    void AsymmetricHorrorMatch::stepEndgame( float32 deltaTime )
    {
        const AsymmetricHorrorRules& rules            = _pCatalog->getRules();
        const int32                  standing         = countStandingSurvivors();
        const bool                   bShouldOpenHatch = _bHatchPlaced == SW_TRUE && _bHatchOpen == SW_FALSE && _bHatchClosed == SW_FALSE && standing > 0 && standing <= rules._endgame._hatchRemaining;
        if ( bShouldOpenHatch )
        {
            _bHatchOpen = SW_TRUE;
            pushEvent( AsymmetricHorrorEvent::Kind::HatchOpened, nullopt, nullopt, 0.0f, _hatchPosition );
        }
        if ( _bCollapseStarted == SW_FALSE || _collapseRemaining.isActive() == false )
            return;
        bool bSlowed = false;
        for ( const AsymmetricHorrorSurvivor& survivor : _listSurvivor )
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

    bool AsymmetricHorrorMatch::drainProgress( InteractionProgress& progress, const float3& position )
    {
        _listInteractionScratch.clear();
        progress.pollEvents( _listInteractionScratch );
        bool bCompleted = false;
        for ( const InteractionEvent& event : _listInteractionScratch )
        {
            const int32 actor = static_cast<int32>( event._actorID );
            switch ( event._kind )
            {
                case InteractionEvent::Kind::SkillCheckStarted:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckStarted, actor, nullopt, event._value, position );
                    break;
                }
                case InteractionEvent::Kind::SkillCheckSucceeded:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckResult, actor, nullopt, 1.0f, position );
                    if ( isValidSurvivor( actor ) )
                        awardScore( _listSurvivor[static_cast<size_t>( actor )]._score, "SkillCheck", 1.0f );
                    break;
                }
                case InteractionEvent::Kind::SkillCheckFailed:
                {
                    pushEvent( AsymmetricHorrorEvent::Kind::SkillCheckResult, actor, nullopt, 0.0f, position );
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

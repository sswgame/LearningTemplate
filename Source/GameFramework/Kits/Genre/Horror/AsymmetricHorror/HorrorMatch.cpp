#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorMatch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/AI/AIPerception.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/HorrorSnapshot.h"

namespace sw
{
    SW_LOG_CALLER( "HorrorMatch" );

    namespace
    {
        struct HorrorMatchInternal
        {
            static constexpr float32 kNeverReviveTime = 1.0e9f; ///< 빈사 회복은 치료 진행이 정하므로, Vitality 의 부활 시계는 멈춤용으로만 쓴다
            static constexpr uint32  kHealSeedMix     = 0x85EBCA6Bu;
            static constexpr uint32  kGateSeedMix     = 0xC2B2AE35u;
        };
    } // namespace

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
        , _eventBuffer{}
        , _listInteractionScratch{}
        , _listVitalityScratch{}
        , _match{}
        , _timer{}
        , _hatchPosition{}
        , _pCatalog{ nullptr }
        , _collapseRemaining{}
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
        _eventBuffer.clear();
        _match.initialize( MatchSettings{} );
        _timer = FixedStepTimer( _settings._fixedStep, 0.25f );
        _collapseRemaining.clear();
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
        _killer._pDef = _pCatalog->findKiller( _settings._killerID );
        if ( _killer._pDef == nullptr )
        {
            SW_LOG_WARNING( "unknown killer '%#'", _settings._killerID.c_str() );
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
        _match.initialize( makeMatchSettings() );
        _survivorTeam = _match.addTeam( hashed_string( "Survivors" ) );
        _killerTeam   = _match.addTeam( hashed_string( "Killer" ) );
        for ( HorrorSurvivor& survivor : _listSurvivor )
        {
            survivor._participant = _match.addParticipant( _survivorTeam, hashed_string( "Survivor" ) );
        }
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
        {
            step( _timer.getStep() );
        }
    }

    // --- 읽기 ------------------------------------------------------------------------------------

    float32 HorrorMatch::computeHeartbeat( int32 survivor ) const
    {
        if ( isValidSurvivor( survivor ) == false || _killer._pDef == nullptr || _killer._pDef->_terrorRadius <= 0.0f )
            return 0.0f;
        const float32 distance = float3::getHorizontalDistance( _killer._position, _listSurvivor[static_cast<size_t>( survivor )]._position );
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
        return rules._survivorSpeed * ( target._hasteRemaining.isActive() ? rules._hitHasteScale : 1.0f );
    }

    float32 HorrorMatch::computeKillerSpeed() const
    {
        if ( _killer._pDef == nullptr || _pCatalog == nullptr || _killer.canAct() == false )
            return 0.0f;
        float32 speed = _pCatalog->getRules()._survivorSpeed * _killer._pDef->_speedRatio;
        if ( _killer._attackCooldown.isActive() )
            speed *= _killer._pDef->_cooldownSpeedScale;
        if ( _killer._carrying >= 0 )
            speed *= _killer._pDef->_carrySpeedScale;
        return speed;
    }

    void HorrorMatch::collectKillerStimuli( vector<AIStimulus>& outListStimulusEntry ) const
    {
        outListStimulusEntry.clear();
        for ( int32 index = 0; index < getSurvivorCount(); ++index )
        {
            const HorrorSurvivor& survivor = _listSurvivor[static_cast<size_t>( index )];
            const bool            bVisible = survivor.isStanding() && survivor._state != SurvivorState::Carried && survivor._activity != SurvivorActivity::InLocker;
            if ( bVisible == false )
                continue;
            AIStimulus stimulus;
            stimulus._position    = survivor._position;
            stimulus._id          = static_cast<uint64>( index + 1 );
            stimulus._noiseRadius = survivor._noiseRemaining.isActive() ? survivor._noiseRadius : 0.0f;
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
        {
            count += survivor.isStanding() ? 1 : 0;
        }
        return count;
    }

    void HorrorMatch::drainEvents( vector<AsymmetricHorrorEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    // --- 걸음 -------------------------------------------------------------------------------------

    bool HorrorMatch::isNear( const float3& from, const float3& to, float32 range ) const
    {
        return float3::getHorizontalDistance( from, to ) <= range;
    }

    void HorrorMatch::pushEvent( AsymmetricHorrorEvent::Kind kind, int32 actor, int32 target, float32 value, const float3& position )
    {
        AsymmetricHorrorEvent event;
        event._kind     = kind;
        event._actor    = actor;
        event._target   = target;
        event._value    = value;
        event._position = position;
        _eventBuffer.push( event );
    }
} // namespace sw

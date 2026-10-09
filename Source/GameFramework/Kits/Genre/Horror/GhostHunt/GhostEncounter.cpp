#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostEncounter.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Utility/Math/RayMath.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"

namespace sw
{
    namespace
    {
        struct GhostEncounterInternal
        {
            static constexpr int32 kFleeDirectionCount = 8;

            /** @brief XZ 로 눕혀 길이 1 로 — 너무 짧으면 0 벡터입니다. */
            static float3 flatten( const float3& direction )
            {
                const float3  flat{ direction._x, 0.0f, direction._z };
                const float32 length = flat.getLength();
                return length > 1.0e-5f ? float3{ flat._x / length, 0.0f, flat._z / length } : float3{};
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( GhostState state )
    {
        switch ( state )
        {
            case GhostState::Hidden:
                return "Hidden";
            case GhostState::Visible:
                return "Visible";
            case GhostState::Attacking:
                return "Attacking";
            case GhostState::Stunned:
                return "Stunned";
            case GhostState::Sucking:
                return "Sucking";
            case GhostState::Caught:
                return "Caught";
        }
        return "Unknown";
    }

    GhostEncounter::GhostEncounter()
        : _pCatalog{ nullptr }
        , _random{}
        , _listGhost{}
        , _eventBuffer{}
        , _strobeCharge{ 0.0f }
        , _surgeGauge{ 0.0f }
        , _suctionTarget{ 0 }
        , _nextGhostId{ 1 }
        , _vacuumStage{ 0 }
    {
    }

    void GhostEncounter::initialize( const GhostCatalog* pCatalog, uint32 seed )
    {
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _vacuumStage = 0;
        clear();
    }

    void GhostEncounter::clear()
    {
        _listGhost.clear();
        _eventBuffer.clear();
        _strobeCharge  = 0.0f;
        _surgeGauge    = 0.0f;
        _suctionTarget = 0;
        _nextGhostId   = 1;
    }

    uint32 GhostEncounter::spawnGhost( const hashed_string& ghostId, const float3& position )
    {
        const GhostDef* pDef = _pCatalog != nullptr ? _pCatalog->findGhost( ghostId ) : nullptr;
        if ( pDef == nullptr )
            return 0;
        GhostInstance ghost;
        ghost._pDef     = pDef;
        ghost._position = position;
        ghost._hp       = pDef->_hp;
        ghost._id       = _nextGhostId++;
        enterState( ghost, GhostState::Hidden );
        _listGhost.push_back( ghost );
        return ghost._id;
    }

    void GhostEncounter::setGhostPosition( uint32 ghostId, const float3& position )
    {
        GhostInstance* pGhost = findGhostMutable( ghostId );
        if ( pGhost != nullptr )
            pGhost->_position = position;
    }

    bool GhostEncounter::isInCone( const float3& eye, const float3& forward, const float3& position, bool bStrobe ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const GhostFlashlightSettings& flashlight = _pCatalog->getFlashlight();
        return bStrobe ? RayMath::isInFlatCone( eye, forward, flashlight._strobeHalfAngle, flashlight._strobeRange, position )
                       : RayMath::isInFlatCone( eye, forward, flashlight._halfAngle, flashlight._range, position );
    }

    int32 GhostEncounter::shineBeam( const float3& eye, const float3& forward )
    {
        int32 stunnedCount = 0;
        for ( GhostInstance& ghost : _listGhost )
        {
            if ( ghost._state != GhostState::Attacking || ghost._pDef->_bStrobeOnly == SW_TRUE || isInCone( eye, forward, ghost._position, false ) == false )
                continue;
            enterState( ghost, GhostState::Stunned );
            ++stunnedCount;
        }
        return stunnedCount;
    }

    void GhostEncounter::chargeStrobe( float32 deltaTime )
    {
        const float32 chargeTime = _pCatalog != nullptr ? _pCatalog->getFlashlight()._strobeChargeTime : 0.0f;
        _strobeCharge            = MathUtil::min( chargeTime, _strobeCharge + MathUtil::max( 0.0f, deltaTime ) );
    }

    int32 GhostEncounter::releaseStrobe( const float3& eye, const float3& forward )
    {
        const float32 chargeTime = _pCatalog != nullptr ? _pCatalog->getFlashlight()._strobeChargeTime : 0.0f;
        const bool    bCharged   = _strobeCharge >= chargeTime - 1.0e-4f;
        _strobeCharge            = 0.0f;
        if ( bCharged == false )
            return 0;
        int32 stunnedCount = 0;
        for ( GhostInstance& ghost : _listGhost )
        {
            const bool bExposed = ghost._state == GhostState::Visible || ghost._state == GhostState::Attacking;
            if ( bExposed == false || isInCone( eye, forward, ghost._position, true ) == false )
                continue;
            enterState( ghost, GhostState::Stunned );
            ++stunnedCount;
        }
        return stunnedCount;
    }

    bool GhostEncounter::startSuction( uint32 ghostId, const float3& playerPosition )
    {
        GhostInstance* pGhost = findGhostMutable( ghostId );
        if ( _suctionTarget != 0 || pGhost == nullptr || pGhost->_state != GhostState::Stunned || _pCatalog == nullptr )
            return false;
        const float3 toGhost{ pGhost->_position._x - playerPosition._x, 0.0f, pGhost->_position._z - playerPosition._z };
        if ( toGhost.getLength() > _pCatalog->getVacuum()._range )
            return false;
        _suctionTarget = ghostId;
        _surgeGauge    = 0.0f;
        enterState( *pGhost, GhostState::Sucking );
        // 처음에는 플레이어에게서 멀어지는 쪽으로 — 그 뒤로는 간격마다 씨앗 난수로.
        pGhost->_fleeDirection = GhostEncounterInternal::flatten( toGhost );
        if ( pGhost->_fleeDirection.getLengthSquared() < 0.5f )
            chooseFleeDirection( *pGhost );
        pushEvent( GhostEventType::SuctionStarted, ghostId );
        return true;
    }

    void GhostEncounter::stopSuction()
    {
        GhostInstance* pGhost = findGhostMutable( _suctionTarget );
        _suctionTarget        = 0;
        _surgeGauge           = 0.0f;
        if ( pGhost == nullptr || pGhost->_state != GhostState::Sucking )
            return;
        enterState( *pGhost, GhostState::Hidden );
        pushEvent( GhostEventType::Escaped, pGhost->_id, pGhost->_hp );
    }

    GhostSuctionTick GhostEncounter::updateSuction( const float3& pullDirection, float32 deltaTime )
    {
        GhostSuctionTick tick;
        GhostInstance*   pGhost = findGhostMutable( _suctionTarget );
        if ( pGhost == nullptr || pGhost->_state != GhostState::Sucking || deltaTime <= 0.0f )
            return tick;
        const GhostVacuumSettings& vacuum = _pCatalog->getVacuum();
        const float3               pull   = GhostEncounterInternal::flatten( pullDirection );
        tick._alignment                   = -pull.dot( pGhost->_fleeDirection );
        const bool bAligned               = pull.getLengthSquared() > 0.5f && tick._alignment >= vacuum._alignThreshold;
        tick._bAligned                    = bAligned ? SW_TRUE : SW_FALSE;
        tick._damage                      = computeVacuumPower() * deltaTime * ( bAligned ? vacuum._alignBonus : 1.0f );
        const float32 dragScale           = vacuum._dragSpeed * pGhost->_pDef->_pull * deltaTime * ( bAligned ? 1.0f - vacuum._dragReduction : 1.0f );
        tick._drag                        = pGhost->_fleeDirection * dragScale;
        if ( bAligned )
            _surgeGauge = MathUtil::min( 1.0f, _surgeGauge + deltaTime / vacuum._surgeFillTime );
        const uint32 ghostId = pGhost->_id;
        applySuctionDamage( *pGhost, tick._damage );
        if ( _suctionTarget == 0 )
        {
            tick._bCaught = SW_TRUE;
            return tick;
        }
        // 도망 방향 바꾸기 — 흡입이 이 시계를 쥔다(update 는 흡입 중인 유령을 건너뛴다).
        GhostInstance* pStill = findGhostMutable( ghostId );
        pStill->_timer.tick( deltaTime );
        if ( pStill->_timer.isActive() == false )
        {
            pStill->_timer.restart( pStill->_pDef->_fleeInterval );
            chooseFleeDirection( *pStill );
        }
        return tick;
    }

    bool GhostEncounter::triggerSurge()
    {
        GhostInstance* pGhost = findGhostMutable( _suctionTarget );
        if ( pGhost == nullptr || _surgeGauge < 1.0f - 1.0e-4f )
            return false;
        _surgeGauge          = 0.0f;
        const float32 damage = _pCatalog->getVacuum()._surgeDamage;
        pushEvent( GhostEventType::Surge, pGhost->_id, damage );
        applySuctionDamage( *pGhost, damage );
        return true;
    }

    void GhostEncounter::setVacuumStage( int32 stage )
    {
        const int32 stageCount = _pCatalog != nullptr ? static_cast<int32>( _pCatalog->getVacuum()._listStagePower.size() ) : 1;
        _vacuumStage           = MathUtil::clamp( stage, 0, MathUtil::max( 0, stageCount - 1 ) );
    }

    float32 GhostEncounter::computeVacuumPower() const
    {
        if ( _pCatalog == nullptr || _pCatalog->getVacuum()._listStagePower.empty() )
            return 0.0f;
        const vector<float32>& listPower = _pCatalog->getVacuum()._listStagePower;
        return listPower[static_cast<size_t>( MathUtil::clamp( _vacuumStage, 0, static_cast<int32>( listPower.size() ) - 1 ) )];
    }

    void GhostEncounter::update( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        for ( GhostInstance& ghost : _listGhost )
        {
            if ( ghost._state == GhostState::Caught || ghost._state == GhostState::Sucking )
                continue;
            ghost._timer.tick( deltaTime );
            if ( ghost._timer.isActive() )
                continue;
            switch ( ghost._state )
            {
                case GhostState::Hidden:
                {
                    enterState( ghost, GhostState::Visible );
                    break;
                }
                case GhostState::Visible:
                {
                    enterState( ghost, GhostState::Attacking );
                    break;
                }
                case GhostState::Attacking:
                {
                    pushEvent( GhostEventType::AttackLanded, ghost._id, ghost._pDef->_attackDamage );
                    enterState( ghost, GhostState::Hidden );
                    break;
                }
                case GhostState::Stunned:
                {
                    enterState( ghost, GhostState::Visible );
                    break; // 깨어났다
                }
                default:
                {
                    break;
                }
            }
        }
    }

    void GhostEncounter::drainEvents( vector<GhostEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const GhostInstance* GhostEncounter::findGhost( uint32 ghostId ) const
    {
        for ( const GhostInstance& ghost : _listGhost )
        {
            if ( ghost._id == ghostId )
                return &ghost;
        }
        return nullptr;
    }

    GhostInstance* GhostEncounter::findGhostMutable( uint32 ghostId )
    {
        if ( ghostId == 0 )
            return nullptr;
        for ( GhostInstance& ghost : _listGhost )
        {
            if ( ghost._id == ghostId )
                return &ghost;
        }
        return nullptr;
    }

    int32 GhostEncounter::countRemaining() const
    {
        int32 count = 0;
        for ( const GhostInstance& ghost : _listGhost )
        {
            if ( ghost._state != GhostState::Caught )
                ++count;
        }
        return count;
    }

    void GhostEncounter::enterState( GhostInstance& ghost, GhostState state )
    {
        ghost._state        = state;
        const GhostDef& def = *ghost._pDef;
        switch ( state )
        {
            case GhostState::Hidden:
            {
                ghost._timer.start( def._hideTime );
                break;
            }
            case GhostState::Visible:
            {
                ghost._timer.start( def._appearTime );
                pushEvent( GhostEventType::Appeared, ghost._id );
                break;
            }
            case GhostState::Attacking:
            {
                ghost._timer.start( def._attackTime );
                break;
            }
            case GhostState::Stunned:
            {
                ghost._timer.start( def._stunTime );
                pushEvent( GhostEventType::Stunned, ghost._id );
                break;
            }
            case GhostState::Sucking:
            {
                ghost._timer.start( def._fleeInterval );
                break;
            }
            case GhostState::Caught:
            {
                ghost._timer.clear();
                break;
            }
        }
    }

    void GhostEncounter::chooseFleeDirection( GhostInstance& ghost )
    {
        const int32   index = _random.nextInt( 0, GhostEncounterInternal::kFleeDirectionCount - 1 );
        const float32 angle = static_cast<float32>( index ) * ( 360.0f / static_cast<float32>( GhostEncounterInternal::kFleeDirectionCount ) ) *
                              MathUtil::kDegreeToRadian;
        ghost._fleeDirection = float3{ MathUtil::sin( angle ), 0.0f, MathUtil::cos( angle ) };
    }

    void GhostEncounter::applySuctionDamage( GhostInstance& ghost, float32 damage )
    {
        ghost._hp -= damage;
        if ( ghost._hp > 0.0f )
            return;
        ghost._hp = 0.0f;
        enterState( ghost, GhostState::Caught );
        _suctionTarget = 0;
        _surgeGauge    = 0.0f;
        pushEvent( GhostEventType::Caught, ghost._id, 0.0f, ghost._pDef->_coins );
    }

    void GhostEncounter::pushEvent( GhostEventType type, uint32 ghostId, float32 amount, int32 coins )
    {
        GhostEvent event;
        event._type    = type;
        event._ghostId = ghostId;
        event._amount  = amount;
        event._coins   = coins;
        _eventBuffer.push( event );
    }

    void GhostEncounter::writeState( Archive& outArchive ) const
    {
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << static_cast<uint32>( _listGhost.size() );
        for ( const GhostInstance& ghost : _listGhost )
        {
            StateArchiveUtil::writeName( outArchive, ghost._pDef->_id );
            outArchive << ghost._position;
            outArchive << ghost._fleeDirection;
            outArchive << ghost._hp;
            StateArchiveUtil::writeCountdown( outArchive, ghost._timer );
            outArchive << ghost._id;
            outArchive << static_cast<uint8>( ghost._state );
        }
        outArchive << _strobeCharge;
        outArchive << _surgeGauge;
        outArchive << _suctionTarget;
        outArchive << _nextGhostId;
        outArchive << _vacuumStage;
    }

    bool GhostEncounter::readState( Archive& archive )
    {
        if ( _pCatalog == nullptr )
            return false;
        GameRandom random = _random;
        uint32     count  = 0;
        // 유령마다 이름(4) + 자리 · 도망 방향(24) + 체력 · 시간 · 번호(12) + 상태(1) 이상
        if ( StateArchiveUtil::readRandom( archive, random ) == false || StateArchiveUtil::readCount( archive, 41, count ) == false )
            return false;
        vector<GhostInstance> listGhost( count );
        for ( GhostInstance& ghost : listGhost )
        {
            hashed_string ghostId;
            uint8         state = 0;
            if ( StateArchiveUtil::readName( archive, ghostId ) == false )
                return false;
            ghost._pDef = _pCatalog->findGhost( ghostId );
            archive >> ghost._position;
            archive >> ghost._fleeDirection;
            archive >> ghost._hp;
            const bool bTimerRead = StateArchiveUtil::readCountdown( archive, ghost._timer );
            archive >> ghost._id;
            archive >> state;
            const bool bValid = bTimerRead && archive.isOk() && ghost._pDef != nullptr && state <= static_cast<uint8>( GhostState::Caught );
            if ( bValid == false )
                return false;
            ghost._state = static_cast<GhostState>( state );
        }
        float32 strobeCharge  = 0.0f;
        float32 surgeGauge    = 0.0f;
        uint32  suctionTarget = 0;
        uint32  nextGhostId   = 1;
        int32   vacuumStage   = 0;
        archive >> strobeCharge;
        archive >> surgeGauge;
        archive >> suctionTarget;
        archive >> nextGhostId;
        archive >> vacuumStage;
        if ( archive.isError() || vacuumStage < 0 )
            return false;
        // 흡입 대상은 있는 유령이어야 한다.
        bool bTargetFound = suctionTarget == 0;
        for ( const GhostInstance& ghost : listGhost )
        {
            bTargetFound = bTargetFound || ghost._id == suctionTarget;
        }
        if ( bTargetFound == false )
            return false;
        _random        = random;
        _listGhost     = std::move( listGhost );
        _strobeCharge  = strobeCharge;
        _surgeGauge    = surgeGauge;
        _suctionTarget = suctionTarget;
        _nextGhostId   = nextGhostId;
        _vacuumStage   = vacuumStage;
        _eventBuffer.clear();
        return true;
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostEncounter.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Runtime/Foundation/Utility/DepletingValue.h"
#include "GameFramework/Runtime/Foundation/Utility/Math/RayMath.h"

namespace sw
{
    namespace
    {
        struct GhostEncounterInternal
        {
            static constexpr int32 kFleeDirectionCount = 8;

            /** @brief XZ 평면으로 눕혀 길이를 1 로 만듭니다. 너무 짧으면 0 벡터를 반환합니다. */
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
    GhostEncounter::GhostEncounter()
        : _pCatalog{ nullptr }
        , _random{}
        , _listGhost{}
        , _eventBuffer{}
        , _strobeCharge{ 0.0f }
        , _surgeGauge{ 0.0f }
        , _suctionTarget{ 0 }
        , _nextGhostID{ 1 }
        , _vacuumStage{ 0 }
    {
    }

    bool GhostEncounter::initialize( const GhostCatalog* pCatalog, uint32 seed )
    {
        if ( pCatalog == nullptr || seed == 0 )
        {
            SW_LOG_ERROR_ID( "GhostEncounter.RefusedInitialize", "GhostEncounter: refused - it needs a catalog and a non-zero seed" );
            return false;
        }
        _pCatalog = pCatalog;
        _random.setSeed( seed );
        _vacuumStage = 0;
        clear();
        return true;
    }

    void GhostEncounter::clear()
    {
        _listGhost.clear();
        _eventBuffer.clear();
        _strobeCharge  = 0.0f;
        _surgeGauge    = 0.0f;
        _suctionTarget = 0;
        _nextGhostID   = 1;
    }

    uint32 GhostEncounter::spawnGhost( const hashed_string& ghostID, const float3& position )
    {
        const GhostDef* pDef = _pCatalog != nullptr ? _pCatalog->findGhost( ghostID ) : nullptr;
        if ( pDef == nullptr )
            return 0;
        GhostInstance ghost;
        ghost._pDef     = pDef;
        ghost._position = position;
        ghost._hp       = pDef->_hp;
        ghost._id       = _nextGhostID++;
        enterState( ghost, GhostState::Hidden );
        _listGhost.push_back( ghost );
        return ghost._id;
    }

    void GhostEncounter::setGhostPosition( uint32 ghostID, const float3& position )
    {
        GhostInstance* pGhost = findGhostMutable( ghostID );
        if ( pGhost != nullptr )
            pGhost->_position = position;
    }

    bool GhostEncounter::isInCone( const float3& eye, const float3& forward, const float3& position, bool bStrobe ) const
    {
        if ( _pCatalog == nullptr )
            return false;
        const GhostFlashlightDef& flashlight = _pCatalog->getFlashlight();
        return bStrobe ? RayMath::isInFlatCone( eye, forward, MathUtil::toDegree( flashlight._strobeHalfAngle ), flashlight._strobeRange, position )
                       : RayMath::isInFlatCone( eye, forward, MathUtil::toDegree( flashlight._halfAngle ), flashlight._range, position );
    }

    int32 GhostEncounter::shineBeam( const float3& eye, const float3& forward )
    {
        int32 stunnedCount = 0;
        for ( GhostInstance& ghost : _listGhost )
        {
            const bool bNotStunnable = ghost._state != GhostState::Attacking || ghost._pDef->_bStrobeOnly || isInCone( eye, forward, ghost._position, false ) == false;
            if ( bNotStunnable )
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

    bool GhostEncounter::startSuction( uint32 ghostID, const float3& playerPosition )
    {
        GhostInstance* pGhost      = findGhostMutable( ghostID );
        const bool     bCannotSuck = _suctionTarget != 0 || pGhost == nullptr || pGhost->_state != GhostState::Stunned || _pCatalog == nullptr;
        if ( bCannotSuck )
            return false;
        const float3 toGhost{ pGhost->_position._x - playerPosition._x, 0.0f, pGhost->_position._z - playerPosition._z };
        if ( toGhost.getLength() > _pCatalog->getVacuum()._range )
            return false;
        _suctionTarget = ghostID;
        _surgeGauge    = 0.0f;
        enterState( *pGhost, GhostState::Sucking );
        // 처음에는 플레이어에게서 멀어지는 쪽으로 정하고, 그 뒤로는 간격마다 시드 난수로 정한다.
        pGhost->_fleeDirection = GhostEncounterInternal::flatten( toGhost );
        if ( pGhost->_fleeDirection.getLengthSquared() < 0.5f )
            chooseFleeDirection( *pGhost );
        pushEvent( GhostEventKind::SuctionStarted, ghostID );
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
        pushEvent( GhostEventKind::Escaped, pGhost->_id, pGhost->_hp );
    }

    GhostSuctionTick GhostEncounter::updateSuction( const float3& pullDirection, float32 deltaTime )
    {
        GhostSuctionTick tick;
        GhostInstance*   pGhost         = findGhostMutable( _suctionTarget );
        const bool       bNoSuctionTick = pGhost == nullptr || pGhost->_state != GhostState::Sucking || deltaTime <= 0.0f;
        if ( bNoSuctionTick )
            return tick;
        const GhostVacuumDef& vacuum = _pCatalog->getVacuum();
        const float3          pull   = GhostEncounterInternal::flatten( pullDirection );
        tick._alignment              = -pull.dot( pGhost->_fleeDirection );
        const bool bAligned          = pull.getLengthSquared() > 0.5f && tick._alignment >= vacuum._alignThreshold;
        tick._bAligned               = bAligned ? SW_TRUE : SW_FALSE;
        tick._damage                 = computeVacuumPower() * deltaTime * ( bAligned ? vacuum._alignBonus : 1.0f );
        const float32 dragScale      = vacuum._dragSpeed * pGhost->_pDef->_pull * deltaTime * ( bAligned ? 1.0f - vacuum._dragReduction : 1.0f );
        tick._drag                   = pGhost->_fleeDirection * dragScale;
        if ( bAligned )
            _surgeGauge = MathUtil::min( 1.0f, _surgeGauge + deltaTime / vacuum._surgeFillTime );
        const uint32 ghostID = pGhost->_id;
        applySuctionDamage( *pGhost, tick._damage );
        if ( _suctionTarget == 0 )
        {
            tick._bCaught = SW_TRUE;
            return tick;
        }
        // 도망 방향 바꾸기는 흡입이 이 타이머를 가지고 있다(update 는 흡입 중인 유령을 건너뛴다).
        GhostInstance* pStill = findGhostMutable( ghostID );
        pStill->_timer.update( deltaTime );
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
        pushEvent( GhostEventKind::Surge, pGhost->_id, damage );
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
            ghost._timer.update( deltaTime );
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
                    pushEvent( GhostEventKind::AttackLanded, ghost._id, ghost._pDef->_attackDamage );
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

    void GhostEncounter::pollEvents( vector<GhostEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    const GhostInstance* GhostEncounter::findGhost( uint32 ghostID ) const
    {
        return VectorUtil::findBy<&GhostInstance::_id>( _listGhost, ghostID );
    }

    GhostInstance* GhostEncounter::findGhostMutable( uint32 ghostID )
    {
        if ( ghostID == 0 )
            return nullptr;
        return VectorUtil::findBy<&GhostInstance::_id>( _listGhost, ghostID );
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
                pushEvent( GhostEventKind::Appeared, ghost._id );
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
                pushEvent( GhostEventKind::Stunned, ghost._id );
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
        if ( DepletingValue::take( ghost._hp, damage )._bDepleted == SW_FALSE )
            return;
        enterState( ghost, GhostState::Caught );
        _suctionTarget = 0;
        _surgeGauge    = 0.0f;
        pushEvent( GhostEventKind::Caught, ghost._id, 0.0f, ghost._pDef->_coins );
    }

    void GhostEncounter::pushEvent( GhostEventKind type, uint32 ghostID, float32 amount, int32 coins )
    {
        GhostEvent event;
        event._type    = type;
        event._ghostID = ghostID;
        event._amount  = amount;
        event._coins   = coins;
        _eventBuffer.push( event );
    }

    void GhostEncounter::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.value( _random );
        // 유령마다 이름(4) + 위치, 도망 방향(24) + 체력, 시간, 번호(12) + 상태(1) 이상
        io.list( _listGhost, 41, [&]( GhostInstance& ghost )
        {
            io.reference( ghost._pDef, [this]( const hashed_string& ghostID )
            { return _pCatalog->findGhost( ghostID ); } );
            io.value( ghost._position );
            io.value( ghost._fleeDirection );
            io.value( ghost._hp );
            io.value( ghost._timer );
            io.value( ghost._id );
            io.enumValue( ghost._state, GhostState::Caught );
            if ( io.isReading() && io.check( ghost._pDef != nullptr ) )
            {
                // 잡힌 유령의 체력은 0 이고, 체력이 0 이면 잡힌 것이다. 도망 방향은 단위 벡터이거나 0 이다.
                const bool bHpValid = ghost._state == GhostState::Caught ? ghost._hp == 0.0f : ( 0.0f < ghost._hp && ghost._hp <= ghost._pDef->_hp );
                io.check( bHpValid && ghost._fleeDirection.getLengthSquared() <= 1.01f && ghost._id != 0 );
            }
        } );
        io.value( _strobeCharge );
        io.value( _surgeGauge );
        io.value( _suctionTarget );
        io.value( _nextGhostID );
        io.value( _vacuumStage );
        if ( io.isWriting() || io.isOk() == false )
            return;
        const int32 lastStage = static_cast<int32>( _pCatalog->getVacuum()._listStagePower.size() ) - 1;
        io.check( 0 <= _vacuumStage && _vacuumStage <= MathUtil::max( 0, lastStage ) && 0.0f <= _strobeCharge && _strobeCharge <= _pCatalog->getFlashlight()._strobeChargeTime && 0.0f <= _surgeGauge && _surgeGauge <= 1.0f );
        io.check( VectorUtil::findDuplicateBy<&GhostInstance::_id>( _listGhost ) == nullptr );
        // 흡입 대상은 흡입 중인 유령 하나이고, 흡입 중인 유령은 대상뿐이다(짝이 없으면 `update` 가 건너뛰어 영영 멈춘다). 다음 번호는 있는 번호와 겹치지 않는다.
        bool bTargetFound = _suctionTarget == 0;
        for ( const GhostInstance& ghost : _listGhost )
        {
            const bool bTarget = ghost._id == _suctionTarget;
            io.check( bTarget == ( ghost._state == GhostState::Sucking ) && ghost._id < _nextGhostID );
            bTargetFound = bTargetFound || bTarget;
        }
        io.check( bTargetFound );
        _eventBuffer.clear();
    }

} // namespace sw

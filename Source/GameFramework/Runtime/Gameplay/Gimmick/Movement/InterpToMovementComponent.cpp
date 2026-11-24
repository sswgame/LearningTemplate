#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/InterpToMovementComponent.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"

namespace sw
{
    InterpToMovementComponent::InterpToMovementComponent()
        : _listWaypoint{}
        , _segmentSeconds{ 2.0f }
        , _tickSeconds{ constant::time::kDefaultFixedStepSeconds }
        , _bPingPong{ true }
        , _turnRate{ Radians::fromRadians( 0.0f ) }
        , _startPosition{}
        , _startRotation{}
        , _bStarted{ false }
        , _boundTickSource{}
        , _bWarnedNoTick{ false }
    {
    }

    void InterpToMovementComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene != nullptr && _bStarted == false )
        {
            _startPosition = pOwner->getManager()->getWorldOrigin().toAbsolute( pScene->getWorldPosition() );
            _startRotation = pScene->getLocalRotation();
            _bStarted      = true;
        }
        registerSubTick( TickGroup::DuringPhysics, kApplySubTickID );
    }

    void InterpToMovementComponent::onEndPlay()
    {
        _bStarted = false;
        Component::onEndPlay();
    }

    void InterpToMovementComponent::onSubTick( uint32 subTickID, TickContext& context )
    {
        Component::onSubTick( subTickID, context );
        if ( subTickID != kApplySubTickID )
            return;
        const GameObject*    pOwner   = getOwner();
        const ControlSystem* pControl = ControlSystem::find( context.getManager() );
        if ( pControl == nullptr )
        {
            if ( _bWarnedNoTick == false )
                SW_LOG_ERROR_ID( "InterpToMovement.InterpToMovementButSceneNo", "'%#' has a InterpToMovement but its scene has no ControlSystem tick - it does not move", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _bWarnedNoTick = true;
            return;
        }
        // 서버 틱을 알리는 틱 뒤에 돈다(같은 프레임의 값). 바뀐 걸기는 틱 뒤에 들어 다음 프레임부터 순서가 맞는다.
        const SubTickHandle& source = pControl->getServerTickSource();
        if ( source != _boundTickSource )
        {
            if ( _boundTickSource.isValid() )
                (void)removeSubTickPrerequisite( kApplySubTickID, _boundTickSource );
            if ( source.isValid() )
                (void)addSubTickPrerequisite( kApplySubTickID, source, TickPrerequisiteKind::ReadsWrites );
            _boundTickSource = source;
        }
        applyTick( pControl->getServerTick() );
    }

    void InterpToMovementComponent::setPath( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong )
    {
        _listWaypoint   = listWaypoint;
        _segmentSeconds = segmentSeconds;
        _bPingPong      = bPingPong;
    }

    bool InterpToMovementComponent::computePose( float64 seconds, float3& outPosition, quaternion& outRotation ) const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || _bStarted == false )
            return false;
        outPosition = pOwner->getManager()->getWorldOrigin().toLocal( _startPosition ) + computeOffset( _listWaypoint, _segmentSeconds, _bPingPong, seconds );
        outRotation = quaternion::makeFromYawPitchRoll( _startRotation._y + computeTurn( _turnRate, seconds ), _startRotation._x, _startRotation._z );
        return true;
    }

    float32 InterpToMovementComponent::computeTurn( Radians turnRate, float64 seconds )
    {
        // 큰 틱에서도 각을 잃지 않게 한 바퀴로 먼저 줄인다.
        const float64 turn = static_cast<float64>( turnRate.radians() ) * seconds;
        return static_cast<float32>( turn - static_cast<float64>( MathUtil::kTwoPi ) * static_cast<float64>( static_cast<int64>( turn / static_cast<float64>( MathUtil::kTwoPi ) ) ) );
    }

    void InterpToMovementComponent::applySeconds( float64 seconds )
    {
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr || _bStarted == false )
            return;
        pScene->setWorldPosition( pOwner->getManager()->getWorldOrigin().toLocal( _startPosition ) + computeOffset( _listWaypoint, _segmentSeconds, _bPingPong, seconds ) );
        if ( _turnRate.radians() != 0.0f )
            pScene->setLocalRotation( float3{ _startRotation._x, _startRotation._y + computeTurn( _turnRate, seconds ), _startRotation._z } );
    }

    float3 InterpToMovementComponent::computeOffset( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong, float64 seconds )
    {
        if ( listWaypoint.empty() || segmentSeconds <= 0.0f )
            return float3{};
        // 점 열: 시작(0) → 경유점들. 고리는 끝에서 시작으로 한 구간 더, 왕복은 끝에서 되돌아온다.
        const size_t  pointCount   = listWaypoint.size() + 1;
        const size_t  segmentCount = bPingPong ? ( pointCount - 1 ) * 2 : pointCount;
        const float64 cycle        = static_cast<float64>( segmentCount ) * static_cast<float64>( segmentSeconds );
        float64       phase        = seconds - cycle * static_cast<float64>( static_cast<int64>( seconds / cycle ) );
        if ( phase < 0.0 )
            phase += cycle;
        const float64 segmentPosition = phase / static_cast<float64>( segmentSeconds );
        size_t        segmentIndex    = static_cast<size_t>( segmentPosition );
        if ( segmentIndex >= segmentCount )
            segmentIndex = segmentCount - 1;
        const float32 alpha = static_cast<float32>( segmentPosition - static_cast<float64>( segmentIndex ) );

        size_t fromIndex = segmentIndex;
        size_t toIndex   = segmentIndex + 1;
        if ( bPingPong && segmentIndex >= pointCount - 1 )
        {
            // 되돌아가는 쪽: 끝에서 시작으로
            const size_t backIndex = segmentIndex - ( pointCount - 1 );
            fromIndex              = ( pointCount - 1 ) - backIndex;
            toIndex                = fromIndex - 1;
        }
        if ( bPingPong == false && toIndex == pointCount )
            toIndex = 0;
        const float3 from = fromIndex == 0 ? float3{} : listWaypoint[fromIndex - 1];
        const float3 to   = toIndex == 0 ? float3{} : listWaypoint[toIndex - 1];
        return float3::lerp( from, to, alpha );
    }

    void InterpToMovementComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<InterpToMovementComponent>( this );
    }

    void InterpToMovementComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<InterpToMovementComponent>( this );
        Component::onUnregister( manager );
    }
} // namespace sw

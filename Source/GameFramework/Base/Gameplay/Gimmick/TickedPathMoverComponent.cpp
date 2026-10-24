#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/TickedPathMoverComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Actor/Control/ControlSystem.h"

namespace sw
{
    SW_LOG_CALLER( "TickedPathMover" );

    TickedPathMoverComponent::TickedPathMoverComponent()
        : _listWaypoint{}
        , _segmentSeconds{ 2.0f }
        , _tickSeconds{ 1.0f / 60.0f }
        , _bPingPong{ true }
        , _startPosition{}
        , _tickOffset{ 0 }
        , _bStarted{ false }
        , _bWarnedNoTick{ false }
    {
    }

    void TickedPathMoverComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        _startPosition               = pScene != nullptr ? pScene->getWorldPosition() : float3{};
        _bStarted                    = pScene != nullptr;
    }

    void TickedPathMoverComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ControlSystem*     pControl = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
        if ( pControl == nullptr )
        {
            if ( _bWarnedNoTick == false )
                SW_LOG_ERROR( "'%#' has a TickedPathMover but its scene has no ControlSystem tick - it does not move", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _bWarnedNoTick = true;
            return;
        }
        const int64 tick = static_cast<int64>( pControl->getTick() ) + _tickOffset;
        applyTick( static_cast<uint32>( MathUtil::max( tick, int64{ 0 } ) ) );
    }

    void TickedPathMoverComponent::setTickOffsetInScene( GameObjectManager& manager, int64 tickOffset )
    {
        manager.forEachComponentOfType<TickedPathMoverComponent>( [tickOffset]( TickedPathMoverComponent* pMover )
        { pMover->setTickOffset( tickOffset ); } );
    }

    void TickedPathMoverComponent::setPath( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong )
    {
        _listWaypoint   = listWaypoint;
        _segmentSeconds = segmentSeconds;
        _bPingPong      = bPingPong;
    }

    void TickedPathMoverComponent::applyTick( uint32 tick )
    {
        GameObject*     pOwner = getOwner();
        SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr || _bStarted == false )
            return;
        const float64 seconds = static_cast<float64>( tick ) * static_cast<float64>( _tickSeconds );
        pScene->setWorldPosition( _startPosition + computeOffset( _listWaypoint, _segmentSeconds, _bPingPong, seconds ) );
    }

    float3 TickedPathMoverComponent::computeOffset( const vector<float3>& listWaypoint, float32 segmentSeconds, bool bPingPong, float64 seconds )
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
} // namespace sw

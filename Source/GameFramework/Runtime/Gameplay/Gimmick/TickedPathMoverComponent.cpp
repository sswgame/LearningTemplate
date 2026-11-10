#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/TickedPathMoverComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"

namespace sw
{
    TickedPathMoverComponent::TickedPathMoverComponent()
        : _listWaypoint{}
        , _segmentSeconds{ 2.0f }
        , _tickSeconds{ constant::time::kDefaultFixedStepSeconds }
        , _bPingPong{ true }
        , _startPosition{}
        , _boundTickSource{}
        , _bStarted{ SW_FALSE }
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
        registerSubTick( TickGroup::DuringPhysics, kApplySubTickID );
    }

    void TickedPathMoverComponent::onSubTick( uint32 subTickID, float32 deltaTime )
    {
        Component::onSubTick( subTickID, deltaTime );
        if ( subTickID != kApplySubTickID )
            return;
        const GameObject*        pOwner   = getOwner();
        const GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const ControlSystem*     pControl = pManager != nullptr ? ControlSystem::find( *pManager ) : nullptr;
        if ( pControl == nullptr )
        {
            if ( _bWarnedNoTick == false )
                SW_LOG_ERROR_ID( "TickedPathMover.TickedPathMoverButSceneNo", "'%#' has a TickedPathMover but its scene has no ControlSystem tick - it does not move", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
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

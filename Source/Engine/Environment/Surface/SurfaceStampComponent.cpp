#include "pch.h"

#include "Engine/Environment/Surface/SurfaceStampComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/OrientationUtil.h"

#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    SurfaceStampComponent::SurfaceStampComponent()
        : _stamp{}
        , _mode{ SurfaceStampMode::Hold }
        , _trailStep{ 0.1f }
        , _trailBreak{ 2.0f }
        , _held{}
        , _lastPoint{}
        , _bHasLastPoint{ SW_FALSE }
        , _bStampChecked{ SW_FALSE }
        , _bValidStamp{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void SurfaceStampComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        _bStampChecked = SW_FALSE;
        // 붙잡은 도장은 옛 칸의 사본이다 — 놓으면 다음 틱이 새 칸으로 다시 붙잡는다.
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr )
            removePersistentStamp( *pManager );
    }

    void SurfaceStampComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        manager.getComponentRegistry().add<SurfaceStampComponent>( this );
    }

    void SurfaceStampComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SurfaceStampComponent>( this );
        removePersistentStamp( manager );
        Component::onUnregister( manager );
    }

    void SurfaceStampComponent::onOwnerActiveInHierarchyChanged()
    {
        Component::onOwnerActiveInHierarchyChanged();
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( pManager != nullptr && pOwner->isActiveInHierarchy() == false )
            removePersistentStamp( *pManager );
    }

    void SurfaceStampComponent::applyStamp( GameObjectManager& manager )
    {
        GameObject*           pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        if ( pScene == nullptr || pOwner->isActiveInHierarchy() == false )
            return;
        if ( _bStampChecked == SW_FALSE )
        {
            _bValidStamp   = _stamp.validate( "SurfaceStampComponent", pOwner->getName().view() ) ? SW_TRUE : SW_FALSE;
            _bStampChecked = SW_TRUE;
        }
        if ( _bValidStamp == SW_FALSE )
            return;
        const float3         point  = pScene->getWorldPosition();
        const float32        yaw    = OrientationUtil::computeYawFromWorld( pScene->getWorldMatrix() );
        SurfaceDeformSystem& deform = manager.getSubsystem<SurfaceDeformSystem>();
        if ( _mode == SurfaceStampMode::Hold )
        {
            // 씬을 비워 변형 층이 도장을 잃었으면 옮기기가 거짓이다 — 다시 붙잡는다.
            if ( _held.isValid() == false || deform.movePersistentStamp( _held, point, yaw ) == false )
                _held = deform.addPersistentStamp( _stamp, point, yaw );
            return;
        }
        if ( _bHasLastPoint == SW_FALSE )
        {
            _lastPoint     = point;
            _bHasLastPoint = SW_TRUE;
            return;
        }
        const float32 deltaX   = point._x - _lastPoint._x;
        const float32 deltaZ   = point._z - _lastPoint._z;
        const float32 distance = MathUtil::sqrt( deltaX * deltaX + deltaZ * deltaZ );
        if ( distance < _trailStep )
            return;
        // 한 프레임에 너무 멀리 간 것은 순간 이동(배치 · 리스폰)이다 — 그 사이 땅을 긋지 않고 새 자리에서 다시 시작한다.
        if ( distance <= _trailBreak )
            deform.applySegmentStamp( _stamp, _lastPoint, point );
        _lastPoint = point;
    }

    void SurfaceStampComponent::removePersistentStamp( GameObjectManager& manager )
    {
        if ( _held.isValid() )
            manager.getSubsystem<SurfaceDeformSystem>().removePersistentStamp( _held );
        _held          = SurfaceStampHandle{};
        _bHasLastPoint = SW_FALSE;
    }
} // namespace sw

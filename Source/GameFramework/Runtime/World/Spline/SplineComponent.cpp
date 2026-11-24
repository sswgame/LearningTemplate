#include "pch.h"

#include "GameFramework/Runtime/World/Spline/SplineComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"

namespace sw
{
    SplineComponent::SplineComponent()
        : _listControlPoint{}
        , _samplesPerSegment{ 16 }
        , _type{ SplineKind::CatmullRom }
        , _bClosed{ false }
        , _localPath{}
        , _worldPath{}
        , _pathRevision{ 0 }
    {
    }

    void SplineComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<SplineComponent>( this );
    }

    void SplineComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<SplineComponent>( this );
        SceneComponent::onUnregister( manager );
    }

    void SplineComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        rebuildLocalPath();
        bakeWorldPath();
    }

    void SplineComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        rebuildLocalPath();
        bakeWorldPath();
    }

    void SplineComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        rebuildLocalPath();
        bakeWorldPath();
    }

    void SplineComponent::setControlPoints( const vector<float3>& listControlPoint, SplineKind type, bool bClosed )
    {
        _listControlPoint = listControlPoint;
        _type             = type;
        _bClosed          = bClosed;
        rebuildLocalPath();
        bakeWorldPath();
    }

    void SplineComponent::onWorldOriginShifted( const float3& delta )
    {
        (void)delta;
        bakeWorldPath(); // 루트는 이미 옮겨졌다
    }

    void SplineComponent::rebuildLocalPath()
    {
        if ( _localPath.initialize( _listControlPoint, _type, _bClosed, _samplesPerSegment ) == false )
            _localPath.clear();
    }

    void SplineComponent::bakeWorldPath()
    {
        ++_pathRevision;
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? this : nullptr;
        if ( pScene == nullptr )
        {
            if ( _worldPath.initialize( _listControlPoint, _type, _bClosed, _samplesPerSegment ) == false )
                _worldPath.clear();
            return;
        }
        const float4x4 worldMatrix = pScene->getWorldMatrix();
        vector<float3> listWorldPoint;
        listWorldPoint.reserve( _listControlPoint.size() );
        for ( const float3& point : _listControlPoint )
        {
            listWorldPoint.push_back( float3::transform( point, worldMatrix ) );
        }
        if ( _worldPath.initialize( listWorldPoint, _type, _bClosed, _samplesPerSegment ) == false )
            _worldPath.clear();
    }
} // namespace sw

#include "pch.h"

#include "GameFramework/Base/Spline/SplineComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    SplineComponent::SplineComponent()
        : _listControlPoint{}
        , _samplesPerSegment{ 16 }
        , _type{ SplineType::CatmullRom }
        , _bClosed{ false }
        , _localPath{}
        , _worldPath{}
    {
    }

    void SplineComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        rebuildLocalPath();
        freezeWorldPath();
    }

    void SplineComponent::onPostLoad()
    {
        Component::onPostLoad();
        rebuildLocalPath();
        freezeWorldPath();
    }

    void SplineComponent::onPropertyChanged( hashed_string propertyName )
    {
        Component::onPropertyChanged( propertyName );
        rebuildLocalPath();
        freezeWorldPath();
    }

    void SplineComponent::setControlPoints( const vector<float3>& listControlPoint, SplineType type, bool bClosed )
    {
        _listControlPoint = listControlPoint;
        _type             = type;
        _bClosed          = bClosed;
        rebuildLocalPath();
        freezeWorldPath();
    }

    void SplineComponent::rebuildLocalPath()
    {
        if ( _localPath.initialize( _listControlPoint, _type, _bClosed, _samplesPerSegment ) == false )
            _localPath.clear();
    }

    void SplineComponent::freezeWorldPath()
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
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

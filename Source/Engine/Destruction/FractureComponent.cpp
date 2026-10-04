#include "pch.h"

#include "Engine/Destruction/FractureComponent.h"

#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/FractureAssetCache.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    SW_LOG_CALLER( "FractureComponent" );

    FractureComponent::FractureComponent()
        : FractureComponentBase()
        , _fracturePath{}
        , _navGeometrySource{ *this }
        , _pSceneNavigation{ nullptr }
        , _acquiredGeneration{ 0 }
    {
    }

    void FractureComponent::onRegister( GameObjectManager& manager )
    {
        FractureComponentBase::onRegister( manager );
        _pSceneNavigation = &manager.getSceneNavigation();
        _pSceneNavigation->registerGeometrySource( &_navGeometrySource );
    }

    void FractureComponent::onUnregister( GameObjectManager& manager )
    {
        if ( _pSceneNavigation != nullptr )
            _pSceneNavigation->unregisterGeometrySource( &_navGeometrySource );
        _pSceneNavigation = nullptr;
        FractureComponentBase::onUnregister( manager );
    }

    void FractureComponent::onAnchoredShapeChanged()
    {
        const FractureAsset* pAsset = findAsset();
        if ( _pSceneNavigation == nullptr || pAsset == nullptr )
            return;
        const AABB meshBounds{ pAsset->_boundsMin, pAsset->_boundsMax };
        _pSceneNavigation->invalidateArea( meshBounds.transformedBy( makeFracturedObjectMatrix() ), true );
    }

    void FractureComponent::setFracturePath( string_view path )
    {
        _fracturePath = string{ path };
    }

    string FractureComponent::resolveFracturePath() const
    {
        if ( _fracturePath.empty() == false )
            return _fracturePath;
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return {};
        for ( Component* pComp : pOwner->getComponents() )
        {
            const MeshComponent* pMesh = pComp != nullptr ? castTo<MeshComponent>( pComp ) : nullptr;
            if ( pMesh != nullptr && pMesh->getMeshId().empty() == false )
                return FractureAsset::makePathForMesh( pMesh->getMeshId() );
        }
        return {};
    }

    shared_ptr<const FractureAsset> FractureComponent::acquireFracture()
    {
        const string path = resolveFracturePath();
        if ( path.empty() )
            return nullptr;
        _acquiredGeneration                   = FractureAssetCache::getReloadGeneration();
        shared_ptr<const FractureAsset> asset = FractureAssetCache::acquire( path );
        if ( asset == nullptr )
            SW_LOG_ERROR( "'%#': fracture asset '%#' could not be read - the object stays intact", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", path.c_str() );
        return asset;
    }

    bool FractureComponent::isFractureStale() const
    {
        return _acquiredGeneration != FractureAssetCache::getReloadGeneration();
    }
} // namespace sw

namespace sw
{
    const GameObject* FractureNavGeometrySource::getNavGeometryOwner() const
    {
        return _fracture.getOwner();
    }

    bool FractureNavGeometrySource::overridesOwnerGeometry() const
    {
        return _fracture.isFractured();
    }

    void FractureNavGeometrySource::collectNavGeometry( NavMeshGeometry& outGeometry, uint8 area ) const
    {
        const FractureAsset* pAsset = _fracture.findAsset();
        if ( pAsset == nullptr || _fracture.isFractured() == false )
            return;
        // 붙어 있는 조각은 쪼갤 때의 오브젝트 자세에 정적 바디로 서 있다 — 그 자세로 조각 삼각형(메시 공간)을 옮긴다.
        const float4x4 world = _fracture.makeFracturedObjectMatrix();
        for ( uint32 leaf = 0; leaf < pAsset->getPieceCount(); ++leaf )
        {
            if ( _fracture.isLeafInAnchoredGroup( leaf ) == false )
                continue;
            const vector_reference<const RHIVertex> listVertex = pAsset->getPieceVertices( leaf );
            if ( listVertex.size() < 3 )
                continue;
            outGeometry.addTriangleList( &listVertex.data()->_arrPosition[0], static_cast<uint32>( listVertex.size() ), sizeof( RHIVertex ), world, area );
        }
    }
} // namespace sw

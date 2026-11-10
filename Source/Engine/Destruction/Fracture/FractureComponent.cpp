#include "pch.h"

#include "Engine/Destruction/Fracture/FractureComponent.h"

#include "Core/Foundation/Macros.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Destruction/Fracture/FractureAsset.h"
#include "Engine/Navigation/NavMeshGeometry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Reflection/ReflectionCast.h"
#include "Engine/Visual/Component/MeshComponent.h"

// FractureAssetCache — 이 파일만 쓴다(한 파일 전용 도우미는 따로 헤더를 두지 않는다)
namespace sw
{
    struct FractureAsset;

    /** @brief 파일 머리말을 참고하세요. 쓰는 쪽은 `SharedAssetCache<FractureAsset>` 으로 부릅니다. */
    extern template class SW_API_EXTERN_TEMPLATE SharedAssetCache<FractureAsset>;
} // namespace sw

namespace sw
{
    /** 쪼갠 조각의 바디와 렌더링이 이전 형상을 가리킨 채 내용만 바뀌면 안 되므로 제자리로 바꾸지 않고 떼어 낸다. */
    SW_SHARED_ASSET_CACHE( FractureAsset, FractureAsset, FractureAsset::kAssetType.withLoader( AssetTypeLoader::of<FractureAsset>( MemoryTag::Physics, SharedAssetReloadMode::Detach ) ) );
} // namespace sw

namespace sw
{
    FractureComponent::FractureComponent()
        : FractureComponentBase()
        , _fracturePath{}
        , _navGeometrySource{ *this }
        , _pSceneNavigation{ nullptr }
        , _fractureReload{}
        , _navGeometryRegistration{}
    {
    }

    void FractureComponent::onRegister( GameObjectManager& manager )
    {
        FractureComponentBase::onRegister( manager );
        _pSceneNavigation        = &manager.getSubsystem<SceneNavigation>();
        _navGeometryRegistration = _pSceneNavigation->registerGeometrySource( &_navGeometrySource );
    }

    void FractureComponent::onUnregister( GameObjectManager& manager )
    {
        _navGeometryRegistration.reset();
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
            return _fracturePath.path();
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return {};
        for ( Component* pComp : pOwner->getComponents() )
        {
            const MeshComponent* pMesh = pComp != nullptr ? castTo<MeshComponent>( pComp ) : nullptr;
            if ( pMesh != nullptr && pMesh->getMeshID().empty() == false )
                return FractureAsset::makePathForMesh( pMesh->getMeshID() );
        }
        return {};
    }

    shared_ptr<const FractureAsset> FractureComponent::acquireFracture()
    {
        const string path = resolveFracturePath();
        if ( path.empty() )
            return nullptr;
        _fractureReload.markSeen();
        shared_ptr<const FractureAsset> asset = SharedAssetCache<FractureAsset>::acquire( path );
        if ( asset == nullptr )
            SW_LOG_ERROR_ID( "FractureComponent.FractureAssetCouldNot", "'%#': fracture asset '%#' could not be read - the object stays intact", getOwner() != nullptr ? getOwner()->getName().c_str() : "?", path.c_str() );
        return asset;
    }

    bool FractureComponent::isFractureStale() const
    {
        return _fractureReload.isStale();
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
        // 붙어 있는 조각은 쪼갤 때의 오브젝트 자세에 정적 바디로 서 있다. 그 자세로 조각 삼각형(메시 공간)을 옮긴다.
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

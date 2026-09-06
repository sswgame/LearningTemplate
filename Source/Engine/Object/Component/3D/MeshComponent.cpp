#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    MeshComponent::MeshComponent()
        : _mesh{}
        , _pMaterial{ nullptr }
        , _materialInstance{}
        , _meshId{}
        , _boundsRadius{ 0.866f }
        , _blendMode{ RHIBlendMode::Opaque }
        , _pPrimitiveRegistry{ nullptr }
        , _primitiveIndex{ kInvalidPrimitiveIndex }
        , _bVisible{ SW_TRUE }
        , _bRenderDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void MeshComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        resolveRuntimeMesh();
    }

    void MeshComponent::resolveRuntimeMesh()
    {
        if ( _mesh != nullptr )
            return;
        _mesh = Mesh::createPrimitive( _meshId );
        markRenderStateDirty();
    }

    void MeshComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        _pPrimitiveRegistry->add( this );
    }

    void MeshComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getPrimitiveRegistry().remove( this );
        _pPrimitiveRegistry = nullptr;
        SceneComponent::onUnregister( manager );
    }

    void MeshComponent::markRenderStateDirty()
    {
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->markDirty( this );
    }

    void MeshComponent::onPropertyChanged( hashed_string propertyName )
    {
        // 트랜스폼 PROPERTY 는 여기서 markTransformDirty 로 이어지고, 그 결과 월드 행렬이 다시
        // 계산될 때 onWorldTransformUpdated 가 렌더 더티를 찍는다. 그래서 여기선 렌더 관련
        // PROPERTY 만 보면 된다 — 어느 쪽이든 빠지는 경로가 없다.
        SceneComponent::onPropertyChanged( propertyName );
        markRenderStateDirty();
    }

    void MeshComponent::onWorldTransformUpdated()
    {
        markRenderStateDirty();
    }

    void MeshComponent::setMesh( shared_ptr<Mesh> mesh )
    {
        _mesh = std::move( mesh );
        markRenderStateDirty();
    }

    void MeshComponent::setMaterial( Material* pMaterial )
    {
        _pMaterial = pMaterial;
        markRenderStateDirty();
    }

    void MeshComponent::setMaterialInstance( shared_ptr<MaterialInstance> instance )
    {
        _materialInstance = std::move( instance );
        markRenderStateDirty();
    }

    void MeshComponent::setBlendMode( RHIBlendMode mode )
    {
        _blendMode = mode;
        markRenderStateDirty();
    }

    void MeshComponent::setBoundsRadius( float32 radius )
    {
        _boundsRadius = radius;
        markRenderStateDirty();
    }

    void MeshComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible ? SW_TRUE : SW_FALSE;
        markRenderStateDirty();
    }

} // namespace sw

#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceManager.h"

namespace sw
{
    SW_LOG_CALLER( "MeshComponent" );

    MeshComponent::MeshComponent()
        : _mesh{}
        , _pMaterial{ nullptr }
        , _materialInstance{}
        , _meshId{}
        , _materialPath{}
        , _acquiredMaterialPath{}
        , _boundsRadius{ 0.866f }
        , _blendMode{ RHIBlendMode::Opaque }
        , _gpuSpinSeed{ 0 }
        , _pPrimitiveRegistry{ nullptr }
        , _primitiveIndex{ kInvalidPrimitiveIndex }
        , _bVisible{ SW_TRUE }
        , _reserved{ 0 }
    {
        // 월드가 바뀌면 트랜스폼 칸의 프리미티브 번호로 렌더 더티가 찍힌다(`setPrimitiveIndex`). 훅은 받지 않는다.
        setWorldTransformNotify( false );
    }

    void MeshComponent::onBeginPlay()
    {
        SceneComponent::onBeginPlay();
        resolveRenderAssets();
    }

    void MeshComponent::resolveRenderAssets()
    {
        resolveRuntimeMesh();
        resolveMaterialAsset();
    }

    void MeshComponent::setMaterialPath( string_view path )
    {
        _materialPath = hashed_string( string{ path }.c_str() );
        resolveRenderAssets();
    }

    void MeshComponent::resolveMaterialAsset()
    {
        const hashed_string path = _materialPath.empty() ? getDefaultMaterialPath() : _materialPath;
        if ( path == _acquiredMaterialPath || engine::areEngineServicesBound() == false )
            return;

        MaterialCache& cache     = engine::getResourceManager().getMaterialManager();
        Material*      pMaterial = nullptr;
        if ( path.empty() == false )
        {
            pMaterial = cache.acquire( path.c_str(), nullptr );
            if ( pMaterial != nullptr )
                cache.requestInitialize( path.c_str() );
            else
                SW_LOG_WARNING( "Material '%#' could not be acquired - the mesh uses the scene default", path.c_str() );
        }

        const hashed_string previousPath = _acquiredMaterialPath;
        _acquiredMaterialPath            = ( pMaterial != nullptr ) ? path : hashed_string{};
        setMaterial( pMaterial );
        if ( previousPath.empty() == false )
            cache.release( previousPath.c_str() );
    }

    void MeshComponent::resolveRuntimeMesh()
    {
        if ( _mesh != nullptr )
            return;
        // **공유되는** 프리미티브를 받는다. 컴포넌트마다 제 메시를 만들면 배치가 그만큼 갈린다.
        _mesh = MeshUtil::acquirePrimitive( _meshId.empty() ? getDefaultMeshId() : string_view{ _meshId } );
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
        if ( _acquiredMaterialPath.empty() == false )
        {
            _pMaterial = nullptr;
            if ( engine::areEngineServicesBound() )
                engine::getResourceManager().getMaterialManager().release( _acquiredMaterialPath.c_str() );
            _acquiredMaterialPath = hashed_string{};
        }
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
        // 계산될 때 트랜스폼 칸의 프리미티브 번호로 렌더 더티가 찍힌다. 그래서 여기서는 렌더 관련
        // PROPERTY 만 보면 된다. 어느 쪽이든 빠지는 경로가 없다.
        SceneComponent::onPropertyChanged( propertyName );
        static const hashed_string s_materialPathName( "_materialPath" );
        if ( propertyName == s_materialPathName )
            resolveRenderAssets();
        markRenderStateDirty();
    }

    void MeshComponent::setPrimitiveIndex( uint32 index )
    {
        _primitiveIndex = index;
        // 틱 뒤 적용 · 플러시는 이 번호로 등록부에 바로 찍는다(컴포넌트를 거치지 않는다).
        setTransformPrimitiveIndex( index == kInvalidPrimitiveIndex ? SceneTransformStorage::kNoPrimitive : index );
    }

    void MeshComponent::onOwnerActiveInHierarchyChanged()
    {
        // 부분 수집은 더티 칸의 포함 여부가 바뀐 것을 보고 전체 수집으로 넘어간다(`setVisible` 과 같은 길).
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

    bool MeshComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        const float4x4 world = getWorldMatrix();
        outCenter            = world.getTranslation();
        outRadius            = _boundsRadius * world.getMaximumAxisScale();
        return true;
    }

    void MeshComponent::setBoundsRadius( float32 radius )
    {
        _boundsRadius = radius;
        markRenderStateDirty();
    }

    void MeshComponent::setGpuSpinSeed( uint32 seed )
    {
        _gpuSpinSeed = seed;
        markRenderStateDirty();
    }

    void MeshComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible ? SW_TRUE : SW_FALSE;
        markRenderStateDirty();
    }

} // namespace sw

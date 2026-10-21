#include "pch.h"

#include "Engine/Object/Component/3D/MeshComponent.h"

#include "Core/Math/AABB.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "MeshComponent" );

    MeshComponent::MeshComponent()
        : _mesh{}
        , _meshLoad{}
        , _pMaterial{ nullptr }
        , _materialInstance{}
        , _meshID{}
        , _materialPath{}
        , _acquiredMaterialPath{}
        , _requestedMaterialPath{}
        , _resolvedMeshID{}
        , _boundsRadius{ 0.866f }
        , _blendMode{ RHIBlendMode::Opaque }
        , _gpuSpinSeed{ 0 }
        , _spriteInstanceData{}
        , _vertexAnimationPhase{ 0.0f }
        , _sortKey{ 0 }
        , _pPrimitiveRegistry{ nullptr }
        , _bVisible{ SW_TRUE }
        , _reserved{ 0 }
    {
        // 월드가 바뀌면 트랜스폼 칸의 프리미티브 번호로 렌더 더티가 찍힌다(`ScenePrimitive::setPrimitiveIndex`). 훅은 받지 않는다.
        setWorldTransformNotify( false );
    }

    void MeshComponent::onBeginPlay()
    {
        ScenePrimitive::onBeginPlay();
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
        if ( path == _requestedMaterialPath || engine::areEngineServicesBound() == false )
            return;
        _requestedMaterialPath = path;

        MaterialCache& cache     = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        Material*      pMaterial = nullptr;
        hashed_string  borrowedPath{};
        if ( path.empty() == false )
        {
            // 디바이스 없이 잡는 acquire 는 파일을 읽지 않아 늘 성공한다 — 없는 파일은 여기서 가린다.
            pMaterial    = ResourceIO::hasResource( path.c_str() ) ? cache.acquire( path.c_str(), nullptr ) : nullptr;
            borrowedPath = path;
            if ( pMaterial == nullptr )
            {
                // 못 읽은 머티리얼은 마젠타 체커로 보인다 — 씬 기본(흰색)이면 화면에서 빠진 것을 알 수 없다. 대체를 정하는 곳은 여기 한 곳이다.
                // 파일은 있는데 내용이 틀린 머티리얼은 GPU 에 올릴 때(`Material::initialize`) 실패해 그려지지 않고 오류가 남는다.
                const string& missingPath = engine::getSubsystem<EngineDefaultAssets>()._missingMaterial.get();
                SW_LOG_ERROR( "Material '%#' could not be acquired - the mesh uses the missing-material checker '%#'", path.c_str(), missingPath.c_str() );
                borrowedPath = hashed_string( missingPath.c_str() );
                pMaterial    = cache.acquire( borrowedPath.c_str(), nullptr );
            }
            if ( pMaterial != nullptr )
                cache.requestInitialize( borrowedPath.c_str() );
        }

        const hashed_string previousPath = _acquiredMaterialPath;
        _acquiredMaterialPath            = ( pMaterial != nullptr ) ? borrowedPath : hashed_string{};
        setMaterial( pMaterial );
        if ( previousPath.empty() == false )
            cache.release( previousPath.c_str() );
    }

    void MeshComponent::resolveRuntimeMesh()
    {
        const string_view   meshID = _meshID.empty() ? getDefaultMeshID() : string_view{ _meshID };
        const hashed_string hashedMeshID( string{ meshID }.c_str() );
        const bool          bSameID = _resolvedMeshID == hashedMeshID;
        if ( bSameID && ( _mesh != nullptr || _meshLoad.isLoading() ) )
            return;
        _resolvedMeshID = hashedMeshID;
        // **공유되는** 메시를 받는다(`.mesh` 경로는 메시 캐시, 그 밖은 내장 도형). 컴포넌트마다 제 메시를 만들면 배치가 그만큼 갈린다.
        if ( MeshAssetFormat::isMeshAssetPath( meshID ) == false )
        {
            _meshLoad.reset();
            _mesh = MeshUtil::acquirePrimitive( meshID );
            markRenderStateDirty();
            return;
        }
        // 에셋은 워커에서 읽는다. 다 읽을 때까지 그리지 않고, 도착하면 `AssetArrival` 단계가 넣는다. 읽지 못한 에셋은 메시 없이 남는다(그리지 않는다) —
        // 캐시가 경로마다 한 번 경고하고, 핫 리로드 알림이 다시 해석한다.
        _meshLoad = MeshCache::request( meshID );
        if ( _meshLoad.isLoading() == false )
        {
            applyMeshLoad();
            return;
        }
        _mesh = nullptr;
        markRenderStateDirty();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateMeshLoad );
    }

    bool MeshComponent::updateMeshLoad( ScenePrimitive& primitive )
    {
        // 레지스트리에 들어오는 것은 메시 계열뿐이다(`ScenePrimitive.h` 의 불변식).
        MeshComponent& self = static_cast<MeshComponent&>( primitive );
        if ( self._meshLoad.isLoading() )
            return true;
        if ( self._meshLoad.isValid() == false )
            return false; // 기다리는 사이 다른 id(내장 도형)로 바뀌었다
        self.applyMeshLoad();
        // 도착한 메시로 렌더 에셋을 다시 푼다 — 파생 타입(스켈레탈)이 스킨 사본을 이때 만든다. 메시 id 는 그대로라 메시를 다시 요청하지 않는다.
        // 실패한 로드는 다시 풀지 않는다 — 메시가 없으면 다시 요청하므로 프레임마다 디스크를 읽게 된다(다음 해석 · 핫 리로드가 다시 시도한다).
        if ( self._mesh != nullptr )
            self.resolveRenderAssets();
        return self._meshLoad.isLoading();
    }

    void MeshComponent::applyMeshLoad()
    {
        _mesh = _meshLoad.get();
        _meshLoad.reset();
        markRenderStateDirty();
    }

    void MeshComponent::onRegister( GameObjectManager& manager )
    {
        ScenePrimitive::onRegister( manager );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        _pPrimitiveRegistry->add( this );
        // 등록 전에 요청한 메시 로드가 아직 끝나지 않았으면 이제부터 씬 프레임이 기다린다.
        if ( _meshLoad.isLoading() )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateMeshLoad );
        // 픽셀 퍼펙트 카메라가 이미 정한 스냅 단위를 이어받는다(정한 뒤에 생긴 스프라이트도 격자에 붙는다).
        setPixelSnapUnit( manager.getCameraRegistry().getPixelSnapUnit() );
    }

    void MeshComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getPrimitiveRegistry().remove( this );
        _pPrimitiveRegistry = nullptr;
        if ( _acquiredMaterialPath.empty() == false )
        {
            _pMaterial = nullptr;
            if ( engine::areEngineServicesBound() )
                engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( _acquiredMaterialPath.c_str() );
            _acquiredMaterialPath = hashed_string{};
        }
        _requestedMaterialPath = hashed_string{};
        ScenePrimitive::onUnregister( manager );
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
        ScenePrimitive::onPropertyChanged( propertyName );
        static const hashed_string s_materialPathName( "_materialPath" );
        static const hashed_string s_meshIDName( "_meshID" );
        if ( propertyName == s_materialPathName || propertyName == s_meshIDName )
            resolveRenderAssets();
        markRenderStateDirty();
    }

    void MeshComponent::onOwnerActiveInHierarchyChanged()
    {
        // 부분 수집은 더티 칸의 포함 여부가 바뀐 것을 보고 전체 수집으로 넘어간다(`setVisible` 과 같은 길).
        markRenderStateDirty();
    }

    void MeshComponent::setMesh( shared_ptr<Mesh> mesh )
    {
        _mesh = std::move( mesh );
        _meshLoad.reset(); // 런타임 지정이 이긴다 — 읽던 에셋이 도착해도 덮지 않는다
        // 지금 id 의 메시로 삼는다 — 시작 · 로드 뒤 해석이 런타임 지정을 덮지 않고, id 가 바뀌면 그 id 의 것으로 바뀐다.
        _resolvedMeshID = hashed_string( string{ _meshID.empty() ? getDefaultMeshID() : string_view{ _meshID } }.c_str() );
        markRenderStateDirty();
    }

    void MeshComponent::setMeshID( string_view meshID )
    {
        _meshID = string{ meshID };
        resolveRuntimeMesh();
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

    void MeshComponent::setSpriteInstanceData( const GPUSpriteInstanceData& data )
    {
        // 프레임 · 색을 바꾸는 쪽은 스냅 단위를 모른다 — 지금 값을 지킨다.
        GPUSpriteInstanceData merged = data;
        merged._pixelSnap            = _spriteInstanceData._pixelSnap;
        if ( _spriteInstanceData == merged )
            return;
        _spriteInstanceData = merged;
        markRenderStateDirty();
    }

    void MeshComponent::setPixelSnapUnit( float32 unit )
    {
        GPUSpriteInstanceData snapped = _spriteInstanceData;
        snapped._pixelSnap            = MathUtil::max( unit, 0.0f );
        if ( _spriteInstanceData == snapped )
            return;
        _spriteInstanceData = snapped;
        markRenderStateDirty();
    }

    void MeshComponent::setSortKey( uint32 sortKey )
    {
        if ( _sortKey == sortKey )
            return;
        _sortKey = sortKey;
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
        outRadius            = getBoundsRadius() * world.getMaximumAxisScale(); // GPU 컬링(`GPUSceneBuilder`)과 같은 반지름
        return true;
    }

    bool MeshComponent::getWorldBox( AABB& outBox ) const
    {
        const AABB local = ( _mesh != nullptr && _mesh->getVertexCount() > 0 ) ? AABB{
                                                                                     _mesh->getLocalBoundsMin(), _mesh->getLocalBoundsMax()
        }
                                                                               : AABB{ float3{ -0.5f, -0.5f, -0.5f }, float3{ 0.5f, 0.5f, 0.5f } };
        outBox           = local.transformedBy( getWorldMatrix() );
        return true;
    }

    float32 MeshComponent::getBoundsRadius() const
    {
        // 메시가 아는 경계를 덮는다 — 적어 둔 값은 키우기만 한다(정점 애니메이션 · 셰이더 변형의 여유). 적어 둔 값(기본 단위 상자 0.866)
        // 만 쓰면 그보다 큰 도형(캡슐 끝 · 평면)이 보이는데도 컬링에 잘린다.
        const float32 meshRadius = ( _mesh != nullptr ) ? _mesh->getBoundingRadius() : 0.0f;
        return MathUtil::max( _boundsRadius, meshRadius );
    }

    void MeshComponent::setBoundsRadius( float32 radius )
    {
        _boundsRadius = radius;
        markRenderStateDirty();
    }

    void MeshComponent::setGPUSpinSeed( uint32 seed )
    {
        _gpuSpinSeed = seed;
        markRenderStateDirty();
    }

    void MeshComponent::setVertexAnimationPhase( float32 phaseSeconds )
    {
        if ( Memory::compare( &_vertexAnimationPhase, &phaseSeconds, sizeof( phaseSeconds ) ) == 0 )
            return;
        _vertexAnimationPhase = phaseSeconds;
        markRenderStateDirty();
    }

    void MeshComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible ? SW_TRUE : SW_FALSE;
        markRenderStateDirty();
    }

} // namespace sw

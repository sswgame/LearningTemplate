#include "pch.h"

#include "Engine/Visual/Component/InstancedMeshComponent.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/MeshInstanceBatch.h"

namespace sw
{
    InstancedMeshComponent::InstancedMeshComponent()
        : _meshID{}
        , _materialPath{}
        , _sectionName{}
        , _listInstance{}
        , _pBatch{}
        , _meshLoad{}
        , _pMaterial{ nullptr }
        , _acquiredMaterialPath{}
        , _pPrimitiveRegistry{ nullptr }
    {
    }

    InstancedMeshComponent::~InstancedMeshComponent()
    {
        releaseBatch();
    }

    float4x4 InstancedMeshComponent::makeLocalMatrix( const InstancedMeshTransform& transform )
    {
        return float4x4::makeScale( transform._scale ) * float4x4::makeFromYawPitchRoll( transform._rotation ) * float4x4::makeTranslation( transform._position );
    }

    void InstancedMeshComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        rebuildBatch();
    }

    void InstancedMeshComponent::onUnregister( GameObjectManager& manager )
    {
        releaseBatch();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->removeAssetWaiter( this );
        _pPrimitiveRegistry = nullptr;
        SceneComponent::onUnregister( manager );
    }

    void InstancedMeshComponent::onPostLoad()
    {
        SceneComponent::onPostLoad();
        // 배치는 게임 스레드에서만 짓는다(머티리얼 캐시 · 프리미티브 레지스트리) — 비동기 씬 로드는 이 함수를 워커에서 부른다(폴리지와 같은 규칙).
        const TaskManager* pTaskManager = engine::findSubsystem<TaskManager>(); // 선택: 작업 시스템 없는 호스트(테스트)는 한 스레드다
        GameObject*        pOwner       = getOwner();
        GameObjectManager* pManager     = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pTaskManager != nullptr && pTaskManager->isMainThread() == false && pManager != nullptr )
        {
            pManager->deferPostTickOnComponent( *this, []( InstancedMeshComponent& self )
            { self.rebuildBatch(); } );
            return;
        }
        rebuildBatch();
    }

    void InstancedMeshComponent::onPropertyChanged( hashed_string propertyName )
    {
        SceneComponent::onPropertyChanged( propertyName );
        if ( propertyName == getActivePropertyName() )
        {
            if ( _pBatch != nullptr )
                _pBatch->markAllEntriesDirty();
            return;
        }
        rebuildBatch();
    }

    void InstancedMeshComponent::onWorldTransformUpdated()
    {
        SceneComponent::onWorldTransformUpdated();
        writeEntryWorlds();
    }

    void InstancedMeshComponent::onOwnerActiveInHierarchyChanged()
    {
        SceneComponent::onOwnerActiveInHierarchyChanged();
        if ( _pBatch != nullptr )
            _pBatch->markAllEntriesDirty();
    }

    void InstancedMeshComponent::setInstances( string_view meshID, string_view materialPath, hashed_string sectionName, vector<InstancedMeshTransform> listInstance )
    {
        _meshID       = meshID;
        _materialPath = materialPath;
        _sectionName  = sectionName;
        _listInstance = std::move( listInstance );
        rebuildBatch();
    }

    void InstancedMeshComponent::releaseBatch()
    {
        if ( _pBatch != nullptr && _pPrimitiveRegistry != nullptr )
        {
            PrimitiveBatch* const arrBatch[] = { _pBatch.get() };
            _pPrimitiveRegistry->removeInstanceBatches( vector_reference<PrimitiveBatch* const>( arrBatch, 1 ) );
        }
        _pBatch.reset();
        if ( _acquiredMaterialPath.empty() == false && engine::areEngineServicesBound() )
            engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( _acquiredMaterialPath.view() );
        _acquiredMaterialPath = hashed_string{};
        _pMaterial            = nullptr;
    }

    void InstancedMeshComponent::rebuildBatch()
    {
        releaseBatch();
        if ( _pPrimitiveRegistry == nullptr || _listInstance.empty() || engine::areEngineServicesBound() == false )
            return;
        // 메시는 워커가 읽는다 — 다 올 때까지 배치 없이 두고 씬 프레임의 도착 단계가 다시 짓는다(언리얼 ISM 도 메시가 스트리밍되기 전에는 그리지 않는다).
        const string&    meshID = _meshID.path();
        shared_ptr<Mesh> mesh;
        if ( MeshAssetFormat::isMeshAssetPath( meshID ) )
        {
            _meshLoad = MeshCache::request( meshID, MeshCPUAccess::ReleaseAfterUpload );
            if ( _meshLoad.isLoading() )
            {
                _pPrimitiveRegistry->addAssetWaiter( this, &InstancedMeshComponent::updateMeshArrival );
                return;
            }
            mesh = _meshLoad.isValid() ? _meshLoad.get() : nullptr;
        }
        else
        {
            _meshLoad = MeshHandle{};
            mesh      = MeshUtil::acquirePrimitive( meshID );
        }
        if ( mesh == nullptr )
        {
            SW_LOG_WARNING_ID( "InstancedMeshComponent.MeshNotAcquired", "Instanced mesh '%#' could not be acquired - its %# instances are not drawn", meshID, static_cast<uint32>( _listInstance.size() ) );
            return;
        }

        // 머티리얼은 메시 컴포넌트와 같은 규칙이다 — 빈 경로는 씬 기본, 없는 파일은 경고 뒤 씬 기본.
        const string& materialPath = _materialPath.path();
        if ( materialPath.empty() == false )
        {
            MaterialCache& cache = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
            _pMaterial           = AssetIO::hasAsset( AssetID{ materialPath } ) ? cache.acquire( materialPath ) : nullptr;
            if ( _pMaterial != nullptr )
            {
                _acquiredMaterialPath = hashed_string( materialPath.c_str() );
                cache.requestInitialize( materialPath );
            }
            else
                SW_LOG_WARNING_ID( "InstancedMeshComponent.MaterialNotAcquired", "Instanced mesh material '%#' could not be acquired - the scene default material is used", materialPath );
        }

        _pBatch = make_unique<MeshInstanceBatch>( mesh, _pMaterial, shared_ptr<MaterialInstance>{}, static_cast<uint32>( _listInstance.size() ), MeshInstanceWriter::CPU );
        _pBatch->setOwnerComponent( this );
        _pBatch->setDrawSection( _sectionName );
        writeEntryWorlds();
        _pPrimitiveRegistry->addInstanceBatch( _pBatch.get() );
    }

    void InstancedMeshComponent::writeEntryWorlds()
    {
        if ( _pBatch == nullptr )
            return;
        const float4x4 world  = getWorldMatrix();
        const Mesh*    pMesh  = _pBatch->getRawMesh();
        const float32  radius = pMesh != nullptr ? pMesh->getBoundingRadius() + pMesh->getLocalBoundsCenter().getLength() : 0.0f; // 원점 둘레 반지름 — 항목 바운드는 원점 중심이다
        for ( uint32 index = 0; index < static_cast<uint32>( _listInstance.size() ); ++index )
        {
            const InstancedMeshTransform& transform = _listInstance[index];
            const float32                 maxScale  = MathUtil::max( MathUtil::abs( transform._scale._x ), MathUtil::max( MathUtil::abs( transform._scale._y ), MathUtil::abs( transform._scale._z ) ) );
            _pBatch->setWorld( index, makeLocalMatrix( transform ) * world );
            _pBatch->setBoundsRadius( index, radius * maxScale );
        }
    }

    bool InstancedMeshComponent::updateMeshArrival( Component& component, float32 /*deltaSeconds*/ )
    {
        // 이 함수를 거는 것은 이 컴포넌트 자신뿐이다(`rebuildBatch`).
        InstancedMeshComponent& self = static_cast<InstancedMeshComponent&>( component );
        if ( self._meshLoad.isLoading() )
            return true;
        self.rebuildBatch();
        return self._meshLoad.isLoading();
    }
} // namespace sw

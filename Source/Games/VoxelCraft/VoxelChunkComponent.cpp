#include "pch.h"

#include "Games/VoxelCraft/VoxelChunkComponent.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "Games/VoxelCraft/VoxelDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct VoxelChunkComponentInternal
        {
            /** @brief 키트 정점 → 엔진 정점입니다(같은 네 속성). */
            static RHIVertex toRhiVertex( const VoxelMeshVertex& source )
            {
                RHIVertex vertex{};
                vertex._arrPosition[0] = source._position._x;
                vertex._arrPosition[1] = source._position._y;
                vertex._arrPosition[2] = source._position._z;
                vertex._arrNormal[0]   = source._normal._x;
                vertex._arrNormal[1]   = source._normal._y;
                vertex._arrNormal[2]   = source._normal._z;
                vertex._arrUv[0]       = source._uv._x;
                vertex._arrUv[1]       = source._uv._y;
                vertex._arrColor[0]    = source._color._x;
                vertex._arrColor[1]    = source._color._y;
                vertex._arrColor[2]    = source._color._z;
                vertex._arrColor[3]    = source._color._w;
                return vertex;
            }

            /** @brief 메시 컴포넌트 하나에 정점을 옮겨 담습니다(비면 숨긴다). */
            static void applyVertices( MeshComponent* pMeshComponent, const vector<VoxelMeshVertex>& listVertex )
            {
                if ( pMeshComponent == nullptr )
                    return;
                if ( listVertex.empty() )
                {
                    pMeshComponent->setVisible( false );
                    return;
                }
                vector<RHIVertex> listRhiVertex;
                listRhiVertex.reserve( listVertex.size() );
                for ( const VoxelMeshVertex& vertex : listVertex )
                {
                    listRhiVertex.push_back( toRhiVertex( vertex ) );
                }
                shared_ptr<Mesh> mesh = Mesh::create();
                if ( mesh == nullptr )
                    return;
                mesh->setVertices( std::move( listRhiVertex ) );
                pMeshComponent->setMesh( mesh );
                pMeshComponent->setVisible( true );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelChunkComponent::VoxelChunkComponent()
        : _director{}
        , _chunkX{ 0 }
        , _chunkZ{ 0 }
        , _waterMeshName{ "Water" }
        , _scratchMesh{}
        , _bRebuildRequested{ SW_FALSE }
        , _bApplyScheduled{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelChunkComponent::~VoxelChunkComponent() = default;

    void VoxelChunkComponent::assignChunk( GameObjectHandle director, int32 chunkX, int32 chunkZ )
    {
        _director = director;
        _chunkX   = chunkX;
        _chunkZ   = chunkZ;
    }

    void VoxelChunkComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        if ( _bRebuildRequested == SW_FALSE || _bApplyScheduled == SW_TRUE )
            return;
        GameObject*                   pOwner    = getOwner();
        GameObjectManager*            pManager  = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const VoxelDirectorComponent* pDirector = pManager != nullptr ? GameDirectorComponent::resolve<VoxelDirectorComponent>( *pManager, _director ) : nullptr;
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        // 메싱은 월드를 읽기만 한다 — 같은 그룹의 다른 청크와 나란히 돈다. 블록은 틱 뒤에만 바뀐다.
        _bRebuildRequested = SW_FALSE;
        VoxelMesher::fillChunkMesh( pDirector->getWorld(), _chunkX, _chunkZ, _scratchMesh );
        _bApplyScheduled           = SW_TRUE;
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            VoxelChunkComponent* pChunk = static_cast<VoxelChunkComponent*>( pManager->resolveComponent( self ) );
            if ( pChunk != nullptr )
                pChunk->applyMeshes();
        } );
    }

    void VoxelChunkComponent::applyMeshes()
    {
        _bApplyScheduled       = SW_FALSE;
        GameObject*    pOwner  = getOwner();
        MeshComponent* pOpaque = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        MeshComponent* pWater  = findWaterMesh();
        if ( pOpaque == pWater )
            pOpaque = nullptr; // 물 메시만 있으면 불투명은 없다
        VoxelChunkComponentInternal::applyVertices( pOpaque, _scratchMesh._listOpaqueVertex );
        VoxelChunkComponentInternal::applyVertices( pWater, _scratchMesh._listTranslucentVertex );
    }

    MeshComponent* VoxelChunkComponent::findWaterMesh() const
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return nullptr;
        MeshComponent* pFound = nullptr;
        pOwner->forEachComponentOfType<MeshComponent>( [this, &pFound]( MeshComponent* pMesh )
        {
            if ( pFound == nullptr && pMesh->getComponentName() == _waterMeshName )
                pFound = pMesh;
        } );
        return pFound;
    }
} // namespace sw

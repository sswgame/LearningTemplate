#include "pch.h"

#include "Games/VoxelCraft/VoxelChunkComponent.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshBuildVertex.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Visual/Component/MeshComponent.h"

#include "Games/VoxelCraft/VoxelGameModeComponent.h"

namespace sw
{
    namespace
    {
        struct VoxelChunkComponentInternal
        {
            /** @brief 키트 정점 → 메시를 짓는 정점입니다(같은 네 속성). GPU 정점 칸으로 옮기는 일은 메시가 한다(`Mesh::setBuildVertices`). */
            static MeshBuildVertex toBuildVertex( const VoxelMeshVertex& source ) { return MeshBuildVertex{ source._position, source._normal, source._uv, source._color }; }

            /**
             * @brief 블록 머티리얼의 아틀라스 칸이 카탈로그와 같은지 봅니다. 메셔는 카탈로그의 열 수로 타일 칸 UV 를 싣고 셰이더(voxel_blocks.hlsl)는 머티리얼 값으로 풀므로,
             *        카탈로그가 정본이고 머티리얼은 따라야 한다. 머티리얼이 아직 없으면(서버 · 로드 전) 견주지 않고 true 입니다. 칸이 없으면 다른 것으로 본다.
             */
            static bool checkAtlasGrid( const MeshComponent* pMeshComponent, const VoxelBlockCatalog& catalog )
            {
                const Material* pMaterial = pMeshComponent != nullptr ? pMeshComponent->getMaterial() : nullptr;
                if ( pMaterial == nullptr )
                    return true;
                const float4* pAtlasGrid = static_cast<const float4*>( pMaterial->getParameterData( "_atlasGrid" ) );
                return catalog.checkAtlasGrid( pAtlasGrid != nullptr ? *pAtlasGrid : float4{}, pMeshComponent->getMaterialPath().c_str() );
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
                vector<MeshBuildVertex> listBuildVertex;
                listBuildVertex.reserve( listVertex.size() );
                for ( const VoxelMeshVertex& vertex : listVertex )
                {
                    listBuildVertex.push_back( toBuildVertex( vertex ) );
                }
                shared_ptr<Mesh> mesh = Mesh::create();
                if ( mesh == nullptr )
                    return;
                mesh->setBuildVertices( listBuildVertex );
                pMeshComponent->setMesh( mesh );
                pMeshComponent->setVisible( true );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    VoxelChunkComponent::VoxelChunkComponent()
        : _chunkX{ 0 }
        , _chunkZ{ 0 }
        , _waterMeshName{}
        , _scratchMesh{}
        , _bRebuildRequested{ SW_FALSE }
        , _bApplyScheduled{ SW_FALSE }
        , _bAtlasChecked{ SW_FALSE }
        , _bAtlasMatches{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    VoxelChunkComponent::~VoxelChunkComponent() = default;

    void VoxelChunkComponent::assignChunk( GameObjectHandle gameMode, int32 chunkX, int32 chunkZ )
    {
        bindGameMode( gameMode );
        _chunkX = chunkX;
        _chunkZ = chunkZ;
    }

    void VoxelChunkComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bRebuildRequested == SW_FALSE || _bApplyScheduled == SW_TRUE )
            return;
        const VoxelGameModeComponent* pGameMode = findGameMode<VoxelGameModeComponent>();
        if ( pGameMode == nullptr || pGameMode->isStarted() == false )
            return;
        // 메싱은 월드를 읽기만 하므로 같은 그룹의 다른 청크와 나란히 실행됩니다. 블록은 틱이 끝난 뒤에만 바뀝니다.
        _bRebuildRequested = SW_FALSE;
        VoxelMesher::fillChunkMesh( pGameMode->getWorld(), _chunkX, _chunkZ, _scratchMesh );
        _bApplyScheduled = SW_TRUE;
        context.deferPostTickOnComponent( *this, []( VoxelChunkComponent& chunk )
        { chunk.applyMeshes(); } );
    }

    void VoxelChunkComponent::applyMeshes()
    {
        _bApplyScheduled       = SW_FALSE;
        GameObject*    pOwner  = getOwner();
        MeshComponent* pOpaque = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        MeshComponent* pWater  = findWaterMesh();
        if ( pOpaque == pWater )
            pOpaque = nullptr; // 물 메시만 있으면 불투명은 없다
        const VoxelGameModeComponent* pGameMode       = findGameMode<VoxelGameModeComponent>();
        const VoxelBlockCatalog*      pCatalog        = pGameMode != nullptr ? pGameMode->getWorld().getCatalog() : nullptr;
        const bool                    bMaterialsReady = ( pOpaque == nullptr || pOpaque->getMaterial() != nullptr ) && ( pWater == nullptr || pWater->getMaterial() != nullptr );
        if ( _bAtlasChecked == SW_FALSE && pCatalog != nullptr && bMaterialsReady )
        {
            _bAtlasChecked            = SW_TRUE;
            const bool bOpaqueMatches = VoxelChunkComponentInternal::checkAtlasGrid( pOpaque, *pCatalog );
            const bool bWaterMatches  = VoxelChunkComponentInternal::checkAtlasGrid( pWater, *pCatalog );
            _bAtlasMatches            = ( bOpaqueMatches && bWaterMatches ) ? SW_TRUE : SW_FALSE;
        }
        if ( _bAtlasChecked != SW_FALSE && _bAtlasMatches == SW_FALSE )
        {
            // 타일 칸이 어긋난 머티리얼로는 그리지 않는다(오류는 견줄 때 남겼다)
            _scratchMesh.clear();
        }
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

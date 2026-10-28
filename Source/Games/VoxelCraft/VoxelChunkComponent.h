/**
 * @file VoxelChunkComponent.h
 * @brief 청크 하나를 화면에 그리는 컴포넌트입니다. 디렉터가 요청하면 월드에서 이 청크를 메싱해, 자기 오브젝트의 불투명 메시와 물 메시를 다시 만듭니다.
 */
#pragma once
#include "Core/Container/GameObjectHandle.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Feature/World/Voxel/View/VoxelMesher.h"

namespace sw
{
    class MeshComponent;

    /**
     * @class VoxelChunkComponent
     * @brief 청크 프리팹의 컴포넌트입니다. 같은 오브젝트에 불투명 메시(첫 메시 컴포넌트)와 물 메시(이름 `_waterMeshName`)가 있습니다.
     * @details 메시는 프리팹에 들어 있지 않고, 이 컴포넌트가 절차적으로 만들어 보관합니다.
     *          디렉터가 `PrePhysics` 에서 `requestRebuild` 로 요청하면, 기본 그룹(`DuringPhysics`)에서 월드를 읽어 메싱합니다.
     *          이때 청크들의 메싱은 서로 나란히 실행됩니다. 결과를 엔진 메시로 옮기는 일은 틱이 끝난 뒤 게임 스레드에서 합니다.
     *          새 메시를 만들어 교체하므로 렌더러가 그리는 중인 정점 버퍼를 덮어쓰지 않습니다. 이전 메시는 컴포넌트가 해제할 때 사라집니다.
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Chunk", Tooltip = "Procedural opaque and water meshes of one voxel chunk" )
    class VoxelChunkComponent : public Component
    {
    public:
        REFLECT_BODY();

        VoxelChunkComponent();
        virtual ~VoxelChunkComponent() override;

        void onTick( float32 deltaTime ) override;

        /** @brief 디렉터와 청크 번호를 정합니다. 디렉터가 청크를 스폰한 뒤에 부릅니다. */
        void assignChunk( GameObjectHandle director, int32 chunkX, int32 chunkZ );
        /** @brief 다음 틱에 메시를 다시 만들도록 요청합니다. 디렉터가 `PrePhysics` 에서 부릅니다. */
        void requestRebuild() { _bRebuildRequested = SW_TRUE; }

    private:
        /** @brief 메싱한 정점을 엔진 메시로 옮겨 적용합니다. 틱이 끝난 뒤 게임 스레드에서 부르고, 정점이 없으면 메시를 숨깁니다. */
        void           applyMeshes();
        MeshComponent* findWaterMesh() const;

    private:
        PROPERTY( Category = "Chunk", DisplayName = "Director", Tooltip = "Object with the VoxelDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Chunk", DisplayName = "Chunk X", Min = 0 )
        int32 _chunkX;
        PROPERTY( Category = "Chunk", DisplayName = "Chunk Z", Min = 0 )
        int32 _chunkZ;
        PROPERTY( Category = "Chunk", DisplayName = "Water Mesh", Tooltip = "Component name of the translucent water mesh" )
        hashed_string _waterMeshName;

        VoxelChunkMesh _scratchMesh;
        uint8          _bRebuildRequested : 1;
        uint8          _bApplyScheduled   : 1;
        uint8          _reserved          : 6;
    };
} // namespace sw

/**
 * @file VoxelChunkComponent.h
 * @brief 청크 하나의 모습 — 디렉터가 맡기면 월드에서 이 청크를 메싱해 자기 오브젝트의 불투명 · 물 메시를 다시 짓습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
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
     * @details 메시는 절차로 짓는다 — 프리팹이 아니라 이 컴포넌트가 든다. 디렉터가 `PrePhysics` 에서 `requestRebuild` 로 맡기면 기본 그룹(`DuringPhysics`)에서
     *          월드를 읽어 메싱하고(청크끼리 나란히), 엔진 메시로 옮기는 것은 틱 뒤 게임 스레드에서 한다. 새 메시를 만들어 건다 — 그리는 중인 정점 버퍼를
     *          덮어쓰지 않는다(옛 것은 컴포넌트가 놓을 때 사라진다).
     */
    REFLECT( Category = "VoxelCraft", DisplayName = "Voxel Chunk", Tooltip = "Procedural opaque and water meshes of one voxel chunk" )
    class VoxelChunkComponent : public Component
    {
    public:
        REFLECT_BODY();

        VoxelChunkComponent();
        virtual ~VoxelChunkComponent() override;

        void onTick( float32 deltaTime ) override;

        /** @brief 디렉터와 청크 번호를 정합니다(디렉터가 스폰한 뒤 부른다). */
        void assignChunk( GameObjectHandle director, int32 chunkX, int32 chunkZ );
        /** @brief 다음 틱에 다시 짓습니다(디렉터가 `PrePhysics` 에서 부른다). */
        void requestRebuild() { _bRebuildRequested = SW_TRUE; }

    private:
        /** @brief 메싱한 정점을 엔진 메시로 옮겨 겁니다(틱 뒤 · 게임 스레드). 비면 숨긴다. */
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

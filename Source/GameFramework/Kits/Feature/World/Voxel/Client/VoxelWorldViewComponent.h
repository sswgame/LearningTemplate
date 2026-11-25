/**
 * @file VoxelWorldViewComponent.h
 * @brief 복셀 키트의 클라이언트 쪽 — 디렉터(`VoxelGameModeComponent`)의 월드에서 몸 둘레의 청크를 청크 프리팹으로 세우고, 블록이 바뀐 청크의 메시를 다시 짓게 합니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_Voxel`, 전용 서버는 짓지 않는다). 디렉터의 읽기 API(`getWorld` · `getCenterChunkX` · `getViewChunkRadius`)만 쓰고 월드를 고치지 않는다 —
 *          무엇이 낡았는지는 청크의 판 번호(`VoxelChunk::_revision`)와 뷰를 지은 때의 번호를 견주어 안다. 디렉터는 같은 오브젝트에 먼저 붙인다(등록에서 찾아 핸들로 든다).
 *          `PostUpdate` 라 디렉터 걸음 · 블록 바꾸기 뒤의 틱 뒤 작업(게임 스레드)에서 이번 프레임의 월드를 본다. 세운 청크는 다음 틱에 서로 나란히 메싱한다(`VoxelChunkComponent`).
 *          그리는 청크는 상주 반경보다 한 칸 안쪽이다 — 메싱이 읽는 이웃 청크가 늘 올라와 있어 경계에 가짜 벽이 생기지 않는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Voxel/Catalog/VoxelBlock.h"

namespace sw
{
    class GameObjectManager;
    class VoxelChunkComponent;
    class VoxelGameModeComponent;

    /**
     * @class VoxelWorldViewComponent
     * @brief 디렉터와 같은 오브젝트에 붙이는 청크 뷰의 주인입니다. 청크 프리팹의 스폰 · 정리, 다시 메싱 차례, 블록 머티리얼의 아틀라스 칸 검사를 맡습니다.
     * @details 스폰한 청크는 상태에서 다시 세우는 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
     *          블록 머티리얼의 아틀라스 칸이 카탈로그와 같다고 견주기 전 · 달랐으면 메싱을 요청하지 않는다(청크 프리팹의 메시는 숨겨져 있다 — 검사 없이 그리는 때가 없다).
     */
    REFLECT( Category = "Voxel", DisplayName = "Voxel World View",
             Tooltip = "Client side of a voxel world: spawns and rebuilds the chunk meshes around the body of the Voxel Game Mode on the same object" )
    class SW_KIT_CLIENT_API VoxelWorldViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        VoxelWorldViewComponent();
        virtual ~VoxelWorldViewComponent() override;

        /** @brief 세운 청크를 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 세운 청크 하나입니다. */
        struct ChunkView
        {
            GameObjectHandle _object{};
            optional<uint32> _builtRevision{}; ///< 마지막으로 메싱을 요청한 청크 판 번호. 아직 요청하지 않았으면 비어 있다
        };

        /** @brief 틱 뒤 게임 스레드: 그릴 청크를 맞추고 낡은 청크에 다시 메싱을 요청합니다. */
        void updateViews( GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 그릴 범위를 벗어났거나 내려간 청크의 뷰를 지웁니다. */
        void removeHiddenViews( GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 청크 하나의 뷰를 세웁니다. 프리팹이 틀리면 한 번 오류를 남기고 더 세우지 않습니다. */
        void spawnView( GameObjectManager& manager, const VoxelGameModeComponent& director, int32 chunkX, int32 chunkZ );
        /** @brief 청크 뷰 하나의 블록 · 물 머티리얼이 준비됐는지 보고, 준비됐으면 아틀라스 칸을 카탈로그와 월드당 한 번 견줍니다. 다르면 오류 한 줄이 남습니다. */
        void updateAtlasGate( const GameObjectManager& manager, const VoxelGameModeComponent& director );
        /** @brief 세운 청크의 컴포넌트입니다. 오브젝트가 사라졌으면 nullptr 입니다. */
        static VoxelChunkComponent* findChunk( GameObjectManager& manager, const ChunkView& view );

    private:
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Chunk Prefab", Tooltip = "Prefab with a VoxelChunkComponent and its opaque and water meshes" )
        AssetRef<PrefabAsset> _chunkPrefab;
        PROPERTY( Category = "World", DisplayName = "Chunk Builds Per Frame", Tooltip = "Changed chunks handed out for meshing each frame, nearest to the body first", Min = 1 )
        int32 _chunkBuildsPerFrame;

        SW_TRANSIENT unordered_map<uint64, ChunkView> _mapChunkView;       ///< 세운 청크. 키는 `VoxelWorld::makeChunkKey` 이다
        SW_TRANSIENT VoxelAtlasGridGate               _atlasGate;          ///< 블록 머티리얼 아틀라스 칸 검사(월드당 한 번). 카탈로그가 정본이다
        SW_TRANSIENT uint8                            _bPrefabInvalid : 1; ///< 청크 프리팹이 틀렸다고 이미 알렸다
        [[maybe_unused]] uint8                        _reserved       : 7;
    };
} // namespace sw

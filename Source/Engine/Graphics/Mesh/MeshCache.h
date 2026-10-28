/**
 * @file MeshCache.h
 * @brief 메시 에셋(`.mesh`)을 경로로 공유하는 캐시와, 그 캐시를 에셋 캐시 레지스트리에 노출하는 함수입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetHandle.h"
#include "Engine/Asset/Cache/IAssetCache.h"
#include "Engine/FileSystem/AssetID.h"

namespace sw
{
    class Mesh;

    /**
     * @class MeshLoadBlock
     * @brief `.mesh` 로드 하나의 공유 상태입니다. 로드 단계에서 파일을 읽어 `Mesh` 를 만들고 캐시에 넣습니다(CPU 만 — 정점 업로드는 업로드 큐가 한다).
     */
    class SW_API MeshLoadBlock final : public AssetLoadBlock
    {
    public:
        using AssetType = Mesh;

        explicit MeshLoadBlock( string_view path );

        /** @brief 읽은 메시입니다. `Ready` 일 때만 값이 있습니다(`AssetHandle::get` 이 상태를 본다). */
        const shared_ptr<Mesh>& getAsset() const { return _pMesh; }

    private:
        [[nodiscard]] bool runLoad( string& outError ) override;

    private:
        friend class MeshCache; // 캐시 적중은 읽지 않고 메시를 채워 `Ready` 로 만든다
        shared_ptr<Mesh> _pMesh;
    };
} // namespace sw

namespace sw
{
    /** @brief 메시 로드 핸들입니다. */
    using MeshHandle = AssetHandle<MeshLoadBlock>;

    /**
     * @class MeshCache
     * @brief 메시 에셋을 경로로 공유하고, 에셋 캐시 레지스트리(`AssetManager`)에 등록되어 핫 리로드, 종료, 진단이 다른 캐시와 같은 경로를 탑니다.
     * @details 캐시는 프로세스에 하나이고(Engine.dll 안에 있고 엔진 수명 범위가 소유합니다) 약한 참조입니다. `MeshUtil::acquirePrimitive`, `SharedAssetCache<SpriteClipAsset>` 와 같은 구조이므로
     *          마지막 사용자가 놓으면 메시도 사라지고, 디바이스가 종료될 때 이 캐시가 보관하는 GPU 자원이 남지 않습니다. 같은 경로는 같은 `Mesh` 이므로
     *          배치 키(메시 포인터)가 갈라지지 않습니다. 씬 로드 워커에서 불러도 됩니다(내부에서 잠급니다).
     *
     *          메시를 다시 읽거나 비우면 무효화 고리(`MeshInvalidationHook`)를 불러, 메시에서 기하를 만든 위 층 캐시(물리 셰이프)도 비우게 합니다.
     */
    class SW_API MeshCache final : public IAssetCache
    {
    public:
        /** @brief 물리 셰이프 캐시에 `.mesh` 읽기 함수를 등록합니다(매니저가 여럿이어도 같은 함수이고, 둘 다 Engine 이미지 안에 있으므로 빼지 않습니다). */
        MeshCache();

        /**
         * @brief 경로의 메시 로드를 요청합니다. 이미 보관 중이면 `Ready` 핸들, 같은 경로를 읽는 중이면 그 로드의 핸들, 아니면 워커에서 새로 읽는 핸들입니다.
         * @details 읽지 못한 경로는 경로마다 한 번만 경고합니다(같은 메시를 쓰는 컴포넌트 수백 개가 같은 줄을 찍지 않게). 실패는 기억하지
         *          않으므로 파일이 생기면 다음 요청이 읽습니다. 캐시는 약한 참조이므로 준비된 메시는 핸들(또는 그 메시를 받은 쪽)이 쥐는 동안만 남습니다.
         */
        static MeshHandle request( string_view path );

        /**
         * @brief 경로의 메시를 **공유받습니다**. `request` 와 같은 로드 블록을 기다립니다 — 아직 아무도 읽지 않았으면 이 스레드가 읽습니다(`AssetLoadStart::OnCallingThread`).
         * @return 읽을 수 없으면 nullptr 입니다.
         */
        static shared_ptr<Mesh> acquire( string_view path );

        /**
         * @brief 사용 중인 메시를 파일에서 **제자리에서** 다시 읽습니다. 그 메시를 가진 컴포넌트 모두가 새 정점을 봅니다.
         * @param pDevice 살아 있는 디바이스면 정점을 바꾸기 전에 `waitIdle` 합니다 — 지난 프레임이 아직 옛 정점 버퍼로 그리고 있을 수 있습니다.
         * @details 틱 밖(게임 스레드)에서만 부릅니다. `Mesh::setVertices` 가 내용 번호를 바꾸므로 정점 풀과 업로드 큐가 다시 업로드합니다.
         * @return 사용 중이었고 다시 읽었으면 true. 아무도 쓰지 않거나 읽을 수 없으면(옛 정점 그대로) false 입니다.
         */
        [[nodiscard]] static bool reloadShared( string_view path, IRHIDevice* pDevice );

        /** @brief 레지스트리의 종류 이름입니다. `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Mesh";
        /** @brief 레지스트리의 종류 이름("Mesh")입니다. 에디터 에셋 종류 목록이 이 이름으로 핫 리로드를 보냅니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /** @brief 그 경로의 메시가 사용 중이면 true 입니다. */
        bool isCached( string_view relativePath ) const override;
        /** @brief 사용 중이면 제자리에서 다시 읽습니다(`reloadShared`). 물리 셰이프 캐시는 사용 중인 메시가 없어도 비웁니다(다음 셰이프가 새 정점을 읽습니다). */
        void reload( const AssetID& assetID, IRHIDevice* pDevice ) override;
        /** @brief 지금 사용 중인 메시 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시와 경고 기록, 물리 셰이프 캐시를 비웁니다. 사용 중인 메시는 그대로이고, 다음 요청은 파일을 새로 읽습니다. */
        void clear() override;
        /** @brief `.mesh` 경로면 `request` 의 로드 블록(곧 메시)을 붙드는 토큰입니다. 아니면 nullptr 입니다. */
        shared_ptr<const void> pinAsset( string_view relativePath ) override;

    private:
        /** @brief `request` 와 `acquire` 의 본문입니다. 새 로드를 @p start 대로 시작합니다(동기 API 는 이 자리에서 읽는다 — `AssetLoadStart`). */
        static MeshHandle requestWith( string_view path, AssetLoadStart start );
    };
} // namespace sw

namespace sw
{
    /** @brief 메시가 다시 읽히거나 비워졌을 때 불리는 함수입니다. */
    using MeshInvalidatedFunction = void ( * )();

    /**
     * @struct MeshInvalidationHook
     * @brief 메시에서 기하를 만들어 따로 들고 있는 위 층 캐시(물리 셰이프)가 정적 초기화에서 거는 무효화 고리입니다(`SW_MESH_INVALIDATION_HOOK`).
     * @details `MeshCache` 는 다시 읽기(`reload`)와 비우기(`clear`) 뒤에 모두 부른다. 위 층이 아래 층에 거는 방향이라 그래픽스가 물리를 include 하지 않는다
     *          (`BuiltInAssetCacheRegistry` 와 같은 모양). 고리는 내리지 않으므로 Engine 이미지 안에서만 건다.
     */
    struct SW_API MeshInvalidationHook
    {
        explicit MeshInvalidationHook( MeshInvalidatedFunction pfnInvalidated );
        MeshInvalidationHook( const MeshInvalidationHook& )            = delete;
        MeshInvalidationHook& operator=( const MeshInvalidationHook& ) = delete;

        /** @brief 건 고리를 모두 부릅니다(`MeshCache` 가 부른다). */
        static void notifyAll();
    };
} // namespace sw

/** @brief 메시 무효화 고리 @p pfn 을 겁니다(그 캐시의 .cpp, 파일 범위, `namespace sw` 안). @p id 는 정적 이름을 만드는 식별자입니다. */
#define SW_MESH_INVALIDATION_HOOK( id, pfn )                             \
    static const ::sw::MeshInvalidationHook sw_meshInvalidationHook_##id \
    {                                                                    \
        pfn                                                              \
    }

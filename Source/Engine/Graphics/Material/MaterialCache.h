/**
 * @file MaterialCache.h
 * @brief 경로를 키로 Material 소유권을 관리합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Graphics/RenderState/RenderState.h"

namespace sw

{
    class IRHIDevice;
    class Material;

    /**
     * @struct MaterialPSOWarmEntry
     * @brief 고정한 머티리얼 하나의 PSO 를 미리 만들 값입니다(셰이더 경로 · define · 해시 · 투명 여부). 게임 스레드가 머티리얼에서 읽어 만든다.
     * @details 렌더 스레드(`FramePSOManager::warmPinnedMaterialPSOs`)는 Material 을 보지 않는다 — 정의 캐시는 게임 스레드가 고치는 값이라 렌더 스레드가 읽으면 경합이다.
     */
    struct MaterialPSOWarmEntry
    {
        string              _shaderPath;
        vector<string>      _listDefine;
        uint64              _shaderPathHash{ 0 };   ///< `Material::getShaderPathHash`
        uint64              _permutationHash{ 0 };  ///< `Material::getPermutationHash`(define · 렌더 칸 해시)
        RenderStateOverride _renderStateOverride{}; ///< `Material::getRenderStateOverride`
        bool                _bTransparent{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @class MaterialCache
     * @brief 리소스 상대 경로를 키로 Material 인스턴스를 소유합니다. Scene 은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아닙니다. `Material` 이 멤버 `RHIRenderResourceHook` 로 통보를 받아서 디바이스가 죽고
     *       서는 것을 **스스로** 통보받습니다. 여기에 일괄 해제나 일괄 재생성 함수를 다시 만들지 마십시오.
     *
     * @note **`TextureCache` 와 구조가 같고, 다른 점 둘은 의도한 것입니다.** 둘 다 경로를 키로 참조를
     *       세고 0 에서 지웁니다. 갈라지는 지점을 여기 적어 둡니다. 적어 두지 않으면 다음 사람이 "한쪽만
     *       고쳐졌나" 로 읽고 맞춰 버립니다.
     *       1. 소유가 `shared_ptr` 입니다. 렌더 패킷이 소유를 빌려 갑니다(`Material.h` 머리 주석).
     *          그래서 `release` 는 GPU 자원을 해제하지 않습니다. 마지막 `shared_ptr` 이 놓일 때 해제됩니다.
     *          `TextureCache` 는 `unique_ptr` 이라 참조가 0 이면 그 자리에서 해제합니다.
     *       2. `acquire` 가 먼저 세고 실패하면 되돌립니다. `TextureCache` 는 성공한 뒤에 셉니다.
     *       게임 스레드 API(`acquire` · `reload`)는 디바이스를 받지 않는다 — 리로드는 엔진이 붙인 디바이스(`bindDevice`)를 쓰고, 백엔드를 바꾸면
     *       `SceneRHISubsystem` 이 떼고 다시 붙인다(언리얼 `UMaterial` 게임 스레드 API 에 RHI 인자가 없다). 초기화는 `initializePending` 한 길이다.
     */
    class SW_API MaterialCache final : public AssetCacheBase
    {
    public:
        /** @brief 레지스트리의 종류 이름입니다. `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Material";
        /** @brief 한 프레임에 초기화하는 머티리얼 수의 상한입니다(`initializePending`). 게임 스레드의 파일 읽기라 작게 둡니다. */
        static constexpr uint32 kMaxInitsPerFrame = 4;
        /** @brief 빈 캐시로 만듭니다. */
        MaterialCache();
        /** @brief 캐시를 해제합니다. */
        ~MaterialCache() override;

        /** @brief 복사를 금지합니다. */
        MaterialCache( const MaterialCache& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialCache& operator=( const MaterialCache& ) = delete;
        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /**
         * @brief 경로의 Material 참조를 하나 확보합니다(없으면 빈 머티리얼 항목을 만든다). 빈 경로면 nullptr 입니다.
         * @details 초기화하지 않는다 — 그릴 머티리얼은 `requestInitialize` 로 줄에 넣고 `initializePending` 이 초기화한다(한 길). 머티리얼 편집기의 미리보기는
         *          줄에 넣지 않고 편집 중인 내용을 넣는다(초기화하면 파일 내용으로 덮인다).
         */
        Material* acquire( string_view relativePath );
        /** @brief 경로의 Material 을 다시 로드합니다(붙은 디바이스가 없으면 하지 않는다). */
        void reload( const AssetID& assetID );
        /** @brief 다시 로드에 쓸 디바이스를 붙이거나(엔진이 RHI 를 세울 때) 뗍니다(nullptr, 내릴 때). */
        void bindDevice( IRHIDevice* pDevice );
        /** @brief 경로의 Material 참조를 하나 놓습니다. */
        void release( string_view relativePath );
        /**
         * @brief 디바이스 없이 확보한 경로의 머티리얼을 다음 `initializePending` 이 GPU 자원으로 초기화하도록 표시합니다. 이미 초기화되어 있으면 아무 일도 없습니다.
         * @details 컴포넌트는 디바이스를 모릅니다(레이어 규칙: Object 는 Scene 을 include 하지 않습니다). 메시가 경로로 머티리얼을 확보할 때 부르고,
         *          게임 스레드가 렌더 패킷을 내기 전(`Engine`)과 씬 초기화가 디바이스로 초기화합니다(언리얼의 `BeginInitResource` 에 해당합니다).
         *          `acquire` 만으로는 표시하지 않습니다. 머티리얼 편집기의 미리보기가 그렇게 확보해 편집 중인 내용을 넣는데,
         *          초기화하면 파일 내용으로 덮이기 때문입니다.
         */
        void requestInitialize( string_view relativePath );
        /**
         * @brief 표시된 머티리얼 중 아직 초기화하지 않은 것을 @p pDevice 로 최대 @p maxCount 개 초기화합니다(파일 읽기 · 텍스처 요청 · 레이아웃). 디바이스가 없으면 표시를 그대로 둡니다.
         * @details 게임 스레드가 부른다(`Engine::submitRenderPacket` — 머티리얼은 게임 스레드 객체다). GPU 자원은 만들지 않으므로 렌더 스레드를 기다리지 않는다 — 상수 버퍼는
         *          렌더 스레드의 프록시가 첫 프레임에 만든다(`MaterialRenderProxy`). `kMaxInitsPerFrame` 로 상한을 두고(`InitState` 가 `Ready` 가 된 머티리얼부터 패킷 빌더가
         *          그립니다), 상한을 넘은 키는 줄에 남아 다음 프레임에 이어집니다. 초기화를 마친 고정 머티리얼은 PSO 미리 만들기 값(`MaterialPSOWarmEntry`)으로 옮긴다.
         *          스튜디오 · 베이커처럼 프레임 없이 한 번에 올리는 호출처는 기본값(전부)을 씁니다.
         */
        void initializePending( IRHIDevice* pDevice, uint32 maxCount = 0xFFFFFFFFu );
        /** @brief 지금 보관 중인 항목 수입니다. */
        size_t getCachedCount() const override;
        /**
         * @brief 그 경로를 지금 캐시가 보관하고 있는지 반환합니다.
         * @details 참조가 0 이 되면 항목이 지워지므로, 이것이 곧 "아직 참조가 남아 있는가" 입니다.
         *          참조 계수 규율을 **밖에서 확인할 수 있는 유일한 방법**이라 테스트가 이것을 봅니다.
         */
        bool isCached( string_view relativePath ) const override;
        /** @brief 캐시를 비웁니다. GPU 자원은 돌려주지 않습니다(디바이스가 죽은 뒤에만 부릅니다). */
        void clear() override;
        /**
         * @brief 머티리얼 경로(`Material::kAssetType`)면 참조 하나를 확보하고(`acquire`) 다음 `initializePending` 에 초기화를 맡긴 뒤,
         *        놓을 때 `release` 하는 토큰을 돌려줍니다. 아니면 nullptr 입니다.
         * @details 초기화가 머티리얼의 텍스처를 확보하므로(비동기 로드) 머티리얼 고정이 그 텍스처도 붙든다.
         */
        shared_ptr<const void> pinAsset( string_view relativePath ) override;
        /**
         * @brief 고정한 머티리얼 가운데 초기화를 마친 것의 PSO 미리 만들기 값을 꺼냅니다(렌더 스레드, 기록 전 — `FramePSOManager::warmPinnedMaterialPSOs`).
         * @details 고정(`pinAsset`)이 줄에 넣고, 게임 스레드의 `initializePending` 이 초기화를 마친 것을 값으로 옮긴다(초기화 전이면 셰이더 경로 · define 을 모른다).
         *          씬에 이미 든 머티리얼은 렌더러가 배치마다 PSO 를 만들므로, 이 줄은 아직 씬에 없는(다음 레벨 · 컷에 나올) 머티리얼의 PSO 생성 히치를 미리 치른다.
         */
        void takePSOWarmEntries( vector<MaterialPSOWarmEntry>& outListEntry );

        /**
         * @brief 엔진에 묶인 머티리얼 캐시입니다. 엔진 서비스가 묶이지 않았으면(엔진 없는 도구 · 테스트) nullptr 입니다.
         * @details 세대를 읽는 쪽(`MaterialRenderInputs::capture` · 장면 빌더)도 같은 캐시를 보므로, 없을 때 올리지 않은 변경은 읽는 쪽도 "모름" 으로 다룬다.
         */
        [[nodiscard]] static MaterialCache* findBound();
        /**
         * @brief 렌더 값 세대입니다(`MaterialRenderVersion::bump` 가 올린다). 같으면 어느 머티리얼 · 인스턴스 · 묶음의 렌더 값도 바뀌지 않았다.
         * @details 머티리얼 틱(물 · 식생)이 워커에서 값을 넣을 수 있어 원자값이다. 세대는 1 부터라 0 은 "모름" 이다. 프로세스 정적 칸이 아니라 이 캐시(엔진 범위)의 칸이다 —
         *          엔진을 다시 세우면 새로 시작한다(언리얼은 머티리얼 인스턴스가 바뀌면 렌더 쪽 유니폼 표현을 무효화해 그 프레임에만 다시 모은다 — 기억에 의존).
         */
        uint64 getRenderValueGeneration() const { return _renderValueGeneration.load( std::memory_order_relaxed ); }
        void   bumpRenderValueGeneration() { _renderValueGeneration.fetch_add( 1u, std::memory_order_relaxed ); }
        /** @brief 퍼뮤테이션 세대입니다(`MaterialPermutation::bumpPermutationGeneration` 이 올린다). 1 부터라 0 은 "모름" 이다. */
        uint64 getPermutationGeneration() const { return _permutationGeneration.load( std::memory_order_relaxed ); }
        void   bumpPermutationGeneration() { _permutationGeneration.fetch_add( 1u, std::memory_order_relaxed ); }

    private:
        struct Impl;
        /** @brief 초기화를 마친 고정 머티리얼의 PSO 미리 만들기 값을 만들어 렌더 스레드가 꺼낼 줄로 옮깁니다(게임 스레드, `initializePending`). */
        void promoteReadyPSOWarmKeys();

    private:
        unique_ptr<Impl> _impl;
        atomic<uint64>   _renderValueGeneration; ///< `getRenderValueGeneration`
        atomic<uint64>   _permutationGeneration; ///< `getPermutationGeneration`
    };
} // namespace sw

namespace sw
{
    class Material;

    /** @brief `AssetRef<Material>` 은 머티리얼 캐시로 간다. 소유권은 캐시가 갖고 받은 쪽이 `release` 한다. 비동기 요청은 없다(GPU 초기화를 게임 스레드가 한다 — `MaterialCache`). */
    template <>
    struct SW_API AssetRefCache<Material>
    {
        static Material* acquire( string_view path );
    };
} // namespace sw

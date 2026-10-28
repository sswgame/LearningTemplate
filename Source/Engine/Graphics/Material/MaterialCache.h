/**
 * @file MaterialCache.h
 * @brief 경로를 키로 Material 소유권을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetID.h"
#include "Engine/Asset/Cache/IAssetCache.h"

namespace sw

{
    class IRHIDevice;
    class Material;

    /**
     * @class MaterialCache
     * @brief 리소스 상대 경로를 키로 Material 인스턴스를 소유합니다. Scene 은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아닙니다. `Material` 이 `RHIRenderResource` 라서 디바이스가 죽고
     *       서는 것을 **스스로** 통보받습니다. 여기에 일괄 해제나 일괄 재생성 함수를 다시 만들지 마십시오.
     *
     * @note **`TextureCache` 와 구조가 같고, 다른 점 둘은 의도한 것입니다.** 둘 다 경로를 키로 참조를
     *       세고 0 에서 지웁니다. 갈라지는 지점을 여기 적어 둡니다. 적어 두지 않으면 다음 사람이 "한쪽만
     *       고쳐졌나" 로 읽고 맞춰 버립니다.
     *       1. 소유가 `shared_ptr` 입니다. 렌더 패킷이 소유를 빌려 갑니다(`Material.h` 머리 주석).
     *          그래서 `release` 는 GPU 자원을 해제하지 않습니다. 마지막 `shared_ptr` 이 놓일 때 해제됩니다.
     *          `TextureCache` 는 `unique_ptr` 이라 참조가 0 이면 그 자리에서 해제합니다.
     *       2. `acquire` 가 먼저 세고 실패하면 되돌립니다. `TextureCache` 는 성공한 뒤에 셉니다.
     *       디바이스는 기억하지 않고 `IAssetCache` 의 계약대로 인자로 받습니다. 기억한 포인터는 백엔드를 바꾸면 죽은
     *       디바이스를 가리킵니다.
     */
    class SW_API MaterialCache final : public IAssetCache
    {
    public:
        /** @brief 빈 캐시로 만듭니다. */
        MaterialCache();
        /** @brief 캐시를 해제합니다. */
        ~MaterialCache() override;

        /** @brief 복사를 금지합니다. */
        MaterialCache( const MaterialCache& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialCache& operator=( const MaterialCache& ) = delete;

        /** @brief 레지스트리의 종류 이름입니다. `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Material";
        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /** @brief 경로의 Material 을 확보하고 GPU 자원까지 만듭니다. */
        Material* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 경로의 Material 을 다시 로드하고 GPU 자원을 다시 만듭니다. */
        void reload( const AssetID& assetID, IRHIDevice* pDevice ) override;
        /** @brief 경로의 Material 참조를 하나 놓습니다. */
        void release( string_view relativePath );
        /**
         * @brief 디바이스 없이 확보한 경로의 머티리얼을 다음 `initializePending` 이 GPU 자원으로 초기화하도록 표시합니다. 이미 초기화되어 있으면 아무 일도 없습니다.
         * @details 컴포넌트는 디바이스를 모릅니다(레이어 규칙: Object 는 Scene 을 include 하지 않습니다). 메시가 경로로 머티리얼을 확보할 때 부르고,
         *          게임 스레드가 렌더 패킷을 내기 전(`Engine`)과 씬 초기화가 디바이스로 초기화합니다(언리얼의 `BeginInitResource` 에 해당합니다).
         *          `acquire( path, nullptr )` 만으로는 표시하지 않습니다. 머티리얼 편집기의 미리보기가 그렇게 확보해 편집 중인 내용을 넣는데,
         *          초기화하면 파일 내용으로 덮이기 때문입니다.
         */
        void requestInitialize( string_view relativePath );
        /**
         * @brief 표시된 머티리얼 중 아직 GPU 에 없는 것을 @p pDevice 로 최대 @p maxCount 개 초기화합니다(파일을 읽고 버퍼를 만듭니다). 디바이스가 없으면 표시를 그대로 둡니다.
         * @details 프레임 경로의 호출자는 렌더 스레드의 셋업 구간(`FrameRenderer`, 기록 전)이고 `kMaxInitsPerFrame` 로 상한을 둡니다(`GpuState` 가 `Ready` 가 된
         *          머티리얼부터 패킷 빌더가 그립니다 — RHI 비동기 업로드 U10). 상한을 넘은 키는 줄에 남아 다음 프레임에 이어집니다. 다른 스레드가 초기화하는 중이면 그 프레임은 넘깁니다.
         *          스튜디오 · 베이커처럼 프레임 없이 한 번에 올리는 호출처는 기본값(전부)을 씁니다.
         */
        void initializePending( IRHIDevice* pDevice, uint32 maxCount = 0xFFFFFFFFu );
        /** @brief 렌더 스레드가 한 프레임에 초기화하는 머티리얼 수의 상한입니다(`initializePending`). 파일 읽기가 렌더 스레드에서 도는 만큼 작게 둡니다. */
        static constexpr uint32 kMaxInitsPerFrame = 4;
        /**
         * @brief 다음 `initializePending` 이 초기화할 머티리얼이 있으면 true 입니다(잠그지 않습니다).
         * @details 초기화는 bindless 테이블에 등록합니다. 렌더 스레드가 지난 프레임을 병렬로 기록하는 동안에는 테이블을 바꿀 수 없으므로
         *          (`IRHIDevice::setParallelRecording`), 부르는 쪽은 이것이 true 인 프레임에만 렌더 스레드를 기다립니다.
         */
        bool hasPendingInitialize() const;
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
         * @brief 머티리얼 경로(`Material::kAssetType`)면 참조 하나를 확보하고(`acquire( path, nullptr )`) 다음 `initializePending` 에 초기화를 맡긴 뒤,
         *        놓을 때 `release` 하는 토큰을 돌려줍니다. 아니면 nullptr 입니다.
         * @details 초기화가 머티리얼의 텍스처를 확보하므로(비동기 로드) 머티리얼 고정이 그 텍스처도 붙든다.
         */
        shared_ptr<const void> pinAsset( string_view relativePath ) override;
        /**
         * @brief 고정한 머티리얼 가운데 GPU 초기화를 마친 것을 PSO 미리 만들기 줄에서 꺼냅니다(렌더 스레드, 기록 전 — `FrameRenderer::warmPinnedMaterialPSOs`).
         * @details 고정(`pinAsset`)이 줄에 넣는다. 아직 초기화하지 않은 것(셰이더 경로 · define 을 모른다)은 줄에 남고, 놓여 캐시에서 빠진 것은 버린다.
         *          씬에 이미 든 머티리얼은 렌더러가 배치마다 PSO 를 만들므로, 이 줄은 아직 씬에 없는(다음 레벨 · 컷에 나올) 머티리얼의 PSO 생성 히치를 미리 치른다.
         */
        void takePSOWarmMaterials( vector<shared_ptr<Material>>& outListMaterial );

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

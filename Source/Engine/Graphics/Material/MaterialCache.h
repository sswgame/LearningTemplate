/**
 * @file MaterialCache.h
 * @brief 경로 키로 Material 소유권을 관리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw

{
    class IRHIDevice;
    class Material;

    /**
     * @class MaterialCache
     * @brief 리소스 상대 경로를 키로 Material 인스턴스를 소유합니다. Scene 은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아닙니다. `Material` 이 `RHIRenderResource` 라서 디바이스가 죽고
     *       서는 것을 **스스로** 통보받습니다. 여기에 일괄 해제 · 일괄 재생성 함수를 다시 만들지 마십시오.
     *
     * @note **`TextureCache` 와 같은 모양이고, 다른 점 둘은 의도한 것입니다.** 둘 다 경로를 키로 참조를
     *       세고 0 에서 지웁니다. 갈라지는 지점을 여기 적어 둡니다. 적어 두지 않으면 다음 사람이 "한쪽만
     *       고쳐졌나" 로 읽고 맞춰 버립니다(실제로 참조 감소 가드가 그렇게 한쪽에만 있었습니다).
     *       1. 소유가 `shared_ptr` 입니다. 렌더 패킷이 소유를 빌려 갑니다(`Material.h` 머리 주석).
     *          그래서 `release` 는 GPU 자원을 내리지 않습니다. 마지막 `shared_ptr` 이 놓일 때 내려갑니다.
     *          `TextureCache` 는 `unique_ptr` 이라 참조가 0 이면 그 자리에서 내립니다.
     *       2. `acquire` 가 먼저 세고 실패하면 되돌립니다. `TextureCache` 는 성공한 뒤에 셉니다.
     *       (예전에는 셋이었습니다. 이 캐시만 디바이스를 `_pDevice` 로 기억했습니다. 그 포인터는 백엔드를
     *        바꾸면 죽은 디바이스를 가리키므로, `IAssetCache` 의 계약대로 인자로 받게 바꿨습니다.)
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

        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return "Material"; }
        /** @brief 경로의 Material 을 확보하고 GPU 에 올립니다. */
        Material* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 경로의 Material 을 다시 로드하고 GPU 자원을 다시 만듭니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 경로의 Material 참조를 하나 놓습니다. */
        void release( string_view relativePath );
        /** @brief 지금 들고 있는 항목 수입니다. */
        size_t getCachedCount() const override;
        /**
         * @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다.
         * @details 참조가 0 이 되면 항목이 지워지므로, 이것이 곧 "아직 참조가 남아 있는가" 입니다.
         *          참조 계수 규율을 **밖에서 확인할 수 있는 유일한 손잡이**라 테스트가 이것을 봅니다.
         */
        bool isCached( string_view relativePath ) const override;
        /** @brief 캐시를 비웁니다. GPU 자원은 돌려주지 않습니다(디바이스가 죽은 뒤에만 부릅니다). */
        void clear() override;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

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
     * @brief 리소스 상대 경로를 키로 Material 인스턴스를 소유합니다. Scene은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아니다. `Material` 이 `RHIRenderResource` 라서 디바이스가 죽고
     *       서는 것을 **스스로** 통보받는다 — 여기에 일괄 해제·일괄 재생성 함수를 다시 만들지 말 것.
     *
     * @note **`TextureCache` 와 같은 모양이고, 다른 점 셋은 의도한 것이다.** 둘 다 경로를 키로 참조를
     *       세고 0 에서 지운다. 갈라지는 지점을 여기 적어 둔다 — 적어 두지 않으면 다음 사람이 "한쪽만
     *       고쳐졌나" 로 읽고 맞춰 버린다(실제로 참조 감소 가드가 그렇게 한쪽에만 있었다).
     *       1. 소유가 `shared_ptr` 다 — 렌더 패킷이 소유를 빌려 간다(`Material.h` 머리 주석).
     *          그래서 `release` 는 GPU 자원을 내리지 않는다. 마지막 `shared_ptr` 이 놓일 때 내려간다.
     *          `TextureCache` 는 `unique_ptr` 이라 참조가 0 이면 그 자리에서 내린다.
     *       2. `acquire` 가 먼저 세고 실패하면 되돌린다. `TextureCache` 는 성공한 뒤에 센다.
     *       (예전에는 셋이었다 — 이 캐시만 디바이스를 `_pDevice` 로 기억했다. 그 포인터는 백엔드를
     *        바꾸면 죽은 디바이스를 가리키므로, `IAssetCache` 의 계약대로 인자로 받게 바꿨다.)
     */
    class SW_API MaterialCache final : public IAssetCache
    {
    public:
        /** @brief 빈 캐시. */
        MaterialCache();
        /** @brief 캐시를 해제합니다. */
        ~MaterialCache() override;

        /** @brief 복사를 금지합니다. */
        MaterialCache( const MaterialCache& ) = delete;
        /** @brief 대입을 금지합니다. */
        MaterialCache& operator=( const MaterialCache& ) = delete;

        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return "Material"; }
        /** @brief 경로의 Material을 확보하고 GPU에 올립니다. */
        Material* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 경로의 Material을 다시 로드하고 GPU 캐시를 갱신합니다. */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 경로의 Material 참조를 해제합니다. */
        void release( string_view relativePath );
        /** @brief 지금 들고 있는 항목 수입니다. */
        size_t getCachedCount() const override;
        /**
         * @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다.
         * @details 참조가 0 이 되면 항목이 지워지므로, 이것이 곧 "아직 참조가 남아 있는가" 다.
         *          참조 계수 규율을 **밖에서 확인할 수 있는 유일한 손잡이**라 테스트가 이것을 본다.
         */
        bool isCached( string_view relativePath ) const override;
        /** @brief 캐시를 비웁니다. */
        void clear() override;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

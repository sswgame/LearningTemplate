/**
 * @file TextureCache.h
 * @brief 경로 키로 Texture2D 소유권을 관리합니다(MaterialCache 와 같은 모양).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Resource/IAssetCache.h"

namespace sw
{
    class IRHIDevice;
    class Texture2D;

    /**
     * @class TextureCache
     * @brief 리소스 상대 경로를 키로 Texture2D 를 소유합니다. 머티리얼은 포인터만 빌립니다.
     * @note 디바이스 수명은 이 캐시의 일이 아닙니다. `Texture2D` 가 `RHIRenderResource` 라서 스스로 통보받습니다.
     * @note `MaterialCache` 와 같은 모양입니다. **갈라지는 지점은 그쪽 헤더에 적어 두었습니다.** 두 벌이
     *       어긋나 있던 자리라, 다음에 한쪽을 고칠 때 다른 쪽도 같이 보십시오.
     */
    class SW_API TextureCache final : public IAssetCache
    {
    public:
        /** @brief 빈 캐시로 만듭니다. */
        TextureCache();
        /** @brief 캐시를 해제합니다. */
        ~TextureCache() override;
        TextureCache( const TextureCache& )            = delete;
        TextureCache& operator=( const TextureCache& ) = delete;

        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return "Texture"; }
        /** @brief 경로의 텍스처를 확보하고 GPU 에 올립니다. 실패하면 nullptr 입니다. */
        Texture2D* acquire( string_view relativePath, IRHIDevice* pDevice );
        /**
         * @brief 경로의 텍스처를 디스크에서 다시 읽어 GPU 에 올립니다(에디터 핫 리로드).
         * @details 캐시에 없으면 아무 일도 하지 않습니다. 아무도 안 쓰는 것을 올릴 이유가 없습니다.
         *          `Texture2D` 객체는 **그대로 두고** 내용만 갈아 끼웁니다. 머티리얼이 포인터를
         *          빌려 가 있으므로 객체를 바꾸면 빌린 쪽이 해제된 것을 가리킵니다.
         */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /** @brief 이미 든 텍스처를 **참조 수를 바꾸지 않고** 찾습니다. 없으면 nullptr 입니다. */
        const Texture2D* find( string_view relativePath ) const;
        /**
         * @brief `reload` 가 텍스처를 다시 올린 횟수입니다.
         * @details 다시 올리면 같은 `Texture2D` 에 **새 SRV 인덱스**가 붙습니다. 머티리얼은 resolve 때 받은 인덱스를 들고 있으므로, 이 값이
         *          바뀌면 다시 받습니다(`Material::refreshTextureBindings` — `GpuSceneBuilder` 가 부릅니다).
         */
        uint32 getReloadGeneration() const;
        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다. */
        void release( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다(`MaterialCache::isCached` 와 같은 뜻). */
        bool isCached( string_view relativePath ) const override;
        /** @brief 지금 들고 있는 항목 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시를 비웁니다(GPU 자원은 이미 내려가 있어야 합니다). */
        void clear() override;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

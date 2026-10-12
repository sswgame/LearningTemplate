/**
 * @file TextureCache.h
 * @brief 경로 키로 Texture2D 소유권을 관리합니다(MaterialCache 와 같은 모양).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Resource/Cache/IAssetCache.h"

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
     * @note **렌더 텍스처**: `rendertarget/` 로 시작하는 경로는 파일이 아니라 렌더 타깃입니다(`Texture2D::createRenderTarget`). 카메라가 그 경로로
     *       그리고(`CameraRenderOutput`), 머티리얼은 DDS 와 같은 길(`acquire`)로 빌려 읽습니다. 크기는 처음 만들 때 정해집니다 — 카메라가
     *       `declareRenderTarget` 으로 먼저 알리면 그 크기, 아니면 `kDefaultRenderTargetSize` 입니다.
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

        /** @brief 레지스트리의 종류 이름 — `AssetManager::getAssetCache<T>()` 가 이것으로 찾습니다. */
        static constexpr const utf8* kAssetKindName = "Texture";
        /** @brief 이 캐시가 다루는 에셋 종류의 이름입니다. */
        const utf8* getAssetKindName() const override { return kAssetKindName; }
        /// @brief 렌더 텍스처 경로의 머리입니다.
        static constexpr const utf8* kRenderTargetPrefix = "rendertarget/";
        /// @brief 크기를 알리지 않은 렌더 텍스처의 한 변(px)입니다.
        static constexpr uint32 kDefaultRenderTargetSize = 256;

        /**
         * @brief 경로의 텍스처를 확보하고 GPU 에 올립니다(렌더 텍스처 경로면 렌더 타깃을 만든다). 실패하면 nullptr 입니다.
         * @details **수명 계약**: 성공하면 참조를 하나 셉니다. 돌려준 포인터는 짝이 되는 `release` 까지 살아 있습니다(객체는 `unique_ptr` 이라
         *          맵이 원소를 옮겨도 주소가 그대로입니다). 올리기는 잠금 밖에서 하고, 같은 경로를 동시에 원한 스레드는 그 결과를 기다려 받습니다.
         */
        Texture2D* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 렌더 텍스처 경로(`rendertarget/...`)인지입니다. */
        static bool isRenderTargetPath( string_view relativePath );
        /**
         * @brief 렌더 텍스처의 크기를 알립니다(게임 스레드 — 카메라가 붙을 때). 이미 만들어진 것은 바꾸지 않습니다(다음에 새로 만들 때부터).
         * @details 머티리얼이 카메라보다 먼저 빌려도 같은 크기로 만들어지게 하는 자리입니다. 렌더러는 늘 텍스처의 실제 크기로 그립니다.
         */
        void declareRenderTarget( string_view relativePath, uint32 width, uint32 height );
        /**
         * @brief 이미 만든 렌더 텍스처를 새 크기로 다시 만듭니다(렌더 스레드 — 기록 전에). 크기가 같거나 아직 없으면 크기만 알리고 false 입니다.
         * @details 언리얼 `UTextureRenderTarget2D::ResizeTarget` 에 해당합니다. 위젯이 그리기 크기를 바꾼 캔버스 대상처럼, 먼저 만든 크기가 낡은 경우에 씁니다.
         *          `Texture2D` 객체는 **그대로 두고** GPU 자원만 바꿉니다(빌린 포인터가 그대로 유효합니다). 새 SRV 인덱스를 받으므로 `reload` 처럼 세대를 올려
         *          머티리얼이 인덱스를 다시 받게 합니다. 옛 자원을 읽는 프레임이 끝나도록 디바이스를 기다리므로 크기가 바뀔 때만 부릅니다.
         * @return 다시 만들었으면 true 입니다. 실패는 로그를 남깁니다.
         */
        bool resizeRenderTarget( string_view relativePath, uint32 width, uint32 height, IRHIDevice* pDevice );
        /**
         * @brief 경로의 텍스처를 디스크에서 다시 읽어 GPU 에 올립니다(에디터 핫 리로드).
         * @details 캐시에 없으면 아무 일도 하지 않습니다. 아무도 안 쓰는 것을 올릴 이유가 없습니다.
         *          `Texture2D` 객체는 **그대로 두고** 내용만 갈아 끼웁니다. 머티리얼이 포인터를
         *          빌려 가 있으므로 객체를 바꾸면 빌린 쪽이 해제된 것을 가리킵니다.
         */
        void reload( string_view relativePath, IRHIDevice* pDevice ) override;
        /**
         * @brief 이미 든 텍스처를 **참조 수를 바꾸지 않고** 찾습니다. 없거나 처음 올리는 중이면 nullptr 입니다.
         * @warning 빌린 포인터입니다. 누군가 `acquire` 로 참조를 쥐고 있는 동안만 유효하고, 마지막 `release` 나 `clear` 뒤에는 해제된 객체입니다.
         *          저장하지 말고 그 자리에서 쓰십시오. 오래 들고 있어야 하면 `acquire` · `release` 짝으로 빌립니다.
         */
        const Texture2D* find( string_view relativePath ) const;
        /**
         * @brief `reload` 가 텍스처를 다시 올린 횟수입니다.
         * @details 다시 올리면 같은 `Texture2D` 에 **새 SRV 인덱스**가 붙습니다. 머티리얼은 resolve 때 받은 인덱스를 들고 있으므로, 이 값이
         *          바뀌면 다시 받습니다(`Material::refreshTextureBindings` — `GPUSceneBuilder` 가 부릅니다).
         */
        uint32 getReloadGeneration() const;
        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다. */
        void release( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 그 경로를 지금 캐시가 들고 있는지 반환합니다(`MaterialCache::isCached` 와 같은 뜻). */
        bool isCached( string_view relativePath ) const override;
        /** @brief 지금 들고 있는 항목 수입니다. */
        size_t getCachedCount() const override;
        /** @brief 캐시를 비웁니다(GPU 자원은 이미 내려가 있어야 합니다). 종료 때만 부르며, 그 뒤 `acquire` · `find` 로 받은 포인터는 모두 무효입니다. */
        void clear() override;

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

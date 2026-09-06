/**
 * @file TextureCache.h
 * @brief 경로 키로 Texture2D 소유권을 관리합니다 (MaterialCache 와 같은 모양).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class IRHIDevice;
    class Texture2D;

    /**
     * @class TextureCache
     * @brief 리소스 상대 경로를 키로 Texture2D 를 소유합니다. 머티리얼은 포인터만 빌린다.
     */
    class SW_API TextureCache
    {
    public:
        /** @brief 빈 캐시. */
        TextureCache();
        /** @brief 캐시를 해제합니다. */
        ~TextureCache();
        TextureCache( const TextureCache& )            = delete;
        TextureCache& operator=( const TextureCache& ) = delete;

        /** @brief 경로의 텍스처를 확보하고 GPU 에 올립니다. 실패하면 nullptr. */
        Texture2D* acquire( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 참조를 하나 놓습니다. 0 이 되면 GPU 자원까지 해제합니다. */
        void release( string_view relativePath, IRHIDevice* pDevice );
        /** @brief 모든 텍스처의 GPU 자원을 해제합니다(디바이스 교체 전). */
        void shutdownAllGpu( IRHIDevice* pDevice );
        /** @brief 모든 텍스처를 새 디바이스에 다시 올립니다. */
        bool reinitializeAll( IRHIDevice* pDevice );
        /** @brief 캐시를 비웁니다(GPU 자원은 이미 내려가 있어야 한다). */
        void clear();

    private:
        struct Impl;
        unique_ptr<Impl> _impl;
    };
} // namespace sw

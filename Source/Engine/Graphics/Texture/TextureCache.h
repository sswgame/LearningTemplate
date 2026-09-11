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
        /**
         * @brief 경로의 텍스처를 디스크에서 다시 읽어 GPU 에 올립니다 (에디터 핫리로드).
         * @details 캐시에 없으면 아무 일도 하지 않는다 — 아무도 안 쓰는 것을 올릴 이유가 없다.
         *          `Texture2D` 객체는 **그대로 두고** 내용만 갈아 끼운다. 머티리얼이 포인터를
         *          빌려 가 있으므로 객체를 바꾸면 빌린 쪽이 해제된 것을 가리킨다.
         */
        void reload( string_view relativePath, IRHIDevice* pDevice );
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

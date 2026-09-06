/**
 * @file Texture2D.h
 * @brief DDS 파일을 GPU 텍스처로 올리고 bindless SRV 인덱스를 쥐는 텍스처 자산
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class Texture2D
     * @brief 리소스 경로의 DDS 를 읽어 createTexture2D + uploadTexture2D + registerBindlessTexture 까지 한 번에 하는 자산.
     * @details 셰이더는 SRV 인덱스만 받는다(MaterialCB 의 uint 슬롯) — 머티리얼이 Texture2D 프로퍼티의 assetPath 로
     *          이 자산을 얻어 인덱스를 패킹한다. 소유권은 TextureCache 가 갖는다.
     */
    class SW_API Texture2D
    {
    public:
        /** @brief 빈 텍스처. */
        Texture2D();
        /** @brief GPU 자원이 남아 있으면 경고만 남긴다 — 해제는 shutdown 으로 명시한다. */
        ~Texture2D();
        Texture2D( const Texture2D& )            = delete;
        Texture2D& operator=( const Texture2D& ) = delete;

        /** @brief 리소스 상대 경로(전역 id, 예: engine/textures/random/grass.dds)의 DDS 를 GPU 에 올립니다. */
        bool loadFromResource( IRHIDevice* pDevice, string_view relativePath );
        /** @brief GPU 텍스처와 SRV 를 해제합니다. */
        void shutdown( IRHIDevice* pDevice );

        bool               isReady() const { return _handle != 0 && _srv != kInvalidDescriptorIndex; }
        RHITextureHandle   getHandle() const { return _handle; }
        RHIDescriptorIndex getSrv() const { return _srv; }
        uint32             getWidth() const { return _width; }
        uint32             getHeight() const { return _height; }
        uint32             getMipCount() const { return _mipCount; }
        RHIFormat          getFormat() const { return _format; }
        const string&      getPath() const { return _path; }

        /** @brief DDS 가 알려 주는 DXGI 포맷 번호를 RHIFormat 으로. 대응이 없으면 Unknown. */
        static RHIFormat toRHIFormatFromDxgi( uint32 dxgiFormat );

    private:
        string             _path;
        RHITextureHandle   _handle;
        RHIDescriptorIndex _srv;
        uint32             _width;
        uint32             _height;
        uint32             _mipCount;
        RHIFormat          _format;
    };
} // namespace sw

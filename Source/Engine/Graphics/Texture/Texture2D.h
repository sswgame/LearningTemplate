/**
 * @file Texture2D.h
 * @brief DDS 파일을 GPU 텍스처로 올리고 bindless SRV 인덱스를 쥐는 텍스처 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;

    /**
     * @class Texture2D
     * @brief 리소스 경로의 DDS 를 읽어 createTexture2D + uploadTexture2D + registerBindlessTexture 까지 한 번에 하는 에셋입니다.
     * @details 셰이더는 SRV 인덱스만 받습니다(MaterialCB 의 uint 슬롯). 머티리얼이 Texture2D 프로퍼티의 assetPath 로
     *          이 에셋을 얻어 인덱스를 패킹합니다. 소유권은 TextureCache 가 갖습니다.
     */
    class SW_API Texture2D final : public RHIRenderResource
    {
    public:
        /** @brief 빈 텍스처로 만듭니다. */
        Texture2D();
        /** @brief GPU 자원이 남아 있으면 경고만 남깁니다. 해제는 releaseRHI 로 명시합니다. */
        ~Texture2D() override;
        Texture2D( const Texture2D& )            = delete;
        Texture2D& operator=( const Texture2D& ) = delete;

        /** @brief 리소스 상대 경로(전역 id, 예: engine/textures/random/grass.dds)의 DDS 를 GPU 에 올립니다. */
        [[nodiscard]] bool loadFromResource( IRHIDevice* pDevice, string_view relativePath );
        /**
         * @brief 그리고 읽는 렌더 타깃으로 만듭니다(렌더 텍스처 — CCTV 모니터 · 백미러). 경로는 이름표입니다(`rendertarget/<이름>`, 파일이 아니다).
         * @details 렌더러가 카메라의 그림을 여기에 그리고, 머티리얼은 DDS 텍스처와 같은 SRV 로 읽습니다. 디바이스가 바뀌면 같은 크기로 다시 만듭니다(내용은 비어 있다).
         */
        [[nodiscard]] bool createRenderTarget( IRHIDevice* pDevice, string_view name, uint32 width, uint32 height, RHIFormat format );
        /** @brief (RHIRenderResource) 살아 있는 디바이스에 텍스처를 돌려줍니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 잊습니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 새 디바이스에 같은 경로의 DDS 를 다시 올립니다(렌더 타깃이면 같은 크기로 다시 만듭니다). */
        bool initRHI( IRHIDevice* pDevice ) override;

        bool               isRHIValid() const { return _handle != 0 && _srv != kInvalidDescriptorIndex; }
        RHITextureHandle   getHandle() const { return _handle; }
        RHIDescriptorIndex getSrv() const { return _srv; }
        uint32             getWidth() const { return _width; }
        uint32             getHeight() const { return _height; }
        RHIFormat          getFormat() const { return _format; }
        const string&      getPath() const { return _path; }
        /** @brief 렌더 타깃(`createRenderTarget`)으로 만든 텍스처인지입니다. */
        bool isRenderTarget() const { return _bRenderTarget == SW_TRUE; }

        /** @brief DDS 가 알려 주는 DXGI 포맷 번호를 RHIFormat 으로 바꿉니다. 대응이 없으면 Unknown 입니다. */
        static RHIFormat toRHIFormatFromDxgi( uint32 dxgiFormat );

    private:
        /** @brief 이 텍스처를 올린 디바이스. 통보가 내 것인지 가릴 때 씁니다. */
        IRHIDevice*        _pDevice;
        string             _path;
        RHITextureHandle   _handle;
        RHIDescriptorIndex _srv;
        uint32             _width;
        uint32             _height;
        uint32             _mipCount;
        RHIFormat          _format;
        uint8              _bRenderTarget; ///< `createRenderTarget` 으로 만들었다 — 다시 만들 때 DDS 를 읽지 않는다
    };
} // namespace sw

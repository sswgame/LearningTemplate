/**
 * @file Texture2D.h
 * @brief DDS 파일을 GPU 텍스처로 업로드하고 bindless SRV 인덱스를 보관하는 텍스처 에셋입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Common/AssetTypeRegistry.h"
#include "Engine/Graphics/RHI/RHIRenderResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    struct DDSImageData;

    /** @brief 텍스처 품질이 읽을 때 버리는 위쪽 밉입니다(`Texture2D::computeMipDrop`). */
    struct Texture2DMipDrop
    {
        uint32 _mipCount{ 0 };     ///< 버리는 밉 수
        uint64 _skippedBytes{ 0 }; ///< 페이로드 앞에서 건너뛸 바이트(버린 밉들의 크기 합)
    };
} // namespace sw

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
        /** @brief 텍스처 파일의 접미사입니다(쿠킹한 DDS). */
        static constexpr AssetSuffix kArrAssetSuffix[] = {
            { ".dds", AssetSuffixRole::Plain },
        };
        /** @brief 텍스처 에셋 종류입니다(등록은 `Texture2D.cpp` 의 `SW_ASSET_TYPE`). 전용 서버 패키지가 빼는 묶음 `Texture` 다. */
        static constexpr AssetTypeDef kAssetType{ "Texture", kArrAssetSuffix, static_cast<uint32>( std::size( kArrAssetSuffix ) ), {}, 0, "Texture" };

        /** @brief 빈 텍스처로 만듭니다. */
        Texture2D();
        /** @brief GPU 자원이 남아 있으면 경고만 남깁니다. 해제는 releaseRHI 로 명시합니다. */
        ~Texture2D() override;
        Texture2D( const Texture2D& )            = delete;
        Texture2D& operator=( const Texture2D& ) = delete;

        /** @brief 리소스 상대 경로(전역 id, 예: engine/textures/random/grass.dds)의 DDS 를 GPU 에 업로드합니다(`readImage` 다음 `loadFromImage`). */
        [[nodiscard]] bool loadFromResource( IRHIDevice* pDevice, string_view relativePath );
        /**
         * @brief 리소스의 DDS 를 읽고 이 엔진이 올릴 수 있는 형식인지 확인합니다. 디바이스를 쓰지 않으므로 어느 스레드에서든 부릅니다(비동기 로드의 워커 단계).
         * @return 읽지 못했거나 포맷 · 차원이 맞지 않으면 오류를 남기고 false 를 반환합니다.
         */
        [[nodiscard]] static bool readImage( string_view relativePath, DDSImageData& outImage );
        /** @brief `readImage` 가 읽은 그림을 GPU 에 올립니다. 디바이스를 쓰는 스레드에서만 부릅니다(DX11 즉시 컨텍스트 · GL 컨텍스트는 스레드에 묶인다). */
        [[nodiscard]] bool loadFromImage( IRHIDevice* pDevice, string_view relativePath, const DDSImageData& image );
        /**
         * @brief 그리고 읽는 렌더 타깃으로 만듭니다(렌더 텍스처: CCTV 모니터, 백미러). 경로는 이름입니다(`rendertarget/<이름>`, 파일이 아닙니다).
         * @details 렌더러가 카메라의 이미지를 여기에 그리고, 머티리얼은 DDS 텍스처와 같은 SRV 로 읽습니다. 디바이스가 바뀌면 같은 크기로 다시 만듭니다(내용은 비어 있습니다).
         */
        [[nodiscard]] bool createRenderTarget( IRHIDevice* pDevice, string_view name, uint32 width, uint32 height, RHIFormat format );
        /**
         * @brief CPU 픽셀(RGBA8, 밉 하나)을 원본으로 둡니다(CPU 텍스처: 지형마다의 스플랫). GPU 자원은 만들지 않습니다(`loadFromCPUBytes`).
         * @details 원본은 이 객체가 보관해 디바이스가 바뀌어도 칠한 내용이 남습니다. 공개 전(게임 스레드, `TextureCache::createCPUTexture`)에만 부릅니다.
         */
        void setCPUBytes( string_view name, uint32 width, uint32 height, vector<uint8> rgbaBytes );
        /** @brief CPU 픽셀 원본을 GPU 에 업로드합니다(렌더 스레드). 이미 업로드되어 있으면 true 입니다. */
        [[nodiscard]] bool loadFromCPUBytes( IRHIDevice* pDevice );
        /**
         * @brief CPU 픽셀 원본의 사각형을 @p desc(RGBA8 행)로 바꾸고, GPU 에 올라가 있으면 그 구간도 업로드합니다(렌더 스레드).
         * @return 원본이 없거나 범위나 크기가 틀리거나 업로드가 실패하면 false 입니다.
         */
        [[nodiscard]] bool writeCPUBytes( IRHIDevice* pDevice, const RHITextureRegionUploadDesc& desc );
        /** @brief CPU 픽셀 원본을 가진 텍스처이면 true 입니다. */
        bool                 hasCPUBytes() const { return _cpuBytes.empty() == false; }
        const vector<uint8>& getCPUBytes() const { return _cpuBytes; }
        /** @brief (RHIRenderResource) 살아 있는 디바이스에 텍스처를 돌려줍니다. */
        void releaseRHI( IRHIDevice* pDevice ) override;
        /** @brief (RHIRenderResource) 디바이스가 이미 없을 때 부릅니다. 핸들만 잊습니다. */
        void forgetRHI( IRHIDevice* pDevice ) override;
        /**
         * @brief (RHIRenderResource) 새 디바이스에 같은 경로의 DDS 를 다시 업로드합니다(렌더 타깃이면 같은 크기로 다시 만듭니다).
         * @details CPU 텍스처는 여기서 업로드하지 않습니다. 빌린 머티리얼이 디바이스 교체 때 놓았다가 다시 빌리면 `TextureCache::acquire` 가 업로드합니다.
         *          참조가 0 인 CPU 텍스처는 GPU 자원이 없다는 캐시의 불변식을 지키기 위해서입니다.
         */
        bool initRHI( IRHIDevice* pDevice ) override;

        bool               isRHIValid() const { return _handle != 0 && _srv != kInvalidDescriptorIndex; }
        RHITextureHandle   getHandle() const { return _handle; }
        RHIDescriptorIndex getSrv() const { return _srv; }
        uint32             getWidth() const { return _width; }
        uint32             getHeight() const { return _height; }
        RHIFormat          getFormat() const { return _format; }
        const string&      getPath() const { return _path; }
        /** @brief 렌더 타깃(`createRenderTarget`)으로 만든 텍스처이면 true 입니다. */
        bool isRenderTarget() const { return _bRenderTarget == SW_TRUE; }

        /** @brief DDS 가 알려 주는 DXGI 포맷 번호를 RHIFormat 으로 바꿉니다. 대응이 없으면 Unknown 입니다. */
        static RHIFormat toRHIFormatFromDxgi( uint32 dxgiFormat );
        /**
         * @brief 텍스처 품질(`gv_textureMipDrop`)로 버릴 위쪽 밉을 고릅니다. 밉 사슬이 있는 텍스처만 줄이고, 남은 밉 0 은 `kMinMipDropExtent` 이상이고
         *        블록 크기의 배수여야 합니다(BC 는 4 의 배수 — 아니면 백엔드가 만들지 못한다). 마지막 밉 하나는 늘 남깁니다.
         * @details 밉이 하나뿐인 텍스처(UI 아이콘 · 글꼴 아틀라스 · 룩업 표)는 줄이지 않는다. 화면에 1:1 로 그리는 것이라 밉을 만들지 않기 때문이다.
         */
        static Texture2DMipDrop computeMipDrop( RHIFormat format, uint32 width, uint32 height, uint32 mipCount, int32 requestedDrop );

        /** @brief 밉을 버린 뒤 밉 0 의 짧은 변이 이보다 작아지지 않습니다(작은 텍스처는 버려도 얻는 메모리가 없다). */
        static constexpr uint32 kMinMipDropExtent = 64;

    private:
        /** @brief 이 텍스처를 업로드한 디바이스입니다. 통보가 내 것인지 가릴 때 씁니다. */
        IRHIDevice*        _pDevice;
        string             _path;
        RHITextureHandle   _handle;
        RHIDescriptorIndex _srv;
        uint32             _width;
        uint32             _height;
        uint32             _mipCount;
        RHIFormat          _format;
        uint8              _bRenderTarget; ///< `createRenderTarget` 으로 만들었다. 다시 만들 때 DDS 를 읽지 않는다
        vector<uint8>      _cpuBytes;      ///< CPU 텍스처의 원본(RGBA8 밉 0). 비었으면 DDS 또는 렌더 타깃이다
    };
} // namespace sw

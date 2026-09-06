#include "pch.h"

#include "Engine/Graphics/Texture/Texture2D.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Resource/DdsLoader.h"

namespace sw
{
    SW_LOG_CALLER( "Texture2D" );

    Texture2D::Texture2D()
        : _path{}
        , _handle{ 0 }
        , _srv{ kInvalidDescriptorIndex }
        , _width{ 0 }
        , _height{ 0 }
        , _mipCount{ 0 }
        , _format{ RHIFormat::Unknown }
    {
    }

    Texture2D::~Texture2D()
    {
        if ( _handle != 0 )
            SW_LOG_WARNING( "Texture2D '%#' destroyed with a live GPU texture — call shutdown first.", _path.c_str() );
    }

    RHIFormat Texture2D::toRHIFormatFromDxgi( uint32 dxgiFormat )
    {
        // DdsLoader 가 쓰는 DXGI 번호(DirectX 헤더 없이 상수로 둔다). 여기 없는 번호는 Unknown.
        switch ( dxgiFormat )
        {
            case 28: // DXGI_FORMAT_R8G8B8A8_UNORM
            case 29: // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB — sRGB 디코드는 아직 없다: 선형으로 샘플링한다
                return RHIFormat::R8G8B8A8_UNORM;
            case 87: // DXGI_FORMAT_B8G8R8A8_UNORM
            case 88: // DXGI_FORMAT_B8G8R8X8_UNORM — X 채널을 알파로 읽는다(불투명 텍스처라면 255)
            case 91: // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                return RHIFormat::B8G8R8A8_UNORM;
            case 10: // DXGI_FORMAT_R16G16B16A16_FLOAT
                return RHIFormat::R16G16B16A16_FLOAT;
            case 6: // DXGI_FORMAT_R32G32B32_FLOAT
                return RHIFormat::R32G32B32_FLOAT;
            case 16: // DXGI_FORMAT_R32G32_FLOAT
                return RHIFormat::R32G32_FLOAT;
            case 41: // DXGI_FORMAT_R32_FLOAT
                return RHIFormat::R32_FLOAT;
            case 71: // DXGI_FORMAT_BC1_UNORM
            case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
                return RHIFormat::BC1_UNORM;
            case 74: // DXGI_FORMAT_BC2_UNORM
            case 75:
                return RHIFormat::BC2_UNORM;
            case 77: // DXGI_FORMAT_BC3_UNORM
            case 78:
                return RHIFormat::BC3_UNORM;
            case 80: // DXGI_FORMAT_BC4_UNORM
                return RHIFormat::BC4_UNORM;
            case 83: // DXGI_FORMAT_BC5_UNORM
                return RHIFormat::BC5_UNORM;
            case 98: // DXGI_FORMAT_BC7_UNORM
            case 99:
                return RHIFormat::BC7_UNORM;
            default:
                return RHIFormat::Unknown;
        }
    }

    bool Texture2D::loadFromResource( IRHIDevice* pDevice, string_view relativePath )
    {
        if ( pDevice == nullptr || relativePath.empty() )
            return false;
        if ( _handle != 0 )
            shutdown( pDevice );

        DdsImageData image;
        if ( DdsLoader::loadFromResource( relativePath, image ) == false || image.isValid() == false )
        {
            SW_LOG_ERROR( "Texture2D: failed to load '%#'", relativePath.data() );
            return false;
        }

        const RHIFormat format = toRHIFormatFromDxgi( image._dxgiFormat );
        if ( format == RHIFormat::Unknown )
        {
            SW_LOG_ERROR( "Texture2D: '%#' uses DXGI format %# which RHIFormat does not cover yet", relativePath.data(), image._dxgiFormat );
            return false;
        }
        if ( image._depth > 1 )
        {
            SW_LOG_ERROR( "Texture2D: '%#' is a volume/array texture (depth=%#) — only 2D is supported", relativePath.data(), image._depth );
            return false;
        }

        RHITextureDesc desc{};
        desc._width             = image._width;
        desc._height            = image._height;
        desc._mipLevels         = image._mipCount > 0 ? image._mipCount : 1;
        desc._format            = format;
        desc._bIsShaderResource = 1;
        IRHIResource* pResource = pDevice->getResource();
        _handle                 = pResource->createTexture2D( desc );
        if ( _handle == 0 )
        {
            SW_LOG_ERROR( "Texture2D: createTexture2D failed for '%#' (%#x%#, %# mips)", relativePath.data(), desc._width, desc._height, desc._mipLevels );
            return false;
        }

        // DDS 페이로드는 밉 0 부터 행 빈틈없이 이어진 배치 — uploadTexture2D 의 규약과 같다.
        RHITextureUploadDesc upload{};
        upload._pData     = image.getPixels();
        upload._sizeBytes = static_cast<uint32>( image._bytes.size() );
        upload._mipLevels = desc._mipLevels;
        if ( pResource->uploadTexture2D( _handle, upload ) == false )
        {
            SW_LOG_ERROR( "Texture2D: uploadTexture2D failed for '%#'", relativePath.data() );
            pResource->destroyTexture( _handle );
            _handle = 0;
            return false;
        }

        _srv = pResource->registerBindlessTexture( _handle );
        if ( _srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR( "Texture2D: registerBindlessTexture failed for '%#'", relativePath.data() );
            pResource->destroyTexture( _handle );
            _handle = 0;
            return false;
        }

        _path     = string{ relativePath };
        _width    = desc._width;
        _height   = desc._height;
        _mipCount = desc._mipLevels;
        _format   = format;
        SW_LOG_INFO( "Texture2D '%#' ready: %#x%#, %# mips, format %#, srv %#", _path.c_str(), _width, _height, _mipCount,
                     static_cast<uint32>( _format ), _srv );
        return true;
    }

    void Texture2D::shutdown( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
        {
            IRHIResource* pResource = pDevice->getResource();
            if ( _srv != kInvalidDescriptorIndex )
                pResource->unregisterBindlessTexture( _srv );
            if ( _handle != 0 )
                pResource->destroyTexture( _handle );
        }
        _handle   = 0;
        _srv      = kInvalidDescriptorIndex;
        _width    = 0;
        _height   = 0;
        _mipCount = 0;
        _format   = RHIFormat::Unknown;
    }
} // namespace sw

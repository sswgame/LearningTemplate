#include "pch.h"

#include "Engine/Graphics/Texture/Texture2D.h"

#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Resource/AssetLoadProfiler.h"
#include "Engine/Resource/Image/DdsLoader.h"

namespace sw
{
    SW_LOG_CALLER( "Texture2D" );

    Texture2D::Texture2D()
        : _pDevice{ nullptr }
        , _path{}
        , _handle{ 0 }
        , _srv{ kInvalidDescriptorIndex }
        , _width{ 0 }
        , _height{ 0 }
        , _mipCount{ 0 }
        , _format{ RHIFormat::Unknown }
        , _bRenderTarget{ SW_FALSE }
    {
    }

    Texture2D::~Texture2D()
    {
        if ( _handle != 0 )
            SW_LOG_WARNING( "Texture2D '%#' destroyed with a live GPU texture — call releaseRhi first.", _path.c_str() );
    }

    RHIFormat Texture2D::toRhiFormatFromDxgi( uint32 dxgiFormat )
    {
        // DdsLoader 가 쓰는 DXGI 번호(DirectX 헤더 없이 상수로 둔다). 여기 없는 번호는 Unknown.
        switch ( dxgiFormat )
        {
            case 28: // DXGI_FORMAT_R8G8B8A8_UNORM
            case 29: // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB. sRGB 디코드는 아직 없다: 선형으로 샘플링한다
                return RHIFormat::R8G8B8A8_UNORM;
            case 87: // DXGI_FORMAT_B8G8R8A8_UNORM
            case 88: // DXGI_FORMAT_B8G8R8X8_UNORM. X 채널을 알파로 읽는다(불투명 텍스처라면 255)
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
            case 61: // DXGI_FORMAT_R8_UNORM. 1 채널 마스크(DX10 머리 DDS) — 임포터는 아직 내지 않는다
                return RHIFormat::R8_UNORM;
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
            case 95: // DXGI_FORMAT_BC6H_UF16
                return RHIFormat::BC6H_UF16;
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
            releaseRhi( pDevice );

        AssetLoadScope loadScope( "Texture", relativePath );
        DdsImageData   image;
        if ( DdsLoader::loadFromResource( relativePath, image ) == false || image.isValid() == false )
        {
            SW_LOG_ERROR( "Texture2D: failed to load '%#'", relativePath );
            return false;
        }

        const RHIFormat format = toRhiFormatFromDxgi( image._dxgiFormat );
        if ( format == RHIFormat::Unknown )
        {
            SW_LOG_ERROR( "Texture2D: '%#' uses DXGI format %# which RHIFormat does not cover yet", relativePath, image._dxgiFormat );
            return false;
        }
        if ( image._depth > 1 )
        {
            SW_LOG_ERROR( "Texture2D: '%#' is a volume/array texture (depth=%#) — only 2D is supported", relativePath, image._depth );
            return false;
        }

        loadScope.setBytes( image._bytes.size() );
        loadScope.beginPhase( AssetLoadPhase::Upload );
        RHITextureDesc desc{};
        desc._width                    = image._width;
        desc._height                   = image._height;
        desc._mipLevels                = image._mipCount > 0 ? image._mipCount : 1;
        desc._format                   = format;
        desc._bIsShaderResource        = SW_TRUE;
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        _handle                        = pResource->createTexture2D( desc );
        if ( _handle == 0 )
        {
            SW_LOG_ERROR( "Texture2D: createTexture2D failed for '%#' (%#×%#, %# mips)", relativePath, desc._width, desc._height, desc._mipLevels );
            return false;
        }

        // DDS 페이로드는 밉 0 부터 행 빈틈없이 이어진 배치다. uploadTexture2D 의 규약과 같다.
        RHITextureUploadDesc upload{};
        upload._pData     = image.getPixels();
        upload._sizeBytes = static_cast<uint32>( image._bytes.size() );
        upload._mipLevels = desc._mipLevels;
        if ( pResource->uploadTexture2D( _handle, upload ) == false )
        {
            SW_LOG_ERROR( "Texture2D: uploadTexture2D failed for '%#'", relativePath );
            pResource->destroyTexture( _handle );
            _handle = 0;
            return false;
        }

        _srv = pResource->registerBindlessTexture( _handle );
        if ( _srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR( "Texture2D: registerBindlessTexture failed for '%#'", relativePath );
            pResource->destroyTexture( _handle );
            _handle = 0;
            return false;
        }

        // `_pDevice` 는 **성공한 뒤에만** 적는다. 먼저 적으면 실패하고 돌아간 뒤에도 "이 디바이스에 올라가 있다" 는
        // 표시가 남는다. `isRhiValid` 는 핸들을 보지만 `releaseRhi` 는 이 값으로 **남의 디바이스 통보인지**를 가른다.
        // 가진 것이 없는데 주인만 적혀 있는 상태를 애초에 만들지 않는다.
        _pDevice  = pDevice;
        _path     = string{ relativePath };
        _width    = desc._width;
        _height   = desc._height;
        _mipCount = desc._mipLevels;
        _format   = format;
        SW_LOG_INFO( "Texture2D '%#' ready: %#×%#, %# mips, format %#, srv %#", _path.c_str(), _width, _height, _mipCount,
                     static_cast<uint32>( _format ), _srv );
        loadScope.setSucceeded();
        return true;
    }

    bool Texture2D::createRenderTarget( IRHIDevice* pDevice, string_view name, uint32 width, uint32 height, RHIFormat format )
    {
        if ( pDevice == nullptr || name.empty() || width == 0 || height == 0 )
            return false;
        if ( _handle != 0 )
            releaseRhi( pDevice );

        RHITextureDesc desc{};
        desc._width                    = width;
        desc._height                   = height;
        desc._mipLevels                = 1;
        desc._format                   = format;
        desc._bIsRenderTarget          = SW_TRUE;
        desc._bIsShaderResource        = SW_TRUE;
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        _handle                        = pResource->createTexture2D( desc );
        if ( _handle == 0 )
        {
            SW_LOG_ERROR( "Texture2D: createTexture2D failed for render target '%#' (%#×%#)", name, width, height );
            return false;
        }
        _srv = pResource->registerBindlessTexture( _handle );
        if ( _srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR( "Texture2D: registerBindlessTexture failed for render target '%#'", name );
            pResource->destroyTexture( _handle );
            _handle = 0;
            return false;
        }
        _pDevice       = pDevice;
        _path          = string{ name };
        _width         = width;
        _height        = height;
        _mipCount      = 1;
        _format        = format;
        _bRenderTarget = SW_TRUE;
        SW_LOG_INFO( "Texture2D render target '%#' ready: %#×%#, srv %#", _path.c_str(), _width, _height, _srv );
        return true;
    }

    bool Texture2D::initRhi( IRHIDevice* pDevice )
    {
        // 머티리얼이 먼저 살아나며 이 텍스처를 이미 올려 놓았을 수 있다. 두 번 올리면 그대로 새는 것이다.
        if ( isRhiValid() )
            return true;
        if ( pDevice == nullptr || _path.empty() )
            return true;
        // 렌더 타깃은 파일이 없다 — 같은 크기 · 포맷으로 다시 만든다(그림은 다음에 그 뷰를 그릴 때 채워진다).
        if ( _bRenderTarget == SW_TRUE )
            return createRenderTarget( pDevice, _path, _width, _height, _format );
        return loadFromResource( pDevice, _path );
    }

    void Texture2D::forgetRhi( IRHIDevice* pDevice )
    {
        if ( _pDevice != pDevice )
            return;
        // 디바이스가 이미 없다. 텍스처는 그와 함께 갔다.
        _handle  = 0;
        _srv     = kInvalidDescriptorIndex;
        _pDevice = nullptr;
    }

    void Texture2D::releaseRhi( IRHIDevice* pDevice )
    {
        // 남의 디바이스가 죽는 통보라면 내 것이 아니다.
        if ( pDevice != nullptr && _pDevice != nullptr && _pDevice != pDevice )
            return;

        if ( pDevice != nullptr )
        {
            // 마지막 소유가 게임 스레드에서 놓일 수 있다(머티리얼과 함께) — 렌더 스레드가 병렬 기록 중이면 핸들 반환을 그 프레임 뒤로 미룬다.
            if ( _srv != kInvalidDescriptorIndex )
                pDevice->releaseHandle( RHIHandleKind::BindlessTexture, _srv );
            if ( _handle != 0 )
                pDevice->releaseHandle( RHIHandleKind::Texture, _handle );
        }
        _handle  = 0;
        _srv     = kInvalidDescriptorIndex;
        _pDevice = nullptr;
        // 렌더 타깃은 크기 · 포맷이 곧 정의다 — 새 디바이스에서 다시 만들 때(`initRhi`) 쓴다.
        if ( _bRenderTarget == SW_TRUE )
            return;
        _width    = 0;
        _height   = 0;
        _mipCount = 0;
        _format   = RHIFormat::Unknown;
    }
} // namespace sw

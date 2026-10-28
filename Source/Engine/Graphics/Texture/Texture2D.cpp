#include "pch.h"

#include "Engine/Graphics/Texture/Texture2D.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Asset/AssetLoadProfiler.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/Transfer/RHITransferUtil.h"

/**
 * @brief 텍스처를 읽을 때 버리는 위쪽 밉 수입니다. 사용자 설정 `graphics.textureQuality` 선택지의 `targetValue` 가 넣는다(`engine.settings.xml`, 다시 시작해야 반영).
 * @details Graphics 가 UserSettings 보다 아래 층이라 이 변수는 읽는 쪽인 여기에 둔다(설정은 이름으로 넣는다).
 */
SW_GLOBAL_VARIABLE( int32, gv_textureMipDrop, 0, "텍스처를 읽을 때 버리는 위쪽 밉 수 (사용자 설정 graphics.textureQuality 의 targetValue, 다시 시작해야 반영)" );

namespace sw
{
    SW_LOG_CALLER( "Texture2D" );

    SW_ASSET_TYPE( Texture, Texture2D::kAssetType );

    Texture2D::Texture2D()
        : _pDevice{ nullptr }
        , _path{}
        , _handle{ 0 }
        , _srv{ kInvalidDescriptorIndex }
        , _width{ 0 }
        , _height{ 0 }
        , _mipCount{ 0 }
        , _streamedMipCount{ 0 }
        , _format{ RHIFormat::Unknown }
        , _bRenderTarget{ SW_FALSE }
    {
    }

    Texture2D::~Texture2D()
    {
        if ( getHandle() != 0 )
            SW_LOG_WARNING( "Texture2D '%#' destroyed with a live GPU texture — call releaseRHI first.", _path.c_str() );
    }

    RHIFormat Texture2D::toRHIFormatFromDxgi( uint32 dxgiFormat )
    {
        // DDSLoader 가 쓰는 DXGI 번호(DirectX 헤더 없이 상수로 둔다). 여기 없는 번호는 Unknown.
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
            case 61: // DXGI_FORMAT_R8_UNORM. 1 채널 마스크(DX10 머리 DDS) — 임포트 규칙 `format: R8_UNORM` 이 낸다
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

    Texture2DMipDrop Texture2D::computeMipDrop( RHIFormat format, uint32 width, uint32 height, uint32 mipCount, int32 requestedDrop )
    {
        Texture2DMipDrop         drop{};
        const RHIFormatBlockInfo block = getRHIFormatBlockInfo( format );
        while ( static_cast<int32>( drop._mipCount ) < requestedDrop && drop._mipCount + 1 < mipCount )
        {
            const uint32 nextWidth     = width >> ( drop._mipCount + 1 );
            const uint32 nextHeight    = height >> ( drop._mipCount + 1 );
            const bool   bLargeEnough  = nextWidth >= kMinMipDropExtent && nextHeight >= kMinMipDropExtent;
            const bool   bBlockAligned = nextWidth % block._blockWidth == 0 && nextHeight % block._blockHeight == 0;
            if ( bLargeEnough == false || bBlockAligned == false )
                break;
            RHITextureMipSpan span{};
            if ( computeRHITextureMipLayout( format, width, height, drop._mipCount, span ) == false )
                break;
            drop._skippedBytes += span._sizeBytes;
            ++drop._mipCount;
        }
        return drop;
    }

    bool Texture2D::loadFromAsset( IRHIDevice* pDevice, const AssetID& assetID )
    {
        const string_view relativePath = assetID.view();
        if ( pDevice == nullptr || relativePath.empty() )
            return false;
        DDSImageData image;
        if ( readImage( assetID, image ) == false )
            return false;
        return loadFromImage( pDevice, relativePath, image, TextureResidency::Full );
    }

    bool Texture2D::readImage( const AssetID& assetID, DDSImageData& outImage )
    {
        const string_view relativePath = assetID.view();
        AssetLoadScope    loadScope( "Texture", relativePath );
        if ( DDSLoader::loadFromAsset( assetID, outImage ) == false || outImage.isValid() == false )
        {
            SW_LOG_ERROR( "Texture2D: failed to load '%#'", relativePath );
            return false;
        }
        if ( toRHIFormatFromDxgi( outImage._dxgiFormat ) == RHIFormat::Unknown )
        {
            SW_LOG_ERROR( "Texture2D: '%#' uses DXGI format %# which RHIFormat does not cover yet", relativePath, outImage._dxgiFormat );
            return false;
        }
        if ( outImage._depth > 1 )
        {
            SW_LOG_ERROR( "Texture2D: '%#' is a volume/array texture (depth=%#) — only 2D is supported", relativePath, outImage._depth );
            return false;
        }
        loadScope.setBytes( outImage._bytes.size() );
        loadScope.setSucceeded();
        return true;
    }

    bool Texture2D::loadFromImage( IRHIDevice* pDevice, string_view relativePath, const DDSImageData& image, TextureResidency residency )
    {
        // 블로킹 헬퍼: 제출 → 대기 → 끝내기. 비동기 로드는 같은 두 단계를 프레임에 나눠 한다(`TextureLoadBlock`).
        if ( beginUploadFromImage( pDevice, relativePath, image, residency, true ) != Texture2DUploadStep::Submitted )
            return false;
        return finishUpload( pDevice, true ) == Texture2DUploadStatus::Ready;
    }

    Texture2DUploadStep Texture2D::beginUploadFromImage( IRHIDevice* pDevice, string_view relativePath, const DDSImageData& image, TextureResidency residency, bool bBlocking )
    {
        if ( pDevice == nullptr || relativePath.empty() )
            return Texture2DUploadStep::Failed;
        if ( getHandle() != 0 )
            releaseRHI( pDevice );

        const RHIFormat format = toRHIFormatFromDxgi( image._dxgiFormat );
        SW_ASSERT( format != RHIFormat::Unknown ); // `readImage` 가 거른다
        // 텍스처 품질(gv_textureMipDrop) — DDS 페이로드는 밉 0 부터 이어지므로 버린 밉의 바이트를 건너뛰고 남은 밉 사슬을 업로드한다.
        // 스트리밍 기반(`StreamingBase`)은 같은 규칙으로 버릴 수 있는 만큼 버린다 — 품질이 버리는 밉보다 늘 많거나 같다(같은 식에 더 큰 요청).
        const uint32           fileMipCount = image._mipCount > 0 ? image._mipCount : 1;
        const Texture2DMipDrop qualityDrop  = computeMipDrop( format, image._width, image._height, fileMipCount, static_cast<int32>( gv_textureMipDrop ) );
        const Texture2DMipDrop residentDrop =
            residency == TextureResidency::StreamingBase ? computeMipDrop( format, image._width, image._height, fileMipCount, static_cast<int32>( fileMipCount ) ) : qualityDrop;
        // 텍스처를 만들고 업로드를 제출한다 — 끝남 확인과 bindless 등록은 `finishUpload` 의 몫이다(전송 계약 1 · 8).
        RHITextureDesc            desc{};
        RHITextureHandle          handle = 0;
        RHITransferTicket         ticket{};
        const Texture2DUploadStep step = createAndSubmitMips( *pDevice, relativePath, image, format, residentDrop, bBlocking, desc, handle, ticket );
        if ( step != Texture2DUploadStep::Submitted )
            return step;

        // `_pDevice` 는 **제출한 뒤에만** 적는다. 먼저 적으면 실패하고 돌아간 뒤에도 "이 디바이스에 올라가 있다" 는
        // 표시가 남는다. `isRHIValid` 는 핸들과 SRV 를 보지만 `releaseRHI` 는 이 값으로 **남의 디바이스 통보인지**를 가른다.
        // 가진 것이 없는데 주인만 적혀 있는 상태를 애초에 만들지 않는다. SRV 는 `finishUpload` 가 완료를 본 뒤에 등록한다.
        _handle.store( handle, std::memory_order_release );
        _srv.store( kInvalidDescriptorIndex, std::memory_order_release );
        _uploadTicket     = ticket;
        _pDevice          = pDevice;
        _path             = string{ relativePath };
        _width            = desc._width;
        _height           = desc._height;
        _mipCount         = desc._mipLevels;
        _streamedMipCount = residentDrop._mipCount - qualityDrop._mipCount;
        _format           = format;
        return Texture2DUploadStep::Submitted;
    }

    Texture2DUploadStep Texture2D::submitCreatedTexture( IRHIResourceFactory& factory, RHITextureHandle handle, const RHITextureUploadDesc& upload, bool bBlocking, string_view label,
                                                         RHITransferTicket& outTicket )
    {
        RHITransferSubmit submit{};
        if ( bBlocking )
        {
            const bool bSettled = RHITransferUtil::submitWhileBusy( [&]()
            { return factory.submitTextureUpload( handle, upload ); }, submit );
            if ( bSettled == false )
            {
                factory.destroyTexture( handle );
                return Texture2DUploadStep::Failed;
            }
        }
        else
        {
            submit = factory.submitTextureUpload( handle, upload );
        }
        if ( submit._result == RHISubmitResult::Busy )
        {
            // 다음 프레임에 텍스처부터 다시 만든다 — 만든 것을 쥔 채 기다리면 `Busy` 인 동안 GPU 메모리만 잡는다.
            factory.destroyTexture( handle );
            return Texture2DUploadStep::Busy;
        }
        if ( submit.isAccepted() == false )
        {
            SW_LOG_ERROR( "Texture2D: submitTextureUpload was rejected for '%#'", label );
            factory.destroyTexture( handle );
            return Texture2DUploadStep::Failed;
        }
        outTicket = submit._ticket;
        return Texture2DUploadStep::Submitted;
    }

    Texture2DUploadStatus Texture2D::finishUpload( IRHIDevice* pDevice, bool bBlocking )
    {
        const bool bNothingInFlight = pDevice == nullptr || _pDevice != pDevice || _uploadTicket.isValid() == false || getHandle() == 0;
        if ( bNothingInFlight )
        {
            // 장치 교체가 `forgetRHI` · `releaseRHI` 로 지웠거나 다른 디바이스의 텍스처다 — 새 디바이스에서는 `initRHI` 가 밉 전부를 다시 올린다.
            SW_LOG_ERROR( "Texture2D: '%#' has no upload in flight on this device", _path.c_str() );
            _uploadTicket               = RHITransferTicket{};
            const bool bOwnsLiveTexture = pDevice != nullptr && _pDevice == pDevice && getHandle() != 0;
            if ( bOwnsLiveTexture )
                releaseRHI( pDevice );
            return Texture2DUploadStatus::Failed;
        }
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        RHITransferStatus    status    = RHITransferStatus::Pending;
        if ( bBlocking )
            status = RHITransferUtil::waitForTransfer( *pResource, _uploadTicket ) ? RHITransferStatus::Complete : RHITransferStatus::Failed;
        else
            status = pResource->pollTransfer( _uploadTicket );
        if ( status == RHITransferStatus::Pending )
            return Texture2DUploadStatus::Pending;
        if ( status == RHITransferStatus::Failed )
        {
            SW_LOG_ERROR( "Texture2D: the upload of '%#' did not complete", _path.c_str() );
            releaseRHI( pDevice );
            return Texture2DUploadStatus::Failed;
        }
        const RHIDescriptorIndex srv = pResource->registerBindlessTexture( getHandle() );
        if ( srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR( "Texture2D: registerBindlessTexture failed for '%#'", _path.c_str() );
            releaseRHI( pDevice );
            return Texture2DUploadStatus::Failed;
        }
        _srv.store( srv, std::memory_order_release );
        _uploadTicket = RHITransferTicket{};
        SW_LOG_INFO( "Texture2D '%#' ready: %#×%#, %# mips (%# top mips streamed later), format %#, srv %#", _path.c_str(), _width, _height, _mipCount, _streamedMipCount,
                     static_cast<uint32>( _format ), srv );
        return Texture2DUploadStatus::Ready;
    }

    Texture2DUploadStep Texture2D::beginUpgradeResidency( IRHIDevice* pDevice, const DDSImageData& image, bool bBlocking )
    {
        // 다른 디바이스의 텍스처(백엔드 교체 뒤 아직 다시 업로드하지 않은 것)는 그 디바이스로 다시 만들 때 밉 전부가 올라간다(`initRHI`).
        const bool bUpgradable = pDevice != nullptr && _pDevice == pDevice && _streamedMipCount > 0 && isRHIValid() && _upgrade._ticket.isValid() == false;
        if ( bUpgradable == false )
            return Texture2DUploadStep::Failed;
        const RHIFormat format = toRHIFormatFromDxgi( image._dxgiFormat );
        SW_ASSERT( format == _format ); // 같은 파일의 같은 이미지다(`TextureCache` 가 로드 단계의 이미지를 보관했다)
        const uint32              fileMipCount = image._mipCount > 0 ? image._mipCount : 1;
        const Texture2DMipDrop    qualityDrop  = computeMipDrop( format, image._width, image._height, fileMipCount, static_cast<int32>( gv_textureMipDrop ) );
        PendingUpgrade            upgrade{};
        RHITextureDesc            desc{};
        const Texture2DUploadStep step = createAndSubmitMips( *pDevice, _path, image, format, qualityDrop, bBlocking, desc, upgrade._handle, upgrade._ticket );
        if ( step != Texture2DUploadStep::Submitted )
            return step;
        upgrade._width    = desc._width;
        upgrade._height   = desc._height;
        upgrade._mipCount = desc._mipLevels;
        _upgrade          = upgrade;
        return Texture2DUploadStep::Submitted;
    }

    Texture2DUploadStatus Texture2D::finishUpgradeResidency( IRHIDevice* pDevice, bool bBlocking, Texture2DRetiredResource& outRetired )
    {
        const bool bNothingInFlight = pDevice == nullptr || _pDevice != pDevice || _upgrade._ticket.isValid() == false;
        if ( bNothingInFlight )
        {
            // 장치 교체가 지웠다(`releaseRHI` · `forgetRHI`) — 새 디바이스에서는 `initRHI` 가 밉 전부를 다시 올린다. 새 텍스처는 그 디바이스와 함께 갔거나 해제 큐가 지웠다.
            _upgrade = PendingUpgrade{};
            return Texture2DUploadStatus::Failed;
        }
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        RHITransferStatus    status    = RHITransferStatus::Pending;
        if ( bBlocking )
            status = RHITransferUtil::waitForTransfer( *pResource, _upgrade._ticket ) ? RHITransferStatus::Complete : RHITransferStatus::Failed;
        else
            status = pResource->pollTransfer( _upgrade._ticket );
        if ( status == RHITransferStatus::Pending )
            return Texture2DUploadStatus::Pending;
        const PendingUpgrade upgrade = _upgrade;
        _upgrade                     = PendingUpgrade{};
        if ( status == RHITransferStatus::Failed )
        {
            // 아래쪽 밉 그대로 남는다. 새 텍스처는 쓰지 않으므로 지운다(전송 중 파괴는 백엔드가 미룬다 — 계약 6).
            SW_LOG_ERROR( "Texture2D: the top-mip upload of '%#' did not complete", _path.c_str() );
            pResource->destroyTexture( upgrade._handle );
            return Texture2DUploadStatus::Failed;
        }
        const RHIDescriptorIndex srv = pResource->registerBindlessTexture( upgrade._handle );
        if ( srv == kInvalidDescriptorIndex )
        {
            SW_LOG_ERROR( "Texture2D: registerBindlessTexture failed for '%#'", _path.c_str() );
            pResource->destroyTexture( upgrade._handle );
            return Texture2DUploadStatus::Failed;
        }
        outRetired._handle = _handle.exchange( upgrade._handle, std::memory_order_acq_rel );
        outRetired._srv    = _srv.exchange( srv, std::memory_order_acq_rel );
        _width             = upgrade._width;
        _height            = upgrade._height;
        _mipCount          = upgrade._mipCount;
        _streamedMipCount  = 0;
        return Texture2DUploadStatus::Ready;
    }

    Texture2DUploadStep Texture2D::createAndSubmitMips( IRHIDevice& device, string_view relativePath, const DDSImageData& image, RHIFormat format, const Texture2DMipDrop& drop,
                                                        bool bBlocking, RHITextureDesc& outDesc, RHITextureHandle& outHandle, RHITransferTicket& outTicket )
    {
        const uint32 fileMipCount = image._mipCount > 0 ? image._mipCount : 1;
        if ( drop._skippedBytes >= image._bytes.size() )
        {
            SW_LOG_ERROR( "Texture2D: '%#' payload (%# bytes) is shorter than its mip chain (%# mips)", relativePath, image._bytes.size(), fileMipCount );
            return Texture2DUploadStep::Failed;
        }
        outDesc                        = RHITextureDesc{};
        outDesc._width                 = image._width >> drop._mipCount;
        outDesc._height                = image._height >> drop._mipCount;
        outDesc._mipLevels             = fileMipCount - drop._mipCount;
        outDesc._format                = format;
        outDesc._bIsShaderResource     = SW_TRUE;
        IRHIResourceFactory* pResource = device.getResourceFactory();
        outHandle                      = pResource->createTexture2D( outDesc );
        if ( outHandle == 0 )
        {
            SW_LOG_ERROR( "Texture2D: createTexture2D failed for '%#' (%#×%#, %# mips)", relativePath, outDesc._width, outDesc._height, outDesc._mipLevels );
            return Texture2DUploadStep::Failed;
        }

        // DDS 페이로드는 밉 0 부터 행 빈틈없이 이어진 배치다. submitTextureUpload 의 규약과 같다.
        RHITextureUploadDesc upload{};
        upload._pData                  = image.getPixels() + drop._skippedBytes;
        upload._sizeBytes              = static_cast<uint32>( image._bytes.size() - drop._skippedBytes );
        upload._mipLevels              = outDesc._mipLevels;
        const Texture2DUploadStep step = submitCreatedTexture( *pResource, outHandle, upload, bBlocking, relativePath, outTicket );
        if ( step != Texture2DUploadStep::Submitted )
            outHandle = 0; // `submitCreatedTexture` 가 지웠다
        return step;
    }

    bool Texture2D::createRenderTarget( IRHIDevice* pDevice, string_view name, uint32 width, uint32 height, RHIFormat format )
    {
        const bool bInvalidRenderTargetArguments = pDevice == nullptr || name.empty() || width == 0 || height == 0;
        if ( bInvalidRenderTargetArguments )
            return false;
        if ( getHandle() != 0 )
            releaseRHI( pDevice );

        RHITextureDesc desc{};
        desc._width                    = width;
        desc._height                   = height;
        desc._mipLevels                = 1;
        desc._format                   = format;
        desc._bIsRenderTarget          = SW_TRUE;
        desc._bIsShaderResource        = SW_TRUE;
        IRHIResourceFactory* pResource = pDevice->getResourceFactory();
        _handle                        = pResource->createTexture2D( desc );
        if ( getHandle() == 0 )
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
        SW_LOG_INFO( "Texture2D render target '%#' ready: %#×%#, srv %#", _path.c_str(), _width, _height, getSRV() );
        return true;
    }

    bool Texture2D::initRHI( IRHIDevice* pDevice )
    {
        // 머티리얼이 먼저 살아나며 이 텍스처를 이미 올려 놓았을 수 있다. 두 번 올리면 그대로 새는 것이다.
        if ( isRHIValid() )
            return true;
        if ( pDevice == nullptr || _path.empty() )
            return true;
        // 렌더 타깃은 파일이 없다 — 같은 크기 · 포맷으로 다시 만든다(그림은 다음에 그 뷰를 그릴 때 채워진다).
        if ( _bRenderTarget == SW_TRUE )
            return createRenderTarget( pDevice, _path, _width, _height, _format );
        // CPU 텍스처는 다시 빌릴 때 캐시가 올린다(헤더 — 참조 0 이면 GPU 자원이 없다).
        if ( hasCPUBytes() )
            return true;
        return loadFromAsset( pDevice, AssetID{ _path } );
    }

    void Texture2D::setCPUBytes( string_view name, uint32 width, uint32 height, vector<uint8> rgbaBytes )
    {
        SW_ASSERT( getHandle() == 0 );
        _path     = string{ name };
        _width    = width;
        _height   = height;
        _mipCount = 1;
        _format   = RHIFormat::R8G8B8A8_UNORM;
        _cpuBytes = std::move( rgbaBytes );
    }

    bool Texture2D::loadFromCPUBytes( IRHIDevice* pDevice )
    {
        if ( isRHIValid() )
            return true;
        // 블로킹 헬퍼: 제출 → 대기 → 끝내기(`loadFromImage` 와 같다).
        if ( beginUploadFromCPUBytes( pDevice, true ) != Texture2DUploadStep::Submitted )
            return false;
        return finishUpload( pDevice, true ) == Texture2DUploadStatus::Ready;
    }

    Texture2DUploadStep Texture2D::beginUploadFromCPUBytes( IRHIDevice* pDevice, bool bBlocking )
    {
        if ( pDevice == nullptr || hasCPUBytes() == false )
            return Texture2DUploadStep::Failed;
        if ( getHandle() != 0 )
            releaseRHI( pDevice );
        RHITextureDesc desc{};
        desc._width                      = _width;
        desc._height                     = _height;
        desc._mipLevels                  = 1;
        desc._format                     = _format;
        desc._bIsShaderResource          = SW_TRUE;
        IRHIResourceFactory*   pResource = pDevice->getResourceFactory();
        const RHITextureHandle handle    = pResource->createTexture2D( desc );
        if ( handle == 0 )
        {
            SW_LOG_ERROR( "Texture2D: createTexture2D failed for CPU texture '%#' (%#×%#)", _path.c_str(), _width, _height );
            return Texture2DUploadStep::Failed;
        }
        RHITextureUploadDesc upload{};
        upload._pData     = _cpuBytes.data();
        upload._sizeBytes = static_cast<uint32>( _cpuBytes.size() );
        upload._mipLevels = 1;
        RHITransferTicket         ticket{};
        const Texture2DUploadStep step = submitCreatedTexture( *pResource, handle, upload, bBlocking, _path, ticket );
        if ( step != Texture2DUploadStep::Submitted )
            return step;
        _handle.store( handle, std::memory_order_release );
        _srv.store( kInvalidDescriptorIndex, std::memory_order_release );
        _uploadTicket = ticket;
        _pDevice      = pDevice;
        return Texture2DUploadStep::Submitted;
    }

    RHISubmitResult Texture2D::writeCPUBytes( IRHIDevice* pDevice, const RHITextureRegionUploadDesc& desc )
    {
        const size_t rowBytes     = static_cast<size_t>( desc._width ) * 4;
        const bool   bInsideX     = desc._x + desc._width <= _width;
        const bool   bInsideY     = desc._y + desc._height <= _height;
        const bool   bEnoughBytes = desc._pData != nullptr && desc._sizeBytes >= rowBytes * desc._height;
        const bool   bValidRegion = bInsideX && bInsideY && bEnoughBytes && desc._mip == 0;
        if ( hasCPUBytes() == false || bValidRegion == false )
            return RHISubmitResult::Rejected;
        const uint8* pSource = static_cast<const uint8*>( desc._pData );
        for ( uint32 row = 0; row < desc._height; ++row )
        {
            const size_t target = ( static_cast<size_t>( desc._y + row ) * _width + desc._x ) * 4;
            Memory::copy( _cpuBytes.data() + target, pSource + row * rowBytes, rowBytes );
        }
        // GPU 에 없으면 원본만 고친다 — 다음에 올릴 때(`loadFromCPUBytes`) 이 내용이 올라간다.
        // 올리는 중(제출했고 아직 등록 전)인 텍스처도 구간을 낸다 — 같은 텍스처의 전송은 낸 순서대로 실행되므로(계약 3c) 처음 업로드 뒤에 이 구간이 덮인다.
        const bool bResidentOnDevice = pDevice != nullptr && ( isRHIValid() || hasUploadInFlight() ) && _pDevice == pDevice;
        if ( bResidentOnDevice == false )
            return RHISubmitResult::Accepted;
        // 등록된 텍스처의 구간 갱신이라 완료를 기다리지 않는다 — 같은 텍스처의 전송은 낸 순서대로, 뒤에 낼 프레임 커맨드보다 먼저 실행된다(전송 계약 3).
        // 전송 중 파괴는 백엔드가 해제를 미룬다(계약 6). 다시 칠하면 원본이 이미 고쳐져 있으므로 `Busy` 의 재시도는 같은 호출을 다시 하면 된다.
        const RHITransferSubmit submit = pDevice->getResourceFactory()->submitTextureRegionUpload( getHandle(), desc );
        return submit._result;
    }

    void Texture2D::detachRHI( Texture2DRetiredResource& outRetired )
    {
        // 렌더 타깃은 올리는 중인 업로드가 없다(`createRenderTarget` 은 제출하지 않는다) — 위쪽 밉 교체의 새 텍스처가 있으면 이 함수의 대상이 아니다.
        SW_ASSERT( _upgrade._handle == 0 );
        outRetired._handle = _handle.exchange( 0, std::memory_order_acq_rel );
        outRetired._srv    = _srv.exchange( kInvalidDescriptorIndex, std::memory_order_acq_rel );
        _uploadTicket      = RHITransferTicket{};
        _pDevice           = nullptr;
        _streamedMipCount  = 0;
    }

    void Texture2D::forgetRHI( IRHIDevice* pDevice )
    {
        if ( _pDevice != pDevice )
            return;
        // 디바이스가 이미 없다. 텍스처는 그와 함께 갔다. 새 디바이스에 다시 업로드할 때는 밉 전부가 올라간다(`initRHI`).
        _handle           = 0;
        _srv              = kInvalidDescriptorIndex;
        _uploadTicket     = RHITransferTicket{};
        _upgrade          = PendingUpgrade{};
        _pDevice          = nullptr;
        _streamedMipCount = 0;
    }

    void Texture2D::releaseRHI( IRHIDevice* pDevice )
    {
        // 남의 디바이스가 죽는 통보라면 내 것이 아니다.
        const bool bForeignDeviceNotice = pDevice != nullptr && _pDevice != nullptr && _pDevice != pDevice;
        if ( bForeignDeviceNotice )
            return;

        if ( pDevice != nullptr )
        {
            // 마지막 소유가 게임 스레드에서 놓일 수 있다(머티리얼과 함께) — 렌더 스레드가 병렬 기록 중이면 핸들 반환을 그 프레임 뒤로 미룬다.
            if ( getSRV() != kInvalidDescriptorIndex )
                pDevice->releaseHandle( RHIHandleKind::BindlessTexture, getSRV() );
            if ( getHandle() != 0 )
                pDevice->releaseHandle( RHIHandleKind::Texture, getHandle() );
            // 위쪽 밉을 올리는 중이던 새 텍스처도 내린다(등록 전이라 SRV 는 없다).
            if ( _upgrade._handle != 0 )
                pDevice->releaseHandle( RHIHandleKind::Texture, _upgrade._handle );
        }
        _handle           = 0;
        _srv              = kInvalidDescriptorIndex;
        _uploadTicket     = RHITransferTicket{};
        _upgrade          = PendingUpgrade{};
        _pDevice          = nullptr;
        _streamedMipCount = 0;
        // 렌더 타깃 · CPU 텍스처는 크기 · 포맷이 곧 정의다 — 다시 만들 때 쓴다.
        if ( _bRenderTarget == SW_TRUE || hasCPUBytes() )
            return;
        _width    = 0;
        _height   = 0;
        _mipCount = 0;
        _format   = RHIFormat::Unknown;
    }
} // namespace sw

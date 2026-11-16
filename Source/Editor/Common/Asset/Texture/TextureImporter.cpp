#include "pch.h"

#include "Editor/Common/Asset/Texture/TextureImporter.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/Texture/RawImageData.h"
#include "Editor/Common/Asset/Texture/TextureBlockEncoder.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"

#include "sw/config/ConfigConstants.h"

#if defined( TileShape )
    #undef TileShape
#endif
#if defined( CursorShape )
    #undef CursorShape
#endif
#if defined( PixmapShape )
    #undef PixmapShape
#endif

#include <DirectXTex.h>

namespace sw::editor
{
    namespace
    {
        /**
         * @brief 규칙의 형식을 DXGI 형식으로 바꿉니다. 규칙은 이미 푼 형식(`TextureImportFormat`)을 들므로 실패가 없다 — 모르는 이름은 설정 로드가 거절했다.
         * @details sRGB 짝이 없는 형식(BC4 · BC5 · BC6H · R16G16B16A16_FLOAT · R8)은 @p bSRGB 와 무관하게 UNORM 이며, 원본 바이트를 곡선 없이 그대로 옮긴다.
         *          번호는 이름 표 하나(`TextureImportFormatUtil::getDXGIFormat`)에서 온다 — 에셋 규칙과 대조하는 테스트가 같은 표를 읽는다.
         */
        [[nodiscard]] DXGI_FORMAT resolveFormatInternal( TextureImportFormat format, bool bSRGB )
        {
            return static_cast<DXGI_FORMAT>( TextureImportFormatUtil::getDXGIFormat( format, bSRGB ) );
        }

        /**
         * @struct BandCompressJob
         * @brief 밉 체인을 BC 형식으로 압축하는 일을 가로 띠로 나눠 작업 시스템에 넘깁니다.
         * @details BC 블록은 4×4 화소마다 독립이므로, 높이가 4 의 배수인 띠로 잘라 따로 압축해 이어 붙이면 통째로 압축한 것과 같은 바이트가 나온다.
         *          `TEX_COMPRESS_DEFAULT` 를 쓸 때만 그렇다. 디더링이나 오차 확산을 켜면 블록 사이가 이어져 이 성질이 깨진다. DirectXTex 의 `TEX_COMPRESS_PARALLEL` 은
         *          OpenMP 로만 동작하고 vcpkg 빌드는 OpenMP 없이(`BC_USE_OPENMP` 꺼짐) 빌드되어 효과가 없으므로 엔진 작업 시스템으로 나눈다.
         *          띠 높이(`kBandRows`)보다 낮은 밉은 띠가 하나뿐이라 나누지 않고, 모든 밉의 띠를 한 목록으로 펼쳐 한 번에 나눈다.
         */
        struct BandCompressJob
        {
            /** @brief 띠 하나의 화소 행 수입니다. BC 블록 높이이므로 4 의 배수여야 합니다. */
            static constexpr uint32 kBandRows = 64;
            static_assert( kBandRows % 4 == 0, "BC bands must cover whole 4x4 block rows" );

            struct Band
            {
                const DirectX::Image* _pSource{ nullptr };
                const DirectX::Image* _pDest{ nullptr };
                uint32                _firstRow{ 0 };
                uint32                _rowCount{ 0 };
                HRESULT               _result{ S_OK };
            };

            Band*                       _pBand{ nullptr };
            DXGI_FORMAT                 _format{ DXGI_FORMAT_UNKNOWN };
            DirectX::TEX_COMPRESS_FLAGS _flags{ DirectX::TEX_COMPRESS_DEFAULT };

            void run( uint32 start, uint32 end )
            {
                for ( uint32 bandIndex = start; bandIndex < end; ++bandIndex )
                {
                    Band&                 band   = _pBand[bandIndex];
                    const DirectX::Image& source = *band._pSource;
                    DirectX::Image        slice  = source;
                    slice.height                 = band._rowCount;
                    slice.slicePitch             = source.rowPitch * band._rowCount;
                    slice.pixels                 = source.pixels + source.rowPitch * band._firstRow;

                    DirectX::ScratchImage compressed;
                    band._result = DirectX::Compress( slice, _format, _flags, DirectX::TEX_THRESHOLD_DEFAULT, compressed );
                    if ( FAILED( band._result ) )
                        continue;

                    // 폭이 같으므로 블록 행의 바이트 수(rowPitch)가 같다. 띠의 블록 행을 결과의 해당 위치(firstRow / 4)에 그대로 놓는다.
                    const DirectX::Image& packed = *compressed.GetImage( 0, 0, 0 );
                    if ( packed.rowPitch != band._pDest->rowPitch )
                    {
                        band._result = E_UNEXPECTED;
                        continue;
                    }
                    const size_t blockRowCount = ( static_cast<size_t>( band._rowCount ) + 3 ) / 4;
                    std::memcpy( band._pDest->pixels + band._pDest->rowPitch * ( band._firstRow / 4 ), packed.pixels, packed.rowPitch * blockRowCount );
                }
            }

            /**
             * @brief @p mipChain(2D 한 장의 밉들)을 @p format 으로 압축해 @p outImage 에 담습니다. 결과는 `DirectX::Compress` 를 통째로 호출한 것과 같은 바이트입니다.
             * @param pTaskManager 띠를 나눠 돌릴 작업 시스템입니다. nullptr 이면(테스트 하니스 · 서비스 연결 전) 현재 스레드가 띠를 차례로 압축합니다.
             *                     워커에서도 불리므로 이 안에서 에디터 서비스를 찾지 않고 부른 쪽이 제출 시점에 찾은 것을 받습니다.
             */
            [[nodiscard]] static HRESULT compress( const DirectX::ScratchImage& mipChain, DXGI_FORMAT format, DirectX::TEX_COMPRESS_FLAGS flags, TaskManager* pTaskManager,
                                                   DirectX::ScratchImage& outImage )
            {
                const DirectX::TexMetadata& metadata           = mipChain.GetMetadata();
                const bool                  bNotPlainTexture2D = metadata.dimension != DirectX::TEX_DIMENSION_TEXTURE2D || metadata.arraySize != 1 || metadata.depth != 1;
                if ( bNotPlainTexture2D )
                {
                    return DirectX::Compress( mipChain.GetImages(), mipChain.GetImageCount(), metadata, format, flags,
                                              DirectX::TEX_THRESHOLD_DEFAULT, outImage );
                }
                HRESULT hr = outImage.Initialize2D( format, metadata.width, metadata.height, 1, metadata.mipLevels );
                if ( FAILED( hr ) )
                    return hr;

                vector<Band> listBand;
                for ( size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel )
                {
                    const DirectX::Image* pSource = mipChain.GetImage( mipLevel, 0, 0 );
                    const DirectX::Image* pDest   = outImage.GetImage( mipLevel, 0, 0 );
                    if ( pSource == nullptr || pDest == nullptr )
                        return E_POINTER;
                    const uint32 height = static_cast<uint32>( pSource->height );
                    for ( uint32 firstRow = 0; firstRow < height; firstRow += kBandRows )
                    {
                        Band band;
                        band._pSource  = pSource;
                        band._pDest    = pDest;
                        band._firstRow = firstRow;
                        band._rowCount = MathUtil::min( kBandRows, height - firstRow );
                        listBand.push_back( band );
                    }
                }

                const Stopwatch stopwatch;
                BandCompressJob job;
                job._pBand  = listBand.data();
                job._format = format;
                job._flags  = flags;
                // 작업 시스템이 없으면(테스트 하니스, 서비스 연결 전) 현재 스레드가 띠를 차례로 압축한다. 결과 바이트는 같다.
                const uint32                bandCount = static_cast<uint32>( listBand.size() );
                const ParallelBlockDelegate body      = SW_DELEGATE_METHOD( ParallelBlockDelegate, &BandCompressJob::run, &job );
                if ( pTaskManager != nullptr )
                    pTaskManager->runParallel( bandCount, 2, body );
                else
                    job.run( 0, bandCount );
                SW_LOG_INFO_ID( "TextureImporter.CompressedXMipsFormat", "Compressed %#x%# (%# mips, format %#) in %# bands on %# workers: %# ms", static_cast<uint32>( metadata.width ), static_cast<uint32>( metadata.height ),
                                static_cast<uint32>( metadata.mipLevels ), static_cast<uint32>( format ), bandCount,
                                pTaskManager != nullptr ? pTaskManager->getWorkerCount() : 0u,
                                stopwatch.getElapsedMilliseconds() );

                for ( const Band& band : listBand )
                {
                    if ( FAILED( band._result ) )
                        return band._result;
                }
                return S_OK;
            }
        };

        /** @brief 임포트 결과(밉까지 끝난 이미지)를 DDS 로 쓰고 결과를 채웁니다. 8비트 경로와 HDR 경로가 함께 쓰며, 둘 다 원본 채널 수를 넘깁니다. */
        [[nodiscard]] bool saveImportedDDSInternal( const DirectX::ScratchImage& finalImage, uint32 sourceChannelCount, [[maybe_unused]] string_view sourcePath, string_view outputPath,
                                                    TextureImportResult* pOutResult )
        {
            // DirectXTex 가 경로를 스스로 열어 쓴다 — `FileUtil` 의 쓰기 입구를 지나지 않으므로 게이트를 직접 묻는다(커밋된 DDS 를 자동화 실행이 덮지 않게).
            if ( FileUtil::isWriteAllowed( outputPath ) == false || FileUtil::ensureParentDirectoryExists( outputPath ) == false )
                return false;

            // 엔진 `DDSLoader` 의 옛 머리(DX9 픽셀 형식) 경로는 1 채널 휘도(L8)를 모른다. R8_UNORM 은 DX10 머리로 써서 DXGI 번호(61)를 그대로 싣는다.
            // 다른 형식은 옛 머리 그대로 둔다 — 이미 커밋된 DDS 의 바이트가 바뀌지 않게 한다.
            const DirectX::DDS_FLAGS saveFlags = finalImage.GetMetadata().format == DXGI_FORMAT_R8_UNORM ? DirectX::DDS_FLAGS_FORCE_DX10_EXT : DirectX::DDS_FLAGS_NONE;
            const wstring            wOutPath  = StringUtil::utf8ToUTF16( string( outputPath ).c_str() );
            const HRESULT            hrSave    = DirectX::SaveToDDSFile( finalImage.GetImages(), finalImage.GetImageCount(), finalImage.GetMetadata(), saveFlags, wOutPath.c_str() );
            if ( FAILED( hrSave ) )
            {
                SW_LOG_ERROR_ID( "TextureImporter.DirectXSaveToDDSFileFailedHr", "DirectX::SaveToDDSFile failed (hr=0x%#) for %#", Fmt( static_cast<uint32>( hrSave ), Format( 8, Format::Padding::Zero ).hex() ),
                                 outputPath );
                return false;
            }

            const DirectX::TexMetadata& metadata        = finalImage.GetMetadata();
            const uint32                mipCount        = static_cast<uint32>( metadata.mipLevels );
            uint64                      outputSizeBytes = 0;
            if ( FileUtil::tryGetFileSize( outputPath, outputSizeBytes ) == false )
                SW_LOG_WARNING_ID( "TextureImporter.ImportedTextureButCannot", "Imported texture %# but cannot read its size", outputPath );
            if ( pOutResult != nullptr )
            {
                pOutResult->_width              = static_cast<uint32>( metadata.width );
                pOutResult->_height             = static_cast<uint32>( metadata.height );
                pOutResult->_mipCount           = mipCount;
                pOutResult->_sourceChannelCount = sourceChannelCount;
                pOutResult->_outputSizeBytes    = outputSizeBytes;
                pOutResult->_bSuccess           = SW_TRUE;
            }
            uint64 sourceSizeBytes = 0;
            (void)FileUtil::tryGetFileSize( sourcePath, sourceSizeBytes ); // 로그 한 줄의 숫자다 - 읽은 원본이라 늘 있다
            SW_LOG_INFO_ID( "TextureImporter.ImportedTextureFormatMips", "Imported texture: %# -> %# (Format: %#, Mips: %#, %# -> %# bytes)", sourcePath, outputPath,
                            static_cast<uint32>( metadata.format ), mipCount, sourceSizeBytes, outputSizeBytes );
            return true;
        }

        /** @brief 규칙의 압축 플래그입니다 — quick_compress 면 BC7 을 빠른 프로필(모드 6 하나)로 압축한다(블록마다 독립이라 띠 나누기와 같은 바이트다). */
        [[nodiscard]] DirectX::TEX_COMPRESS_FLAGS compressFlagsInternal( const TextureImportRule& rule )
        {
            return rule._bQuickCompress == SW_TRUE ? DirectX::TEX_COMPRESS_BC7_QUICK : DirectX::TEX_COMPRESS_DEFAULT;
        }

        /**
         * @brief 원본 한 장에서 밉 체인을 만들고(`bGenerateMips` 가 아니면 한 장 그대로) @p targetFormat 으로 압축하거나 변환해 @p outFinalImage 에 담습니다.
         * @details 8비트 경로와 HDR 경로가 함께 씁니다. WIC 필터는 Windows 에만 있고 결과가 OS 구현에 따라 달라집니다. 임포트된 DDS 를 커밋하고 그 해시를
         *          스탬프에 기록하므로, 어디서 임포트하든 같은 바이트가 나오는 DirectXTex 자체 필터만 씁니다. 실패하면 단계와 HRESULT 를 남기고 false 입니다.
         */
        [[nodiscard]] bool encodeTextureMipsInternal( const DirectX::Image& baseImage, bool bGenerateMips, DXGI_FORMAT targetFormat, DirectX::TEX_COMPRESS_FLAGS compressFlags,
                                                      string_view sourcePath, TaskManager* pTaskManager, DirectX::ScratchImage& outFinalImage );

        /** @brief 임포트 · 쿠킹의 필터입니다 — WIC 를 쓰지 않아 어디서나 같은 바이트입니다(`encodeTextureMipsInternal`). */
        constexpr DirectX::TEX_FILTER_FLAGS kMipFilterFlags = DirectX::TEX_FILTER_DEFAULT | DirectX::TEX_FILTER_FORCE_NON_WIC;

        /** @brief 원본 한 장의 밉 체인(중간본)을 만듭니다. @p bBlockCompressed 면 맨 위 크기를 4 의 배수로 다시 표본합니다. BC 임포트와 쿠킹 변형이 함께 씁니다. 실패하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool buildMipChainInternal( const DirectX::Image& baseImage, bool bGenerateMips, bool bBlockCompressed, string_view sourcePath, DirectX::ScratchImage& outMipChain )
        {
            constexpr DirectX::TEX_FILTER_FLAGS kFilterFlags = kMipFilterFlags;

            // BC 압축은 맨 위 크기가 4 의 배수여야 한다 — D3D11 은 아니면 텍스처를 만들지 못한다(1008x981 의 꽃 아틀라스가 DX11 에서만 체크무늬였다. D3D12 · Vulkan · GL 은 받아 준다).
            // 덧대면 UV 가 어긋나므로 4 의 배수로 다시 표본한다(언리얼도 BC 원본은 4 배수를 요구한다). 밉은 그 크기에서 만든다.
            DirectX::ScratchImage alignedImage;
            const DirectX::Image* pBaseImage    = &baseImage;
            const size_t          alignedWidth  = ( baseImage.width + 3 ) & ~static_cast<size_t>( 3 );
            const size_t          alignedHeight = ( baseImage.height + 3 ) & ~static_cast<size_t>( 3 );
            if ( bBlockCompressed && ( alignedWidth != baseImage.width || alignedHeight != baseImage.height ) )
            {
                const HRESULT hrResize = DirectX::Resize( baseImage, alignedWidth, alignedHeight, kFilterFlags, alignedImage );
                if ( FAILED( hrResize ) )
                {
                    SW_LOG_ERROR_ID( "TextureImporter.BlockAlignResizeFailed", "DirectX::Resize to %#x%# failed (hr=0x%#) for %#", static_cast<uint32>( alignedWidth ), static_cast<uint32>( alignedHeight ),
                                     Fmt( static_cast<uint32>( hrResize ), Format( 8, Format::Padding::Zero ).hex() ), sourcePath );
                    return false;
                }
                SW_LOG_INFO_ID( "TextureImporter.BlockAlignResized", "%# is %#x%# - resampled to %#x%# for block compression", sourcePath, static_cast<uint32>( baseImage.width ),
                                static_cast<uint32>( baseImage.height ), static_cast<uint32>( alignedWidth ), static_cast<uint32>( alignedHeight ) );
                pBaseImage = alignedImage.GetImage( 0, 0, 0 );
            }

            const HRESULT hr = bGenerateMips ? DirectX::GenerateMipMaps( *pBaseImage, kFilterFlags, 0, outMipChain ) : outMipChain.InitializeFromImage( *pBaseImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR_ID( "TextureImporter.FailedHrX", "%# failed (hr=0x%#) for %#", bGenerateMips ? "DirectX::GenerateMipMaps" : "DirectX::InitializeFromImage",
                                 Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ), sourcePath );
                return false;
            }
            return true;
        }

        [[nodiscard]] bool encodeTextureMipsInternal( const DirectX::Image& baseImage, bool bGenerateMips, DXGI_FORMAT targetFormat, DirectX::TEX_COMPRESS_FLAGS compressFlags,
                                                      string_view sourcePath, TaskManager* pTaskManager, DirectX::ScratchImage& outFinalImage )
        {
            constexpr DirectX::TEX_FILTER_FLAGS kFilterFlags = kMipFilterFlags;
            DirectX::ScratchImage               mipChain;
            if ( buildMipChainInternal( baseImage, bGenerateMips, DirectX::IsCompressed( targetFormat ), sourcePath, mipChain ) == false )
                return false;

            HRESULT hr = S_OK;
            if ( DirectX::IsCompressed( targetFormat ) )
            {
                hr = BandCompressJob::compress( mipChain, targetFormat, compressFlags, pTaskManager, outFinalImage );
                if ( FAILED( hr ) )
                {
                    SW_LOG_ERROR_ID( "TextureImporter.DirectXCompressFailedHr", "DirectX::Compress failed (hr=0x%#) for format %# (%#)", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                                     static_cast<uint32>( targetFormat ), sourcePath );
                    return false;
                }
                return true;
            }
            if ( targetFormat == mipChain.GetMetadata().format )
            {
                outFinalImage = std::move( mipChain );
                return true;
            }
            // 1 채널(R8)은 R 채널을 그대로 옮긴다 — DirectXTex 는 따로 말하지 않으면 RGB 를 휘도로 섞는다(마스크 텍스처의 값이 바뀐다).
            const DirectX::TEX_FILTER_FLAGS convertFlags = targetFormat == DXGI_FORMAT_R8_UNORM ? kFilterFlags | DirectX::TEX_FILTER_RGB_COPY_RED : kFilterFlags;
            hr                                           = DirectX::Convert( mipChain.GetImages(), mipChain.GetImageCount(), mipChain.GetMetadata(), targetFormat, convertFlags, DirectX::TEX_THRESHOLD_DEFAULT,
                                                                             outFinalImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR_ID( "TextureImporter.DirectXConvertFailedHr", "DirectX::Convert failed (hr=0x%#) for format %# (%#)", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                                 static_cast<uint32>( targetFormat ), sourcePath );
                return false;
            }
            return true;
        }

        /**
         * @brief `.hdr`(Radiance RGBE)을 부동소수점으로 읽어 BC6H_UF16 또는 R16G16B16A16_FLOAT 로 임포트합니다.
         * @details 규칙의 포맷이 다른 것(8비트)이면 실패로 알린다. 1 을 넘는 값을 자르지 않기 위해서다. 밉은 선형 값 그대로 섞으며 sRGB 가 아니다.
         */
        [[nodiscard]] bool importHDRTextureInternal( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TaskManager* pTaskManager,
                                                     TextureImportResult* pOutResult )
        {
            const DXGI_FORMAT targetFormat = resolveFormatInternal( rule._format, false );
            if ( targetFormat != DXGI_FORMAT_BC6H_UF16 && targetFormat != DXGI_FORMAT_R16G16B16A16_FLOAT )
            {
                SW_LOG_ERROR_ID( "TextureImporter.HDRSourceNeedsFormat", "HDR source needs format bc6h or rgba16f (rule format '%#'): %#", TextureImportFormatUtil::getName( rule._format ), sourcePath );
                return false;
            }
            const wstring         wSourcePath = StringUtil::utf8ToUTF16( string( sourcePath ).c_str() );
            DirectX::TexMetadata  metadata{};
            DirectX::ScratchImage sourceImage;
            HRESULT               hr = DirectX::LoadFromHDRFile( wSourcePath.c_str(), &metadata, sourceImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR_ID( "TextureImporter.DirectXLoadFromHDRFileFailedHr", "DirectX::LoadFromHDRFile failed (hr=0x%#) for %#", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                                 sourcePath );
                return false;
            }
            DirectX::ScratchImage finalImage;
            if ( encodeTextureMipsInternal( *sourceImage.GetImage( 0, 0, 0 ), rule._bGenerateMips == SW_TRUE, targetFormat, compressFlagsInternal( rule ), sourcePath, pTaskManager, finalImage ) == false )
                return false;
            // Radiance RGBE 는 알파 없는 RGB 세 채널이다.
            constexpr uint32 kHDRSourceChannelCount = 3;
            return saveImportedDDSInternal( finalImage, kHDRSourceChannelCount, sourcePath, outputPath, pOutResult );
        }

        /**
         * @struct SwizzleLayout
         * @brief 스위즐 하나가 정하는 두 가지, 바이트를 어떻게 놓는지와 그것을 무슨 이름으로 부르는지입니다.
         * @details 이 둘을 따로 두면 반드시 어긋난다. 섞기 쪽만 아는 스위즐은 바이트는 옮겨지는데 결과물에 "RGBA" 라고
         *          기록되어 색이 깨진다. 스위즐을 추가하려면 이 테이블에 항목 하나만 추가하면 된다.
         */
        struct SwizzleLayout
        {
            /** @brief 결과의 n 번째 바이트를 원본(항상 RGBA)의 몇 번째 바이트에서 가져올지 정합니다. */
            uint8 _arrSourceChannel[4];
            /** @brief 알파를 255 로 덮을지 정합니다(RGB1). */
            uint8 _bForceOpaqueAlpha;
            /** @brief 그렇게 놓인 바이트 배열의 DXGI 이름입니다. */
            DXGI_FORMAT _format;
        };

        const SwizzleLayout& swizzleLayoutInternal( TextureSwizzle swizzle )
        {
            // BGRA 와 ARGB 는 같은 것이다. D3D9 의 `D3DFMT_A8R8G8B8` 은 메모리에서 B, G, R, A 순서이고 DXGI 는 그것을 `B8G8R8A8`
            // 이라 부른다. 열거형 주석의 "레거시 ARGB" 가 그 뜻이다. RGBA 를 왼쪽으로 한 채널 돌린 GBAR 은 어떻게 읽어도
            // ARGB 가 아니므로 주의한다.
            static constexpr SwizzleLayout kRGBA{
                { 0, 1, 2, 3 },
                SW_FALSE,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };
            static constexpr SwizzleLayout kBgra{
                { 2, 1, 0, 3 },
                SW_FALSE,
                DXGI_FORMAT_B8G8R8A8_UNORM
            };
            static constexpr SwizzleLayout kRGB1{
                { 0, 1, 2, 3 },
                SW_TRUE,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };

            switch ( swizzle )
            {
                case TextureSwizzle::BGRA:
                case TextureSwizzle::ARGB:
                    return kBgra;
                case TextureSwizzle::RGB1:
                    return kRGB1;
                case TextureSwizzle::RGBA:
                    return kRGBA;
            }
        }

        /**
         * @struct TextureImporterInternal
         * @brief 원본(`textures_raw/`)과 임포트된 DDS(`textures/`)를 연결하는 이름과 버전입니다. 스탬프 절차는 `AssetImportStampUtil` 이 합니다.
         */
        struct TextureImporterInternal
        {
            /** @brief 원본 이미지를 두는 폴더 이름입니다. 쿠킹은 이 폴더를 팩에서 뺍니다(`Config/Engine/PackConfig.json`). */
            static constexpr string_view kRawTextureFolder = "textures_raw";
            /** @brief 원본 폴더 옆에 임포트된 DDS 를 두는 폴더 이름입니다. 런타임은 여기의 DDS 만 읽습니다. */
            static constexpr string_view kImportedTextureFolder = "textures";
            /** @brief 스탬프의 헤더 줄입니다. 형식이나 판정이 바뀌면 올립니다. 이전 스탬프는 모두 어긋남이 되어 한 번 다시 임포트합니다. */
            static constexpr string_view kImportStampHeader = "SWTEXIMPORT 1";
            /** @brief 임포터(인코더) 버전입니다. 같은 원본과 규칙에서 다른 바이트가 나오게 바꿨다면 올립니다. DirectXTex 버전을 올린 것도 그렇습니다. 원본 해시에 섞입니다. */
            static constexpr uint32 kImporterVersion = 2; // 2: BC 압축 원본을 4 의 배수 크기로 다시 표본한다

            /** @brief 임포트하는 원본 이미지 확장자인지 확인합니다. `.dds`, `import.stamp` 같은 파일은 원본이 아닙니다. */
            static bool isSourceImage( string_view path ) { return FileUtil::hasAnyExtension( path, { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr" } ); }
        };

        /**
         * @class TextureRawImporter
         * @brief 텍스처 원본이 일괄 임포트에 제공하는 함수들입니다. 원본마다 임포트 설정에서 규칙을 고릅니다.
         * @details `importSource` 는 워커에서 불린다. 밴드 압축에 쓸 작업 시스템은 만든 쪽(`importAllTextures`, 호출 스레드)이 찾아 넘기고, 여기서는 에디터 서비스를 찾지 않는다.
         */
        class TextureRawImporter : public IRawAssetImporter
        {
        public:
            TextureRawImporter( const TextureImportConfig& config, TaskManager* pTaskManager )
                : _config{ config }
                , _pTaskManager{ pTaskManager }
            {
            }

            string_view getRawFolderName() const override { return TextureImporterInternal::kRawTextureFolder; }
            string_view getStampHeader() const override { return TextureImporterInternal::kImportStampHeader; }
            string_view getImportedLabel() const override { return "DDS"; }
            bool        isSourceFile( string_view path ) const override { return TextureImporterInternal::isSourceImage( path ); }
            string      makeImportedPath( string_view sourcePath ) const override { return TextureImporter::makeImportedTexturePath( sourcePath ); }
            uint64      computeSourceHash( string_view sourcePath, string_view assetPath ) const override
            {
                TextureImportRule rule;
                if ( findRule( assetPath, rule ) == false )
                    return 0; // 규칙이 겹치는 원본은 해시를 만들지 않는다(0 = 읽지 못함). 어긋남으로 잡히고 임포트도 같은 오류로 멈춘다
                return TextureImporter::computeSourceHash( sourcePath, rule );
            }
            const utf8* findUnsupportedReason( string_view sourcePath ) const override
            {
                (void)sourcePath;
                return nullptr;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view assetPath ) const override
            {
                TextureImportRule rule;
                if ( findRule( assetPath, rule ) == false )
                    return false;
                return TextureImporter::importTexture( sourcePath, importedPath, rule, nullptr, _pTaskManager );
            }

        protected:
            /** @brief 리소스 루트 기준 경로에 맞는 규칙을 @p outRule 에 담습니다. 규칙이 겹치면 오류를 남기고 false 입니다. 맞는 규칙이 없으면 기본 규칙이고 true 입니다. */
            [[nodiscard]] bool findRule( string_view assetPath, TextureImportRule& outRule ) const
            {
                TextureImportRuleMatch match = _config.findMatchingRule( assetPath );
                if ( match.hasError() )
                {
                    SW_LOG_ERROR_ID( "TextureImporter.ImportRuleRejected", "%#", match._error.c_str() );
                    return false;
                }
                outRule = std::move( match._rule );
                return true;
            }

        protected:
            const TextureImportConfig& _config;
            TaskManager* const         _pTaskManager; ///< 밴드 압축을 나눠 돌릴 작업 시스템(빌림). nullptr 이면 호출한 스레드가 차례로 압축한다
        };

        /**
         * @class TextureVariantRawImporter
         * @brief 형식 묶음 하나의 쿠킹 변형을 일괄 절차(`AssetImportStampUtil`)에 태웁니다. 결과는 임포트한 DDS 곁(`a.astc.dds`)이고 스탬프는 묶음마다 따로입니다.
         */
        class TextureVariantRawImporter final : public TextureRawImporter
        {
        public:
            TextureVariantRawImporter( const TextureImportConfig& config, TaskManager* pTaskManager, const TextureFormatFamily& family )
                : TextureRawImporter{ config, pTaskManager }
                , _family{ family }
                , _stampFileName{ "import" + family._suffix + ".stamp" }
            {
            }

            string_view getStampHeader() const override { return "SWTEXVARIANT 1"; }
            string_view getStampFileName() const override { return _stampFileName; }
            string_view getImportedLabel() const override { return "variant DDS"; }
            string      makeImportedPath( string_view sourcePath ) const override { return TextureFormatFamily::makeVariantPath( TextureImporter::makeImportedTexturePath( sourcePath ), _family._suffix ); }
            uint64      computeSourceHash( string_view sourcePath, string_view assetPath ) const override
            {
                const uint64 sourceHash = TextureRawImporter::computeSourceHash( sourcePath, assetPath );
                if ( sourceHash == 0 )
                    return 0;
                // 인코더 판과 그 묶음의 형식이 결과를 정한다 — 표를 고치거나 인코더를 바꾸면 다시 굽는다.
                uint64 hash = HashUtil::combine( sourceHash, kVariantEncoderVersion );
                for ( const RHIFormat format : { _family._color, _family._normal, _family._mask, _family._hdr } )
                {
                    hash = HashUtil::combine( hash, static_cast<uint64>( format ) );
                }
                return hash;
            }
            [[nodiscard]] bool importSource( string_view sourcePath, string_view importedPath, string_view assetPath ) const override
            {
                TextureImportRule rule;
                if ( findRule( assetPath, rule ) == false )
                    return false;
                return TextureImporter::cookTextureVariant( sourcePath, importedPath, rule, _family, _pTaskManager );
            }

        private:
            /** @brief `TextureBlockEncoder` 의 판입니다. 같은 입력에서 다른 바이트가 나오게 바꾸면 올립니다. */
            static constexpr uint64 kVariantEncoderVersion = 1;

            const TextureFormatFamily& _family;
            const string               _stampFileName;
        };

        /** @brief 변형 DDS 머리에 적을 형식 번호입니다(`DDSFormat`). 인코더가 만드는 형식만 받습니다. */
        [[nodiscard]] uint32 variantDXGIFormatInternal( RHIFormat format )
        {
            if ( format == RHIFormat::ASTC_4x4_UNORM )
                return DDSFormat::kDXGIASTC4x4Unorm;
            if ( format == RHIFormat::ETC2_RGBA8_UNORM )
                return DDSFormat::kEngineDXGIETC2RGBA8;
            return format == RHIFormat::EAC_R11_UNORM ? DDSFormat::kEngineDXGIEACR11 : DDSFormat::kEngineDXGIEACRG11;
        }
    } // namespace

    bool TextureImporter::cookTextureVariant( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, const TextureFormatFamily& family, TaskManager* pTaskManager )
    {
        const TextureFormatUsage usage = TextureImportFormatUtil::getUsage( rule._format );
        if ( usage == TextureFormatUsage::Raw )
            return importTexture( sourcePath, outputPath, rule, nullptr, pTaskManager ); // 어느 장치나 읽는다 — 묶음의 파일 이름으로 그대로 둔다
        const RHIFormat format = family.getFormat( usage );
        if ( usage == TextureFormatUsage::HDR )
        {
            if ( format != RHIFormat::R16G16B16A16_FLOAT )
            {
                SW_LOG_ERROR_ID( "TextureImporter.VariantHDRFormat", "Texture format family '%#' needs R16G16B16A16_FLOAT for HDR sources (no HDR encoder for the others): %#", family._name, sourcePath );
                return false;
            }
            TextureImportRule hdrRule = rule;
            hdrRule._format           = TextureImportFormat::R16G16B16A16Float;
            return importTexture( sourcePath, outputPath, hdrRule, nullptr, pTaskManager );
        }
        if ( TextureBlockEncoder::isEncodable( format ) == false )
        {
            SW_LOG_ERROR_ID( "TextureImporter.VariantNoEncoder", "Texture format family '%#' names a format the cooker cannot encode (RHIFormat %#): %#", family._name, static_cast<uint32>( format ), sourcePath );
            return false;
        }

        RawImageData rawImage;
        if ( RawImageData::loadImage( sourcePath, rawImage ) == false || rawImage.isValid() == false )
        {
            SW_LOG_ERROR_ID( "TextureImporter.VariantDecodeFailed", "Failed to decode source image: %#", sourcePath );
            return false;
        }
        // 인코더는 RGBA 순서를 읽는다 — 규칙의 BGRA 는 BC 파일의 바이트 배치라 여기서는 뜻이 없다.
        TextureImportRule variantRule = rule;
        if ( variantRule._swizzle == TextureSwizzle::BGRA || variantRule._swizzle == TextureSwizzle::ARGB )
            variantRule._swizzle = TextureSwizzle::RGBA;
        applyChannelManipulations( rawImage, variantRule, static_cast<size_t>( rawImage._width ) * static_cast<size_t>( rawImage._height ) );

        DirectX::Image baseImage{};
        baseImage.width      = static_cast<size_t>( rawImage._width );
        baseImage.height     = static_cast<size_t>( rawImage._height );
        baseImage.format     = rule._bSRGB == SW_TRUE ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM; // 밉 필터가 이 이름을 보고 선형 공간에서 섞는다(`importTexture` 와 같다)
        baseImage.rowPitch   = static_cast<size_t>( rawImage._width ) * RawImageData::kChannelCount;
        baseImage.slicePitch = baseImage.rowPitch * static_cast<size_t>( rawImage._height );
        baseImage.pixels     = rawImage._bytes.data();
        DirectX::ScratchImage mipChain;
        if ( buildMipChainInternal( baseImage, rule._bGenerateMips == SW_TRUE, true, sourcePath, mipChain ) == false )
            return false;

        const DirectX::TexMetadata& metadata = mipChain.GetMetadata();
        vector<uint8>               payload;
        for ( size_t mip = 0; mip < metadata.mipLevels; ++mip )
        {
            const DirectX::Image* pImage = mipChain.GetImage( mip, 0, 0 );
            if ( pImage == nullptr || TextureBlockEncoder::encodeMip( format, pImage->pixels, static_cast<uint32>( pImage->width ), static_cast<uint32>( pImage->height ), payload ) == false )
                return false;
        }
        if ( FileUtil::isWriteAllowed( outputPath ) == false || FileUtil::ensureParentDirectoryExists( outputPath ) == false )
            return false;
        const uint32 width    = static_cast<uint32>( metadata.width );
        const uint32 rowBytes = ( width + 3 ) / 4 * getRHIFormatBlockInfo( format )._blockBytes;
        return ImageFileWriter::writeDDS( outputPath, payload, width, static_cast<uint32>( metadata.height ), static_cast<uint32>( metadata.mipLevels ), variantDXGIFormatInternal( format ), rowBytes );
    }

    AssetImportSummary TextureImporter::cookTextureVariants( string_view resourceRoot, const TextureImportConfig& config, const TextureFormatFamily& family, AssetImportMode mode )
    {
        TaskManager* const              pTaskManager = editor::findSubsystem<TaskManager>(); // 선택: 없으면 이 스레드가 직렬로 굽는다(`importAllTextures` 와 같다)
        const TextureVariantRawImporter importer{ config, pTaskManager, family };
        return AssetImportStampUtil::importAll( resourceRoot, importer, mode, pTaskManager );
    }

    bool TextureImporter::importTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureImportResult* pOutResult, TaskManager* pTaskManager )
    {
        if ( pOutResult != nullptr )
        {
            pOutResult->_sourcePath = string( sourcePath );
            pOutResult->_outputPath = string( outputPath );
            pOutResult->_bSuccess   = SW_FALSE;
            (void)FileUtil::tryGetFileSize( sourcePath, pOutResult->_sourceSizeBytes ); // 실패 보고다 - 원본이 없으면 크기 칸은 처음 값(0) 그대로
        }

        // `.hdr`(Radiance RGBE)은 1 을 넘는 값을 가진다. 8비트 디코더(stb)를 거치지 않고 부동소수점으로 읽어 HDR 포맷으로만 임포트한다.
        if ( FileUtil::hasExtension( sourcePath, ".hdr" ) )
            return importHDRTextureInternal( sourcePath, outputPath, rule, pTaskManager, pOutResult );

        // 1) RawImageData 로 소스 이미지를 디코딩한다
        RawImageData rawImage;
        if ( RawImageData::loadImage( sourcePath, rawImage ) == false || rawImage.isValid() == false )
        {
            SW_LOG_ERROR_ID( "TextureImporter.FailedDecodeSourceImage", "Failed to decode source image: %#", sourcePath );
            return false;
        }

        const size_t totalPixels = static_cast<size_t>( rawImage._width ) * static_cast<size_t>( rawImage._height );

        applyChannelManipulations( rawImage, rule, totalPixels );

        const DXGI_FORMAT targetFormat = resolveFormatInternal( rule._format, rule._bSRGB == SW_TRUE );

        // 2) DirectXTex 기본 Image 구성
        DirectX::Image baseImage{};
        baseImage.width  = static_cast<size_t>( rawImage._width );
        baseImage.height = static_cast<size_t>( rawImage._height );
        // 채널 섞기와 같은 테이블에서 가져온다. 둘이 어긋날 여지를 아예 없앤다.
        // 원본 이미지 바이트는 이미 sRGB 로 인코딩되어 있다. 결과가 sRGB 형식이면 바이트에 이름만 sRGB 를 붙인다. UNORM 으로 두면
        // 아래의 변환과 압축이 선형에서 sRGB 로 가는 곡선을 한 번 더 적용한다. 밉 필터도 이 이름을 보고 선형 공간에서 섞는다.
        const DXGI_FORMAT layoutFormat = swizzleLayoutInternal( rule._swizzle )._format;
        baseImage.format               = DirectX::IsSRGB( targetFormat ) ? DirectX::MakeSRGB( layoutFormat ) : layoutFormat;
        baseImage.rowPitch             = static_cast<size_t>( rawImage._width ) * RawImageData::kChannelCount;
        baseImage.slicePitch           = baseImage.rowPitch * static_cast<size_t>( rawImage._height );
        baseImage.pixels               = rawImage._bytes.data();

        // 3) 밉맵(켜져 있으면)과 4) 압축 또는 포맷 변환
        DirectX::ScratchImage finalImage;
        if ( encodeTextureMipsInternal( baseImage, rule._bGenerateMips == SW_TRUE, targetFormat, compressFlagsInternal( rule ), sourcePath, pTaskManager, finalImage ) == false )
            return false;

        // 5) DDS 파일로 저장한다
        return saveImportedDDSInternal( finalImage, static_cast<uint32>( rawImage._sourceChannelCount ), sourcePath, outputPath, pOutResult );
    }

    void TextureImporter::applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels )
    {
        if ( rawImage._bytes.size() < totalPixels * 4 )
            return;

        uint8*               pData  = rawImage._bytes.data();
        const SwizzleLayout& layout = swizzleLayoutInternal( rule._swizzle );

        // 그린 반전을 먼저 한다. 입력은 항상 RGBA 이므로 이 시점의 초록 채널 위치는 1번으로 정해져 있다. 섞은 뒤에 뒤집으려면
        // "결과의 1번이 초록" 이라는 가정이 필요한데, 이는 현재 스위즐에서 우연히 맞을 뿐이고 새 스위즐을 추가하는 순간 조용히 틀린다.
        if ( rule._bInvertGreen == SW_TRUE )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                pData[index * 4 + 1] = static_cast<uint8>( 255 - pData[index * 4 + 1] );
            }
        }

        for ( size_t index = 0; index < totalPixels; ++index )
        {
            uint8* pPixel = pData + index * 4;

            const uint8 arrSource[4] = { pPixel[0], pPixel[1], pPixel[2], pPixel[3] };
            for ( size_t channel = 0; channel < 4; ++channel )
            {
                pPixel[channel] = arrSource[layout._arrSourceChannel[channel]];
            }

            if ( layout._bForceOpaqueAlpha != SW_FALSE )
                pPixel[3] = 255;
        }
    }

    bool TextureImporter::importChangedSourceImage( const AssetPaths& assetPaths, string_view relativePath )
    {
        if ( FileUtil::hasExtension( relativePath, ".dds" ) )
            return false;

        const string& resourceRoot = assetPaths.getRootFolderPath(); // 워커다 — 잡을 올린 에디터 스레드가 넘긴 경로 상태를 쓴다
        const string  normalized   = FileUtil::normalizeSeparators( FileUtil::joinPath( resourceRoot, relativePath ) );
        if ( makeImportedTexturePath( normalized ).empty() )
        {
            SW_LOG_WARNING_ID( "TextureImporter.SourceImagesMustUnder", "Source images must be under `%#` to be imported: %#", TextureImporterInternal::kRawTextureFolder, relativePath );
            return true;
        }

        // 바뀐 원본 하나만 임포트한다(루트 전체를 훑지 않는다). 스탬프를 기록하는 길은 `importAllTextures` 와 같은 `AssetImportStampUtil` 한 곳이라
        // 에디터에서 임포트한 것과 `App --import-textures` 로 임포트한 것이 같은 판정을 받는다. 내용이 그대로이면(저장만 다시 했다) 임포트하지 않는다.
        // 설정 파일이 없으면 기본 규칙을 쓴다. 깨졌으면 기본 규칙으로 임포트하지 않는다(결과가 말없이 바뀐다) — 로드가 오류를 남겼다.
        TextureImportConfig config{};
        if ( config.loadFromFile( makeDefaultImportConfigPath() ) == ConfigReadResult::Invalid )
            return true;
        // 핫 리로드의 임포트 잡이 워커에서 부르므로 작업 시스템을 찾지 않는다(워커는 에디터 서비스 범위를 읽지 않는다) — 밴드 압축도 이 워커가 차례로 한다.
        const TextureRawImporter importer{ config, nullptr };
        const AssetImportSummary summary = AssetImportStampUtil::importSources( resourceRoot, importer, vector<string>{ string( relativePath ) }, nullptr );
        for ( const string& problem : summary._listProblem )
        {
            SW_LOG_ERROR_ID( "TextureImporter.TextureImportFailed", "Texture import failed: %#", problem.c_str() );
        }

        SW_LOG_INFO_ID( "TextureImporter.TextureImportChangedImported", "Texture import: %# changed -> %# imported", relativePath, summary._importedCount );
        return true;
    }

    AssetImportSummary TextureImporter::importAllTextures( string_view resourceRoot, const TextureImportConfig& config, AssetImportMode mode )
    {
        // 작업 시스템은 여기(호출 스레드)서 한 번 찾아 값으로 넘긴다 — 임포트는 워커에서 도는데 워커는 에디터 서비스 범위를 읽지 않는다(`findEditorScope`).
        TaskManager* const       pTaskManager = editor::findSubsystem<TaskManager>(); // 선택: 없으면 이 스레드가 직렬로 임포트한다(계약)
        const TextureRawImporter importer{ config, pTaskManager };
        AssetImportSummary       summary = AssetImportStampUtil::importAll( resourceRoot, importer, mode, pTaskManager );
        // 공유 원본이 쓰는 규칙은 패턴에 맞는 파일이 없어도(모델 옆 자리로 골랐다) 살아 있다.
        const vector<string> listSourcePath = AssetImportStampUtil::collectSourceAssetPaths( resourceRoot, importer );
        // 원본마다 공유 규칙 이름을 한 번만 구한다 — 규칙마다 원본 전체를 훑으면 규칙 수 × 원본 수다.
        unordered_set<string> uniqueSharedRuleName;
        for ( const string& path : listSourcePath )
        {
            uniqueSharedRuleName.insert( TextureImportConfig::findSharedRuleName( path ) );
        }
        vector<TextureImportRule> listCheckedRule;
        for ( const TextureImportRule& rule : config.getRules() )
        {
            const bool bShared = uniqueSharedRuleName.contains( StringUtil::toLower( rule._name.c_str() ) );
            if ( bShared == false )
                listCheckedRule.push_back( rule );
        }
        for ( const string& rule : AssetImportRuleUtil::findRulesMatchingNothing( listCheckedRule, listSourcePath ) )
        {
            summary._listProblem.push_back( "TextureImportConfig: rule " + rule + " matches no texture source - its patterns are dead (a moved folder?)" );
        }
        return summary;
    }

    string TextureImporter::makeImportedTexturePath( string_view rawTexturePath )
    {
        return AssetImportStampUtil::makeImportedPath( rawTexturePath, TextureImporterInternal::kRawTextureFolder, TextureImporterInternal::kImportedTextureFolder,
                                                       ".dds" );
    }

    uint64 TextureImporter::computeSourceHash( string_view sourcePath, const TextureImportRule& rule )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( sourcePath, bytes ) == false || bytes.empty() )
            return 0;

        // 규칙은 해석한 결과로 섞는다. "bc7" 과 "BC7_UNORM", BGRA 와 ARGB 처럼 같은 결과를 내는 표기는 같은 해시가 된다.
        const DXGI_FORMAT                        resolvedFormat = resolveFormatInternal( rule._format, rule._bSRGB == SW_TRUE );
        const SwizzleLayout&                     layout         = swizzleLayoutInternal( rule._swizzle );
        StringBuilder<constant::buffer::kMax256> ruleText;
        ruleText.appendFormat( "importer=%#;format=%#;channel=%#%#%#%#;opaque=%#;mips=%#;srgb=%#;invertGreen=%#",
                               TextureImporterInternal::kImporterVersion,
                               static_cast<uint32>( resolvedFormat ),
                               static_cast<uint32>( layout._arrSourceChannel[0] ), static_cast<uint32>( layout._arrSourceChannel[1] ),
                               static_cast<uint32>( layout._arrSourceChannel[2] ), static_cast<uint32>( layout._arrSourceChannel[3] ),
                               static_cast<uint32>( layout._bForceOpaqueAlpha ), static_cast<uint32>( rule._bGenerateMips ), static_cast<uint32>( rule._bSRGB ),
                               static_cast<uint32>( rule._bInvertGreen ) );
        if ( rule._bQuickCompress == SW_TRUE )
            ruleText.append( ";quick=1" ); // 끈 값(기본)은 섞지 않는다 — 이 칸이 생기기 전의 스탬프가 그대로 맞는다

        const uint64 contentHash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
        return HashUtil::computeStringHash64( ruleText.c_str(), ruleText.size(), false, contentHash );
    }

    string TextureImporter::makeDefaultImportConfigPath()
    {
        return FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._project.str(), config::kDirConfigEditor ), EditorPaths::kTextureImportConfigFileName );
    }
} // namespace sw::editor

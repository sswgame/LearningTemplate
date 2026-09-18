#include "pch.h"

#include "Editor/Common/Asset/TextureBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureImportConfig.h"

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
        DXGI_FORMAT resolveFormatInternal( string_view formatStr, bool bSrgb )
        {
            if ( formatStr == "BC1_UNORM" || formatStr == "bc1" )
                return bSrgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
            if ( formatStr == "BC2_UNORM" || formatStr == "bc2" )
                return bSrgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
            if ( formatStr == "BC3_UNORM" || formatStr == "bc3" )
                return bSrgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
            if ( formatStr == "BC4_UNORM" || formatStr == "bc4" )
                return DXGI_FORMAT_BC4_UNORM;
            if ( formatStr == "BC5_UNORM" || formatStr == "bc5" )
                return DXGI_FORMAT_BC5_UNORM;
            if ( formatStr == "BC6H_UF16" || formatStr == "bc6h" )
                return DXGI_FORMAT_BC6H_UF16;
            if ( formatStr == "BC7_UNORM" || formatStr == "bc7" )
                return bSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
            if ( formatStr == "B8G8R8A8_UNORM" || formatStr == "bgra8" )
                return bSrgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
            if ( formatStr == "R8G8B8A8_UNORM" || formatStr == "rgba8" )
                return bSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

            return bSrgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
        }

        /**
         * @struct SwizzleLayoutInternal
         * @brief 스위즐 하나가 정하는 것 — **바이트를 어떻게 놓는가와 그것을 무슨 이름으로 부르는가.**
         * @details 이 둘이 따로 있으면 반드시 어긋난다. 예전에는 섞기가
         *          `applyChannelManipulations` 의 if 사슬에, 이름이 `bakeTexture` 의
         *          `_swizzle == BGRA ? BGRA : RGBA` 삼항에 있었다 — 그래서 `ARGB` 는 섞기 쪽만
         *          알고 이름 쪽은 몰랐다. 바이트는 옮겨졌는데 결과물에는 "RGBA 다" 라고 적혀
         *          나가서 색이 깨졌다. 스위즐을 하나 더하려면 이제 **이 표에 줄 하나**다.
         */
        struct SwizzleLayoutInternal
        {
            /** @brief 결과의 n 번째 바이트를 원본(언제나 RGBA)의 몇 번째에서 가져오는가. */
            uint8 _arrSourceChannel[4];
            /** @brief 알파를 255 로 덮는가 (RGB1). */
            uint8 _bForceOpaqueAlpha;
            /** @brief 그렇게 놓인 바이트 배열을 가리키는 DXGI 이름. */
            DXGI_FORMAT _format;
        };

        const SwizzleLayoutInternal& swizzleLayoutInternal( TextureSwizzle swizzle )
        {
            // **BGRA 와 ARGB 는 같은 것이다.** D3D9 의 `D3DFMT_A8R8G8B8` 은 메모리에서 B,G,R,A 이고
            // DXGI 는 그것을 `B8G8R8A8` 이라 부른다 — 열거형 주석의 "레거시 ARGB" 가 그 뜻이다.
            // 예전 코드는 ARGB 를 왼쪽으로 한 칸 돌려 RGBA 를 GBAR 로 만들었는데, 그것은 어떤
            // 읽기로도 ARGB 가 아니다.
            static constexpr SwizzleLayoutInternal kRgba{
                { 0, 1, 2, 3 },
                SW_FALSE,
                DXGI_FORMAT_R8G8B8A8_UNORM
            };
            static constexpr SwizzleLayoutInternal kBgra{
                { 2, 1, 0, 3 },
                SW_FALSE,
                DXGI_FORMAT_B8G8R8A8_UNORM
            };
            static constexpr SwizzleLayoutInternal kRgb1{
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
                    return kRgb1;
                case TextureSwizzle::RGBA:
                default:
                    return kRgba;
            }
        }
    } // namespace

    SW_LOG_CALLER( "TextureBaker" );

    bool TextureBaker::bakeTexture( string_view sourcePath, string_view outputPath, const TextureImportRule& rule, TextureBakeResult* pOutResult )
    {
        if ( pOutResult != nullptr )
        {
            pOutResult->_sourcePath      = string( sourcePath );
            pOutResult->_outputPath      = string( outputPath );
            pOutResult->_bSuccess        = SW_FALSE;
            pOutResult->_sourceSizeBytes = FileUtil::getFileSize( sourcePath );
        }

        // 1) Decode source image with ImageUtil
        RawImageData rawImage;
        if ( ImageUtil::loadImage( sourcePath, rawImage ) == false || rawImage.isValid() == false )
        {
            SW_LOG_ERROR( "Failed to decode source image: %#", sourcePath.data() );
            return false;
        }

        const size_t totalPixels = static_cast<size_t>( rawImage._width ) * static_cast<size_t>( rawImage._height );

        applyChannelManipulations( rawImage, rule, totalPixels );

        // 4) Build DirectXTex base Image
        DirectX::Image baseImage{};
        baseImage.width  = static_cast<size_t>( rawImage._width );
        baseImage.height = static_cast<size_t>( rawImage._height );
        // 섞기와 같은 표에서 가져온다 — 둘이 어긋날 수 있는 자리를 아예 없앤다.
        baseImage.format     = swizzleLayoutInternal( rule._swizzle )._format;
        baseImage.rowPitch   = static_cast<size_t>( rawImage._width ) * 4;
        baseImage.slicePitch = baseImage.rowPitch * static_cast<size_t>( rawImage._height );
        baseImage.pixels     = rawImage._bytes.data();

        // 5) Generate Mipmaps if enabled
        DirectX::ScratchImage mipChain;
        if ( rule._bGenerateMips == SW_TRUE )
        {
            const HRESULT hr = DirectX::GenerateMipMaps( baseImage, DirectX::TEX_FILTER_DEFAULT, 0, mipChain );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::GenerateMipMaps failed (hr=0x%#) for %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    sourcePath.data() );
                return false;
            }
        }
        else
        {
            const HRESULT hr = mipChain.InitializeFromImage( baseImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::InitializeFromImage failed (hr=0x%#) for %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    sourcePath.data() );
                return false;
            }
        }

        const uint32 mipCount = static_cast<uint32>( mipChain.GetMetadata().mipLevels );

        // 6) Compress or Convert Format
        const DXGI_FORMAT     targetFormat = resolveFormatInternal( rule._format, rule._bSrgb == SW_TRUE );
        DirectX::ScratchImage finalImage;

        if ( DirectX::IsCompressed( targetFormat ) )
        {
            const HRESULT hr = DirectX::Compress(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                targetFormat,
                DirectX::TEX_COMPRESS_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                finalImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::Compress failed (hr=0x%#) for format %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    static_cast<uint32>( targetFormat ) );
                return false;
            }
        }
        else if ( targetFormat != mipChain.GetMetadata().format )
        {
            const HRESULT hr = DirectX::Convert(
                mipChain.GetImages(),
                mipChain.GetImageCount(),
                mipChain.GetMetadata(),
                targetFormat,
                DirectX::TEX_FILTER_DEFAULT,
                DirectX::TEX_THRESHOLD_DEFAULT,
                finalImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR(
                    "DirectX::Convert failed (hr=0x%#) for format %#",
                    Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ),
                    static_cast<uint32>( targetFormat ) );
                return false;
            }
        }
        else
        {
            finalImage = std::move( mipChain );
        }

        // 7) Save DDS to output file
        const string outputDir = FileUtil::getDirectoryPart( outputPath );
        if ( outputDir.empty() == false )
            FileUtil::ensureDirectoryExists( outputDir );

        const wstring wOutPath = StringUtil::utf8ToUtf16( string( outputPath ).c_str() );

        const HRESULT hrSave = DirectX::SaveToDDSFile(
            finalImage.GetImages(),
            finalImage.GetImageCount(),
            finalImage.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            wOutPath.c_str() );

        if ( FAILED( hrSave ) )
        {
            SW_LOG_ERROR(
                "DirectX::SaveToDDSFile failed (hr=0x%#) for %#",
                Fmt( static_cast<uint32>( hrSave ), Format( 8, Format::Padding::Zero ).hex() ),
                outputPath.data() );
            return false;
        }

        const uint64 outputSizeBytes = FileUtil::getFileSize( outputPath );
        if ( pOutResult != nullptr )
        {
            pOutResult->_width           = static_cast<uint32>( rawImage._width );
            pOutResult->_height          = static_cast<uint32>( rawImage._height );
            pOutResult->_mipCount        = mipCount;
            pOutResult->_outputSizeBytes = outputSizeBytes;
            pOutResult->_bSuccess        = SW_TRUE;
        }

        SW_LOG_INFO( "Baked texture: %# -> %# (Format: %d, Mips: %u, %llu -> %llu bytes)",
                     sourcePath.data(), outputPath.data(), targetFormat, mipCount,
                     FileUtil::getFileSize( sourcePath ), outputSizeBytes );

        return true;
    }

    void TextureBaker::applyChannelManipulations( RawImageData& rawImage, const TextureImportRule& rule, size_t totalPixels )
    {
        if ( rawImage._bytes.size() < totalPixels * 4 )
            return;

        uint8*                       pData  = rawImage._bytes.data();
        const SwizzleLayoutInternal& layout = swizzleLayoutInternal( rule._swizzle );

        // **그린 반전이 먼저다.** 입력은 언제나 RGBA 이므로 이 시점의 초록 자리는 1 로 확정이다.
        // 섞은 뒤에 뒤집으면 "결과의 1번이 초록" 이라는 가정이 필요한데, 그것은 지금 스위즐들에서
        // 우연히 맞을 뿐 새 스위즐을 더하는 순간 조용히 틀린다.
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

    bool TextureBaker::bakeTextureWithConfig( string_view sourcePath, string_view outputPath, const TextureImportConfig& config, TextureBakeResult* pOutResult )
    {
        TextureImportRule rule;
        if ( config.findMatchingRule( sourcePath, rule ) == false )
            SW_LOG_WARNING( "No matching rule found in config for %#; using default rule.", sourcePath.data() );

        return bakeTexture( sourcePath, outputPath, rule, pOutResult );
    }
} // namespace sw::editor

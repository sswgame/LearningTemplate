#include "pch.h"

#include "Editor/Common/Asset/TextureBaker.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/ImageUtil.h"

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
        uint8*       pData       = rawImage._bytes.data();

        // 2) Apply Swizzle & Channel Manipulations
        if ( rule._swizzle == TextureSwizzle::BGRA )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                std::swap( pData[index * 4], pData[index * 4 + 2] );
            }
        }
        else if ( rule._swizzle == TextureSwizzle::ARGB )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                const uint8 a        = pData[index * 4];
                pData[index * 4]     = pData[index * 4 + 1];
                pData[index * 4 + 1] = pData[index * 4 + 2];
                pData[index * 4 + 2] = pData[index * 4 + 3];
                pData[index * 4 + 3] = a;
            }
        }
        else if ( rule._swizzle == TextureSwizzle::RGB1 )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                pData[index * 4 + 3] = 255;
            }
        }

        // 3) Normal map green channel invert
        if ( rule._bInvertGreen == SW_TRUE )
        {
            for ( size_t index = 0; index < totalPixels; ++index )
            {
                pData[index * 4 + 1] = 255 - pData[index * 4 + 1];
            }
        }

        // 4) Build DirectXTex base Image
        DirectX::Image baseImage{};
        baseImage.width      = static_cast<size_t>( rawImage._width );
        baseImage.height     = static_cast<size_t>( rawImage._height );
        baseImage.format     = ( rule._swizzle == TextureSwizzle::BGRA ) ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
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
                SW_LOG_ERROR( "DirectX::GenerateMipMaps failed (hr=0x%08X) for %#", hr, sourcePath.data() );
                return false;
            }
        }
        else
        {
            const HRESULT hr = mipChain.InitializeFromImage( baseImage );
            if ( FAILED( hr ) )
            {
                SW_LOG_ERROR( "DirectX::InitializeFromImage failed (hr=0x%08X) for %#", hr, sourcePath.data() );
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
                SW_LOG_ERROR( "DirectX::Compress failed (hr=0x%08X) for format %d", hr, targetFormat );
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
                SW_LOG_ERROR( "DirectX::Convert failed (hr=0x%08X) for format %d", hr, targetFormat );
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
        {
            FileUtil::ensureDirectoryExists( outputDir );
        }

        const wstring wOutPath = StringUtil::utf8ToUtf16( string( outputPath ).c_str() );

        const HRESULT hrSave = DirectX::SaveToDDSFile(
            finalImage.GetImages(),
            finalImage.GetImageCount(),
            finalImage.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            wOutPath.c_str() );

        if ( FAILED( hrSave ) )
        {
            SW_LOG_ERROR( "DirectX::SaveToDDSFile failed (hr=0x%08X) for %#", hrSave, outputPath.data() );
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

    bool TextureBaker::bakeTextureWithConfig( string_view sourcePath, string_view outputPath, const TextureImportConfig& config, TextureBakeResult* pOutResult )
    {
        TextureImportRule rule;
        if ( config.matchRule( sourcePath, rule ) == false )
        {
            SW_LOG_WARNING( "No matching rule found in config for %#; using default rule.", sourcePath.data() );
        }

        return bakeTexture( sourcePath, outputPath, rule, pOutResult );
    }
} // namespace sw::editor

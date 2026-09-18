#include "pch.h"

#include "Editor/Common/Asset/ImageUtil.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace sw::editor
{
    SW_LOG_CALLER( "ImageUtil" );

    bool ImageUtil::loadImage( string_view filePath, RawImageData& outImage )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( filePath, bytes ) == false || bytes.empty() )
        {
            SW_LOG_ERROR( "Failed to read image file: %#", filePath.data() );
            return false;
        }

        return loadImageFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool ImageUtil::loadImageFromMemory( const uint8* pBuffer, size_t bufferSize, RawImageData& outImage )
    {
        // 실패는 출력에 아무것도 남기지 않는다 — `DdsLoader::loadFromMemory` 와 같은 약속이다.
        outImage = RawImageData{};

        if ( pBuffer == nullptr || bufferSize == 0 )
        {
            SW_LOG_ERROR( "Image buffer is null or empty." );
            return false;
        }

        // stb 는 길이를 `int` 로 받는다. 잘라서 넘기면 **뒷부분이 없는 것처럼** 읽혀 디코딩이
        // 엉뚱하게 성공하거나 실패한다 — 넘기기 전에 거절한다.
        if ( bufferSize > static_cast<size_t>( MathUtil::MaxInt32 ) )
        {
            SW_LOG_ERROR( "Image buffer is larger than stb_image can address (%# bytes).", bufferSize );
            return false;
        }

        int32 width    = 0;
        int32 height   = 0;
        int32 channels = 0;

        uint8* pDecoded = stbi_load_from_memory(
            pBuffer,
            static_cast<int32>( bufferSize ),
            &width,
            &height,
            &channels,
            4 );

        if ( pDecoded == nullptr )
        {
            SW_LOG_ERROR( "stbi_load_from_memory failed: %#", stbi_failure_reason() );
            return false;
        }

        const size_t totalBytes = static_cast<size_t>( width ) * static_cast<size_t>( height ) * 4;
        outImage._bytes.resize( totalBytes );
        Memory::copy( outImage._bytes.data(), pDecoded, totalBytes );
        outImage._width    = width;
        outImage._height   = height;
        outImage._channels = 4;

        stbi_image_free( pDecoded );
        return true;
    }
} // namespace sw::editor

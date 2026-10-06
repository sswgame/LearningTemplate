#include "pch.h"

#include "Editor/Common/Asset/ImageUtil.h"

#include "Core/Common/StdHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw::editor
{
    namespace
    {
        /** @brief stb_image 의 할당 훅(`STBI_MALLOC` · `STBI_REALLOC_SIZED` · `STBI_FREE`)입니다. 디코드 버퍼가 sw 할당자를 지나 메모리 태그에 세입니다. */
        struct ImageUtilInternal
        {
            static void* allocate( size_t size ) { return Memory::allocate( size ); }

            /** @brief 새 블록을 잡아 앞쪽 `min( oldSize, newSize )` 바이트를 옮기고 옛 블록을 풉니다. 실패하면 옛 블록을 그대로 두고 nullptr 입니다. */
            static void* reallocate( void* pOld, size_t oldSize, size_t newSize )
            {
                void* pNew = Memory::allocate( newSize );
                if ( pNew == nullptr )
                    return nullptr;
                if ( pOld != nullptr )
                {
                    Memory::copy( pNew, pOld, oldSize < newSize ? oldSize : newSize );
                    Memory::free( pOld );
                }
                return pNew;
            }

            static void free( void* pAddress ) { Memory::free( pAddress ); }
        };
    } // namespace
} // namespace sw::editor

#define STBI_MALLOC( size )                          sw::editor::ImageUtilInternal::allocate( size )
#define STBI_REALLOC_SIZED( pOld, oldSize, newSize ) sw::editor::ImageUtilInternal::reallocate( pOld, oldSize, newSize )
#define STBI_FREE( pAddress )                        sw::editor::ImageUtilInternal::free( pAddress )
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
            SW_LOG_ERROR( "Failed to read image file: %#", filePath );
            return false;
        }

        return loadImageFromMemory( bytes.data(), bytes.size(), outImage );
    }

    bool ImageUtil::loadImageFromMemory( const uint8* pBuffer, size_t bufferSize, RawImageData& outImage )
    {
        // 실패하면 출력에 아무것도 남기지 않는다. `DdsLoader::loadFromMemory` 와 같은 약속이다.
        outImage = RawImageData{};

        if ( pBuffer == nullptr || bufferSize == 0 )
        {
            SW_LOG_ERROR( "Image buffer is null or empty." );
            return false;
        }

        // stb 는 길이를 `int` 로 받는다. 잘라서 넘기면 **뒷부분이 없는 것처럼** 읽혀 디코딩이 엉뚱하게 성공하거나 실패하므로,
        // 넘기기 전에 거절한다.
        if ( bufferSize > static_cast<size_t>( MathUtil::kMaxInt32 ) )
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

    bool ImageUtil::loadGray16FromMemory( const uint8* pBuffer, size_t bufferSize, vector<uint16>& outListSample, int32& outWidth, int32& outHeight )
    {
        outListSample.clear();
        outWidth  = 0;
        outHeight = 0;
        if ( pBuffer == nullptr || bufferSize == 0 || bufferSize > static_cast<size_t>( MathUtil::kMaxInt32 ) )
        {
            SW_LOG_ERROR( "Image buffer is empty or larger than stb_image can address (%# bytes).", bufferSize );
            return false;
        }
        int32   width    = 0;
        int32   height   = 0;
        int32   channels = 0;
        uint16* pDecoded = stbi_load_16_from_memory( pBuffer, static_cast<int32>( bufferSize ), &width, &height, &channels, 1 );
        if ( pDecoded == nullptr )
        {
            SW_LOG_ERROR( "stbi_load_16_from_memory failed: %#", stbi_failure_reason() );
            return false;
        }
        outListSample.assign( pDecoded, pDecoded + static_cast<size_t>( width ) * static_cast<size_t>( height ) );
        outWidth  = width;
        outHeight = height;
        stbi_image_free( pDecoded );
        return true;
    }
} // namespace sw::editor

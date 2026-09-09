#include "pch.h"

#include "Core/File/PlatformFileUtil.h"

namespace sw
{
    FILE* PlatformFileUtil::openFile( const utf8* pFilePath, const utf8* pMode )
    {
        if ( pFilePath == nullptr || pMode == nullptr )
            return nullptr;

        FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        fopen_s( &pFile, pFilePath, pMode );
#else
        pFile = fopen( pFilePath, pMode );
#endif
        return pFile;
    }

    bool PlatformFileUtil::seekTo( FILE* pFile, int64 offset, int32 origin )
    {
        if ( pFile == nullptr )
            return false;

#if defined( SW_PLATFORM_WINDOWS )
        return _fseeki64( pFile, offset, origin ) == 0;
#else
        return fseeko( pFile, static_cast<off_t>( offset ), origin ) == 0;
#endif
    }

    int64 PlatformFileUtil::tellPosition( FILE* pFile )
    {
        if ( pFile == nullptr )
            return kInvalidOffset;

#if defined( SW_PLATFORM_WINDOWS )
        return _ftelli64( pFile );
#else
        return static_cast<int64>( ftello( pFile ) );
#endif
    }

    int64 PlatformFileUtil::getOpenFileSizeAndRewind( FILE* pFile )
    {
        if ( seekTo( pFile, 0, SEEK_END ) == false )
            return kInvalidOffset;

        const int64 size = tellPosition( pFile );

        // 크기를 읽었어도 되돌리지 못하면 호출부는 엉뚱한 위치에서 읽는다 — 실패로 본다.
        if ( seekTo( pFile, 0, SEEK_SET ) == false )
            return kInvalidOffset;

        return size;
    }
} // namespace sw

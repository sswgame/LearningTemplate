#include "pch.h"

#include "Core/File/PlatformFileUtil.h"

#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
#endif

namespace sw
{
    FILE* PlatformFileUtil::openFile( const utf8* pFilePath, const utf8* pMode )
    {
        if ( pFilePath == nullptr || pMode == nullptr )
            return nullptr;

        FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        // 모드는 ASCII 몇 글자다("rb" · "wb" · "a+" ...). 그대로 넓힌다.
        utf16  arrWideMode[constant::kMaxBuffer16]{};
        size_t modeLength = 0;
        while ( pMode[modeLength] != '\0' && modeLength + 1 < constant::kMaxBuffer16 )
        {
            arrWideMode[modeLength] = static_cast<utf16>( static_cast<uint8>( pMode[modeLength] ) );
            ++modeLength;
        }
        const wstring widePath = StringUtil::utf8ToUtf16( pFilePath );
        _wfopen_s( &pFile, widePath.c_str(), arrWideMode );
#else
        pFile = fopen( pFilePath, pMode );
#endif
        return pFile;
    }

    bool PlatformFileUtil::replaceFile( const utf8* pSourcePath, const utf8* pTargetPath )
    {
        if ( pSourcePath == nullptr || pTargetPath == nullptr )
            return false;

#if defined( SW_PLATFORM_WINDOWS )
        const wstring wideSource = StringUtil::utf8ToUtf16( pSourcePath );
        const wstring wideTarget = StringUtil::utf8ToUtf16( pTargetPath );

        // 1) POSIX 방식 이름 바꾸기(Windows 10 1709+, NTFS). **대상 파일을 누가 열고 있어도** 바꿔 끼운다 — 연 쪽은 옛 파일을 끝까지 읽는다.
        //    `MoveFileExW` 는 대상을 연 핸들이 하나라도 있으면(삭제 공유로 열었어도) 실패해서, 에디터 · 파일 감시가 자주 읽는 파일은 저장이
        //    계속 실패했다(동시 읽기 테스트에서 재시도 열 번이 모두 실패). 이름 바꾸기 정보의 경로는 전체 경로여야 한다.
        const DWORD fullLength = GetFullPathNameW( wideTarget.c_str(), 0, nullptr, nullptr );
        if ( fullLength > 0 )
        {
            wstring     fullTarget( static_cast<size_t>( fullLength ), L'\0' );
            const DWORD writtenLength = GetFullPathNameW( wideTarget.c_str(), fullLength, fullTarget.data(), nullptr );
            fullTarget.resize( static_cast<size_t>( writtenLength ) );

            HANDLE hSource = CreateFileW( wideSource.c_str(), DELETE | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
            if ( writtenLength > 0 && hSource != INVALID_HANDLE_VALUE )
            {
                const size_t  nameBytes = fullTarget.size() * sizeof( utf16 );
                vector<uint8> infoBytes( sizeof( FILE_RENAME_INFO ) + nameBytes, 0 );
                auto*         pInfo   = reinterpret_cast<FILE_RENAME_INFO*>( infoBytes.data() );
                pInfo->Flags          = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
                pInfo->RootDirectory  = nullptr;
                pInfo->FileNameLength = static_cast<DWORD>( nameBytes );
                std::memcpy( pInfo->FileName, fullTarget.c_str(), nameBytes );
                const BOOL bRenamed = SetFileInformationByHandle( hSource, FileRenameInfoEx, pInfo, static_cast<DWORD>( infoBytes.size() ) );
                CloseHandle( hSource );
                if ( bRenamed != FALSE )
                    return true;
            }
            else if ( hSource != INVALID_HANDLE_VALUE )
            {
                CloseHandle( hSource );
            }
        }

        // 2) POSIX 방식을 모르는 파일 시스템(FAT · exFAT · 일부 네트워크 드라이브) · 오래된 Windows.
        return MoveFileExW( wideSource.c_str(), wideTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) != FALSE;
#else
        return std::rename( pSourcePath, pTargetPath ) == 0;
#endif
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

        // 크기를 읽었어도 처음으로 되돌리지 못하면 호출하는 쪽이 엉뚱한 위치에서 읽는다. 그래서 실패로 본다.
        if ( seekTo( pFile, 0, SEEK_SET ) == false )
            return kInvalidOffset;

        return size;
    }
} // namespace sw

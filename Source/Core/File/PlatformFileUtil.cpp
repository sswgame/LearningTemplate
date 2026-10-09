#include "pch.h"

#include "Core/File/PlatformFileUtil.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <share.h>
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <cerrno>
    #include <fcntl.h>
#endif

namespace sw
{
    namespace
    {
        struct PlatformFileUtilInternal
        {

#if defined( SW_PLATFORM_WINDOWS )
            /** @brief 스레드마다 하나인 수동 리셋 이벤트입니다 — 동기 위치 읽기가 완료를 기다린다. 스레드가 끝나면 닫는다. */
            struct ThreadReadEvent
            {
                HANDLE _hEvent{ nullptr };

                ~ThreadReadEvent()
                {
                    if ( _hEvent != nullptr )
                        CloseHandle( _hEvent );
                }

                HANDLE get()
                {
                    if ( _hEvent == nullptr )
                        _hEvent = CreateEventW( nullptr, TRUE, FALSE, nullptr );
                    return _hEvent;
                }
            };

            /** @brief 이 스레드의 읽기 이벤트입니다(처음 부를 때 만든다). 만들지 못하면 nullptr 입니다. */
            static HANDLE getThreadReadEvent()
            {
                thread_local ThreadReadEvent t_readEvent{};
                return t_readEvent.get();
            }
#endif
        };
    } // namespace
} // namespace sw

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
        // 공유를 막지 않는다(`_SH_DENYNO`) — POSIX `fopen` 과 같다. `_wfopen_s` 는 배타적으로 열어, 엔진이 쓰는 동안 그 로그 파일을
        // 다른 프로그램(편집기 · tail · 시험)이 열지 못한다.
        pFile = _wfsopen( widePath.c_str(), arrWideMode, _SH_DENYNO );
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
        //    계속 실패한다(동시 읽기 테스트에서 재시도 열 번이 모두 실패). 이름 바꾸기 정보의 경로는 전체 경로여야 한다.
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

    NativeFileHandle PlatformFileUtil::openNativeFileForRead( const utf8* pFilePath )
    {
        if ( pFilePath == nullptr || pFilePath[0] == '\0' )
            return kInvalidNativeFileHandle;
#if defined( SW_PLATFORM_WINDOWS )
        const wstring widePath = StringUtil::utf8ToUtf16( pFilePath );
        const HANDLE  hFile    = CreateFileW( widePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr );
        if ( hFile == INVALID_HANDLE_VALUE )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( reinterpret_cast<intptr_t>( hFile ) );
#else
        int32 fileDescriptor{ -1 };
        do
        {
            fileDescriptor = ::open( pFilePath, O_RDONLY | O_CLOEXEC );
        } while ( fileDescriptor < 0 && errno == EINTR );
        if ( fileDescriptor < 0 )
            return kInvalidNativeFileHandle;
        // 디렉터리는 열리지만 읽을 수 없다 — 파일로 다루지 않는다(Windows 는 CreateFileW 가 거절한다).
        struct stat fileStat{};
        if ( fstat( fileDescriptor, &fileStat ) != 0 || S_ISREG( fileStat.st_mode ) == 0 )
        {
            ::close( fileDescriptor );
            return kInvalidNativeFileHandle;
        }
        return static_cast<NativeFileHandle>( fileDescriptor );
#endif
    }

    void PlatformFileUtil::closeNativeFile( NativeFileHandle handle )
    {
        if ( handle == kInvalidNativeFileHandle )
            return;
#if defined( SW_PLATFORM_WINDOWS )
        CloseHandle( reinterpret_cast<HANDLE>( static_cast<intptr_t>( handle ) ) );
#else
        ::close( static_cast<int32>( handle ) );
#endif
    }

    int64 PlatformFileUtil::getNativeFileSize( NativeFileHandle handle )
    {
        if ( handle == kInvalidNativeFileHandle )
            return kInvalidOffset;
#if defined( SW_PLATFORM_WINDOWS )
        LARGE_INTEGER size{};
        if ( GetFileSizeEx( reinterpret_cast<HANDLE>( static_cast<intptr_t>( handle ) ), &size ) == FALSE )
            return kInvalidOffset;
        return static_cast<int64>( size.QuadPart );
#else
        struct stat fileStat{};
        if ( fstat( static_cast<int32>( handle ), &fileStat ) != 0 )
            return kInvalidOffset;
        return static_cast<int64>( fileStat.st_size );
#endif
    }

    bool PlatformFileUtil::readNativeFileAt( NativeFileHandle handle, uint64 offset, void* pDst, size_t size, size_t& outReadBytes )
    {
        outReadBytes = 0;
        if ( handle == kInvalidNativeFileHandle || ( pDst == nullptr && size > 0 ) )
            return false;

        uint8* pCursor = static_cast<uint8*>( pDst );
        while ( outReadBytes < size )
        {
            const size_t remaining  = size - outReadBytes;
            const size_t chunkBytes = remaining < constant::kMaxFileReadChunkBytes ? remaining : constant::kMaxFileReadChunkBytes;
            const uint64 position   = offset + outReadBytes;
#if defined( SW_PLATFORM_WINDOWS )
            const HANDLE hEvent = PlatformFileUtilInternal::getThreadReadEvent();
            if ( hEvent == nullptr )
                return false;
            OVERLAPPED overlapped{};
            overlapped.Offset     = static_cast<DWORD>( position & 0xFFFFFFFFull );
            overlapped.OffsetHigh = static_cast<DWORD>( position >> 32 );
            // 낮은 비트를 세운 이벤트 — 핸들이 완료 포트에 묶여 있어도(AsyncFileIo IOCP) 이 읽기의 완료는 포트로 가지 않는다.
            overlapped.hEvent = reinterpret_cast<HANDLE>( reinterpret_cast<uintptr_t>( hEvent ) | 1 );

            const HANDLE hFile = reinterpret_cast<HANDLE>( static_cast<intptr_t>( handle ) );
            DWORD        readBytes{ 0 };
            if ( ReadFile( hFile, pCursor + outReadBytes, static_cast<DWORD>( chunkBytes ), nullptr, &overlapped ) == FALSE )
            {
                const DWORD errorCode = GetLastError();
                if ( errorCode == ERROR_HANDLE_EOF )
                    return true;
                if ( errorCode != ERROR_IO_PENDING )
                    return false;
            }
            if ( GetOverlappedResult( hFile, &overlapped, &readBytes, TRUE ) == FALSE )
                return GetLastError() == ERROR_HANDLE_EOF;
#else
            ssize_t readBytes{ -1 };
            do
            {
                readBytes = ::pread( static_cast<int32>( handle ), pCursor + outReadBytes, chunkBytes, static_cast<off_t>( position ) );
            } while ( readBytes < 0 && errno == EINTR );
            if ( readBytes < 0 )
                return false;
#endif
            if ( readBytes == 0 )
                return true; // 파일 끝
            outReadBytes += static_cast<size_t>( readBytes );
        }
        return true;
    }
} // namespace sw

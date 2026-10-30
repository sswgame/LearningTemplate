#include "pch.h"

#include "Core/File/PlatformFileUtil.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Foundation/Defines.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Time/MonotonicClock.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <share.h>
    #include <shellapi.h>
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <cerrno>
    #include <fcntl.h>
#endif

namespace sw
{
    namespace
    {
        struct PlatformFileUtilInternal
        {
            /** @brief 읽으려고 연 파일 수입니다(`noteFileOpenedForRead`). */
            inline static atomic<uint64> s_fileOpenCount{ 0 };
            /** @brief 디스크 흉내의 장치가 비는 시각(`MonotonicClock` 나노초) — 읽기마다 이 뒤에 제 지연을 붙인다. */
            inline static atomic<int64> s_emulatedDiskBusyUntilNano{ 0 };

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

    SW_TEST_GLOBAL_VARIABLE( int32, gv_ioEmulationReadLatencyMs, 0, "디스크 흉내: 파일 읽기 한 번마다 더하는 지연(ms, 0=끔)" );
    SW_TEST_GLOBAL_VARIABLE( int32, gv_ioEmulationReadKilobytesPerSecond, 0, "디스크 흉내: 파일 읽기 대역폭(KB/s, 0=제한 없음)" );
    SW_TEST_GLOBAL_VARIABLE( bool, gv_ioLogReads, false, "읽으려고 연 파일마다 경로 · 바이트 · 스레드를 로그로 남긴다(기동 · 히치의 동기 읽기 찾기)" );
} // namespace sw

namespace sw
{
    void PlatformFileUtil::emulateReadDelay( size_t byteCount )
    {
        int64 delayMicro = static_cast<int64>( MathUtil::max( 0, gv_ioEmulationReadLatencyMs ) ) * constant::kMicrosecondsPerMillisecond;
        if ( gv_ioEmulationReadKilobytesPerSecond > 0 )
            delayMicro += static_cast<int64>( byteCount ) * constant::kMicrosecondsPerSecond / ( static_cast<int64>( gv_ioEmulationReadKilobytesPerSecond ) * static_cast<int64>( constant::kBytesPerKilobyte ) );
        if ( delayMicro <= 0 )
            return;
        // 디스크 한 대(큐 깊이 1): 여러 스레드의 읽기가 장치에서 줄을 선다 — 스레드마다 따로 자면 읽기를 나눠 부르기만 해도 디스크가 빨라진 것처럼 보인다.
        // `sleep_for` 는 Windows 기본 타이머 해상도(약 15.6 ms)로 반올림돼 1 ms 와 8 ms 가 같은 지연이 된다 — 고해상도 마감 대기를 쓴다.
        const int64 delayNano = delayMicro * constant::kNanosecondsPerMicrosecond;
        const int64 nowNano   = MonotonicClock::nowNanoseconds();
        int64       busyUntil = PlatformFileUtilInternal::s_emulatedDiskBusyUntilNano.load( std::memory_order_relaxed );
        int64       doneNano  = 0;
        do
        {
            doneNano = MathUtil::max( nowNano, busyUntil ) + delayNano;
        } while ( PlatformFileUtilInternal::s_emulatedDiskBusyUntilNano.compare_exchange_weak( busyUntil, doneNano, std::memory_order_relaxed ) == false );
        MonotonicClock::sleepUntilNanoseconds( doneNano );
    }

    void PlatformFileUtil::noteFileOpenedForRead( [[maybe_unused]] string_view filePath, [[maybe_unused]] size_t byteCount )
    {
        PlatformFileUtilInternal::s_fileOpenCount.fetch_add( 1, std::memory_order_relaxed );
        if ( gv_ioLogReads == false )
            return;
        utf8 arrThreadName[64]{};
        if ( ThreadName::tryGetCurrentThreadName( arrThreadName, static_cast<uint32>( sizeof( arrThreadName ) ) ) == false )
            arrThreadName[0] = '\0';
        SW_LOG_INFO( "[IO] open %# bytes=%# thread=%#", filePath, byteCount, static_cast<const utf8*>( arrThreadName ) );
    }

    uint64 PlatformFileUtil::getFileOpenCount()
    {
        return PlatformFileUtilInternal::s_fileOpenCount.load( std::memory_order_relaxed );
    }

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
        const wstring widePath = StringUtil::utf8ToUTF16( pFilePath );
        // 공유를 막지 않는다(`_SH_DENYNO`) — POSIX `fopen` 과 같다. `_wfopen_s` 는 배타적으로 열어, 엔진이 쓰는 동안 그 로그 파일을
        // 다른 프로그램(편집기 · tail · 테스트)이 열지 못한다.
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
        const wstring wideSource = StringUtil::utf8ToUTF16( pSourcePath );
        const wstring wideTarget = StringUtil::utf8ToUTF16( pTargetPath );

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

    bool PlatformFileUtil::hasRecycleBin()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return true;
#else
        return false;
#endif
    }

    bool PlatformFileUtil::moveToRecycleBin( const vector<string>& listPath )
    {
        if ( listPath.empty() )
            return true;
#if defined( SW_PLATFORM_WINDOWS )
        // 셸 파일 작업의 경로 목록은 경로마다 널 하나, 끝에 널 하나 더다. 셸은 `/` 를 구분자로 받지 않아 `\` 로 바꾼다.
        vector<utf16> listWideChar;
        for ( const string& path : listPath )
        {
            const wstring widePath = StringUtil::utf8ToUTF16( path.c_str() );
            for ( const utf16 ch : widePath )
            {
                listWideChar.push_back( ch == L'/' ? L'\\' : ch );
            }
            listWideChar.push_back( L'\0' );
        }
        listWideChar.push_back( L'\0' );

        SHFILEOPSTRUCTW operation{};
        operation.wFunc  = FO_DELETE;
        operation.pFrom  = listWideChar.data();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        return SHFileOperationW( &operation ) == 0 && operation.fAnyOperationsAborted == FALSE;
#else
        // 휴지통 규약(freedesktop Trash)은 데스크톱마다 다르다. 아무것도 건드리지 않는다.
        return false;
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
        const wstring widePath = StringUtil::utf8ToUTF16( pFilePath );
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

    NativeFileHandle PlatformFileUtil::createNativeFileForWrite( const utf8* pFilePath )
    {
        if ( pFilePath == nullptr || pFilePath[0] == '\0' )
            return kInvalidNativeFileHandle;
#if defined( SW_PLATFORM_WINDOWS )
        // 힙 없이 스택에서 UTF-16 으로 바꾼다(StringUtil::utf8ToUTF16 은 할당한다).
        utf16 arrWidePath[constant::kMaxBuffer1024]{};
        if ( MultiByteToWideChar( CP_UTF8, 0, pFilePath, -1, arrWidePath, static_cast<int32>( constant::kMaxBuffer1024 ) ) == 0 )
            return kInvalidNativeFileHandle;
        const HANDLE hFile = CreateFileW( arrWidePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( hFile == INVALID_HANDLE_VALUE )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( reinterpret_cast<intptr_t>( hFile ) );
#else
        int32 fileDescriptor{ -1 };
        do
        {
            fileDescriptor = ::open( pFilePath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
        } while ( fileDescriptor < 0 && errno == EINTR );
        if ( fileDescriptor < 0 )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( fileDescriptor );
#endif
    }

    bool PlatformFileUtil::writeNativeFile( NativeFileHandle handle, const void* pData, size_t size )
    {
        if ( handle == kInvalidNativeFileHandle || ( pData == nullptr && size > 0 ) )
            return false;
        const uint8* pCursor = static_cast<const uint8*>( pData );
        while ( size > 0 )
        {
            const size_t chunkBytes = size < constant::kMaxFileReadChunkBytes ? size : constant::kMaxFileReadChunkBytes;
#if defined( SW_PLATFORM_WINDOWS )
            DWORD written{ 0 };
            if ( WriteFile( reinterpret_cast<HANDLE>( static_cast<intptr_t>( handle ) ), pCursor, static_cast<DWORD>( chunkBytes ), &written, nullptr ) == FALSE || written == 0 )
                return false;
#else
            const ssize_t written = ::write( static_cast<int32>( handle ), pCursor, chunkBytes );
            if ( written < 0 && errno == EINTR )
                continue;
            if ( written <= 0 )
                return false;
#endif
            pCursor += written;
            size -= static_cast<size_t>( written );
        }
        return true;
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
        emulateReadDelay( size );

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
            // 낮은 비트를 세운 이벤트 — 핸들이 완료 포트에 묶여 있어도(AsyncFileIO IOCP) 이 읽기의 완료는 포트로 가지 않는다.
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

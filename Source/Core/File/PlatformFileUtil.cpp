#include "pch.h"

#include "Core/File/PlatformFileUtil.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/Constants.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"

    #include <share.h>
    #include <shellapi.h>
#elif defined( SW_PLATFORM_POSIX )
    #include <cerrno>
    #include <ctime>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#else
    #error "Unsupported platform"
#endif

namespace sw
{
    namespace
    {
        struct PlatformFileUtilInternal
        {
            /** @brief 읽으려고 연 파일 수입니다(`recordFileOpenedForRead`). */
            inline static atomic<uint64> s_fileOpenCount{ 0 };
            /** @brief 걸린 읽기 관찰자입니다(`registerReadObserver`). 없으면 nullptr 입니다. */
            inline static atomic<IFileReadObserver*> s_pReadObserver{ nullptr };
            /** @brief 걸린 쓰기 관찰자입니다(`registerWriteObserver`). 없으면 nullptr 입니다. */
            inline static atomic<IFileWriteObserver*> s_pWriteObserver{ nullptr };

#if defined( SW_PLATFORM_LINUX )
            /** @brief freedesktop 휴지통 폴더입니다(`$XDG_DATA_HOME/Trash`, 없으면 `~/.local/share/Trash`). 홈을 모르면 빈 글입니다. */
            static string findTrashFolder()
            {
                string folder;
                if ( ProcessEnvironment::tryReadNonEmpty( "XDG_DATA_HOME", folder ) )
                    return folder + "/Trash";
                if ( ProcessEnvironment::tryReadNonEmpty( "HOME", folder ) )
                    return folder + "/.local/share/Trash";
                return {};
            }

            /** @brief 폴더를 가는 길까지 만듭니다. */
            [[nodiscard]] static bool ensureFolders( const string& folder )
            {
                for ( size_t slash = folder.find( '/', 1 ); slash != string::npos; slash = folder.find( '/', slash + 1 ) )
                {
                    (void)::mkdir( folder.substr( 0, slash ).c_str(), 0700 ); // 이미 있으면 실패한다. 마지막 칸의 결과로 판단한다
                }
                return ::mkdir( folder.c_str(), 0700 ) == 0 || errno == EEXIST;
            }

            /** @brief `.trashinfo` 의 Path 형식(URL 퍼센트 인코딩)입니다. */
            static string encodeTrashPath( const string& path )
            {
                static constexpr utf8 kArrHex[] = "0123456789ABCDEF";
                string                encoded;
                for ( const utf8 character : path )
                {
                    const uint8 byte       = static_cast<uint8>( character );
                    const bool  bLetter    = ( 'a' <= byte && byte <= 'z' ) || ( 'A' <= byte && byte <= 'Z' ) || ( '0' <= byte && byte <= '9' );
                    const bool  bUnchanged = bLetter || byte == '/' || byte == '-' || byte == '_' || byte == '.' || byte == '~';
                    if ( bUnchanged )
                    {
                        encoded.push_back( character );
                        continue;
                    }
                    encoded.push_back( '%' );
                    encoded.push_back( kArrHex[byte >> 4] );
                    encoded.push_back( kArrHex[byte & 0x0F] );
                }
                return encoded;
            }

            /**
             * @brief 파일 하나를 휴지통으로 옮깁니다. 정보 파일을 먼저 배타적으로 만들어 이름을 차지하고(같은 이름은 `.1` · `.2`), 옮기지 못하면 정보 파일을 지웁니다.
             * @details shortcut: 홈과 다른 파일 시스템의 파일은 rename 이 실패해 false 다(지우지 않고 남는다). 볼륨별 `.Trash-<uid>` 는 필요해지면 더한다.
             */
            [[nodiscard]] static bool moveToTrash( const string& path, const string& trashFolder )
            {
                static constexpr int32 kMaxNameAttempt = 1000;
                const size_t           slash           = path.rfind( '/' );
                const string           baseName        = slash != string::npos ? path.substr( slash + 1 ) : path;
                utf8                   arrDate[32]{};
                const time_t           nowSeconds = ::time( nullptr );
                tm                     localTime{};
                (void)::localtime_r( &nowSeconds, &localTime );
                (void)::strftime( arrDate, sizeof( arrDate ), "%Y-%m-%dT%H:%M:%S", &localTime );

                for ( int32 attempt = 0; attempt < kMaxNameAttempt; ++attempt )
                {
                    string name = baseName;
                    if ( attempt > 0 )
                        name += "." + to_string( attempt );
                    const string infoPath = trashFolder + "/info/" + name + ".trashinfo";
                    const string destPath = trashFolder + "/files/" + name;
                    const int32  file     = ::open( infoPath.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600 );
                    if ( file < 0 )
                    {
                        if ( errno == EEXIST )
                            continue;
                        return false;
                    }
                    const string text     = "[Trash Info]\nPath=" + encodeTrashPath( path ) + "\nDeletionDate=" + arrDate + "\n";
                    const bool   bWritten = ::write( file, text.data(), text.size() ) == static_cast<ssize_t>( text.size() );
                    ::close( file );
                    const bool bNameTaken = ::access( destPath.c_str(), F_OK ) == 0; // rename 은 있는 파일을 덮어쓴다
                    if ( bWritten && bNameTaken == false && ::rename( path.c_str(), destPath.c_str() ) == 0 )
                        return true;
                    ::unlink( infoPath.c_str() );
                    if ( bWritten == false || bNameTaken == false )
                        return false;
                }
                return false;
            }
#endif

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
    void PlatformFileUtil::recordDeviceRead( size_t byteCount )
    {
        if ( IFileReadObserver* const pObserver = PlatformFileUtilInternal::s_pReadObserver.load( std::memory_order_acquire ) )
            pObserver->recordDeviceRead( byteCount );
    }

    void PlatformFileUtil::recordFileOpenedForRead( string_view filePath, size_t byteCount )
    {
        PlatformFileUtilInternal::s_fileOpenCount.fetch_add( 1, std::memory_order_relaxed );
        if ( IFileReadObserver* const pObserver = PlatformFileUtilInternal::s_pReadObserver.load( std::memory_order_acquire ) )
            pObserver->recordFileOpenedForRead( filePath, byteCount );
    }

    bool PlatformFileUtil::registerReadObserver( IFileReadObserver* pObserver )
    {
        IFileReadObserver* pExpected{ nullptr };
        return PlatformFileUtilInternal::s_pReadObserver.compare_exchange_strong( pExpected, pObserver, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void PlatformFileUtil::unregisterReadObserver( IFileReadObserver* pObserver )
    {
        IFileReadObserver* pExpected = pObserver;
        (void)PlatformFileUtilInternal::s_pReadObserver.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void PlatformFileUtil::recordFileWritten( string_view filePath )
    {
        if ( IFileWriteObserver* const pObserver = PlatformFileUtilInternal::s_pWriteObserver.load( std::memory_order_acquire ) )
            pObserver->recordFileWritten( filePath );
    }

    bool PlatformFileUtil::registerWriteObserver( IFileWriteObserver* pObserver )
    {
        IFileWriteObserver* pExpected{ nullptr };
        return PlatformFileUtilInternal::s_pWriteObserver.compare_exchange_strong( pExpected, pObserver, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    void PlatformFileUtil::unregisterWriteObserver( IFileWriteObserver* pObserver )
    {
        IFileWriteObserver* pExpected = pObserver;
        (void)PlatformFileUtilInternal::s_pWriteObserver.compare_exchange_strong( pExpected, nullptr, std::memory_order_acq_rel, std::memory_order_relaxed );
    }

    bool PlatformFileUtil::acceptsPathInput( string_view path )
    {
        if ( path.empty() || AbsolutePath::isStrictAbsolute( path ) )
            return true;
        SW_LOG_ERROR_ID( "File.RelativePath", "A relative path reached the OS boundary: '%#' - resolve it where it enters (AbsolutePath::tryResolve, ProcessRoots)", path );
        return false;
    }

#if defined( SW_PLATFORM_WINDOWS )
    wstring PlatformFileUtil::toWindowsFilePath( string_view path )
    {
        string separated{ path };
        StringUtil::replaceChar( separated, '/', '\\' );
        wstring wide = StringUtil::utf8ToUTF16( separated.c_str() );
        if ( wide.size() < kLongPathThreshold || wide.compare( 0, 4, L"\\\\?\\" ) == 0 )
            return wide;
        // 확장 경로는 `.` · `..` 를 풀지 않으므로 먼저 편다.
        const DWORD needed = GetFullPathNameW( wide.c_str(), 0, nullptr, nullptr );
        if ( needed == 0 )
            return {};
        wstring     full( static_cast<size_t>( needed ), L'\0' );
        const DWORD written = GetFullPathNameW( wide.c_str(), needed, full.data(), nullptr );
        if ( written == 0 || written >= needed )
            return {};
        full.resize( static_cast<size_t>( written ) );
        if ( full.compare( 0, 2, L"\\\\" ) == 0 )
            return wstring( L"\\\\?\\UNC\\" ) + full.substr( 2 );
        return wstring( L"\\\\?\\" ) + full;
    }
#endif

    uint64 PlatformFileUtil::getFileOpenCount()
    {
        return PlatformFileUtilInternal::s_fileOpenCount.load( std::memory_order_relaxed );
    }

    FILE* PlatformFileUtil::openFile( const utf8* pFilePath, const utf8* pMode )
    {
        if ( pFilePath == nullptr || pMode == nullptr )
            return nullptr;

        if ( acceptsPathInput( pFilePath ) == false )
            return nullptr;
        FILE* pFile{ nullptr };
#if defined( SW_PLATFORM_WINDOWS )
        // 모드는 ASCII 몇 글자다("rb" · "wb" · "a+" ...). 그대로 넓힌다.
        utf16  arrWideMode[constant::buffer::kMax16]{};
        size_t modeLength = 0;
        while ( pMode[modeLength] != '\0' && modeLength + 1 < constant::buffer::kMax16 )
        {
            arrWideMode[modeLength] = static_cast<utf16>( static_cast<uint8>( pMode[modeLength] ) );
            ++modeLength;
        }
        const wstring widePath = toWindowsFilePath( pFilePath );
        // 공유를 막지 않는다(`_SH_DENYNO`) — POSIX `fopen` 과 같다. `_wfopen_s` 는 배타적으로 열어, 엔진이 쓰는 동안 그 로그 파일을
        // 다른 프로그램(편집기 · tail · 테스트)이 열지 못한다.
        pFile = _wfsopen( widePath.c_str(), arrWideMode, _SH_DENYNO );
#elif defined( SW_PLATFORM_POSIX )
        pFile = fopen( pFilePath, pMode );
#else
    #error "Unsupported platform"
#endif
        return pFile;
    }

    bool PlatformFileUtil::replaceFile( const utf8* pSourcePath, const utf8* pTargetPath )
    {
        if ( pSourcePath == nullptr || pTargetPath == nullptr )
            return false;
        if ( acceptsPathInput( pSourcePath ) == false || acceptsPathInput( pTargetPath ) == false )
            return false;

#if defined( SW_PLATFORM_WINDOWS )
        const wstring wideSource = toWindowsFilePath( pSourcePath );
        const wstring wideTarget = toWindowsFilePath( pTargetPath );

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
#elif defined( SW_PLATFORM_POSIX )
        return std::rename( pSourcePath, pTargetPath ) == 0;
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformFileUtil::hasRecycleBin()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return true;
#elif defined( SW_PLATFORM_LINUX )
        return PlatformFileUtilInternal::findTrashFolder().empty() == false;
#elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
        return false; // 샌드박스에는 휴지통이 없다(에디터 기능이다)
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_LINUX )
        const string trashFolder = PlatformFileUtilInternal::findTrashFolder();
        if ( trashFolder.empty() || PlatformFileUtilInternal::ensureFolders( trashFolder + "/files" ) == false || PlatformFileUtilInternal::ensureFolders( trashFolder + "/info" ) == false )
            return false;
        for ( const string& path : listPath )
        {
            if ( PlatformFileUtilInternal::moveToTrash( path, trashFolder ) == false )
                return false;
        }
        return true;
#elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
        return false; // hasRecycleBin 이 false 다 — 부르는 쪽이 먼저 묻는다
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformFileUtil::seekTo( FILE* pFile, int64 offset, int32 origin )
    {
        if ( pFile == nullptr )
            return false;

#if defined( SW_PLATFORM_WINDOWS )
        return _fseeki64( pFile, offset, origin ) == 0;
#elif defined( SW_PLATFORM_POSIX )
        return fseeko( pFile, static_cast<off_t>( offset ), origin ) == 0;
#else
    #error "Unsupported platform"
#endif
    }

    int64 PlatformFileUtil::tellPosition( FILE* pFile )
    {
        if ( pFile == nullptr )
            return kInvalidOffset;

#if defined( SW_PLATFORM_WINDOWS )
        return _ftelli64( pFile );
#elif defined( SW_PLATFORM_POSIX )
        return static_cast<int64>( ftello( pFile ) );
#else
    #error "Unsupported platform"
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
        if ( acceptsPathInput( pFilePath ) == false )
            return kInvalidNativeFileHandle;
#if defined( SW_PLATFORM_WINDOWS )
        const wstring widePath = toWindowsFilePath( pFilePath );
        const HANDLE  hFile    = CreateFileW( widePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr );
        if ( hFile == INVALID_HANDLE_VALUE )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( reinterpret_cast<intptr_t>( hFile ) );
#elif defined( SW_PLATFORM_POSIX )
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
#else
    #error "Unsupported platform"
#endif
    }

    NativeFileHandle PlatformFileUtil::createNativeFileForWrite( const utf8* pFilePath )
    {
        if ( pFilePath == nullptr || pFilePath[0] == '\0' )
            return kInvalidNativeFileHandle;
#if defined( SW_PLATFORM_WINDOWS )
        // 힙 없이 스택에서 UTF-16 으로 바꾼다(StringUtil::utf8ToUTF16 은 할당한다).
        utf16 arrWidePath[constant::buffer::kMax1024]{};
        if ( MultiByteToWideChar( CP_UTF8, 0, pFilePath, -1, arrWidePath, static_cast<int32>( constant::buffer::kMax1024 ) ) == 0 )
            return kInvalidNativeFileHandle;
        const HANDLE hFile = CreateFileW( arrWidePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( hFile == INVALID_HANDLE_VALUE )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( reinterpret_cast<intptr_t>( hFile ) );
#elif defined( SW_PLATFORM_POSIX )
        int32 fileDescriptor{ -1 };
        do
        {
            fileDescriptor = ::open( pFilePath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644 );
        } while ( fileDescriptor < 0 && errno == EINTR );
        if ( fileDescriptor < 0 )
            return kInvalidNativeFileHandle;
        return static_cast<NativeFileHandle>( fileDescriptor );
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformFileUtil::writeNativeFile( NativeFileHandle handle, const void* pData, size_t size )
    {
        if ( handle == kInvalidNativeFileHandle || ( pData == nullptr && size > 0 ) )
            return false;
        const uint8* pCursor = static_cast<const uint8*>( pData );
        while ( size > 0 )
        {
            const size_t chunkBytes = size < constant::file::kMaxFileReadChunkBytes ? size : constant::file::kMaxFileReadChunkBytes;
#if defined( SW_PLATFORM_WINDOWS )
            DWORD written{ 0 };
            if ( WriteFile( reinterpret_cast<HANDLE>( static_cast<intptr_t>( handle ) ), pCursor, static_cast<DWORD>( chunkBytes ), &written, nullptr ) == FALSE || written == 0 )
                return false;
#elif defined( SW_PLATFORM_POSIX )
            const ssize_t written = ::write( static_cast<int32>( handle ), pCursor, chunkBytes );
            if ( written < 0 && errno == EINTR )
                continue;
            if ( written <= 0 )
                return false;
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_POSIX )
        ::close( static_cast<int32>( handle ) );
#else
    #error "Unsupported platform"
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
#elif defined( SW_PLATFORM_POSIX )
        struct stat fileStat{};
        if ( fstat( static_cast<int32>( handle ), &fileStat ) != 0 )
            return kInvalidOffset;
        return static_cast<int64>( fileStat.st_size );
#else
    #error "Unsupported platform"
#endif
    }

    bool PlatformFileUtil::readNativeFileAt( NativeFileHandle handle, uint64 offset, void* pDst, size_t size, size_t& outReadBytes )
    {
        outReadBytes = 0;
        if ( handle == kInvalidNativeFileHandle || ( pDst == nullptr && size > 0 ) )
            return false;
        recordDeviceRead( size );

        uint8* pCursor = static_cast<uint8*>( pDst );
        while ( outReadBytes < size )
        {
            const size_t remaining  = size - outReadBytes;
            const size_t chunkBytes = remaining < constant::file::kMaxFileReadChunkBytes ? remaining : constant::file::kMaxFileReadChunkBytes;
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
#elif defined( SW_PLATFORM_POSIX )
            ssize_t readBytes{ -1 };
            do
            {
                readBytes = ::pread( static_cast<int32>( handle ), pCursor + outReadBytes, chunkBytes, static_cast<off_t>( position ) );
            } while ( readBytes < 0 && errno == EINTR );
            if ( readBytes < 0 )
                return false;
#else
    #error "Unsupported platform"
#endif
            if ( readBytes == 0 )
                return true; // 파일 끝
            outReadBytes += static_cast<size_t>( readBytes );
        }
        return true;
    }
} // namespace sw

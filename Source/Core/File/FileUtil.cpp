#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileDialog.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileDialog.h"
#elif defined( SW_PLATFORM_MACOS )
    #include "Core/File/Mac/MacFileDialog.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        struct FileUtilInternal
        {
#if !defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief 읽기용으로 열고 크기를 잽니다(처음으로 되감긴 상태). 실패하면 로그를 남기고 nullptr 입니다. 연 파일은 호출하는 쪽이 닫습니다.
             * @details `readFile` · `readTextFile` 이 이 열두 줄을 각자 들고 있었고, 크기를 재지 못했을 때 한쪽만 로그를 남겼습니다.
             */
            static FILE* openForReading( string_view fileName, int64& outFileSize )
            {
                const string filePath = FileUtil::normalizeSeparators( fileName );
                FILE*        pFile    = PlatformFileUtil::openFile( filePath.c_str(), "rb" );
                if ( pFile == nullptr )
                {
                    SW_LOG_ERROR( "File not found: %#", fileName );
                    return nullptr;
                }

                outFileSize = PlatformFileUtil::getOpenFileSizeAndRewind( pFile );
                if ( outFileSize < 0 )
                {
                    std::fclose( pFile );
                    SW_LOG_ERROR( "Failed to query size of: %#", fileName );
                    return nullptr;
                }
                return pFile;
            }
#endif

            /**
             * @brief 파일의 [offset, offset + maxReadCount) 를 @p outBuffer 에 읽습니다(버퍼 크기는 실제로 읽은 만큼입니다). 실패하면 로그를 남기고 false 입니다.
             * @details `readFile` · `readTextFile` 의 본체입니다. **Windows 에서는 Win32 API 로 바로 읽습니다.** 열기 · 크기 · 읽기 · 닫기의
             *          시스템 호출 네 번입니다. stdio 경로(`fopen_s` → 끝으로 이동 → 위치 질의 → 처음으로 되감기 → `fread`)는 크기를
             *          재려고 파일 위치를 세 번 옮겼고, UCRT 의 잠금과 버퍼 준비를 거쳤습니다. 게다가 `fopen_s` 는 좁은 문자 경로를
             *          **ANSI 코드 페이지**로 해석해서 UTF-8 경로의 한글이 깨졌습니다(앱 매니페스트가 UTF-8 코드 페이지를 켜지 않습니다).
             *          여기서는 UTF-16 으로 바꿔 엽니다. 2026-09-23 시작 시간 프로파일에서 `readFile` 이 게임 스레드 초기화의 12.6 %
             *          였습니다(셰이더 굽기 도장이 소스를 읽어 해시합니다). 공유 모드는 stdio(`_SH_DENYNO`)와 같이 읽기 · 쓰기를
             *          허용하므로, 에디터가 쓰고 있는 파일도 전처럼 열립니다.
             */
            template <typename BufferType>
            static bool readRange( string_view fileName, uint64 offset, uint64 maxReadCount, BufferType& outBuffer )
            {
#if defined( SW_PLATFORM_WINDOWS )
                const string  filePath = FileUtil::normalizeSeparators( fileName );
                const wstring widePath = StringUtil::utf8ToUtf16( filePath.c_str() );
                HANDLE        hFile    = CreateFileW( widePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
                if ( hFile == INVALID_HANDLE_VALUE )
                {
                    SW_LOG_ERROR( "File not found: %#", fileName );
                    return false;
                }

                LARGE_INTEGER fileSize{};
                if ( GetFileSizeEx( hFile, &fileSize ) == FALSE || fileSize.QuadPart < 0 )
                {
                    CloseHandle( hFile );
                    SW_LOG_ERROR( "Failed to query size of: %#", fileName );
                    return false;
                }
                const uint64 uFileSize = static_cast<uint64>( fileSize.QuadPart );
                if ( offset > uFileSize )
                {
                    CloseHandle( hFile );
                    SW_LOG_ERROR( "Read offset %# exceeds size %# of: %#", offset, uFileSize, fileName );
                    return false;
                }

                const uint64 dataSize = MathUtil::min( uFileSize - offset, maxReadCount );
                outBuffer.resize( static_cast<size_t>( dataSize ) );
                // 읽을 위치는 OVERLAPPED 의 오프셋으로 준다(동기 핸들이면 위치를 옮기는 호출이 따로 필요 없다). ReadFile 은 한 번에 4 GB 미만만 읽는다.
                uint64 readTotal = 0;
                while ( readTotal < dataSize )
                {
                    constexpr uint64 kMaxChunk = 1ull << 30;
                    const DWORD      chunkSize = static_cast<DWORD>( MathUtil::min( dataSize - readTotal, kMaxChunk ) );
                    const uint64     position  = offset + readTotal;
                    OVERLAPPED       overlapped{};
                    overlapped.Offset     = static_cast<DWORD>( position & 0xFFFFFFFFull );
                    overlapped.OffsetHigh = static_cast<DWORD>( position >> 32 );
                    DWORD readNow         = 0;
                    if ( ReadFile( hFile, reinterpret_cast<uint8*>( outBuffer.data() ) + readTotal, chunkSize, &readNow, &overlapped ) == FALSE || readNow == 0 )
                        break;
                    readTotal += readNow;
                }
                CloseHandle( hFile );
                if ( readTotal != dataSize )
                    outBuffer.resize( static_cast<size_t>( readTotal ) );
                return true;
#else
                int64 fileSize{ 0 };
                FILE* pFile = openForReading( fileName, fileSize );
                if ( pFile == nullptr )
                    return false;

                const uint64 uFileSize = static_cast<uint64>( fileSize );
                if ( offset > uFileSize )
                {
                    std::fclose( pFile );
                    SW_LOG_ERROR( "Read offset %# exceeds size %# of: %#", offset, uFileSize, fileName );
                    return false;
                }

                const uint64 dataSize = MathUtil::min( uFileSize - offset, maxReadCount );
                PlatformFileUtil::seekTo( pFile, static_cast<int64>( offset ), SEEK_SET );
                outBuffer.resize( static_cast<size_t>( dataSize ) );
                if ( dataSize > 0 )
                {
                    const size_t readBytes = std::fread( outBuffer.data(), 1, static_cast<size_t>( dataSize ), pFile );
                    if ( readBytes != static_cast<size_t>( dataSize ) )
                        outBuffer.resize( readBytes );
                }
                std::fclose( pFile );
                return true;
#endif
            }
        };

        /** @brief 다이얼로그 스레드가 담고 메인 스레드가 꺼내는 결과 하나입니다. */
        struct FileDialogResult
        {
            FileDialogDelegate _delegate{};
            vector<string>     _listPath{};
        };

        /** @brief 파일 다이얼로그 결과 큐입니다. 담는 쪽은 분리된 스레드, 꺼내는 쪽은 메인 스레드입니다. */
        struct FileDialogQueueInternal
        {
            inline static mutex                    _s_mutex{};
            inline static vector<FileDialogResult> _s_listResult{};
            /// @brief cancelFileDialogResults 가 올립니다. 이미 열려 있는 다이얼로그의 결과를 버리는 표시입니다.
            inline static uint32 _s_generation{ 0 };
        };

        /** @brief 윈도우 경로 길이의 절대 상한(유니코드 확장 경로, 문자 수)입니다. */
        constexpr size_t kMaxWindowsPathSize = 32768;

        /**
         * @brief 디렉터리를 훑으며 항목마다 함수를 실행합니다. **예외를 던지지 않습니다.**
         * @details `std::filesystem` 의 순회자는 `error_code` 를 받지 않으면 **예외를 던집니다.** 권한이 없는 폴더, 순회 도중
         *          지워진 폴더, 윈도우의 보호된 정션이 그런 경우입니다. `collectFiles` · `collectFolders` 는 `bool` 로 실패를
         *          알리기로 약속했는데, 그 예외가 호출부까지 뚫고 나갔습니다. 같은 파일의 `makeRelativePath` · `makeAbsolutePath` 는
         *          처음부터 `error_code` 를 받고 있었는데, 그 방식이 여기에는 옮겨지지 않았던 것입니다.
         *
         *          따로 적혀 있던 같은 코드 네 벌(파일/폴더 × 재귀/비재귀)도 여기서 하나로 모읍니다. 코드가 갈라질 곳을 줄이는
         *          것이 이 저장소가 되풀이해 겪은 문제의 해법입니다.
         */
        template <typename Func>
        void forEachDirectoryEntry( const std::filesystem::path& directoryPath, const bool bRecursive, Func&& func )
        {
            std::error_code ec;

            if ( bRecursive )
            {
                // `skip_permission_denied` 는 하위 폴더에 들어갈 수 없을 때 멈추지 않고 건너뛰게 한다.
                std::filesystem::recursive_directory_iterator iter{ directoryPath, std::filesystem::directory_options::skip_permission_denied, ec };
                if ( ec )
                    return;

                const std::filesystem::recursive_directory_iterator last{};
                while ( iter != last )
                {
                    func( *iter );
                    iter.increment( ec );
                    if ( ec )
                        return;
                }
                return;
            }

            std::filesystem::directory_iterator iter{ directoryPath, std::filesystem::directory_options::skip_permission_denied, ec };
            if ( ec )
                return;

            const std::filesystem::directory_iterator last{};
            while ( iter != last )
            {
                func( *iter );
                iter.increment( ec );
                if ( ec )
                    return;
            }
        }

        /** @brief 항목이 디렉터리인지 확인합니다. 확인하다 예외를 던지지 않습니다. */
        bool isDirectoryEntry( const std::filesystem::directory_entry& entry )
        {
            std::error_code ec;
            const bool      bIsDirectory = entry.is_directory( ec );
            return ec ? false : bIsDirectory;
        }
    } // namespace

    void FileUtil::splitPath( string_view fullPath, string_view& outDirectoryPath, string_view& outFileName )
    {
        const size_t found = fullPath.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outDirectoryPath = {};
            outFileName      = fullPath;
            return;
        }
        outDirectoryPath = fullPath.substr( 0, found + 1 );
        outFileName      = fullPath.substr( found + 1 );
    }

    void FileUtil::getFileNamePart( string_view fullPath, string_view& outFileName )
    {
        if ( fullPath.empty() )
        {
            outFileName = {};
            return;
        }

        const size_t found = fullPath.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outFileName = fullPath;
            return;
        }

        outFileName = fullPath.substr( found + 1 );
    }

    string FileUtil::getFileNamePart( string_view fullPath )
    {
        string_view fileView;
        getFileNamePart( fullPath, fileView );
        return string{ fileView };
    }

    void FileUtil::getDirectoryPart( string_view fullPath, string_view& outDirectoryPath )
    {
        if ( fullPath.empty() )
        {
            outDirectoryPath = {};
            return;
        }

        string_view v = fullPath;
        while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
        {
            v.remove_suffix( 1 );
        }

        if ( v.empty() )
        {
            outDirectoryPath = {};
            return;
        }

        const size_t found = v.find_last_of( "/\\" );
        if ( found == string_view::npos )
        {
            outDirectoryPath = {};
            return;
        }

        outDirectoryPath = v.substr( 0, found );
    }

    string FileUtil::getDirectoryPart( string_view fullPath )
    {
        string_view dirView;
        getDirectoryPart( fullPath, dirView );
        return string{ dirView };
    }

    void FileUtil::getExtension( string_view fileName, string_view& outExtension )
    {
        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );
        if ( dot == string_view::npos || dot < start )
        {
            outExtension = {};
            return;
        }
        outExtension = fileName.substr( dot );
    }

    string FileUtil::getExtension( string_view fileName )
    {
        string_view extView;
        getExtension( fileName, extView );
        return string{ extView };
    }

    bool FileUtil::hasExtension( string_view fileName, string_view extension )
    {
        if ( extension.empty() || fileName.empty() )
            return false;

        string_view want = extension;
        if ( want.front() == '.' )
            want.remove_prefix( 1 );

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );
        if ( dot == string_view::npos || dot < start )
            return false;

        const string_view have = fileName.substr( dot + 1 );
        return StringUtil::equals( have, want, true );
    }

    bool FileUtil::hasAnyExtension( string_view fileName, std::initializer_list<string_view> listExtension )
    {
        for ( string_view extension : listExtension )
        {
            if ( hasExtension( fileName, extension ) )
                return true;
        }
        return false;
    }

    string FileUtil::replaceExtension( string_view fileName, string_view extension )
    {
        if ( fileName.empty() )
            return string( extension );

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );

        string_view base = fileName;
        if ( dot != string_view::npos && dot >= start )
            base = fileName.substr( 0, dot );

        string_view ext = extension;
        if ( ext.empty() )
            return string( base );

        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( base );
        if ( ext.front() != '.' )
            sb.append( '.' );
        sb.append( ext );
        return string( sb.view() );
    }

    void FileUtil::removeExtension( string_view fileName, string_view& outFileName )
    {
        if ( fileName.empty() )
        {
            outFileName = {};
            return;
        }

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );

        if ( dot != string_view::npos && dot >= start )
        {
            outFileName = fileName.substr( 0, dot );
            return;
        }

        outFileName = fileName;
    }

    string FileUtil::removeExtension( string_view fileName )
    {
        string_view stemView;
        removeExtension( fileName, stemView );
        return string{ stemView };
    }

    bool FileUtil::makeRelativePath( string_view rootDir, string_view path, string& outResult )
    {
        if ( rootDir.empty() || path.empty() )
            return false;

        const std::filesystem::path filepath{ path };
        if ( filepath.is_absolute() )
        {
            const std::filesystem::path rootpath{ rootDir };
            std::error_code             ec;
            const std::filesystem::path relative = std::filesystem::relative( filepath, rootpath, ec );
            if ( ec.value() == 0 && relative.empty() == false )
            {
                outResult = string( relative.generic_string().c_str() );
                return true;
            }
            return false;
        }
        return false;
    }

    bool FileUtil::makeAbsolutePath( string_view path, string& outResult )
    {
        std::error_code             ec;
        const std::filesystem::path absolute = std::filesystem::absolute( path, ec );
        if ( ec.value() == 0 && absolute.empty() == false )
        {
            outResult = string( absolute.generic_string().c_str() );
            return true;
        }
        return false;
    }

    bool FileUtil::isAbsolutePath( string_view path )
    {
        if ( path.empty() )
            return false;

        if ( path.size() >= 2 && ( ( 'a' <= path[0] && path[0] <= 'z' ) || ( 'A' <= path[0] && path[0] <= 'Z' ) ) && path[1] == ':' )
            return true;

        return path[0] == '/' || path[0] == '\\';
    }

    string FileUtil::normalizePath( string_view path )
    {
        string outResult{ path };
        for ( utf8& ch : outResult )
        {
            if ( ch == '\\' )
                ch = '/';
            else
                ch = StringUtil::toLowerChar( ch );
        }
        return outResult;
    }

    string FileUtil::normalizeSeparators( string_view path )
    {
        string outResult{ path };
        StringUtil::replaceChar( outResult, '\\', '/' );
        return outResult;
    }

    string FileUtil::toNativeSeparators( string_view path )
    {
        string outResult{ path };
#if defined( SW_PLATFORM_WINDOWS )
        StringUtil::replaceChar( outResult, '/', '\\' );
#else
        StringUtil::replaceChar( outResult, '\\', '/' );
#endif
        return outResult;
    }

    bool FileUtil::pathsEqualNormalized( string_view lhs, string_view rhs )
    {
        const string aNorm = normalizePath( trimTrailingSlashes( lhs ) );
        const string bNorm = normalizePath( trimTrailingSlashes( rhs ) );
        return aNorm == bNorm;
    }

    string FileUtil::trimTrailingSlashes( string_view path )
    {
        string_view v = path;
        while ( v.size() > 1 && ( v.back() == '/' || v.back() == '\\' ) )
        {
            v.remove_suffix( 1 );
        }
        return string{ v };
    }

    string FileUtil::joinPath( string_view root, string_view relative )
    {
        if ( root.empty() )
            return {};

        string_view r = root;
        while ( r.size() > 1 && ( r.back() == '/' || r.back() == '\\' ) )
        {
            r.remove_suffix( 1 );
        }

        if ( relative.empty() )
            return normalizeSeparators( r );

        string_view rel = relative;
        while ( rel.empty() == false && ( rel.front() == '/' || rel.front() == '\\' ) )
        {
            rel.remove_prefix( 1 );
        }

        StringBuilder<constant::kMaxBuffer512> sb;
        sb.ensureCapacity( static_cast<uint32>( r.size() + 1 + rel.size() ) );
        for ( const utf8 ch : r )
            sb.append( ( ch == '\\' ) ? '/' : ch );

        if ( rel.empty() == false )
        {
            if ( sb.view().empty() == false && sb.view().back() != '/' )
                sb.append( '/' );
            for ( const utf8 ch : rel )
                sb.append( ( ch == '\\' ) ? '/' : ch );
        }

        return string( sb.view() );
    }

    bool FileUtil::startsWithPathComponent( string_view path, string_view component )
    {
        if ( path.size() < component.size() )
            return false;
        if ( path.compare( 0, component.size(), component ) != 0 )
            return false;
        return path.size() == component.size() || path[component.size()] == '/' || path[component.size()] == '\\';
    }

    string FileUtil::suffixAfterPathComponent( string_view path, string_view component )
    {
        if ( path.size() <= component.size() )
            return {};
        // "component/" 를 건너뛴다
        return string{ path.substr( component.size() + 1 ) };
    }

    void FileUtil::createParentDirectory( string_view filePath )
    {
        const string directoryPart = getDirectoryPart( filePath );
        if ( directoryPart.empty() || directoryExists( directoryPart ) )
            return;

        std::error_code ec;
        std::filesystem::create_directories( directoryPart.c_str(), ec );
    }

    void FileUtil::ensureDirectoryExists( string_view directoryPath )
    {
        if ( directoryPath.empty() || directoryExists( directoryPath ) )
            return;

        std::error_code ec;
        std::filesystem::create_directories( normalizeSeparators( directoryPath ).c_str(), ec );
    }

    bool FileUtil::fileExists( string_view fileName )
    {
        return std::filesystem::exists( fileName );
    }

    bool FileUtil::directoryExists( string_view path )
    {
        return std::filesystem::exists( path ) && std::filesystem::is_directory( path );
    }

    string FileUtil::getCurrentPath()
    {
        const std::filesystem::path path = std::filesystem::current_path();
        return string( path.generic_string().c_str() );
    }

    string FileUtil::getExecutablePath()
    {
#if defined( SW_PLATFORM_WINDOWS )
        // `GetModuleFileNameW` 는 버퍼가 모자라면 **잘라서** 돌려주고, 그 사실을 반환값(= 버퍼 크기)으로만 알린다. 이를
        // 확인하지 않으면 260자를 넘는 경로에 설치된 순간 exe 위치가 조용히 틀어지고, 그 위치를 기준으로 찾는 모듈 DLL ·
        // RHI 백엔드 · 셰이더 · 리소스가 **모두** "없다" 가 된다. 원인은 어디에도 남지 않는다. 그래서 다 담길 때까지
        // 버퍼를 키운다.
        vector<utf16> listPathBuffer( constant::kMaxPathSize );
        for ( ;; )
        {
            const DWORD writtenCount = GetModuleFileNameW( nullptr, listPathBuffer.data(), static_cast<DWORD>( listPathBuffer.size() ) );
            if ( writtenCount == 0 )
                return string{};

            // 반환값이 버퍼 크기와 같으면 잘린 것이다(작으면 다 담긴 것이다).
            if ( writtenCount < listPathBuffer.size() )
                return StringUtil::utf16ToUtf8( listPathBuffer.data() );

            // 윈도우 경로 상한(32767자)을 넘으면 더 키워 봐야 소용없다.
            if ( listPathBuffer.size() >= kMaxWindowsPathSize )
                return string{};

            listPathBuffer.resize( listPathBuffer.size() * 2 );
        }
#elif defined( SW_PLATFORM_MACOS )
        utf8   arrPathBuf[constant::kMaxBuffer1024];
        uint32 bufSize = sizeof( arrPathBuf );
        if ( _NSGetExecutablePath( arrPathBuf, &bufSize ) == 0 )
        {
            std::error_code ec;
            auto            pathObj = std::filesystem::canonical( arrPathBuf, ec );
            if ( ec.value() == 0 )
                return string{ pathObj.generic_string().c_str() };
            return string{ arrPathBuf };
        }
        return string{};
#else
        std::error_code ec;
        auto            pathObj = std::filesystem::canonical( "/proc/self/exe", ec );
        if ( ec.value() == 0 )
            return string{ pathObj.generic_string().c_str() };
        return string{};
#endif
    }

    uint64 FileUtil::getFileTimestamp( string_view fileName )
    {
        if ( fileExists( fileName ) == false )
            return 0;

        const string                          filePath = normalizeSeparators( fileName );
        const std::filesystem::file_time_type tim      = std::filesystem::last_write_time( filePath.c_str() );
        return std::chrono::duration_cast<std::chrono::duration<uint64>>( tim.time_since_epoch() ).count();
    }

    uint64 FileUtil::getCurrentFileTimestamp()
    {
        const std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
        return std::chrono::duration_cast<std::chrono::duration<uint64>>( now.time_since_epoch() ).count();
    }

    uint64 FileUtil::getFileSize( string_view fileName )
    {
        if ( fileName.empty() )
            return 0;

        // 크기만 알면 되는데 예전에는 파일을 열고 끝까지 이동하고 있었다. 바로 위 getFileTimestamp 와 같은 방식으로
        // 묻는다. 핸들도 플랫폼 분기도 필요 없다.
        const string    filePath = normalizeSeparators( fileName );
        std::error_code errorCode;
        const uintmax_t size = std::filesystem::file_size( filePath.c_str(), errorCode );
        if ( errorCode.value() != 0 )
            return 0;

        return static_cast<uint64>( size );
    }

    bool FileUtil::getFileStamp( string_view fileName, FileStamp& outStamp )
    {
        if ( fileName.empty() )
            return false;
        const string filePath = normalizeSeparators( fileName );
#if defined( SW_PLATFORM_WINDOWS )
        // 크기와 시각을 한 번에 얻는다. `std::filesystem` 으로는 두 번 물어야 한다(`file_size` · `last_write_time`).
        const wstring             widePath = StringUtil::utf8ToUtf16( filePath.c_str() );
        WIN32_FILE_ATTRIBUTE_DATA attribute{};
        if ( GetFileAttributesExW( widePath.c_str(), GetFileExInfoStandard, &attribute ) == FALSE )
            return false;
        if ( ( attribute.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 )
            return false;
        outStamp._size      = ( static_cast<uint64>( attribute.nFileSizeHigh ) << 32 ) | attribute.nFileSizeLow;
        outStamp._writeTime = ( static_cast<uint64>( attribute.ftLastWriteTime.dwHighDateTime ) << 32 ) | attribute.ftLastWriteTime.dwLowDateTime;
        return true;
#else
        std::error_code errorCode;
        const uintmax_t size = std::filesystem::file_size( filePath.c_str(), errorCode );
        if ( errorCode.value() != 0 )
            return false;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time( filePath.c_str(), errorCode );
        if ( errorCode.value() != 0 )
            return false;
        outStamp._size      = static_cast<uint64>( size );
        outStamp._writeTime = static_cast<uint64>( writeTime.time_since_epoch().count() );
        return true;
#endif
    }

    bool FileUtil::copyFile( string_view source, string_view destination )
    {
        std::error_code ec;
        bool            result = std::filesystem::copy_file( source, destination, std::filesystem::copy_options::overwrite_existing, ec );
        if ( ec.value() != 0 )
        {
            SW_LOG_ERROR( "copyFile failed: %#", ec.message().c_str() );
            return false;
        }
        return result;
    }

    bool FileUtil::removeFile( string_view path )
    {
        if ( path.empty() )
            return true;
        const string    normalized = normalizeSeparators( path );
        std::error_code ec;
        std::filesystem::remove( normalized.c_str(), ec );
        return fileExists( normalized ) == false;
    }

    bool FileUtil::removeDirectory( string_view path )
    {
        if ( path.empty() )
            return true;
        const string    normalized = normalizeSeparators( path );
        std::error_code ec;
        std::filesystem::remove_all( normalized.c_str(), ec );
        return directoryExists( normalized ) == false;
    }

    string FileUtil::getTempDirectory()
    {
        std::error_code             ec;
        const std::filesystem::path p = std::filesystem::temp_directory_path( ec );
        if ( ec.value() != 0 )
            return {};
        return string( normalizeSeparators( p.generic_string().c_str() ).c_str() );
    }

    bool FileUtil::writeFile( string_view fileName, const uint8* pData, const uint64 size )
    {
        if ( size == 0 || pData == nullptr )
            return false;

        const string filePath = normalizeSeparators( fileName );
        FILE*        pFile    = PlatformFileUtil::openFile( filePath.c_str(), "wb" );
        if ( pFile == nullptr )
            return false;

        const size_t written = std::fwrite( pData, 1, static_cast<size_t>( size ), pFile );
        std::fclose( pFile );
        return written == static_cast<size_t>( size );
    }

    bool FileUtil::readFile( string_view fileName, vector<uint8>& outBytes, const uint32 offset, const uint32 maxReadCount )
    {
        return FileUtilInternal::readRange( fileName, offset, maxReadCount, outBytes );
    }

    bool FileUtil::readTextFile( string_view fileName, string& outText )
    {
        if ( FileUtilInternal::readRange( fileName, 0, std::numeric_limits<uint64>::max(), outText ) == false )
            return false;

        // BOM 판정의 기준은 아래 skipUtf8Bom 이다. 예전에는 여기서 바이트를 따로 세고 있었다. 한쪽만 고치면 읽기 경로와
        // 질의 경로가 서로 다른 답을 준다.
        const string_view withoutBom = skipUtf8Bom( outText );
        if ( withoutBom.size() != outText.size() )
            outText.erase( 0, outText.size() - withoutBom.size() );

        return true;
    }

    bool FileUtil::writeTextFile( string_view fileName, string_view text )
    {
        const string filePath = normalizeSeparators( fileName );
        FILE*        pFile    = PlatformFileUtil::openFile( filePath.c_str(), "wb" );
        if ( pFile == nullptr )
            return false;

        if ( text.empty() == false )
            std::fwrite( text.data(), 1, text.size(), pFile );
        std::fclose( pFile );
        return true;
    }

    string_view FileUtil::skipUtf8Bom( string_view text )
    {
        if ( text.size() >= 3 &&
             static_cast<uint8>( text[0] ) == 0xEF &&
             static_cast<uint8>( text[1] ) == 0xBB &&
             static_cast<uint8>( text[2] ) == 0xBF )
            return text.substr( 3 );
        return text;
    }

    void FileUtil::skipUtf8Bom( const uint8*& pData, size_t& size )
    {
        if ( pData != nullptr && size >= 3 &&
             pData[0] == 0xEF &&
             pData[1] == 0xBB &&
             pData[2] == 0xBF )
        {
            pData += 3;
            size -= 3;
        }
    }

    void FileUtil::openFileDialog( const FileDialogParams& params, FileDialogDelegate onSuccess )
    {
        if ( onSuccess.isBound() == false )
            return;

        uint32 openGeneration{ 0 };
        {
            std::scoped_lock<mutex> lock( FileDialogQueueInternal::_s_mutex );
            openGeneration = FileDialogQueueInternal::_s_generation;
        }

        std::thread(
            [delegateCallback = std::move( onSuccess ), params, openGeneration]
        {
            vector<string> listResult;
            bool           bSuccess{ false };

#if defined( SW_PLATFORM_WINDOWS )
            bSuccess = WindowsFileDialog::open( params, listResult );
#elif defined( SW_PLATFORM_LINUX )
            bSuccess = LinuxFileDialog::open( params, listResult );
#elif defined( SW_PLATFORM_MACOS )
            bSuccess = MacFileDialog::open( params, listResult );
#else
            (void)params;
            SW_LOG_WARNING( "openFileDialog is not supported on this platform." );
#endif

            if ( bSuccess == false || listResult.empty() )
                return;

            // **여기서 델리게이트를 부르지 않는다.** 이 스레드는 메인 스레드와 아무런 동기화 약속이 없다.
            std::scoped_lock<mutex> lock( FileDialogQueueInternal::_s_mutex );
            if ( FileDialogQueueInternal::_s_generation != openGeneration )
                return; // 여는 사이에 취소됐다. 델리게이트가 가리키던 모듈이 이미 없을 수 있다.
            FileDialogResult result;
            result._delegate = delegateCallback;
            result._listPath = std::move( listResult );
            FileDialogQueueInternal::_s_listResult.push_back( std::move( result ) );
        } )
            .detach();
    }

    void FileUtil::pumpFileDialogResults()
    {
        vector<FileDialogResult> listReady;
        {
            std::scoped_lock<mutex> lock( FileDialogQueueInternal::_s_mutex );
            if ( FileDialogQueueInternal::_s_listResult.empty() )
                return;
            listReady.swap( FileDialogQueueInternal::_s_listResult );
        }

        // 델리게이트는 **락 밖에서** 부른다. 콜백이 다시 다이얼로그를 열면 같은 뮤텍스에 재진입하기 때문이다.
        for ( FileDialogResult& result : listReady )
        {
            if ( result._delegate.isBound() )
                result._delegate( result._listPath );
        }
    }

    void FileUtil::cancelFileDialogResults()
    {
        vector<FileDialogResult> listDropped;
        {
            std::scoped_lock<mutex> lock( FileDialogQueueInternal::_s_mutex );
            ++FileDialogQueueInternal::_s_generation;
            listDropped.swap( FileDialogQueueInternal::_s_listResult );
        }
        // 델리게이트 파괴도 락 밖에서 한다.
        listDropped.clear();
    }

    bool FileUtil::collectFiles( string_view directory, string_view filterExtension, vector<string>& outListFilePath, const bool bRecursive )
    {
        if ( directoryExists( directory ) == false )
            return false;

        const std::filesystem::path directoryPath{ directory };
        const bool                  bHasFilter = filterExtension.empty() == false;

        forEachDirectoryEntry( directoryPath, bRecursive, [&]( const std::filesystem::directory_entry& entry )
        {
            if ( isDirectoryEntry( entry ) )
                return;

            const string genericStd = entry.path().generic_string().c_str();
            string_view  genericView{ genericStd };
            if ( bHasFilter && hasExtension( genericView, filterExtension ) == false )
                return;

            outListFilePath.push_back( string( genericView ) );
        } );

        return true;
    }

    bool FileUtil::collectFolders( string_view directory, vector<string>& outListFolder, const bool bRecursive )
    {
        if ( directoryExists( directory ) == false )
            return false;

        const std::filesystem::path directoryPath{ directory };

        forEachDirectoryEntry( directoryPath, bRecursive, [&]( const std::filesystem::directory_entry& entry )
        {
            if ( isDirectoryEntry( entry ) == false )
                return;

            const string genericStd = entry.path().generic_string().c_str();
            string_view  genericView{ genericStd };
            outListFolder.push_back( string( genericView ) );
        } );

        return true;
    }

    string_view FileUtil::getSharedLibraryPrefix()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return "";
#else
        return "lib";
#endif
    }

    string_view FileUtil::getSharedLibraryExtension()
    {
#if defined( SW_PLATFORM_WINDOWS )
        return ".dll";
#elif defined( SW_PLATFORM_MACOS )
        return ".dylib";
#else
        return ".so";
#endif
    }

    string FileUtil::formatSharedLibraryName( string_view baseName )
    {
        StringBuilder<constant::kMaxBuffer128> sb;
        sb.append( getSharedLibraryPrefix() ).append( baseName ).append( getSharedLibraryExtension() );
        return string( sb.view() );
    }

    string FileUtil::getDebugSymbolPath( string_view libraryPath )
    {
#if defined( SW_PLATFORM_WINDOWS )
        return replaceExtension( libraryPath, ".pdb" );
#elif defined( SW_PLATFORM_MACOS ) || defined( SW_PLATFORM_APPLE )
        StringBuilder<constant::kMaxBuffer256> sb;
        sb.append( libraryPath ).append( ".dSYM" );
        return string( sb.view() );
#else
        return replaceExtension( libraryPath, ".debug" );
#endif
    }

    void* FileUtil::loadDynamicLibrary( string_view libraryName )
    {
        if ( libraryName.empty() )
            return nullptr;

#if defined( SW_PLATFORM_WINDOWS )
        string absPath;
        if ( makeAbsolutePath( libraryName, absPath ) && fileExists( absPath ) )
        {
            // Windows 커널 로더(LOAD_WITH_ALTERED_SEARCH_PATH)는 '\'(백슬래시)를 기준으로 디렉터리를 잘라 DLL 검색 경로의
            // 첫 순위로 넣는다. '/' 경로를 넘기면 디렉터리 해석에 실패해 의존 DLL(Engine.dll 등)을 찾지 못하고
            // ERROR_MOD_NOT_FOUND(126) 오류가 나므로, 네이티브 구분자('\')로 바꾼다.
            const string nativePath = toNativeSeparators( absPath );

            const string dir = getDirectoryPart( nativePath );
            utf8         arrPreviousDllDir[constant::kMaxPathSize]{};
            const DWORD  previousDllDirLen = GetDllDirectoryA( static_cast<DWORD>( sizeof( arrPreviousDllDir ) ), arrPreviousDllDir );
            if ( dir.empty() == false )
                SetDllDirectoryA( dir.c_str() );

            // 1) LOAD_WITH_ALTERED_SEARCH_PATH 로 대상 DLL 의 위치를 가장 먼저 검색해 로드한다
            HMODULE hMod = LoadLibraryExA( nativePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
            // 2) LOAD_WITH_ALTERED_SEARCH_PATH 를 쓰면 SetDllDirectory 가 무시되는 Win32 제약이 있어, 실패하면 LoadLibraryA 로 다시 시도한다
            if ( hMod == nullptr )
                hMod = LoadLibraryA( nativePath.c_str() );

            if ( previousDllDirLen > 0 )
                SetDllDirectoryA( arrPreviousDllDir );
            else
                SetDllDirectoryA( nullptr );

            if ( hMod != nullptr )
                return hMod;
        }
        const string nativeName = toNativeSeparators( libraryName );
        return LoadLibraryA( nativeName.c_str() );
#else
        string absPath;
        if ( makeAbsolutePath( libraryName, absPath ) && fileExists( absPath ) )
        {
            void* pHandle = dlopen( absPath.c_str(), RTLD_NOW | RTLD_LOCAL );
            if ( pHandle != nullptr )
                return pHandle;
        }
        const string libraryNameNt( libraryName );
        return dlopen( libraryNameNt.c_str(), RTLD_NOW | RTLD_LOCAL );
#endif
    }

    void* FileUtil::getDynamicSymbol( void* pHandle, string_view symbolName )
    {
        if ( pHandle == nullptr || symbolName.empty() )
            return nullptr;

        const string symbolNameNt( symbolName );
#if defined( SW_PLATFORM_WINDOWS )
        return reinterpret_cast<void*>( GetProcAddress( static_cast<HMODULE>( pHandle ), symbolNameNt.c_str() ) );
#else
        return dlsym( pHandle, symbolNameNt.c_str() );
#endif
    }

    void FileUtil::unloadDynamicLibrary( void* pHandle )
    {
        if ( pHandle == nullptr )
            return;

#if defined( SW_PLATFORM_WINDOWS )
        FreeLibrary( static_cast<HMODULE>( pHandle ) );
#else
        dlclose( pHandle );
#endif
    }
} // namespace sw

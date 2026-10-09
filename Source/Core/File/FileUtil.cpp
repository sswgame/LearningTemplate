#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Concurrency/mutex.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/Process/CrashHandler.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/string_splitter.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/File/Windows/WindowsFileDialog.h"
#elif defined( SW_PLATFORM_LINUX )
    #include "Core/File/Linux/LinuxFileDialog.h"
#endif

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        struct FileUtilInternal
        {
            /** @brief 임시 파일 이름을 겹치지 않게 하는 번호입니다(같은 프로세스의 여러 스레드가 같은 파일을 동시에 저장해도 서로 밟지 않게). */
            static inline atomic<uint32> s_tempFileSerial{ 0 };

            /** @brief 실패한 쓰기의 임시 파일을 지웁니다. 지우지 못하면 경고합니다 — 원본 옆에 `.tmp<pid>_<번호>` 가 남습니다. */
            static void discardTempFile( const string& tempPath )
            {
                if ( FileUtil::removeFile( tempPath ) == false )
                    SW_LOG_WARNING( "Failed to remove the temporary file %# - delete it by hand", tempPath.c_str() );
            }

            /**
             * @brief @p fileName 을 **원자적으로** 씁니다: 같은 폴더의 임시 파일에 다 쓰고, 쓰기 · 닫기 결과를 확인한 뒤 원본 자리로 바꿔 끼웁니다.
             * @details 주의: 원본을 "wb" 로 열면 그 순간 길이 0 이 됩니다. 그 자리에 쓰면서 `fwrite` · `fclose` 결과를 보지 않으면, 디스크가
             *          차거나 백신이 파일을 잡거나 쓰는 도중 죽을 때 씬 · 에셋 · `.meta` 가 빈 파일로 남는데 저장은 성공으로 보입니다. 4 KB 보다
             *          작은 쓰기는 stdio 버퍼에 머물다 `fclose` 에서야 디스크로 가므로, 가득 찬 디스크는 `fclose` 만 알려 줍니다.
             *          언리얼 `FFileHelper::SaveArrayToFile` 과 같은 방식(임시 파일 → 이름 바꾸기)입니다.
             */
            [[nodiscard]] static bool writeAtomically( string_view fileName, const void* pData, size_t size )
            {
                const string filePath = FileUtil::normalizeSeparators( fileName );
#if defined( SW_PLATFORM_WINDOWS )
                const uint64 processId = static_cast<uint64>( GetCurrentProcessId() );
#else
                const uint64 processId = static_cast<uint64>( ::getpid() );
#endif
                // 같은 폴더에 둔다. 다른 볼륨(시스템 임시 폴더)이면 이름 바꾸기가 원자적이지 않고 복사가 된다.
                StringBuilder<constant::kMaxPathSize> tempPathBuilder;
                tempPathBuilder.append( filePath.c_str() )
                    .append( ".tmp" )
                    .append( processId )
                    .append( '_' )
                    .append( s_tempFileSerial.fetch_add( 1, std::memory_order_relaxed ) );
                const string tempPath{ tempPathBuilder.c_str() };

                FILE* pFile = PlatformFileUtil::openFile( tempPath.c_str(), "wb" );
                if ( pFile == nullptr )
                {
                    SW_LOG_ERROR( "Failed to open temporary file for writing: %#", tempPath.c_str() );
                    return false;
                }

                const size_t written  = ( size > 0 ) ? std::fwrite( pData, 1, size, pFile ) : 0;
                const bool   bFlushed = std::fflush( pFile ) == 0;
                const bool   bClosed  = std::fclose( pFile ) == 0;
                if ( written != size || bFlushed == false || bClosed == false )
                {
                    SW_LOG_ERROR( "Failed to write %# bytes to %# (wrote %#) — the original file was left untouched", size, filePath.c_str(), written );
                    discardTempFile( tempPath );
                    return false;
                }

                // 다른 프로세스(파일 감시 · 백신 · 에디터)가 대상을 잠깐 쥐고 있으면 Windows 의 바꿔치기가 실패한다. 잠깐 기다렸다 다시 한다.
                constexpr uint32 kReplaceAttemptCount = 10;
                for ( uint32 attemptIndex = 0; attemptIndex < kReplaceAttemptCount; ++attemptIndex )
                {
                    if ( PlatformFileUtil::replaceFile( tempPath.c_str(), filePath.c_str() ) )
                        return true;
                    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
                }
                SW_LOG_ERROR( "Failed to replace %# with the newly written file — the original file was left untouched", filePath.c_str() );
                discardTempFile( tempPath );
                return false;
            }

#if !defined( SW_PLATFORM_WINDOWS )
            /**
             * @brief 읽기용으로 열고 크기를 잽니다(처음으로 되감긴 상태). 실패하면 로그를 남기고 nullptr 입니다. 연 파일은 호출하는 쪽이 닫습니다.
             * @details `readFile` · `readTextFile` 이 함께 씁니다(크기를 재지 못했을 때 둘 다 같은 로그를 남깁니다).
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
             *          재려고 파일 위치를 세 번 옮기고, UCRT 의 잠금과 버퍼 준비를 거칩니다. 게다가 `fopen_s` 는 좁은 문자 경로를
             *          **ANSI 코드 페이지**로 해석해서 UTF-8 경로의 한글이 깨집니다(앱 매니페스트가 UTF-8 코드 페이지를 켜지 않습니다).
             *          여기서는 UTF-16 으로 바꿔 엽니다. `readFile` 은 시작 시간에 큰 몫입니다(셰이더 쿠킹 도장이 소스를 읽어 해시합니다).
             *          공유 모드는 stdio(`_SH_DENYNO`)와 같이 읽기 · 쓰기를 허용하므로, 에디터가 쓰고 있는 파일도 열립니다.
             */
            template <typename BufferType>
            [[nodiscard]] static bool readRange( string_view fileName, uint64 offset, uint64 maxReadCount, BufferType& outBuffer )
            {
#if defined( SW_PLATFORM_WINDOWS )
                const string  filePath = FileUtil::normalizeSeparators( fileName );
                const wstring widePath = StringUtil::utf8ToUtf16( filePath.c_str() );
                // FILE_SHARE_DELETE: 읽는 동안에도 저장(`writeAtomically`)이 이 파일을 새 파일로 바꿔 끼울 수 있게 한다. 없으면 에디터가
                // 파일을 읽는 순간 겹친 저장이 실패한다(POSIX 의 rename 은 원래 막히지 않는다).
                HANDLE hFile = CreateFileW( widePath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
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
                // 잰 크기만큼 읽지 못했으면 실패다: 읽기 오류이거나, 읽는 도중 누가 파일을 줄였다(에디터 저장 · 핫 리로드). 버퍼를 줄이고
                // true 를 돌려주면 잘린 JSON · XML · 설정이 "정상으로 읽은 것" 이 되고, 체크섬이 없는 호출부는 알 길이 없다.
                if ( readTotal != dataSize )
                {
                    outBuffer.clear();
                    SW_LOG_ERROR( "Short read (%# of %# bytes) from: %#", readTotal, dataSize, fileName );
                    return false;
                }
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
                if ( PlatformFileUtil::seekTo( pFile, static_cast<int64>( offset ), SEEK_SET ) == false )
                {
                    std::fclose( pFile );
                    SW_LOG_ERROR( "Failed to seek to %# in: %#", offset, fileName );
                    return false;
                }
                outBuffer.resize( static_cast<size_t>( dataSize ) );
                size_t readBytes = 0;
                if ( dataSize > 0 )
                    readBytes = std::fread( outBuffer.data(), 1, static_cast<size_t>( dataSize ), pFile );
                std::fclose( pFile );
                // Windows 갈래와 같은 규칙: 잰 크기만큼 읽지 못했으면 실패다.
                if ( readBytes != static_cast<size_t>( dataSize ) )
                {
                    outBuffer.clear();
                    SW_LOG_ERROR( "Short read (%# of %# bytes) from: %#", readBytes, dataSize, fileName );
                    return false;
                }
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

#if defined( SW_PLATFORM_WINDOWS )
        /** @brief 윈도우 경로 길이의 절대 상한(유니코드 확장 경로, 문자 수)입니다. */
        constexpr size_t kMaxWindowsPathSize = 32768;
#endif

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

        string_view expected = extension;
        if ( expected.front() == '.' )
            expected.remove_prefix( 1 );

        const size_t slash = fileName.find_last_of( "/\\" );
        const size_t start = ( slash == string_view::npos ) ? 0 : slash + 1;
        const size_t dot   = fileName.find_last_of( '.' );
        if ( dot == string_view::npos || dot < start )
            return false;

        const string_view actual = fileName.substr( dot + 1 );
        return StringUtil::equals( actual, expected, true );
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
        {
            sb.append( ( ch == '\\' ) ? '/' : ch );
        }

        if ( rel.empty() == false )
        {
            if ( sb.view().empty() == false && sb.view().back() != '/' )
                sb.append( '/' );
            for ( const utf8 ch : rel )
            {
                sb.append( ( ch == '\\' ) ? '/' : ch );
            }
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

    bool FileUtil::ensureParentDirectoryExists( string_view filePath )
    {
        // 구분자 정규화까지 `ensureDirectoryExists` 와 같게 한다.
        return ensureDirectoryExists( getDirectoryPart( filePath ) );
    }

    string FileUtil::getBinaryDirectory()
    {
        static constexpr string_view kTestBinaryFolder = "TestBin";
        static constexpr string_view kBinaryFolder     = "Bin";
        const string                 executableDir     = getDirectoryPart( getExecutablePath() );
        string_view                  folderName;
        getFileNamePart( executableDir, folderName );
        if ( folderName != kTestBinaryFolder )
            return executableDir;
        const string siblingBinary = joinPath( getDirectoryPart( executableDir ), kBinaryFolder );
        return isDirectory( siblingBinary ) ? siblingBinary : executableDir;
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
#else
        // `/proc/self/exe` 는 실행 파일을 가리키는 링크다 — 풀어 둔 실제 경로가 답이다.
        string executablePath;
        if ( makeCanonicalPath( "/proc/self/exe", executablePath ) )
            return executablePath;
        return string{};
#endif
    }

    bool FileUtil::getFileStamp( string_view fileName, FileStamp& outStamp )
    {
        if ( fileName.empty() )
            return false;
        const string filePath = normalizeSeparators( fileName );
#if defined( SW_PLATFORM_WINDOWS )
        // 크기와 시각을 한 번에 얻는다(`getFileSize` · `getFileWriteTime` 두 번이 아니라).
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
        // 시스템 호출 하나 — Windows 갈래와 같은 "한 번의 조회". 디렉터리는 false(Windows 갈래와 같다).
        struct stat fileStat{};
        if ( ::stat( filePath.c_str(), &fileStat ) != 0 || S_ISREG( fileStat.st_mode ) == 0 )
            return false;
        outStamp._size      = static_cast<uint64>( fileStat.st_size );
        outStamp._writeTime = static_cast<uint64>( fileStat.st_mtim.tv_sec ) * 1'000'000'000ull + static_cast<uint64>( fileStat.st_mtim.tv_nsec );
        return true;
#endif
    }

    bool FileUtil::writeFile( string_view fileName, const uint8* pData, const uint64 size )
    {
        if ( size == 0 || pData == nullptr )
            return false;
        static_assert( sizeof( size_t ) == sizeof( uint64 ), "64 비트 대상만 지원한다 — size_t 로 줄여도 잘리지 않는다" );
        return FileUtilInternal::writeAtomically( fileName, pData, static_cast<size_t>( size ) );
    }

    bool FileUtil::readFile( string_view fileName, vector<uint8>& outBytes, const uint64 offset, const uint64 maxReadCount )
    {
        return FileUtilInternal::readRange( fileName, offset, maxReadCount, outBytes );
    }

    bool FileUtil::readTextFile( string_view fileName, string& outText )
    {
        if ( FileUtilInternal::readRange( fileName, 0, std::numeric_limits<uint64>::max(), outText ) == false )
            return false;

        // BOM 판정의 기준은 아래 skipUtf8Bom 이다. 여기서 바이트를 따로 세면 한쪽만 고쳤을 때 읽기 경로와
        // 질의 경로가 서로 다른 답을 준다.
        const string_view withoutBom = skipUtf8Bom( outText );
        if ( withoutBom.size() != outText.size() )
            outText.erase( 0, outText.size() - withoutBom.size() );

        return true;
    }

    bool FileUtil::writeTextFile( string_view fileName, string_view text )
    {
        return FileUtilInternal::writeAtomically( fileName, text.data(), text.size() );
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
            [delegateCallback = std::move( onSuccess ), params, openGeneration, memoryTag = MemoryProfiler::getCurrentMemoryTag()]
        {
            // 이 스레드의 할당은 대화상자를 연 쪽의 용도로 센다.
            const ScopedMemoryTag threadMemoryTag{ memoryTag };
            CrashHandler::initializeCurrentThread();
            vector<string> listResult;
            bool           bSuccess{ false };

#if defined( SW_PLATFORM_WINDOWS )
            bSuccess = WindowsFileDialog::open( params, listResult );
#elif defined( SW_PLATFORM_LINUX )
            bSuccess = LinuxFileDialog::open( params, listResult );
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
        if ( isDirectory( directory ) == false )
            return false;

        const bool bHasFilter = filterExtension.empty() == false;
        // 열지 못하는 디렉터리는 위에서 걸렀다. 순회 도중 멈춘 것은 거기까지 모은 것으로 끝난다(순회가 경고한다).
        (void)forEachDirectoryEntry( directory, bRecursive, [&outListFilePath, filterExtension, bHasFilter]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory || ( bHasFilter && hasExtension( entry._path, filterExtension ) == false ) )
                return true;
            outListFilePath.push_back( string( entry._path ) );
            return true;
        } );
        return true;
    }

    bool FileUtil::collectFolders( string_view directory, vector<string>& outListFolder, const bool bRecursive )
    {
        if ( isDirectory( directory ) == false )
            return false;

        (void)forEachDirectoryEntry( directory, bRecursive, [&outListFolder]( const DirectoryEntry& entry )
        {
            if ( entry._bDirectory )
                outListFolder.push_back( string( entry._path ) );
            return true;
        } );
        return true;
    }
} // namespace sw

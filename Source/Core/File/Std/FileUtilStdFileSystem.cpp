/**
 * @file FileUtilStdFileSystem.cpp
 * @brief `FileUtil` 가운데 파일 시스템에 묻는 함수(존재 · 속성 · 크기 · 시각 · 순회 · 만들기 · 지우기 · 복사 · 경로 해석)의 `std::filesystem` 구현입니다.
 * @details **`<filesystem>` 은 이 폴더(`Core/File/Std/`) 밖에서 include 하지 않습니다**(`CheckStdFilesystemIsolation`). 한 함수를 플랫폼 API 로 바꿀 때는
 *          그 정의를 `File/Windows` · `File/Linux` 로 옮기면 되고 호출부는 그대로입니다 — 측정에서 이길 때만 합니다(docs/09_Decisions.md 3절).
 *          - 오류는 `error_code` 판으로만 받습니다. 예외 판은 권한 · 경로 변환 실패에서 던집니다.
 *          - 경로는 넓은 문자로 만듭니다(Windows). 좁은 문자 생성자는 ANSI 코드 페이지로 바꾸다 잘못된 UTF-8 바이트에서 **던집니다**.
 *          - `path` 는 표준 할당자를 씁니다(sw 할당자 밖). 호출 하나에 `path` 하나, 순회는 항목마다 둘입니다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/WallClock.h"

#include <chrono>
#include <filesystem>
#include <limits>

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        struct FileUtilStdInternal
        {
            /** @brief 파일 시각 100 ns 눈금의 길이 타입입니다. */
            using TickDuration = std::chrono::duration<int64, std::ratio<1, FileUtil::kFileTimeTicksPerSecond>>;

#if !defined( SW_PLATFORM_WINDOWS )
            /** @brief 1601-01-01(Windows FILETIME 기원)에서 유닉스 기원까지의 100 ns 눈금 수입니다. */
            static constexpr int64 kUnixEpochTicks = 116'444'736'000'000'000;

            /**
             * @brief `file_clock` 이 `WallClock` 보다 몇 눈금 앞서는지입니다(처음 한 번 측정한다).
             * @details libstdc++ 의 `file_clock` 은 기원이 2174 년이라(C++20 의 `clock_cast` 를 쓸 수 없는 C++17 이다) 두 시계를 맞대어 측정한다.
             *          창이 가장 좁은 표본을 골라 오차를 줄이고, 한 번만 재므로 읽기 · 쓰기가 같은 값으로 오간다.
             */
            static int64 getFileClockOffsetTicks()
            {
                static const int64 s_offset = []()
                {
                    constexpr int64 kTicksPerMilli = FileUtil::kFileTimeTicksPerSecond / constant::kMillisecondsPerSecond;
                    int64           bestOffset     = 0;
                    int64           bestWindow     = std::numeric_limits<int64>::max();
                    for ( uint32 sample = 0; sample < 8; ++sample )
                    {
                        const int64 beforeMilli = WallClock::nowUnixMilliseconds().count();
                        const int64 file        = std::chrono::duration_cast<TickDuration>( std::filesystem::file_time_type::clock::now().time_since_epoch() ).count();
                        const int64 afterMilli  = WallClock::nowUnixMilliseconds().count();
                        if ( afterMilli - beforeMilli < bestWindow )
                        {
                            bestWindow = afterMilli - beforeMilli;
                            bestOffset = file - ( beforeMilli + afterMilli ) * kTicksPerMilli / 2;
                        }
                    }
                    return bestOffset;
                }();
                return s_offset;
            }

#endif

            /** @brief 파일 시각을 1601 기원 100 ns 눈금으로 바꿉니다(플랫폼마다 `file_clock` 기원이 달라 같은 기원으로 맞춘다). */
            static int64 toTicks( const std::filesystem::file_time_type& writeTime )
            {
                const int64 fileTicks = std::chrono::duration_cast<TickDuration>( writeTime.time_since_epoch() ).count();
#if defined( SW_PLATFORM_WINDOWS )
                return fileTicks; // 이미 1601 기원이다
#else
                return fileTicks - getFileClockOffsetTicks() + kUnixEpochTicks;
#endif
            }

            /** @brief `toTicks` 의 역입니다. */
            static std::filesystem::file_time_type fromTicks( const int64 ticks )
            {
#if defined( SW_PLATFORM_WINDOWS )
                const int64 fileTicks = ticks;
#else
                const int64 fileTicks = ticks - kUnixEpochTicks + getFileClockOffsetTicks();
#endif
                using FileDuration = std::filesystem::file_time_type::duration;
                return std::filesystem::file_time_type{ std::chrono::duration_cast<FileDuration>( TickDuration{ fileTicks } ) };
            }

            /** @brief UTF-8 경로를 `path` 로 만듭니다. Windows 는 넓은 문자로 직접 만든다(좁은 문자 생성자는 잘못된 UTF-8 에서 던진다). */
            static std::filesystem::path toPath( string_view path )
            {
#if defined( SW_PLATFORM_WINDOWS )
                const string  separated = FileUtil::normalizeSeparators( path );
                const wstring widePath  = StringUtil::utf8ToUtf16( separated.c_str() );
                return std::filesystem::path( widePath.c_str() );
#else
                // POSIX 의 좁은 문자 경로는 인코딩 변환이 없다. 구분자만 맞춘다(`\` 가 섞인 경로도 Windows 와 같은 폴더를 가리킨다).
                const string separated = FileUtil::normalizeSeparators( path );
                return std::filesystem::path( std::string_view( separated.data(), separated.size() ) );
#endif
            }

            /** @brief `path` 를 `/` 구분 UTF-8 로 @p outPath 에 담습니다(버퍼를 다시 쓴다). */
            static void toUtf8( const std::filesystem::path& path, string& outPath )
            {
#if defined( SW_PLATFORM_WINDOWS )
                outPath = StringUtil::utf16ToUtf8( path.c_str() );
                StringUtil::replaceChar( outPath, '\\', '/' );
#else
                outPath.assign( path.c_str() );
#endif
            }

            /** @brief `path` 를 `/` 구분 UTF-8 로 돌려줍니다. */
            static string toUtf8( const std::filesystem::path& path )
            {
                string result;
                toUtf8( path, result );
                return result;
            }

            /** @brief OS 오류를 사람이 읽는 문장으로 바꿉니다(실패 경로에서만 부른다 — 문자열을 만든다). */
            static string describe( const std::error_code& errorCode ) { return string( errorCode.message().c_str() ); }

            /**
             * @brief 순회자 하나를 끝까지 돌며 항목마다 @p visit 를 부릅니다. 순회자 종류(재귀 · 한 단계)만 다르고 몸은 같다.
             * @return 순회가 도중에 멈췄으면(경고를 남긴다) false. 콜백이 멈춘 것은 실패가 아니다.
             */
            template <typename Iterator>
            [[nodiscard]] static bool visitEntries( Iterator& iterator, string_view directory, DirectoryEntryVisitFn visit, void* pContext )
            {
                string          entryPath;
                const Iterator  end{};
                std::error_code errorCode;
                while ( iterator != end )
                {
                    std::error_code typeError;
                    const bool      bDirectory = iterator->is_directory( typeError );
                    toUtf8( iterator->path(), entryPath );
                    const DirectoryEntry entry{ string_view{ entryPath }, typeError ? false : bDirectory };
                    if ( visit( pContext, entry ) == false )
                        return true;
                    iterator.increment( errorCode );
                    if ( errorCode )
                    {
                        SW_LOG_WARNING( "Directory walk of '%#' stopped early at '%#': %#", directory, entryPath.c_str(), describe( errorCode ).c_str() );
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace

    bool FileUtil::makeRelativePath( string_view rootDir, string_view path, string& outResult )
    {
        if ( rootDir.empty() || path.empty() )
            return false;

        const std::filesystem::path filePath = FileUtilStdInternal::toPath( path );
        if ( filePath.is_absolute() == false )
            return false;
        std::error_code             errorCode;
        const std::filesystem::path relative = std::filesystem::relative( filePath, FileUtilStdInternal::toPath( rootDir ), errorCode );
        if ( errorCode || relative.empty() )
            return false;
        FileUtilStdInternal::toUtf8( relative, outResult );
        return true;
    }

    bool FileUtil::makeAbsolutePath( string_view path, string& outResult )
    {
        std::error_code             errorCode;
        const std::filesystem::path absolute = std::filesystem::absolute( FileUtilStdInternal::toPath( path ), errorCode );
        if ( errorCode || absolute.empty() )
            return false;
        FileUtilStdInternal::toUtf8( absolute, outResult );
        return true;
    }

    bool FileUtil::makeCanonicalPath( string_view path, string& outResult )
    {
        std::error_code             errorCode;
        const std::filesystem::path canonical = std::filesystem::canonical( FileUtilStdInternal::toPath( path ), errorCode );
        if ( errorCode || canonical.empty() )
            return false;
        FileUtilStdInternal::toUtf8( canonical, outResult );
        return true;
    }

    bool FileUtil::ensureDirectoryExists( string_view directoryPath )
    {
        if ( directoryPath.empty() || isDirectory( directoryPath ) )
            return true;

        std::error_code errorCode;
        if ( std::filesystem::create_directories( FileUtilStdInternal::toPath( directoryPath ), errorCode ) )
            noteFileCreated(); // 폴더를 만들었다(없는 폴더를 기억한 캐시가 버린다)
        // 성공은 오류 코드가 아니라 "폴더가 있는가" 로만 가른다. 병렬 쿠킹 · 임포트가 같은 폴더를 동시에 만들 때 libstdc++ · libc++ 의 `create_directories` 는
        // 다른 스레드가 먼저 만든 경쟁에서 오류 코드를 줄 수 있다(MSVC STL 은 주지 않는다). 폴더가 이미 있으면 성공이고, 같은 이름의 파일이 막고 있으면 여전히 실패다.
        if ( isDirectory( directoryPath ) == false )
        {
            SW_LOG_ERROR_ID( "File.CreateDirectoryFailed", "Could not create directory '%#': %#", directoryPath, errorCode ? FileUtilStdInternal::describe( errorCode ).c_str() : "a file is in the way" );
            return false;
        }
        return true;
    }

    bool FileUtil::exists( string_view path )
    {
        if ( path.empty() )
            return false;
        std::error_code errorCode;
        return std::filesystem::exists( FileUtilStdInternal::toPath( path ), errorCode );
    }

    bool FileUtil::isDirectory( string_view path )
    {
        if ( path.empty() )
            return false;
        std::error_code errorCode;
        return std::filesystem::is_directory( FileUtilStdInternal::toPath( path ), errorCode );
    }

    bool FileUtil::isRegularFile( string_view path )
    {
        if ( path.empty() )
            return false;
        std::error_code errorCode;
        return std::filesystem::is_regular_file( FileUtilStdInternal::toPath( path ), errorCode );
    }

    bool FileUtil::isReadOnlyFile( string_view fileName )
    {
        if ( fileName.empty() )
            return false;
        std::error_code                    errorCode;
        const std::filesystem::file_status status = std::filesystem::status( FileUtilStdInternal::toPath( fileName ), errorCode );
        if ( errorCode || std::filesystem::is_regular_file( status ) == false )
            return false;
        return ( status.permissions() & std::filesystem::perms::owner_write ) == std::filesystem::perms::none;
    }

    bool FileUtil::setWritable( string_view path, const bool bWritable )
    {
        constexpr std::filesystem::perms kAllWrite = std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write;
        std::error_code                  errorCode;
        if ( bWritable )
            std::filesystem::permissions( FileUtilStdInternal::toPath( path ), std::filesystem::perms::owner_write, std::filesystem::perm_options::add, errorCode );
        else
            std::filesystem::permissions( FileUtilStdInternal::toPath( path ), kAllWrite, std::filesystem::perm_options::remove, errorCode );
        if ( errorCode )
        {
            SW_LOG_WARNING( "Could not make '%#' %#: %#", path, bWritable ? "writable" : "read-only", FileUtilStdInternal::describe( errorCode ).c_str() );
            return false;
        }
        return true;
    }

    string FileUtil::getCurrentPath()
    {
        std::error_code             errorCode;
        const std::filesystem::path current = std::filesystem::current_path( errorCode );
        if ( errorCode )
        {
            SW_LOG_WARNING( "Could not read the current directory: %#", FileUtilStdInternal::describe( errorCode ).c_str() );
            return {};
        }
        return FileUtilStdInternal::toUtf8( current );
    }

    bool FileUtil::setCurrentPath( string_view path )
    {
        if ( path.empty() )
            return false;
        std::error_code errorCode;
        std::filesystem::current_path( FileUtilStdInternal::toPath( path ), errorCode );
        if ( errorCode )
        {
            SW_LOG_WARNING( "Could not change the current directory to '%#': %#", path, FileUtilStdInternal::describe( errorCode ).c_str() );
            return false;
        }
        return true;
    }

    uint64 FileUtil::getFileTimestamp( string_view fileName )
    {
        if ( fileName.empty() )
            return 0;
        // 있는지 따로 묻지 않는다 — 묻고 나서 읽는 사이에 지워지면(파일 감시 · 핫 리로드) 예외 판은 던진다.
        std::error_code                       errorCode;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time( FileUtilStdInternal::toPath( fileName ), errorCode );
        if ( errorCode )
            return 0;
        return static_cast<uint64>( FileUtilStdInternal::toTicks( writeTime ) / kFileTimeTicksPerSecond );
    }

    uint64 FileUtil::getCurrentFileTimestamp()
    {
        return static_cast<uint64>( getCurrentFileWriteTime() / kFileTimeTicksPerSecond );
    }

    bool FileUtil::getFileWriteTime( string_view path, int64& outTicks )
    {
        outTicks = 0;
        if ( path.empty() )
            return false;
        std::error_code                       errorCode;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time( FileUtilStdInternal::toPath( path ), errorCode );
        if ( errorCode )
            return false;
        outTicks = FileUtilStdInternal::toTicks( writeTime );
        return true;
    }

    bool FileUtil::setFileWriteTime( string_view path, const int64 ticks )
    {
        const std::filesystem::file_time_type writeTime = FileUtilStdInternal::fromTicks( ticks );
        std::error_code                       errorCode;
        std::filesystem::last_write_time( FileUtilStdInternal::toPath( path ), writeTime, errorCode );
        if ( errorCode )
        {
            SW_LOG_WARNING( "Could not set the write time of '%#': %#", path, FileUtilStdInternal::describe( errorCode ).c_str() );
            return false;
        }
        return true;
    }

    int64 FileUtil::getCurrentFileWriteTime()
    {
        return FileUtilStdInternal::toTicks( std::filesystem::file_time_type::clock::now() );
    }

    uint64 FileUtil::getFileSize( string_view fileName )
    {
        if ( fileName.empty() )
            return 0;
        // 크기만 알면 되므로 파일을 열지 않는다.
        std::error_code errorCode;
        const uintmax_t size = std::filesystem::file_size( FileUtilStdInternal::toPath( fileName ), errorCode );
        if ( errorCode )
            return 0;
        return static_cast<uint64>( size );
    }

    bool FileUtil::copyFile( string_view source, string_view destination )
    {
        std::error_code errorCode;
        const bool      bCopied = std::filesystem::copy_file( FileUtilStdInternal::toPath( source ), FileUtilStdInternal::toPath( destination ),
                                                              std::filesystem::copy_options::overwrite_existing, errorCode );
        if ( errorCode )
        {
            SW_LOG_ERROR( "copyFile '%#' -> '%#' failed: %#", source, destination, FileUtilStdInternal::describe( errorCode ).c_str() );
            return false;
        }
        noteFileCreated(); // 복사는 대상 파일을 만든다(없는 파일을 기억한 캐시가 버린다)
        return bCopied;
    }

    bool FileUtil::removeFile( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        if ( std::filesystem::remove( FileUtilStdInternal::toPath( path ), errorCode ) )
            noteFileRemoved(); // 지웠다(있는 파일을 기억한 캐시가 버린다)
        if ( exists( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove '%#': %#", path, errorCode ? FileUtilStdInternal::describe( errorCode ).c_str() : "still there" );
        return false;
    }

    bool FileUtil::tryRemoveFile( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        if ( std::filesystem::remove( FileUtilStdInternal::toPath( path ), errorCode ) )
            noteFileRemoved();
        return exists( path ) == false;
    }

    bool FileUtil::removeDirectory( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        if ( std::filesystem::remove_all( FileUtilStdInternal::toPath( path ), errorCode ) > 0 )
            noteFileRemoved(); // 일부만 지웠어도 지운 항목의 옛 경로는 낡았다
        if ( isDirectory( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove directory '%#': %#", path, errorCode ? FileUtilStdInternal::describe( errorCode ).c_str() : "still there" );
        return false;
    }

    string FileUtil::getTempDirectory()
    {
        std::error_code             errorCode;
        const std::filesystem::path tempPath = std::filesystem::temp_directory_path( errorCode );
        if ( errorCode )
            return {};
        return FileUtilStdInternal::toUtf8( tempPath );
    }

    bool FileUtil::forEachDirectoryEntry( string_view directory, const bool bRecursive, const DirectoryEntryVisitFn visit, void* pContext )
    {
        if ( visit == nullptr || directory.empty() )
            return false;

        const std::filesystem::path root = FileUtilStdInternal::toPath( directory );
        std::error_code             errorCode;
        if ( bRecursive )
        {
            // `skip_permission_denied` — 들어갈 수 없는 하위 폴더는 멈추지 않고 건너뛴다.
            std::filesystem::recursive_directory_iterator iterator{ root, std::filesystem::directory_options::skip_permission_denied, errorCode };
            if ( errorCode )
                return false;
            return FileUtilStdInternal::visitEntries( iterator, directory, visit, pContext );
        }

        std::filesystem::directory_iterator iterator{ root, std::filesystem::directory_options::skip_permission_denied, errorCode };
        if ( errorCode )
            return false;
        return FileUtilStdInternal::visitEntries( iterator, directory, visit, pContext );
    }
} // namespace sw

/**
 * @file FileUtilStdFileSystem.cpp
 * @brief `FileUtil` 가운데 파일 시스템에 묻는 함수(존재 · 속성 · 크기 · 시각 · 순회 · 만들기 · 지우기 · 복사 · 경로 해석)의 `std::filesystem` 구현입니다.
 * @details **`<filesystem>` 은 이 폴더(`Core/File/Std/`) 밖에서 include 하지 않습니다**(`CheckStdFilesystemIsolation`). 한 함수를 플랫폼 API 로 바꿀 때는
 *          그 정의를 `File/Windows` · `File/Linux` 로 옮기면 되고 호출부는 그대로입니다 — 측정에서 이길 때만 합니다(백로그 3-12).
 *          - 오류는 `error_code` 판으로만 받습니다. 예외 판은 권한 · 경로 변환 실패에서 던집니다.
 *          - 경로는 넓은 문자로 만듭니다(Windows). 좁은 문자 생성자는 ANSI 코드 페이지로 바꾸다 잘못된 UTF-8 바이트에서 **던집니다**.
 *          - `path` 는 표준 할당자를 씁니다(sw 할당자 밖). 호출 하나에 `path` 하나, 순회는 항목마다 둘입니다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include <filesystem>

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        struct FileUtilStdInternal
        {
            /** @brief 파일 시각 100 ns 눈금의 길이 타입입니다. */
            using TickDuration = std::chrono::duration<int64, std::ratio<1, FileUtil::kFileTimeTicksPerSecond>>;

            /** @brief UTF-8 경로를 `path` 로 만듭니다. Windows 는 넓은 문자로 직접 만든다(좁은 문자 생성자는 잘못된 UTF-8 에서 던진다). */
            static std::filesystem::path toPath( string_view path )
            {
#if defined( SW_PLATFORM_WINDOWS )
                const string  separated = FileUtil::normalizeSeparators( path );
                const wstring widePath  = StringUtil::utf8ToUtf16( separated.c_str() );
                return std::filesystem::path( widePath.c_str() );
#else
                // POSIX 의 좁은 문자 경로는 바이트 그대로다(변환이 없다).
                return std::filesystem::path( std::string_view( path.data(), path.size() ) );
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

            /** @brief 순회자 하나를 끝까지 돌며 항목마다 @p visit 를 부릅니다. 순회자 종류(재귀 · 한 단계)만 다르고 몸은 같다. */
            template <typename Iterator>
            static void visitEntries( Iterator& iterator, string_view directory, DirectoryEntryVisitFn visit, void* pContext )
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
                        return;
                    iterator.increment( errorCode );
                    if ( errorCode )
                    {
                        SW_LOG_WARNING( "Directory walk of '%#' stopped early at '%#': %#", directory, entryPath.c_str(), describe( errorCode ).c_str() );
                        return;
                    }
                }
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
        if ( directoryPath.empty() || directoryExists( directoryPath ) )
            return true;

        std::error_code errorCode;
        std::filesystem::create_directories( FileUtilStdInternal::toPath( directoryPath ), errorCode );
        if ( errorCode || directoryExists( directoryPath ) == false )
        {
            SW_LOG_ERROR( "Could not create directory '%#': %#", directoryPath, errorCode ? FileUtilStdInternal::describe( errorCode ).c_str() : "a file is in the way" );
            return false;
        }
        return true;
    }

    bool FileUtil::fileExists( string_view fileName )
    {
        if ( fileName.empty() )
            return false;
        std::error_code errorCode;
        return std::filesystem::exists( FileUtilStdInternal::toPath( fileName ), errorCode );
    }

    bool FileUtil::directoryExists( string_view path )
    {
        if ( path.empty() )
            return false;
        std::error_code errorCode;
        return std::filesystem::is_directory( FileUtilStdInternal::toPath( path ), errorCode );
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

    uint64 FileUtil::getFileTimestamp( string_view fileName )
    {
        if ( fileName.empty() )
            return 0;
        // 있는지 따로 묻지 않는다 — 묻고 나서 읽는 사이에 지워지면(파일 감시 · 핫 리로드) 예외 판은 던진다.
        std::error_code                       errorCode;
        const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time( FileUtilStdInternal::toPath( fileName ), errorCode );
        if ( errorCode )
            return 0;
        return std::chrono::duration_cast<std::chrono::duration<uint64>>( writeTime.time_since_epoch() ).count();
    }

    uint64 FileUtil::getCurrentFileTimestamp()
    {
        const std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
        return std::chrono::duration_cast<std::chrono::duration<uint64>>( now.time_since_epoch() ).count();
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
        outTicks = std::chrono::duration_cast<FileUtilStdInternal::TickDuration>( writeTime.time_since_epoch() ).count();
        return true;
    }

    bool FileUtil::setFileWriteTime( string_view path, const int64 ticks )
    {
        using FileDuration = std::filesystem::file_time_type::duration;
        const std::filesystem::file_time_type writeTime{ std::chrono::duration_cast<FileDuration>( FileUtilStdInternal::TickDuration{ ticks } ) };
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
        const std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
        return std::chrono::duration_cast<FileUtilStdInternal::TickDuration>( now.time_since_epoch() ).count();
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
        return bCopied;
    }

    bool FileUtil::removeFile( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        std::filesystem::remove( FileUtilStdInternal::toPath( path ), errorCode );
        if ( fileExists( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove '%#': %#", path, errorCode ? FileUtilStdInternal::describe( errorCode ).c_str() : "still there" );
        return false;
    }

    bool FileUtil::tryRemoveFile( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        std::filesystem::remove( FileUtilStdInternal::toPath( path ), errorCode );
        return fileExists( path ) == false;
    }

    bool FileUtil::removeDirectory( string_view path )
    {
        if ( path.empty() )
            return true;
        std::error_code errorCode;
        std::filesystem::remove_all( FileUtilStdInternal::toPath( path ), errorCode );
        if ( directoryExists( path ) == false )
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
            FileUtilStdInternal::visitEntries( iterator, directory, visit, pContext );
            return true;
        }

        std::filesystem::directory_iterator iterator{ root, std::filesystem::directory_options::skip_permission_denied, errorCode };
        if ( errorCode )
            return false;
        FileUtilStdInternal::visitEntries( iterator, directory, visit, pContext );
        return true;
    }
} // namespace sw

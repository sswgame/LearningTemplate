/**
 * @file FileUtilFileSystem.cpp
 * @brief `FileUtil` 가운데 파일 시스템에 묻는 함수(존재 · 속성 · 크기 · 시각 · 순회 · 만들기 · 지우기 · 복사 · 경로 해석)입니다.
 * @details OS 호출은 `PlatformFileSystem` 의 원시 연산(Win32 · POSIX 네이티브, `File/Windows` · `File/Linux`)뿐이고, 여기서는 그 위에 재귀 만들기 · 지우기 · 순회 · 고유 경로 ·
 *          상대 경로(글 연산)와 로그 · 자동화 쓰기 문(`isWriteAllowed`) · 캐시 알림(`noteFileCreated` · `noteFileRemoved`)을 얹는다. `std::filesystem` 은 쓰지 않는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileSystem.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Path/AbsolutePath.h"

namespace sw
{
    SW_LOG_CALLER( "FileUtil" );

    namespace
    {
        struct FileUtilFileSystemInternal
        {
            /** @brief 재귀 순회가 폴더마다 쓰는 상태입니다. */
            struct WalkContext
            {
                string                _directory; ///< `/` 구분, 끝에 구분자 없음
                bool                  _bRecursive{ false };
                DirectoryEntryVisitFn _visit{ nullptr };
                void*                 _pContext{ nullptr };
                bool                  _bStopped{ false }; ///< 콜백이 멈췄다
            };

            /** @brief 폴더 안 이름들을 모으는 상태입니다. */
            struct CollectContext
            {
                vector<string> _listName;
                vector<uint8>  _listDirectory; ///< `_listName` 과 같은 순서, 링크가 아닌 폴더면 1
            };

            /** @brief 경로를 `/` 구분으로 맞추고 끝의 구분자를 지웁니다(루트 `/` · `C:/` 는 남긴다). */
            static string trimTrailingSeparators( string_view path )
            {
                string result = FileUtil::normalizeSeparators( path );
                while ( result.size() > 1 && result.back() == '/' && result.back() != result[result.size() - 2] && result[result.size() - 2] != ':' )
                    result.pop_back();
                return result;
            }

            /** @brief 경로를 구간으로 나눕니다(빈 구간 제외). 글을 가리키는 뷰라 @p path 가 살아 있는 동안만 유효합니다. */
            static vector<string_view> splitComponents( const string& path )
            {
                vector<string_view> listComponent;
                size_t              begin = 0;
                while ( begin < path.size() )
                {
                    size_t end = path.find( '/', begin );
                    if ( end == string::npos )
                        end = path.size();
                    if ( end > begin )
                        listComponent.push_back( string_view{ path.data() + begin, end - begin } );
                    begin = end + 1;
                }
                return listComponent;
            }

            /** @brief 두 경로 구간이 같은지입니다(Windows 는 대소문자 무시 — 영문자만 접는다). */
            static bool isSameComponent( string_view left, string_view right )
            {
                if ( left.size() != right.size() )
                    return false;
                for ( size_t index = 0; index < left.size(); ++index )
                {
                    if ( PlatformFileSystem::kPathsAreCaseInsensitive )
                    {
                        if ( StringUtil::toLowerChar( left[index] ) != StringUtil::toLowerChar( right[index] ) )
                            return false;
                    }
                    else if ( left[index] != right[index] )
                    {
                        return false;
                    }
                }
                return true;
            }

            /** @brief 폴더 하나와 그 아래를 만듭니다. 이 호출이 하나라도 만들었으면 true 입니다(이미 있거나 다른 스레드가 먼저 만들었으면 false). */
            static bool createDirectoryTree( string_view path )
            {
                if ( PlatformFileSystem::createDirectory( path ) )
                    return true;
                if ( FileUtil::isDirectory( path ) )
                    return false;
                const string parent = FileUtil::getDirectoryPart( path );
                if ( parent.empty() || parent == path )
                    return false;
                const bool bParentCreated = createDirectoryTree( parent );
                return PlatformFileSystem::createDirectory( path ) || bParentCreated;
            }

            static bool collectChild( void* pContext, string_view name, bool bDirectory, bool bLink )
            {
                CollectContext& context = *static_cast<CollectContext*>( pContext );
                context._listName.push_back( string{ name } );
                context._listDirectory.push_back( bDirectory && bLink == false ? 1 : 0 );
                return true;
            }

            /** @brief 폴더 안을 비우고 폴더 자신도 지웁니다(링크는 따라가지 않고 링크만 지운다). 지운 항목 수를 돌려줍니다. */
            static size_t removeTree( const string& directory )
            {
                CollectContext collected;
                (void)PlatformFileSystem::forEachChild( directory, collectChild, &collected ); // 못 열면 폴더 지우기가 실패로 알린다 - 이유는 호출한 쪽이 로그
                size_t removedCount = 0;
                for ( size_t index = 0; index < collected._listName.size(); ++index )
                {
                    string child = directory;
                    child.push_back( '/' );
                    child.append( collected._listName[index] );
                    if ( collected._listDirectory[index] != 0 )
                        removedCount += removeTree( child );
                    else if ( PlatformFileSystem::removeEntry( child ) )
                        ++removedCount;
                }
                if ( PlatformFileSystem::removeEntry( directory ) )
                    ++removedCount;
                return removedCount;
            }

            static bool walkChild( void* pContext, string_view name, bool bDirectory, bool bLink )
            {
                WalkContext& walk = *static_cast<WalkContext*>( pContext );
                string       path = walk._directory;
                path.push_back( '/' );
                path.append( name.data(), name.size() );

                const DirectoryEntry entry{ string_view{ path }, bDirectory };
                if ( walk._visit( walk._pContext, entry ) == false )
                {
                    walk._bStopped = true;
                    return false;
                }
                if ( walk._bRecursive && bDirectory && bLink == false )
                {
                    WalkContext child;
                    child._directory  = path;
                    child._bRecursive = true;
                    child._visit      = walk._visit;
                    child._pContext   = walk._pContext;
                    // 들어갈 수 없는 하위 폴더는 멈추지 않고 건너뛴다(권한 없음).
                    (void)PlatformFileSystem::forEachChild( path, walkChild, &child );
                    if ( child._bStopped )
                    {
                        walk._bStopped = true;
                        return false;
                    }
                }
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool FileUtil::makeRelativePath( string_view rootDir, string_view path, string& outResult )
    {
        if ( rootDir.empty() || path.empty() || AbsolutePath::isStrictAbsolute( path ) == false )
            return false;

        // 글 연산이다(링크를 풀지 않는다). `.` · `..` 는 절대화가 정리한다.
        string absoluteRoot;
        string absolutePath;
        if ( PlatformFileSystem::makeAbsolute( rootDir, absoluteRoot ) == false || PlatformFileSystem::makeAbsolute( path, absolutePath ) == false )
            return false;
        const vector<string_view> listRoot = FileUtilFileSystemInternal::splitComponents( absoluteRoot );
        const vector<string_view> listPath = FileUtilFileSystemInternal::splitComponents( absolutePath );

        size_t commonCount = 0;
        while ( commonCount < listRoot.size() && commonCount < listPath.size() && FileUtilFileSystemInternal::isSameComponent( listRoot[commonCount], listPath[commonCount] ) )
            ++commonCount;
        if ( PlatformFileSystem::kPathsAreCaseInsensitive && commonCount == 0 )
            return false; // 드라이브 · 서버가 다르면 상대 경로가 없다

        outResult.clear();
        for ( size_t index = commonCount; index < listRoot.size(); ++index )
        {
            if ( outResult.empty() == false )
                outResult.push_back( '/' );
            outResult.append( ".." );
        }
        for ( size_t index = commonCount; index < listPath.size(); ++index )
        {
            if ( outResult.empty() == false )
                outResult.push_back( '/' );
            outResult.append( listPath[index].data(), listPath[index].size() );
        }
        if ( outResult.empty() )
            outResult = ".";
        return true;
    }

    bool FileUtil::makeAbsolutePath( string_view path, string& outResult )
    {
        return PlatformFileSystem::makeAbsolute( path, outResult );
    }

    bool FileUtil::makeCanonicalPath( string_view path, string& outResult )
    {
        return PlatformFileSystem::makeCanonical( path, outResult );
    }

    bool FileUtil::ensureDirectoryExists( string_view directoryPath )
    {
        if ( directoryPath.empty() || isDirectory( directoryPath ) )
            return true;

        const string directory = FileUtilFileSystemInternal::trimTrailingSeparators( directoryPath );
        if ( FileUtilFileSystemInternal::createDirectoryTree( directory ) )
            noteFileCreated(); // 폴더를 만들었다(없는 폴더를 기억한 캐시가 버린다)
        // 성공은 "폴더가 있는가" 로만 가른다. 병렬 쿠킹 · 임포트가 같은 폴더를 동시에 만들 때 다른 스레드가 먼저 만든 경쟁은 실패가 아니다.
        // 같은 이름의 파일이 막고 있으면 여전히 실패다.
        if ( isDirectory( directory ) == false )
        {
            SW_LOG_ERROR_ID( "File.CreateDirectoryFailed", "Could not create directory '%#': %#", directoryPath, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    string FileUtil::reserveUniquePath( string_view basePath, string_view extension, bool bDirectory, uint32 maxAttempt, string_view numberSeparator )
    {
        if ( basePath.empty() || ensureParentDirectoryExists( basePath ) == false )
            return {};
        for ( uint32 attempt = 1; attempt <= maxAttempt; ++attempt )
        {
            string path{ basePath };
            if ( attempt > 1 )
            {
                utf8 arrNumber[constant::kMaxBuffer64];
                formatstring( arrNumber, constant::kMaxBuffer64, "%#%#", numberSeparator, attempt );
                path.append( arrNumber );
            }
            path.append( extension );
            if ( isWriteAllowed( path ) == false )
                return {};
            if ( bDirectory )
            {
                // createDirectory 는 이 호출이 만들었을 때만 true 다 - 이미 있으면(다른 프로세스가 먼저 잡았으면) 다음 번호로.
                if ( PlatformFileSystem::createDirectory( path ) )
                {
                    noteFileCreated();
                    return path;
                }
                if ( exists( path ) == false )
                {
                    SW_LOG_ERROR_ID( "File.CreateDirectoryFailed", "Could not create directory '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
                    return {};
                }
                continue;
            }
            // "x"(C11): 이미 있으면 열지 않는다 - 만들기와 있는지 보기가 한 시스템 호출이다.
            FILE* pFile = PlatformFileUtil::openFile( path.c_str(), "wbx" );
            if ( pFile != nullptr )
            {
                std::fclose( pFile );
                noteFileCreated();
                return path;
            }
            if ( exists( path ) == false )
            {
                SW_LOG_ERROR_ID( "File.CreateFailed", "Could not create file '%#'", path );
                return {};
            }
        }
        SW_LOG_ERROR_ID( "File.CreateFailed", "Every name from '%#%#' to suffix %#%# is taken", basePath, extension, numberSeparator, maxAttempt );
        return {};
    }

    bool FileUtil::exists( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status );
    }

    bool FileUtil::isDirectory( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status ) && status._kind == PlatformFileSystem::FileKind::Directory;
    }

    bool FileUtil::isRegularFile( string_view path )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( path, status ) && status._kind == PlatformFileSystem::FileKind::File;
    }

    bool FileUtil::isReadOnlyFile( string_view fileName )
    {
        PlatformFileSystem::FileStatus status;
        return PlatformFileSystem::queryStatus( fileName, status ) && status._kind == PlatformFileSystem::FileKind::File && status._bReadOnly;
    }

    bool FileUtil::setWritable( string_view path, const bool bWritable )
    {
        if ( PlatformFileSystem::setWritable( path, bWritable ) == false )
        {
            SW_LOG_WARNING( "Could not make '%#' %#: %#", path, bWritable ? "writable" : "read-only", PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    string FileUtil::getSavedFolderPath()
    {
        return normalizeSeparators( joinPath( getCurrentPath(), path::kSavedFolder ) );
    }

    string FileUtil::getCurrentPath()
    {
        string current;
        if ( PlatformFileSystem::getCurrentDirectory( current ) == false )
        {
            SW_LOG_WARNING( "Could not read the current directory: %#", PlatformFileSystem::describeLastError().c_str() );
            return {};
        }
        return current;
    }

    bool FileUtil::setCurrentPath( string_view path )
    {
        if ( PlatformFileSystem::setCurrentDirectory( path ) == false )
        {
            SW_LOG_WARNING( "Could not change the current directory to '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    bool FileUtil::tryGetFileTimestamp( string_view fileName, uint64& outSeconds )
    {
        // 있는지 따로 묻지 않는다 - 묻고 나서 읽는 사이에 지워지면(파일 감시 · 핫 리로드) 어긋난다. 한 번 묻는다.
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( fileName, status ) == false )
            return false;
        outSeconds = static_cast<uint64>( status._writeTicks / kFileTimeTicksPerSecond );
        return true;
    }

    uint64 FileUtil::getCurrentFileTimestamp()
    {
        return static_cast<uint64>( getCurrentFileWriteTime() / kFileTimeTicksPerSecond );
    }

    bool FileUtil::getFileWriteTime( string_view path, int64& outTicks )
    {
        outTicks = 0;
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( path, status ) == false )
            return false;
        outTicks = status._writeTicks;
        return true;
    }

    bool FileUtil::setFileWriteTime( string_view path, const int64 ticks )
    {
        if ( PlatformFileSystem::setWriteTicks( path, ticks ) == false )
        {
            SW_LOG_WARNING( "Could not set the write time of '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        return true;
    }

    int64 FileUtil::getCurrentFileWriteTime()
    {
        return PlatformFileSystem::getNowTicks();
    }

    bool FileUtil::tryGetFileSize( string_view fileName, uint64& outSize )
    {
        // 크기만 알면 되므로 파일을 열지 않는다.
        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( fileName, status ) == false || status._kind != PlatformFileSystem::FileKind::File )
            return false;
        outSize = status._size;
        return true;
    }

    bool FileUtil::copyFile( string_view source, string_view destination )
    {
        if ( isWriteAllowed( destination ) == false )
            return false;
        if ( PlatformFileSystem::copyFileOverwrite( source, destination ) == false )
        {
            SW_LOG_ERROR( "copyFile '%#' -> '%#' failed: %#", source, destination, PlatformFileSystem::describeLastError().c_str() );
            return false;
        }
        noteFileCreated(); // 복사는 대상 파일을 만든다(없는 파일을 기억한 캐시가 버린다)
        return true;
    }

    bool FileUtil::removeFile( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false ) // 없는 파일을 지우는 정리는 바꾸는 것이 아니다
            return false;
        if ( PlatformFileSystem::removeEntry( path ) )
            noteFileRemoved(); // 지웠다(있는 파일을 기억한 캐시가 버린다)
        if ( exists( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
        return false;
    }

    bool FileUtil::tryRemoveFile( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false )
            return false;
        if ( PlatformFileSystem::removeEntry( path ) )
            noteFileRemoved();
        return exists( path ) == false;
    }

    bool FileUtil::removeDirectory( string_view path )
    {
        if ( path.empty() )
            return true;
        if ( exists( path ) && isWriteAllowed( path ) == false )
            return false;

        PlatformFileSystem::FileStatus status;
        if ( PlatformFileSystem::queryStatus( path, status ) )
        {
            const string directory      = FileUtilFileSystemInternal::trimTrailingSeparators( path );
            const bool   bRealDirectory = status._kind == PlatformFileSystem::FileKind::Directory && status._bLink == false;
            const size_t removedCount   = bRealDirectory ? FileUtilFileSystemInternal::removeTree( directory ) : ( PlatformFileSystem::removeEntry( directory ) ? 1 : 0 );
            if ( removedCount > 0 )
                noteFileRemoved(); // 일부만 지웠어도 지운 항목의 옛 경로는 낡았다
        }
        if ( isDirectory( path ) == false )
            return true;
        SW_LOG_WARNING( "Could not remove directory '%#': %#", path, PlatformFileSystem::describeLastError().c_str() );
        return false;
    }

    string FileUtil::getTempDirectory()
    {
        string temp;
        if ( PlatformFileSystem::getTempDirectory( temp ) == false )
            return {};
        return temp;
    }

    bool FileUtil::forEachDirectoryEntry( string_view directory, const bool bRecursive, const DirectoryEntryVisitFn visit, void* pContext )
    {
        if ( visit == nullptr || directory.empty() )
            return false;

        FileUtilFileSystemInternal::WalkContext walk;
        walk._directory  = FileUtilFileSystemInternal::trimTrailingSeparators( directory );
        walk._bRecursive = bRecursive;
        walk._visit      = visit;
        walk._pContext   = pContext;
        return PlatformFileSystem::forEachChild( walk._directory, FileUtilFileSystemInternal::walkChild, &walk );
    }
} // namespace sw

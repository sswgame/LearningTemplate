/**
 * @file LinuxFileSystem.cpp
 * @brief `PlatformFileSystem` 의 파일 시스템 연산(질의 · 폴더 · 지우기 · 복사 · 시각 · 경로 해석 · 나열)의 POSIX 구현입니다.
 * @details POSIX 의 좁은 문자 경로는 인코딩 변환이 없다 - 구분자만 `/` 로 맞춘다. 시각은 Windows 와 같은 눈금(1601 기원 100 ns)으로 바꿔 주고받는다.
 *          Windows 짝은 `File/Windows/WindowsFileSystem.cpp` 다.
 */
#include "pch.h"

#include "Core/Foundation/Defines.h"
#include "Core/Foundation/PlatformOSHeaders.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Container/StringUtil.h"
    #include "Core/File/PlatformFileSystem.h"
    #include "Core/Process/ProcessEnvironment.h"

    #include <cerrno>
    #include <climits>
    #include <cstring>
    #include <dirent.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <time.h>
    #include <unistd.h>

namespace sw
{
    namespace
    {
        struct LinuxFileSystemInternal
        {
            /** @brief 1601-01-01(Windows FILETIME 기원)에서 유닉스 기원까지의 100 ns 눈금 수입니다. */
            static constexpr int64 kUnixEpochTicks = 116'444'736'000'000'000;
            /** @brief 복사가 한 번에 읽는 바이트 수입니다. */
            static constexpr size_t kCopyChunkSize = 64 * constant::kBytesPerKilobyte;

            /** @brief 구분자를 `/` 로 맞춘 경로입니다(`\` 가 섞인 경로도 Windows 와 같은 폴더를 가리킨다). */
            static string toNative( string_view path )
            {
                string separated{ path };
                StringUtil::replaceChar( separated, '\\', '/' );
                return separated;
            }

            static int64 toTicks( const timespec& time ) { return static_cast<int64>( time.tv_sec ) * 10'000'000 + time.tv_nsec / 100 + kUnixEpochTicks; }

            static timespec fromTicks( const int64 ticks )
            {
                const int64 sinceUnix = ticks - kUnixEpochTicks;
                int64       seconds   = sinceUnix / 10'000'000;
                int64       rest      = sinceUnix % 10'000'000;
                if ( rest < 0 ) // 1970 이전
                {
                    rest += 10'000'000;
                    --seconds;
                }
                timespec time;
                time.tv_sec  = static_cast<time_t>( seconds );
                time.tv_nsec = static_cast<long>( rest * 100 );
                return time;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PlatformFileSystem::queryStatus( string_view path, FileStatus& outStatus )
    {
        if ( path.empty() )
            return false;
        const string native = LinuxFileSystemInternal::toNative( path );
        struct stat  info;
        struct stat  linkInfo;
        outStatus._bLink = lstat( native.c_str(), &linkInfo ) == 0 && S_ISLNK( linkInfo.st_mode );
        if ( stat( native.c_str(), &info ) != 0 ) // 링크는 따라간다
            return false;
        outStatus._kind       = S_ISDIR( info.st_mode ) ? FileKind::Directory : ( S_ISREG( info.st_mode ) ? FileKind::File : FileKind::Other );
        outStatus._size       = static_cast<uint64>( info.st_size );
        outStatus._writeTicks = LinuxFileSystemInternal::toTicks( info.st_mtim );
        outStatus._bReadOnly  = ( info.st_mode & S_IWUSR ) == 0;
        return true;
    }

    bool PlatformFileSystem::createDirectory( string_view path )
    {
        if ( path.empty() )
            return false;
        return mkdir( LinuxFileSystemInternal::toNative( path ).c_str(), 0777 ) == 0; // umask 가 깎는다
    }

    bool PlatformFileSystem::removeEntry( string_view path )
    {
        if ( path.empty() )
            return false;
        return remove( LinuxFileSystemInternal::toNative( path ).c_str() ) == 0; // 파일 · 링크는 unlink, 폴더는 rmdir
    }

    bool PlatformFileSystem::copyFileOverwrite( string_view source, string_view destination )
    {
        if ( source.empty() || destination.empty() )
            return false;
        const int sourceFd = open( LinuxFileSystemInternal::toNative( source ).c_str(), O_RDONLY | O_CLOEXEC );
        if ( sourceFd < 0 )
            return false;
        struct stat info;
        if ( fstat( sourceFd, &info ) != 0 || S_ISREG( info.st_mode ) == false )
        {
            close( sourceFd );
            return false;
        }
        const int destinationFd = open( LinuxFileSystemInternal::toNative( destination ).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, info.st_mode & 0777 );
        if ( destinationFd < 0 )
        {
            close( sourceFd );
            return false;
        }

        bool         bOk = true;
        vector<utf8> chunk( LinuxFileSystemInternal::kCopyChunkSize );
        while ( bOk )
        {
            const ssize_t readBytes = read( sourceFd, chunk.data(), chunk.size() );
            if ( readBytes == 0 )
                break;
            if ( readBytes < 0 )
            {
                bOk = errno == EINTR;
                continue;
            }
            ssize_t written = 0;
            while ( bOk && written < readBytes )
            {
                const ssize_t count = write( destinationFd, chunk.data() + written, static_cast<size_t>( readBytes - written ) );
                if ( count < 0 )
                    bOk = errno == EINTR;
                else
                    written += count;
            }
        }
        const int lastError = errno;
        close( sourceFd );
        if ( close( destinationFd ) != 0 )
            bOk = false;
        if ( bOk == false )
            errno = lastError;
        return bOk;
    }

    bool PlatformFileSystem::setWritable( string_view path, const bool bWritable )
    {
        if ( path.empty() )
            return false;
        const string native = LinuxFileSystemInternal::toNative( path );
        struct stat  info;
        if ( stat( native.c_str(), &info ) != 0 )
            return false;
        // 켤 때는 소유자만(파일이 가진 다른 쓰기 비트는 그대로), 끌 때는 셋 모두 끈다.
        const mode_t mode = bWritable ? ( info.st_mode | S_IWUSR ) : ( info.st_mode & ~static_cast<mode_t>( S_IWUSR | S_IWGRP | S_IWOTH ) );
        return chmod( native.c_str(), mode & 07777 ) == 0;
    }

    bool PlatformFileSystem::setWriteTicks( string_view path, const int64 ticks )
    {
        if ( path.empty() )
            return false;
        const timespec arrTime[2] = {
            { 0, UTIME_OMIT },
            LinuxFileSystemInternal::fromTicks( ticks )
        }; // 접근 시각은 건드리지 않는다
        return utimensat( AT_FDCWD, LinuxFileSystemInternal::toNative( path ).c_str(), arrTime, 0 ) == 0;
    }

    int64 PlatformFileSystem::getNowTicks()
    {
        timespec now;
        clock_gettime( CLOCK_REALTIME, &now ); // WallClock 은 밀리초 눈금이라 100 ns 파일 시각에 못 쓴다
        return LinuxFileSystemInternal::toTicks( now );
    }

    bool PlatformFileSystem::makeAbsolute( string_view path, string& outResult )
    {
        if ( path.empty() )
            return false;
        string native = LinuxFileSystemInternal::toNative( path );
        if ( native[0] != '/' )
        {
            string current;
            if ( getCurrentDirectory( current ) == false )
                return false;
            current.push_back( '/' );
            current.append( native );
            native = current;
        }

        // `.` · `..` 를 글로 정리한다(`realpath` 와 달리 파일이 없어도 된다). 링크는 풀지 않는다.
        string result;
        size_t begin = 0;
        while ( begin <= native.size() )
        {
            size_t end = native.find( '/', begin );
            if ( end == string::npos )
                end = native.size();
            const string_view part{ native.data() + begin, end - begin };
            if ( part == ".." )
            {
                const size_t slash = result.rfind( '/' );
                if ( slash != string::npos )
                    result.erase( slash );
            }
            else if ( part.empty() == false && part != "." )
            {
                result.push_back( '/' );
                result.append( part.data(), part.size() );
            }
            begin = end + 1;
        }
        outResult = result.empty() ? string( "/" ) : result;
        return true;
    }

    bool PlatformFileSystem::makeCanonical( string_view path, string& outResult )
    {
        if ( path.empty() )
            return false;
        utf8 arrResolved[PATH_MAX];
        if ( realpath( LinuxFileSystemInternal::toNative( path ).c_str(), arrResolved ) == nullptr )
            return false;
        outResult = arrResolved;
        return true;
    }

    bool PlatformFileSystem::getCurrentDirectory( string& outResult )
    {
        utf8 arrCurrent[PATH_MAX];
        if ( getcwd( arrCurrent, sizeof( arrCurrent ) ) == nullptr )
            return false;
        outResult = arrCurrent;
        return true;
    }

    bool PlatformFileSystem::setCurrentDirectory( string_view path )
    {
        return path.empty() == false && chdir( LinuxFileSystemInternal::toNative( path ).c_str() ) == 0;
    }

    bool PlatformFileSystem::getExecutablePath( string& outResult )
    {
        // `/proc/self/exe` 는 실행 파일을 가리키는 링크다 - 풀어 둔 실제 경로가 답이다.
        return makeCanonical( "/proc/self/exe", outResult );
    }

    bool PlatformFileSystem::getTempDirectory( string& outResult )
    {
        // 표준 라이브러리 `temp_directory_path` 와 같은 순서로 찾고, 모두 없으면 `/tmp` 다.
        for ( const utf8* pName : { "TMPDIR", "TMP", "TEMP", "TEMPDIR" } )
        {
            if ( ProcessEnvironment::tryReadNonEmpty( pName, outResult ) )
            {
                while ( outResult.size() > 1 && outResult.back() == '/' )
                    outResult.pop_back();
                return true;
            }
        }
        outResult = "/tmp";
        return true;
    }

    bool PlatformFileSystem::forEachChild( string_view directory, const ChildVisitFn visit, void* pContext )
    {
        if ( directory.empty() || visit == nullptr )
            return false;
        const string native = LinuxFileSystemInternal::toNative( directory );
        DIR* const   pDir   = opendir( native.c_str() );
        if ( pDir == nullptr )
            return false;

        for ( const dirent* pEntry = readdir( pDir ); pEntry != nullptr; pEntry = readdir( pDir ) )
        {
            const utf8* pName = pEntry->d_name;
            if ( pName[0] == '.' && ( pName[1] == '\0' || ( pName[1] == '.' && pName[2] == '\0' ) ) )
                continue;

            bool bLink      = pEntry->d_type == DT_LNK;
            bool bDirectory = pEntry->d_type == DT_DIR;
            if ( pEntry->d_type == DT_UNKNOWN || bLink ) // 파일 시스템이 종류를 주지 않거나 링크이면 묻는다(링크는 가리키는 쪽의 종류)
            {
                string full{ native };
                full.push_back( '/' );
                full.append( pName );
                struct stat info;
                if ( pEntry->d_type == DT_UNKNOWN )
                {
                    struct stat linkInfo;
                    bLink = lstat( full.c_str(), &linkInfo ) == 0 && S_ISLNK( linkInfo.st_mode );
                }
                bDirectory = stat( full.c_str(), &info ) == 0 && S_ISDIR( info.st_mode );
            }
            if ( visit( pContext, string_view{ pName }, bDirectory, bLink ) == false )
                break;
        }
        closedir( pDir );
        return true;
    }

    string PlatformFileSystem::describeLastError()
    {
        const int code = errno;
        utf8      arrCode[constant::kMaxBuffer64];
        formatstring( arrCode, constant::kMaxBuffer64, " (%#)", code );
        string message{ std::strerror( code ) };
        message.append( arrCode );
        return message;
    }
} // namespace sw
#endif

/**
 * @file WindowsFileSystem.cpp
 * @brief `PlatformFileSystem` 의 파일 시스템 연산(질의 · 폴더 · 지우기 · 복사 · 시각 · 경로 해석 · 나열)의 Win32 구현입니다.
 * @details 경로는 UTF-8 → UTF-16 으로 직접 바꿔 넓은 문자 API 에 넘긴다(좁은 문자 API 는 ANSI 코드 페이지로 읽는다). 오류는 던지지 않고 `bool` 로 돌려준다.
 *          shortcut: 경로를 돌려주는 호출은 `kMaxBuffer4096` 글자 버퍼 하나로 받는다 - 더 긴 경로는 실패로 돌려준다(긴 경로 접두 `\\?\` 를 쓰게 되면 늘린다).
 *          POSIX 짝은 `File/Linux/LinuxFileSystem.cpp` 다.
 */
#include "pch.h"

#include "Core/Foundation/Defines.h"
#include "Core/Foundation/PlatformOSHeaders.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Container/StringUtil.h"
    #include "Core/File/PlatformFileSystem.h"
    #include "Core/File/PlatformFileUtil.h"

namespace sw
{
    namespace
    {
        struct WindowsFileSystemInternal
        {
            static constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
            /** @brief 윈도우 경로 길이의 절대 상한(유니코드 확장 경로, 문자 수)입니다. */
            static constexpr size_t kMaxWindowsPathSize = 32768;

            /** @brief UTF-8 경로를 넓은 문자 경로로 바꿉니다(구분자는 `\`). 빈 경로는 빈 글입니다. */
            static wstring toWide( string_view path )
            {
                // 파일 시스템 연산이 모두 지나는 길목이다 - 상대 경로는 빈 글로 돌려 그 연산을 실패시킨다(R2).
                if ( PlatformFileUtil::acceptsPathInput( path ) == false )
                    return {};
                string separated{ path };
                StringUtil::replaceChar( separated, '/', '\\' );
                return StringUtil::utf8ToUTF16( separated.c_str() );
            }

            /** @brief 넓은 문자 경로를 `/` 구분 UTF-8 로 바꿉니다. */
            static string toUTF8( const wchar_t* pWidePath )
            {
                string result = StringUtil::utf16ToUTF8( pWidePath );
                StringUtil::replaceChar( result, '\\', '/' );
                return result;
            }

            static int64 toTicks( const FILETIME& fileTime ) { return static_cast<int64>( ( static_cast<uint64>( fileTime.dwHighDateTime ) << 32 ) | fileTime.dwLowDateTime ); }

            static FILETIME fromTicks( const int64 ticks )
            {
                FILETIME fileTime;
                fileTime.dwLowDateTime  = static_cast<DWORD>( static_cast<uint64>( ticks ) & 0xFFFFFFFFu );
                fileTime.dwHighDateTime = static_cast<DWORD>( static_cast<uint64>( ticks ) >> 32 );
                return fileTime;
            }

            /** @brief 심볼릭 링크 · 정션(폴더를 다른 곳으로 잇는 재해석 지점)인지입니다. OneDrive 자리 표시 같은 다른 재해석 지점은 링크가 아니다. */
            static bool isLinkTag( const DWORD reparseTag ) { return reparseTag == IO_REPARSE_TAG_SYMLINK || reparseTag == IO_REPARSE_TAG_MOUNT_POINT; }

            /** @brief Win32 가 쓴 글자 수 @p written 이 버퍼에 들어갔는지입니다(0 은 실패, 용량 이상은 모자람 - 성공이면 끝 0 을 뺀 글자 수가 온다). */
            static bool fits( const DWORD written, const uint32 capacity ) { return written != 0 && written < capacity; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PlatformFileSystem::queryStatus( string_view path, FileStatus& outStatus )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;

        WIN32_FILE_ATTRIBUTE_DATA data{};
        if ( GetFileAttributesExW( wide.c_str(), GetFileExInfoStandard, &data ) == FALSE )
            return false;

        outStatus._bLink = false;
        // 링크의 속성은 링크 자체의 것이다 - 가리키는 대상의 종류 · 크기는 핸들을 열어 묻는다(끊어진 링크는 없는 것).
        if ( ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) != 0 )
        {
            const HANDLE handle = CreateFileW( wide.c_str(), FILE_READ_ATTRIBUTES, WindowsFileSystemInternal::kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
            if ( handle == INVALID_HANDLE_VALUE )
                return false;
            BY_HANDLE_FILE_INFORMATION info{};
            const BOOL                 bOk = GetFileInformationByHandle( handle, &info );
            FILE_ATTRIBUTE_TAG_INFO    tagInfo{};
            if ( bOk != FALSE && GetFileInformationByHandleEx( handle, FileAttributeTagInfo, &tagInfo, sizeof( tagInfo ) ) != FALSE )
                outStatus._bLink = WindowsFileSystemInternal::isLinkTag( tagInfo.ReparseTag );
            CloseHandle( handle );
            if ( bOk == FALSE )
                return false;
            data.dwFileAttributes = info.dwFileAttributes;
            data.nFileSizeHigh    = info.nFileSizeHigh;
            data.nFileSizeLow     = info.nFileSizeLow;
            data.ftLastWriteTime  = info.ftLastWriteTime;
        }

        outStatus._kind       = ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ? FileKind::Directory : FileKind::File;
        outStatus._size       = ( static_cast<uint64>( data.nFileSizeHigh ) << 32 ) | data.nFileSizeLow;
        outStatus._writeTicks = WindowsFileSystemInternal::toTicks( data.ftLastWriteTime );
        outStatus._bReadOnly  = ( data.dwFileAttributes & FILE_ATTRIBUTE_READONLY ) != 0;
        return true;
    }

    bool PlatformFileSystem::createDirectory( string_view path )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        return wide.empty() == false && CreateDirectoryW( wide.c_str(), nullptr ) != FALSE;
    }

    bool PlatformFileSystem::removeEntry( string_view path )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;
        const DWORD attributes = GetFileAttributesW( wide.c_str() );
        if ( attributes == INVALID_FILE_ATTRIBUTES )
            return false;
        if ( ( attributes & FILE_ATTRIBUTE_READONLY ) != 0 && SetFileAttributesW( wide.c_str(), attributes & ~static_cast<DWORD>( FILE_ATTRIBUTE_READONLY ) ) == FALSE )
            return false;
        // 폴더 링크도 `RemoveDirectoryW` 가 링크만 지운다(가리키는 폴더는 건드리지 않는다).
        const BOOL bRemoved = ( attributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ? RemoveDirectoryW( wide.c_str() ) : DeleteFileW( wide.c_str() );
        return bRemoved != FALSE;
    }

    bool PlatformFileSystem::copyFileOverwrite( string_view source, string_view destination )
    {
        const wstring wideSource      = WindowsFileSystemInternal::toWide( source );
        const wstring wideDestination = WindowsFileSystemInternal::toWide( destination );
        return wideSource.empty() == false && wideDestination.empty() == false && CopyFileW( wideSource.c_str(), wideDestination.c_str(), FALSE ) != FALSE;
    }

    bool PlatformFileSystem::setWritable( string_view path, const bool bWritable )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;
        const DWORD attributes = GetFileAttributesW( wide.c_str() );
        if ( attributes == INVALID_FILE_ATTRIBUTES )
            return false;
        DWORD updated = bWritable ? ( attributes & ~static_cast<DWORD>( FILE_ATTRIBUTE_READONLY ) ) : ( attributes | FILE_ATTRIBUTE_READONLY );
        if ( updated == 0 )
            updated = FILE_ATTRIBUTE_NORMAL; // 속성 0 은 "바꾸지 않음" 이라 NORMAL 로 쓴다
        return SetFileAttributesW( wide.c_str(), updated ) != FALSE;
    }

    bool PlatformFileSystem::setWriteTicks( string_view path, const int64 ticks )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;
        const HANDLE handle = CreateFileW( wide.c_str(), FILE_WRITE_ATTRIBUTES, WindowsFileSystemInternal::kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
        if ( handle == INVALID_HANDLE_VALUE )
            return false;
        const FILETIME fileTime = WindowsFileSystemInternal::fromTicks( ticks );
        const BOOL     bOk      = SetFileTime( handle, nullptr, nullptr, &fileTime );
        CloseHandle( handle );
        return bOk != FALSE;
    }

    int64 PlatformFileSystem::getNowTicks()
    {
        FILETIME fileTime;
        GetSystemTimePreciseAsFileTime( &fileTime ); // WallClock 은 밀리초 눈금이라 100 ns 파일 시각에 못 쓴다
        return WindowsFileSystemInternal::toTicks( fileTime );
    }

    bool PlatformFileSystem::makeAbsolute( string_view path, string& outResult )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;
        wchar_t     arrAbsolute[constant::kMaxBuffer4096];
        const DWORD written = GetFullPathNameW( wide.c_str(), constant::kMaxBuffer4096, arrAbsolute, nullptr );
        if ( WindowsFileSystemInternal::fits( written, constant::kMaxBuffer4096 ) == false )
            return false;
        outResult = WindowsFileSystemInternal::toUTF8( arrAbsolute );
        return true;
    }

    bool PlatformFileSystem::makeCanonical( string_view path, string& outResult )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        if ( wide.empty() )
            return false;
        const HANDLE handle = CreateFileW( wide.c_str(), 0, WindowsFileSystemInternal::kShareAll, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
        if ( handle == INVALID_HANDLE_VALUE )
            return false;
        wchar_t     arrFinal[constant::kMaxBuffer4096];
        const DWORD written = GetFinalPathNameByHandleW( handle, arrFinal, constant::kMaxBuffer4096, VOLUME_NAME_DOS );
        CloseHandle( handle );
        if ( WindowsFileSystemInternal::fits( written, constant::kMaxBuffer4096 ) == false )
            return false;

        // `\\?\C:\x` → `C:\x`, `\\?\UNC\server\share` → `\\server\share`
        const wstring finalPath{ arrFinal };
        if ( finalPath.compare( 0, 8, L"\\\\?\\UNC\\" ) == 0 )
        {
            wstring unc{ L"\\\\" };
            unc.append( finalPath, 8, wstring::npos );
            outResult = WindowsFileSystemInternal::toUTF8( unc.c_str() );
        }
        else if ( finalPath.compare( 0, 4, L"\\\\?\\" ) == 0 )
        {
            outResult = WindowsFileSystemInternal::toUTF8( arrFinal + 4 );
        }
        else
        {
            outResult = WindowsFileSystemInternal::toUTF8( arrFinal );
        }
        return true;
    }

    bool PlatformFileSystem::getCurrentDirectory( string& outResult )
    {
        wchar_t     arrCurrent[constant::kMaxBuffer4096];
        const DWORD written = GetCurrentDirectoryW( constant::kMaxBuffer4096, arrCurrent );
        if ( WindowsFileSystemInternal::fits( written, constant::kMaxBuffer4096 ) == false )
            return false;
        outResult = WindowsFileSystemInternal::toUTF8( arrCurrent );
        return true;
    }

    bool PlatformFileSystem::setCurrentDirectory( string_view path )
    {
        const wstring wide = WindowsFileSystemInternal::toWide( path );
        return wide.empty() == false && SetCurrentDirectoryW( wide.c_str() ) != FALSE;
    }

    bool PlatformFileSystem::getExecutablePath( string& outResult )
    {
        // `GetModuleFileNameW` 는 버퍼가 모자라면 **잘라서** 돌려주고, 그 사실을 반환값(= 버퍼 크기)으로만 알린다. 잘린 경로를 쓰면 exe 위치를 기준으로 찾는
        // 모듈 DLL · RHI 백엔드 · 셰이더가 모두 "없다" 가 되고 원인은 남지 않는다. 그래서 다 담길 때까지 버퍼를 키운다(윈도우 경로 상한 32767 자까지).
        wstring buffer( constant::kMaxPathSize, L'\0' );
        while ( buffer.size() <= WindowsFileSystemInternal::kMaxWindowsPathSize )
        {
            const DWORD written = GetModuleFileNameW( nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
            if ( written == 0 )
                return false;
            if ( written < buffer.size() ) // 같으면 잘린 것이다
            {
                buffer.resize( written );
                outResult = WindowsFileSystemInternal::toUTF8( buffer.c_str() );
                return true;
            }
            buffer.resize( buffer.size() * 2 );
        }
        return false;
    }

    bool PlatformFileSystem::getTempDirectory( string& outResult )
    {
        wchar_t     arrTemp[constant::kMaxBuffer4096];
        const DWORD written = GetTempPathW( constant::kMaxBuffer4096, arrTemp );
        if ( WindowsFileSystemInternal::fits( written, constant::kMaxBuffer4096 ) == false )
            return false;
        outResult = WindowsFileSystemInternal::toUTF8( arrTemp );
        while ( outResult.size() > 1 && outResult.back() == '/' )
            outResult.pop_back();
        return true;
    }

    bool PlatformFileSystem::forEachChild( string_view directory, const ChildVisitFn visit, void* pContext )
    {
        wstring pattern = WindowsFileSystemInternal::toWide( directory );
        if ( pattern.empty() || visit == nullptr )
            return false;
        if ( pattern.back() != L'\\' )
            pattern.push_back( L'\\' );
        pattern.push_back( L'*' );

        WIN32_FIND_DATAW data{};
        const HANDLE     find = FindFirstFileExW( pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH );
        if ( find == INVALID_HANDLE_VALUE )
            return false;

        do
        {
            const wchar_t* pName = data.cFileName;
            if ( pName[0] == L'.' && ( pName[1] == L'\0' || ( pName[1] == L'.' && pName[2] == L'\0' ) ) )
                continue;
            const string name       = StringUtil::utf16ToUTF8( pName );
            const bool   bDirectory = ( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0;
            const bool   bLink      = ( data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ) != 0 && WindowsFileSystemInternal::isLinkTag( data.dwReserved0 );
            if ( visit( pContext, name, bDirectory, bLink ) == false )
                break;
        } while ( FindNextFileW( find, &data ) != FALSE );

        FindClose( find );
        return true;
    }

    string PlatformFileSystem::describeLastError()
    {
        const DWORD code                                = GetLastError();
        wchar_t     arrMessage[constant::kMaxBuffer512] = {};
        const DWORD length                              = FormatMessageW( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, arrMessage, constant::kMaxBuffer512, nullptr );
        string      message                             = length > 0 ? StringUtil::utf16ToUTF8( arrMessage ) : string( "unknown error" );
        while ( message.empty() == false && ( message.back() == '\n' || message.back() == '\r' || message.back() == ' ' ) )
            message.pop_back();
        utf8 arrCode[constant::kMaxBuffer64];
        formatstring( arrCode, constant::kMaxBuffer64, " (%#)", static_cast<uint32>( code ) );
        message.append( arrCode );
        return message;
    }
} // namespace sw
#endif

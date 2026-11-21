#include "pch.h"

#include "Engine/Text/SystemFontLocator.h"

#include "Core/File/FileUtil.h"
#include "Core/Process/ProcessEnvironment.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Foundation/PlatformOSHeaders.h"
#endif

namespace sw
{
    namespace
    {
        struct SystemFontLocatorInternal
        {
            /** @brief 디렉터리가 실제로 있을 때만 구분자를 맞춰 목록에 더합니다. */
            static void appendIfDirectory( vector<string>& outListDirectory, const string& candidate )
            {
                if ( candidate.empty() == false && FileUtil::isDirectory( FileUtil::toDiskPath( candidate ) ) )
                    outListDirectory.push_back( FileUtil::normalizeSeparators( candidate ) );
            }
        };
    } // namespace
} // namespace sw

#if defined( SW_PLATFORM_LINUX )
namespace sw
{
    string SystemFontLocator::findInRecursiveIndex( string_view fileName )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bIndexBuilt == false )
        {
            _bIndexBuilt = true;
            for ( const string& directory : getSystemFontDirectories() )
            {
                vector<string> listFilePath;
                if ( FileUtil::collectFiles( FileUtil::toDiskPath( directory ), {}, listFilePath, true ) == false )
                    continue;
                for ( string& filePath : listFilePath )
                {
                    const string key = StringUtil::toLower( FileUtil::getFileNamePart( filePath ).c_str() );
                    if ( _mapFileNameToPath.find( key ) == _mapFileNameToPath.end() )
                        _mapFileNameToPath.emplace( key, std::move( filePath ) );
                }
            }
        }
        const auto iter = _mapFileNameToPath.find( StringUtil::toLower( fileName ) );
        return iter != _mapFileNameToPath.end() ? iter->second : string{};
    }
} // namespace sw
#endif

namespace sw
{
    vector<string> SystemFontLocator::getSystemFontDirectories()
    {
        vector<string> listDirectory;
#if defined( SW_PLATFORM_WINDOWS )
        // 버퍼가 모자라면 0 이 아니라 필요한 크기(끝 0 포함)를 돌려준다 - 그 크기로 다시 받는다(0 아님만 보면 채워지지 않은 버퍼를 읽었다).
        const UINT required = GetWindowsDirectoryW( nullptr, 0 );
        if ( required != 0 )
        {
            wstring    windowsDirectory( required, L' ' );
            const UINT written = GetWindowsDirectoryW( windowsDirectory.data(), required );
            if ( written != 0 && written < required )
            {
                windowsDirectory.resize( written );
                SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( StringUtil::utf16ToUTF8( windowsDirectory.c_str() ), "Fonts" ) );
            }
        }
        // 관리자 권한 없이 설치한 글꼴(Windows 10 1809+)은 사용자 폴더에 있다.
        string localAppData;
        if ( ProcessEnvironment::tryReadNonEmpty( "LOCALAPPDATA", localAppData ) ) // 빈 값이면 현재 폴더 아래 상대 경로를 뒤지게 되므로 없는 것으로 본다
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( localAppData, "Microsoft/Windows/Fonts" ) );
#elif defined( SW_PLATFORM_LINUX )
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/share/fonts" );
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/local/share/fonts" );
        string home;
        if ( ProcessEnvironment::tryReadNonEmpty( "HOME", home ) )
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( home, ".local/share/fonts" ) );
#elif defined( SW_PLATFORM_ANDROID )
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/system/fonts" ); // 앱이 읽을 수 있는 시스템 폰트(Roboto · NotoSansCJK)
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/product/fonts" );
#elif defined( SW_PLATFORM_WEB )
        // 브라우저는 시스템 폰트 파일을 주지 않는다 — 카탈로그의 저장소 폰트만 쓴다(빈 목록)
#else
    #error "Unsupported platform"
#endif
        return listDirectory;
    }

    string SystemFontLocator::findSystemFontFile( string_view fileName )
    {
        if ( fileName.empty() )
            return {};
        for ( const string& directory : getSystemFontDirectories() )
        {
            string direct = FileUtil::joinPath( directory, fileName );
            if ( FileUtil::isRegularFile( FileUtil::toDiskPath( direct ) ) )
                return direct;
        }
#if defined( SW_PLATFORM_LINUX )
        const string indexed = findInRecursiveIndex( fileName );
        if ( indexed.empty() == false )
            return indexed;
#endif
        return {};
    }
} // namespace sw

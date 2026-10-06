#include "pch.h"

#include "Engine/Text/SystemFontLocator.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"
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
                if ( candidate.empty() == false && FileUtil::isDirectory( candidate ) )
                    outListDirectory.push_back( FileUtil::normalizeSeparators( candidate ) );
            }

#if defined( SW_PLATFORM_LINUX )
            /** @brief 하위 폴더까지 훑은 (소문자 파일 이름 → 경로) 표입니다. 처음 부를 때 한 번 채웁니다(앞 폴더가 이긴다). */
            static const unordered_map<string, string>& getRecursiveIndex()
            {
                static mutex                         s_mutex;
                static bool                          s_bBuilt{ false };
                static unordered_map<string, string> s_mapFileNameToPath;
                std::scoped_lock<mutex>              lock{ s_mutex };
                if ( s_bBuilt )
                    return s_mapFileNameToPath;
                s_bBuilt = true;
                for ( const string& directory : SystemFontLocator::getSystemFontDirectories() )
                {
                    vector<string> listFilePath;
                    if ( FileUtil::collectFiles( directory, {}, listFilePath, true ) == false )
                        continue;
                    for ( string& filePath : listFilePath )
                    {
                        const string key = StringUtil::toLower( FileUtil::getFileNamePart( filePath ).c_str() );
                        if ( s_mapFileNameToPath.find( key ) == s_mapFileNameToPath.end() )
                            s_mapFileNameToPath.emplace( key, std::move( filePath ) );
                    }
                }
                return s_mapFileNameToPath;
            }
#endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    vector<string> SystemFontLocator::getSystemFontDirectories()
    {
        vector<string> listDirectory;
#if defined( SW_PLATFORM_WINDOWS )
        utf16 windowsDirectory[constant::kMaxPathSize];
        if ( GetWindowsDirectoryW( reinterpret_cast<LPWSTR>( windowsDirectory ), constant::kMaxPathSize ) != 0 )
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( StringUtil::utf16ToUtf8( windowsDirectory ), "Fonts" ) );
        // 관리자 권한 없이 설치한 글꼴(Windows 10 1809+)은 사용자 폴더에 있다.
        const utf8* pLocalAppData = std::getenv( "LOCALAPPDATA" );
        if ( pLocalAppData != nullptr )
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( pLocalAppData, "Microsoft/Windows/Fonts" ) );
#elif defined( SW_PLATFORM_LINUX )
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/share/fonts" );
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/local/share/fonts" );
        const utf8* pHome = std::getenv( "HOME" );
        if ( pHome != nullptr )
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( pHome, ".local/share/fonts" ) );
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
            if ( FileUtil::isRegularFile( direct ) )
                return direct;
        }
#if defined( SW_PLATFORM_LINUX )
        const unordered_map<string, string>& mapIndex = SystemFontLocatorInternal::getRecursiveIndex();
        const auto                           iter     = mapIndex.find( StringUtil::toLower( string( fileName ).c_str() ) );
        if ( iter != mapIndex.end() )
            return iter->second;
#endif
        return {};
    }
} // namespace sw

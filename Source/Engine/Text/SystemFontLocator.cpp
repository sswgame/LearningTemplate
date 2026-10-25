#include "pch.h"

#include "Engine/Text/SystemFontLocator.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Process/ProcessEnvironment.h"

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
            /** @brief 하위 폴더까지 훑은 (소문자 파일 이름 → 경로) 표입니다. 처음 찾을 때 한 번 채웁니다(앞 폴더가 이긴다). */
            struct RecursiveFontIndex
            {
                mutex                         _mutex;
                bool                          _bBuilt{ false };
                unordered_map<string, string> _mapFileNameToPath;
            };

            /** @brief 위 표의 소유자입니다 — 엔진 수명 범위가 소유한다(함수 정적이면 종료 누수 보고 뒤까지 남는다). */
            inline static ScopeOwned<RecursiveFontIndex> s_recursiveIndex{};

            /** @brief 하위 폴더 색인에서 @p fileName(대소문자 무시)의 경로를 찾습니다. 없거나 엔진 수명 범위가 닫혔으면 빈 글입니다. */
            static string findInRecursiveIndex( string_view fileName )
            {
                RecursiveFontIndex* const pIndex = s_recursiveIndex.getOrCreate();
                if ( pIndex == nullptr )
                    return {};
                std::scoped_lock<mutex> lock{ pIndex->_mutex };
                if ( pIndex->_bBuilt == false )
                {
                    pIndex->_bBuilt = true;
                    for ( const string& directory : SystemFontLocator::getSystemFontDirectories() )
                    {
                        vector<string> listFilePath;
                        if ( FileUtil::collectFiles( directory, {}, listFilePath, true ) == false )
                            continue;
                        for ( string& filePath : listFilePath )
                        {
                            const string key = StringUtil::toLower( FileUtil::getFileNamePart( filePath ).c_str() );
                            if ( pIndex->_mapFileNameToPath.find( key ) == pIndex->_mapFileNameToPath.end() )
                                pIndex->_mapFileNameToPath.emplace( key, std::move( filePath ) );
                        }
                    }
                }
                const auto iter = pIndex->_mapFileNameToPath.find( StringUtil::toLower( string( fileName ).c_str() ) );
                return iter != pIndex->_mapFileNameToPath.end() ? iter->second : string{};
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
        string localAppData;
        if ( ProcessEnvironment::tryReadNonEmpty( "LOCALAPPDATA", localAppData ) ) // 빈 값이면 현재 폴더 아래 상대 경로를 뒤지게 되므로 없는 것으로 본다
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( localAppData, "Microsoft/Windows/Fonts" ) );
#elif defined( SW_PLATFORM_LINUX )
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/share/fonts" );
        SystemFontLocatorInternal::appendIfDirectory( listDirectory, "/usr/local/share/fonts" );
        string home;
        if ( ProcessEnvironment::tryReadNonEmpty( "HOME", home ) )
            SystemFontLocatorInternal::appendIfDirectory( listDirectory, FileUtil::joinPath( home, ".local/share/fonts" ) );
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
        const string indexed = SystemFontLocatorInternal::findInRecursiveIndex( fileName );
        if ( indexed.empty() == false )
            return indexed;
#endif
        return {};
    }
} // namespace sw

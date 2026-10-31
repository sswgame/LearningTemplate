#include "pch.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Foundation/PlatformOSHeaders.h"
#endif
#include "Core/File/UserDataPath.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    namespace
    {
        struct UserDataPathInternal
        {
            static string makeFolderName( string_view gameName ) { return gameName.empty() ? string( "default" ) : StringUtil::toLower( string( gameName ).c_str() ); }

            /** @brief @p pRootEnvironment 가 있으면 그 아래, 없으면 `$HOME/<homeRelative>` 아래 `<vendor>/<game>` 입니다. 둘 다 없으면 빈 글. */
            static string makeUnderEnvironment( const utf8* pRootEnvironment, const utf8* pHomeRelative, const utf8* pVendor, string_view gameName )
            {
                string root;
                if ( ProcessEnvironment::tryReadNonEmpty( pRootEnvironment, root ) )
                    return FileUtil::joinPath( FileUtil::joinPath( root, pVendor ), makeFolderName( gameName ) );
                if ( pHomeRelative == nullptr )
                    return string{};
                string home;
                if ( ProcessEnvironment::tryReadNonEmpty( "HOME", home ) == false )
                    return string{};
                const utf8* pHome = home.c_str();
                return FileUtil::joinPath( FileUtil::joinPath( FileUtil::joinPath( pHome, pHomeRelative ), pVendor ), makeFolderName( gameName ) );
            }

            /** @brief OS 사용자 폴더를 모를 때 멈춥니다 - 작업 폴더의 `Saved` 로 대신하면(옛 폴백) 세이브 · 설정이 띄운 자리마다 갈렸다. */
            [[noreturn]] static void failUnknownUserFolder()
            {
                SW_FATAL( "The user data folder is unknown (no LOCALAPPDATA / XDG_*_HOME / HOME) - set %# to choose it", kUserDataRootVariable );
            }

            /** @brief 환경 변수 `kUserDataRootVariable` 이 값을 가지면 그 아래 `<game>` 폴더, 아니면 빈 글입니다(그때는 OS 의 사용자 폴더 규칙을 따른다). */
            static string makeUnderRootOverride( string_view gameName )
            {
                string root;
                if ( ProcessEnvironment::tryReadNonEmpty( kUserDataRootVariable, root ) == false )
                    return string{};
                return FileUtil::joinPath( root, makeFolderName( gameName ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string UserDataPath::getDataDirectory( string_view gameName )
    {
        using Internal = UserDataPathInternal;
        if ( string overridden = Internal::makeUnderRootOverride( gameName ); overridden.empty() == false )
            return overridden;
#if defined( SW_PLATFORM_WINDOWS )
        const string directory = Internal::makeUnderEnvironment( "LOCALAPPDATA", nullptr, "SWEngine", gameName );
#elif defined( SW_PLATFORM_LINUX )
        const string directory = Internal::makeUnderEnvironment( "XDG_DATA_HOME", ".local/share", "swengine", gameName );
#else
        const string directory;
#endif
        if ( directory.empty() )
            Internal::failUnknownUserFolder();
        return directory;
    }

    string UserDataPath::getConfigDirectory( string_view gameName )
    {
        using Internal = UserDataPathInternal;
        if ( string overridden = Internal::makeUnderRootOverride( gameName ); overridden.empty() == false )
            return overridden;
#if defined( SW_PLATFORM_WINDOWS )
        const string directory = Internal::makeUnderEnvironment( "LOCALAPPDATA", nullptr, "SWEngine", gameName );
#elif defined( SW_PLATFORM_LINUX )
        const string directory = Internal::makeUnderEnvironment( "XDG_CONFIG_HOME", ".config", "swengine", gameName );
#else
        const string directory;
#endif
        if ( directory.empty() )
            Internal::failUnknownUserFolder();
        return directory;
    }

} // namespace sw

namespace sw
{
    LocalStoreLocation::LocalStoreLocation( Kind kind, string path )
        : _path{ std::move( path ) }
        , _kind{ kind }
    {
    }

    LocalStoreLocation LocalStoreLocation::memory()
    {
        return LocalStoreLocation( Kind::Memory, string{} );
    }

    std::optional<LocalStoreLocation> LocalStoreLocation::tryUnderUserData( string_view relativePath )
    {
        // 느슨한 판정(`isAbsolutePath`)으로 거른다 - 드라이브 상대 · 드라이브 없는 루트도 데이터 폴더 아래 상대 경로가 아니다.
        if ( relativePath.empty() || FileUtil::isAbsolutePath( relativePath ) )
            return std::nullopt;
        size_t segmentBegin = 0;
        while ( segmentBegin <= relativePath.size() )
        {
            size_t segmentEnd = relativePath.find_first_of( "/\\", segmentBegin );
            if ( segmentEnd == string_view::npos )
                segmentEnd = relativePath.size();
            if ( relativePath.substr( segmentBegin, segmentEnd - segmentBegin ) == ".." )
                return std::nullopt; // 데이터 폴더 밖으로 나간다
            segmentBegin = segmentEnd + 1;
        }
        return LocalStoreLocation( Kind::UnderUserData, string( relativePath ) );
    }

    LocalStoreLocation LocalStoreLocation::absolute( const AbsolutePath& path )
    {
        return LocalStoreLocation( Kind::Absolute, path.str() );
    }

    string LocalStoreLocation::resolveDiskPath( string_view gameName ) const
    {
        if ( _kind == Kind::Memory )
            SW_FATAL( "A memory local store location has no disk path - check getKind() first" );
        if ( _kind == Kind::UnderUserData )
            return FileUtil::joinPath( UserDataPath::getDataDirectory( gameName ), _path );
        return _path;
    }
} // namespace sw

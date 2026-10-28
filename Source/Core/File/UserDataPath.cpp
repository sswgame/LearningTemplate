#include "pch.h"

#if defined( SW_PLATFORM_LINUX )
    #include "Core/Common/PlatformOsHeaders.h"
#endif
#include "Core/File/UserDataPath.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/ProcessEnvironment.h"

namespace sw
{
    namespace
    {
        struct UserDataPathInternal
        {
            static constexpr utf8 kFallbackFolder[] = "Saved";

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

            static string makeFallback( string_view gameName ) { return FileUtil::joinPath( kFallbackFolder, makeFolderName( gameName ) ); }

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
        return directory.empty() ? Internal::makeFallback( gameName ) : directory;
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
        return directory.empty() ? Internal::makeFallback( gameName ) : directory;
    }

    string UserDataPath::resolve( string_view gameName, string_view relativeOrAbsolutePath )
    {
        const bool bKeepAsIs = relativeOrAbsolutePath == ":memory:" || FileUtil::isAbsolutePath( relativeOrAbsolutePath );
        if ( bKeepAsIs )
            return string( relativeOrAbsolutePath );
        return FileUtil::joinPath( getDataDirectory( gameName ), relativeOrAbsolutePath );
    }
} // namespace sw

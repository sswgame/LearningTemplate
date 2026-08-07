/**
 * @file EditorUtil.cpp
 * @brief EditorModule 유틸 구현
 */
#include "EditorUtil.h"
#include "EditorDefines.h"
#include "Core/Common/CommonDefines.h"
#include "Core/Common/PlatformHeaders.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/File/FileUtil.h"

namespace sw
{
	namespace
	{
		void appendIfDirectory( std::vector<std::filesystem::path>& out, const std::filesystem::path& candidate )
		{
			std::error_code ec;
			if ( candidate.empty() == false && std::filesystem::is_directory( candidate, ec ) )
				out.push_back( candidate );
		}
	} // namespace

	std::vector<std::filesystem::path> EditorUtil::getSystemFontsDirectories()
	{
		std::vector<std::filesystem::path> dirs;

#if defined( SW_PLATFORM_WINDOWS )
		wchar_t windowsDir[constant::kMaxPathSize] = {};
		if ( GetWindowsDirectoryW( windowsDir, constant::kMaxPathSize ) > 0 )
			appendIfDirectory( dirs, std::filesystem::path( windowsDir ) / editor::path::kFontsFolder );

#elif defined( SW_PLATFORM_LINUX )
		appendIfDirectory( dirs, "/usr/share/fonts" );
		appendIfDirectory( dirs, "/usr/local/share/fonts" );
		if ( const char* home = std::getenv( "HOME" ) )
			appendIfDirectory( dirs, std::filesystem::path( home ) / ".local" / "share" / "fonts" );

#elif defined( SW_PLATFORM_MACOS )
		appendIfDirectory( dirs, "/System/Library/Fonts" );
		appendIfDirectory( dirs, "/Library/Fonts" );
		if ( const char* home = std::getenv( "HOME" ) )
			appendIfDirectory( dirs, std::filesystem::path( home ) / "Library" / "Fonts" );
		else if ( const passwd* pw = getpwuid( getuid() ) )
			appendIfDirectory( dirs, std::filesystem::path( pw->pw_dir ) / "Library" / "Fonts" );

#endif
		return dirs;
	}

	std::filesystem::path EditorUtil::resolveFontFile( const utf8* fileName )
	{
		if ( fileName == nullptr || fileName[0] == '\0' )
			return {};

		std::error_code ec;

		const std::string& resourceRoot = ResourceUtil::getRootFolderPath();
		if ( resourceRoot.empty() == false )
		{
			std::filesystem::path candidate =
				std::filesystem::path( resourceRoot ) / editor::path::kEditorFolder / editor::path::kFontsFolder / fileName;
			if ( std::filesystem::is_regular_file( candidate, ec ) )
				return candidate;
		}

		for ( const std::string& editorRoot : ResourceUtil::getResourceFolders( editor::path::kEditorFolder ) )
		{
			std::filesystem::path candidate =
				std::filesystem::path( editorRoot ) / editor::path::kFontsFolder / fileName;
			if ( std::filesystem::is_regular_file( candidate, ec ) )
				return candidate;
		}

		for ( const std::filesystem::path& fontsDir : getSystemFontsDirectories() )
		{
			std::filesystem::path candidate = fontsDir / fileName;
			if ( std::filesystem::is_regular_file( candidate, ec ) )
				return candidate;
		}

		return {};
	}

	std::filesystem::path EditorUtil::getProjectRootPath()
	{
		// getRootFolderPath() == <Project>/Resource
		const std::string& resourceRoot = ResourceUtil::getRootFolderPath();
		if ( resourceRoot.empty() )
			return {};

		return std::filesystem::path( resourceRoot ).lexically_normal().parent_path();
	}

	std::filesystem::path EditorUtil::getEditorConfigDirectory()
	{
		const std::filesystem::path projectRoot = getProjectRootPath();
		if ( projectRoot.empty() )
			return {};

		const std::filesystem::path configDir =
			projectRoot / editor::path::kConfigFolder / editor::path::kEditorConfigFolder;
		const std::filesystem::path markerFile = configDir / editor::path::kImGuiIniFile;
		// createDirectory treats the argument as a file path and creates its parent dirs.
		FileUtil::createDirectory( markerFile.string() );
		return configDir;
	}

	std::filesystem::path EditorUtil::resolveEditorConfigFile( const utf8* fileName )
	{
		if ( fileName == nullptr || fileName[0] == '\0' )
			return {};

		const std::filesystem::path configDir = getEditorConfigDirectory();
		if ( configDir.empty() )
			return {};

		return configDir / fileName;
	}
} // namespace sw

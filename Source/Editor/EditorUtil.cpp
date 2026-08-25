#include "pch.h"

#include "Editor/Config/EditorConfig.h"
#include "Editor/Config/EditorData.h"
#include "Editor/EditorUtil.h"

#include "RuntimeAPI/EditorService.h"

namespace sw
{

	namespace
	{
		/**
		 * @brief 해당 디렉터리가 실제로 존재하는 경우에만 정규화하여 출력 목록에 추가합니다.
		 */
		void appendIfDirectory( vector<string>& out, const string& candidate )
		{
			if ( candidate.empty() == false && FileUtil::directoryExists( candidate ) )
				out.push_back( FileUtil::normalizeSeparators( candidate ) );
		}

	} // namespace

	/**
	 * @brief 현재 운영체제의 기본 시스템 폰트 디렉터리 경로 목록을 반환합니다.
	 */
	vector<string> EditorUtil::getSystemFontsDirectories()
	{
		vector<string> listDirs;

#if defined( SW_PLATFORM_WINDOWS )
		utf16 windowsDir[constant::kMaxPathSize] = {};
		if ( GetWindowsDirectoryW( windowsDir, constant::kMaxPathSize ) > 0 )
			appendIfDirectory( listDirs, FileUtil::joinPath( StringUtil::utf16ToUtf8( windowsDir ), "Fonts" ) );

#elif defined( SW_PLATFORM_LINUX )
		appendIfDirectory( listDirs, "/usr/share/fonts" );
		appendIfDirectory( listDirs, "/usr/local/share/fonts" );
		const utf8* pHome = std::getenv( "HOME" );
		if ( pHome != nullptr )
			appendIfDirectory( listDirs, FileUtil::joinPath( pHome, ".local/share/fonts" ) );

#elif defined( SW_PLATFORM_MACOS )
		appendIfDirectory( listDirs, "/System/Library/Fonts" );
		appendIfDirectory( listDirs, "/Library/Fonts" );
		const utf8* pHome = std::getenv( "HOME" );
		if ( pHome != nullptr )
		{
			appendIfDirectory( listDirs, FileUtil::joinPath( pHome, "Library/Fonts" ) );
		}
		else
		{
			const passwd* pPw = getpwuid( getuid() );
			if ( pPw != nullptr )
				appendIfDirectory( listDirs, FileUtil::joinPath( pPw->pw_dir, "Library/Fonts" ) );
		}

#endif
		return listDirs;
	}

	/**
	 * @brief 폰트 파일 이름을 프로젝트 에디터 폰트 폴더 및 OS 시스템 폰트 디렉터리에서 검색하여 절대 경로를 반환합니다.
	 */
	string EditorUtil::resolveFontFile( const utf8* pFileName )
	{
		if ( pFileName == nullptr || pFileName[0] == '\0' )
			return {};

		const EditorData& data		 = editor::getEditorData();
		const string&	  editorRoot = ResourceUtil::getEditorFolderPath();
		if ( editorRoot.empty() == false )
		{
			const string candidate = FileUtil::joinPath( FileUtil::joinPath( editorRoot, data._fontsFolder ), pFileName );
			if ( FileUtil::fileExists( candidate ) )
				return candidate;
		}

		const string& resourceRoot = ResourceUtil::getRootFolderPath();
		if ( resourceRoot.empty() == false && data._editorFolder.empty() == false )
		{
			const string candidate = FileUtil::joinPath(
				FileUtil::joinPath( FileUtil::joinPath( resourceRoot, data._editorFolder ), data._fontsFolder ),
				pFileName );
			if ( FileUtil::fileExists( candidate ) )
				return candidate;
		}

		// 프로젝트 내부에 없으면 OS 시스템 폰트 폴더 검색
		for ( const string& fontsDir : getSystemFontsDirectories() )
		{
			string direct = FileUtil::joinPath( fontsDir, pFileName );
			if ( FileUtil::fileExists( direct ) )
				return string( std::move( direct ) );

			vector<string> listNested;
			FileUtil::collectFiles( fontsDir, {}, listNested, true, false );
			for ( const string& nestedPath : listNested )
			{
				if ( FileUtil::getFileNamePart( nestedPath ) == pFileName )
					return FileUtil::normalizeSeparators( nestedPath );
			}
		}

		return {};
	}

	/**
	 * @brief 우선순위 순서로 나열된 폰트 파일명 목록 중 가장 먼저 존재하는 폰트의 절대 경로를 반환합니다.
	 */
	string EditorUtil::resolveFontFile( const vector<string>& fileNames )
	{
		for ( const string& name : fileNames )
		{
			if ( name.empty() )
				continue;
			string found = resolveFontFile( name.c_str() );
			if ( found.empty() == false )
				return found;
		}
		return {};
	}

	string EditorUtil::getProjectRootPath()
	{
		const string& projectRoot = ResourceUtil::getProjectFolderPath();
		if ( projectRoot.empty() == false )
			return projectRoot;

		const string& resourceRoot = ResourceUtil::getRootFolderPath();
		if ( resourceRoot.empty() )
			return {};

		return FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( resourceRoot ) );
	}

	string EditorUtil::getEditorConfigDirectory()
	{
		const EditorConfig& editorCfg	= EditorConfig::getActive();
		const string		projectRoot = getProjectRootPath();
		if ( projectRoot.empty() )
			return {};

		const string configDir =
			FileUtil::joinPath( FileUtil::joinPath( projectRoot, editorCfg._configFolder ), editorCfg._editorConfigFolder );
		const string markerFile = FileUtil::joinPath( configDir, editorCfg._imguiIniFile );
		FileUtil::createDirectory( markerFile );
		return configDir;
	}

	string EditorUtil::resolveEditorConfigFile( const utf8* pFileName )
	{
		if ( pFileName == nullptr || pFileName[0] == '\0' )
			return {};

		const string configDir = getEditorConfigDirectory();
		if ( configDir.empty() )
			return {};

		return FileUtil::joinPath( configDir, pFileName );
	}
} // namespace sw

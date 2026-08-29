#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <fa_solid_900.h>
#include <IconsFontAwesome6.h>

namespace sw::editor
{
	namespace
	{
		struct EditorUtilInternal
		{
			/**
			 * @brief 해당 디렉터리가 실제로 존재하는 경우에만 정규화하여 출력 목록에 추가합니다.
			 */
			static void appendIfDirectory( vector<string>& out, const string& candidate )
			{
				if ( candidate.empty() == false && FileUtil::directoryExists( candidate ) )
					out.push_back( FileUtil::normalizeSeparators( candidate ) );
			}
		};
	} // namespace
} // namespace sw::editor

namespace sw::editor
{
	SW_LOG_CALLER( "EditorUtil" );

	/**
	 * @brief 현재 운영체제의 기본 시스템 폰트 디렉터리 경로 목록을 반환합니다.
	 */
	vector<string> EditorUtil::getSystemFontsDirectories()
	{
		vector<string> listDirs;

#if defined( SW_PLATFORM_WINDOWS )
		utf16 windowsDir[constant::kMaxPathSize] = {};
		if ( GetWindowsDirectoryW( windowsDir, constant::kMaxPathSize ) > 0 )
			EditorUtilInternal::appendIfDirectory( listDirs, FileUtil::joinPath( StringUtil::utf16ToUtf8( windowsDir ), "Fonts" ) );

#elif defined( SW_PLATFORM_LINUX )
		EditorUtilInternal::appendIfDirectory( listDirs, "/usr/share/fonts" );
		EditorUtilInternal::appendIfDirectory( listDirs, "/usr/local/share/fonts" );
		const utf8* pHome = std::getenv( "HOME" );
		if ( pHome != nullptr )
			EditorUtilInternal::appendIfDirectory( listDirs, FileUtil::joinPath( pHome, ".local/share/fonts" ) );

#elif defined( SW_PLATFORM_MACOS )
		EditorUtilInternal::appendIfDirectory( listDirs, "/System/Library/Fonts" );
		EditorUtilInternal::appendIfDirectory( listDirs, "/Library/Fonts" );
		const utf8* pHome = std::getenv( "HOME" );
		if ( pHome != nullptr )
		{
			EditorUtilInternal::appendIfDirectory( listDirs, FileUtil::joinPath( pHome, "Library/Fonts" ) );
		}
		else
		{
			const passwd* pPw = getpwuid( getuid() );
			if ( pPw != nullptr )
				EditorUtilInternal::appendIfDirectory( listDirs, FileUtil::joinPath( pPw->pw_dir, "Library/Fonts" ) );
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

		// 프로젝트 내부에 없으면 OS 시스템 폰트 폴더 직접 검색 (재귀 스캔 제외하여 I/O 지연 방지)
		for ( const string& fontsDir : getSystemFontsDirectories() )
		{
			string direct = FileUtil::joinPath( fontsDir, pFileName );
			if ( FileUtil::fileExists( direct ) )
				return string( std::move( direct ) );
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

	void EditorUtil::setupFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		// FreeType 폰트 로더 연동 (고속 래스터라이징 및 서브픽셀 앤티에일리어싱/힌팅)
		io.Fonts->SetFontLoader( ImGuiFreeType::GetFontLoader() );
		io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

		const EditorData& data		 = editor::getEditorData();
		const string	  basePath	 = resolveFontFile( data._listBaseFonts );
		const string	  koreanPath = resolveFontFile( data._listKoreanFonts );

		ImFont* pBaseFont{ nullptr };
		BLOCK( "Base Font" )
		{
			ImFontConfig baseConfig{};
			baseConfig.OversampleH = 1;
			baseConfig.OversampleV = 1;
			// 명시적 크기가 필요합니다: MergeMode가 명시 크기를 쓰는데
			// 대상 폰트가 암시적 참조 크기(AddFontDefault)이면 ImGui가 assert합니다.
			baseConfig.SizePixels = data._fontSize;

			if ( basePath.empty() == false )
			{
				pBaseFont = io.Fonts->AddFontFromFileTTF( basePath.c_str(), data._fontSize, &baseConfig,
														  io.Fonts->GetGlyphRangesDefault() );
				SW_LOG_TRACE( "Loaded base font: %#", basePath.c_str() );
			}

			if ( pBaseFont == nullptr )
			{
				pBaseFont = io.Fonts->AddFontDefault( &baseConfig );
				SW_LOG_WARNING( "No system UI font found - using ImGui default font." );
			}
		}

		BLOCK( "Korean Glyph Merge" )
		{
			if ( koreanPath.empty() == false )
			{
				ImFontConfig mergeConfig{};
				mergeConfig.MergeMode	= true;
				mergeConfig.OversampleH = 2;
				mergeConfig.OversampleV = 1;
				mergeConfig.PixelSnapH	= true;
				io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), data._fontSize, &mergeConfig,
											  io.Fonts->GetGlyphRangesKorean() );
				SW_LOG_TRACE( "Merged Korean glyphs from: %#", koreanPath.c_str() );
			}
			else
				SW_LOG_WARNING( "Korean font not found - Hangul may not render." );
		}

		BLOCK( "Font Awesome 6 (ImGuiNotify icons)" )
		{
			const float32			 iconFontSize	   = data._fontSize * 2.0f / 3.0f;
			static constexpr ImWchar kArrIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
			ImFontConfig			 iconsConfig{};
			iconsConfig.MergeMode			 = true;
			iconsConfig.PixelSnapH			 = true;
			iconsConfig.GlyphMinAdvanceX	 = iconFontSize;
			iconsConfig.FontDataOwnedByAtlas = false;
			io.Fonts->AddFontFromMemoryTTF( const_cast<void*>( static_cast<const void*>( fa_solid_900 ) ),
											sizeof( fa_solid_900 ), iconFontSize, &iconsConfig,
											kArrIconsRanges );
		}

		io.FontDefault = pBaseFont;
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

	bool EditorUtil::isPrefabAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::endsWithAnyIgnoreCase( pPath, { ".prefab.xml", ".prefab.json", ".prefab.bin", ".prefab" } ) ||
			   FileUtil::hasExtension( pPath, ".pfb" );
	}

	bool EditorUtil::isTextureAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasAnyExtension( pPath, { ".png", ".jpg", ".jpeg", ".tga", ".dds", ".hdr", ".bmp" } );
	}

	bool EditorUtil::isMaterialAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasAnyExtension( pPath, { "._material", ".mat", ".material" } );
	}

	bool EditorUtil::isSceneAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr || pPath[0] == '\0' )
			return false;
		if ( FileUtil::hasExtension( pPath, ".scene" ) )
			return true;
		if ( FileUtil::hasExtension( pPath, ".xml" ) && StringUtil::stristr( pPath, ".scene" ) != nullptr )
			return true;
		return FileUtil::endsWithIgnoreCase( pPath, "_scene.xml" );
	}

	bool EditorUtil::isShaderAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasAnyExtension( pPath, { ".hlsl", ".glsl", ".vert", ".frag", ".spv" } );
	}

	bool EditorUtil::isAudioAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasAnyExtension( pPath, { ".wav", ".mp3", ".ogg" } );
	}

	bool EditorUtil::isDataAssetPath( const utf8* pPath )
	{
		if ( pPath == nullptr )
			return false;
		return FileUtil::hasAnyExtension( pPath, { ".xml", ".json", ".csv", ".ini", ".kv" } );
	}

	GameObject* EditorUtil::spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
	{
		if ( pManager == nullptr || pPath == nullptr || pPath[0] == '\0' )
			return nullptr;

		if ( isPrefabAssetPath( pPath ) == false )
		{
			SW_LOG_TRACE( "Not a prefab path: %#", pPath );
			return nullptr;
		}

		GameObject* pSpawned = editor::getService<ResourceManager>()->getPrefabManager().spawn( pManager, pPath );
		if ( pSpawned == nullptr )
		{
			SW_LOG_WARNING( "Failed to spawn prefab: %#", pPath );
			return nullptr;
		}

		if ( pParent != nullptr )
			pSpawned->attachToParent( pParent );

		EditorContext* pContext = EditorContext::get();
		if ( pContext != nullptr )
			pContext->getWorkspace().setGameObjectPrefabPath( pSpawned->getObjectId(), pPath );

		SW_LOG_TRACE( "Spawned prefab from %#", pPath );
		return pSpawned;
	}
} // namespace sw::editor

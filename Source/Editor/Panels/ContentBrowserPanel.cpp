#include "pch.h"

#include "Editor/Panels/ContentBrowserPanel.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/AssetEditorManager.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Utility/Resource/AssetDatabase.h"
#include "Engine/Utility/Resource/ResourceManager.h"

#include "RuntimeAPI/Service/EditorService.h"

#include <imgui.h>

SW_LOG_CALLER( "ContentBrowserPanel" );
namespace sw::editor
{
	namespace
	{
		ImVec4 colorForExtension( string_view ext )
		{
			if ( StringUtil::equalsIgnoreCase( ext, "._material" ) )
				return ImVec4( 0.85f, 0.35f, 0.25f, 1.0f );
			if ( StringUtil::equalsIgnoreCase( ext, ".hlsl" ) || StringUtil::equalsIgnoreCase( ext, ".glsl" ) ||
				 StringUtil::equalsIgnoreCase( ext, ".vert" ) || StringUtil::equalsIgnoreCase( ext, ".frag" ) )
				return ImVec4( 0.25f, 0.65f, 0.90f, 1.0f );
			if ( StringUtil::equalsIgnoreCase( ext, ".png" ) || StringUtil::equalsIgnoreCase( ext, ".jpg" ) ||
				 StringUtil::equalsIgnoreCase( ext, ".jpeg" ) || StringUtil::equalsIgnoreCase( ext, ".tga" ) ||
				 StringUtil::equalsIgnoreCase( ext, ".dds" ) )
				return ImVec4( 0.45f, 0.80f, 0.35f, 1.0f );
			if ( ext.empty() == false )
				return ImVec4( 0.55f, 0.55f, 0.60f, 1.0f );
			return ImVec4( 0.35f, 0.40f, 0.55f, 1.0f ); // folder
		}

		const utf8* typeLabel( string_view ext, bool bIsDirectory )
		{
			if ( bIsDirectory )
				return "Folder";
			if ( StringUtil::equalsIgnoreCase( ext, "._material" ) )
				return "Material";
			if ( StringUtil::equalsIgnoreCase( ext, ".hlsl" ) )
				return "Shader";
			if ( ext.empty() )
				return "File";
			thread_local fixed_string<constant::kMaxBuffer64> t_extLabel;
			t_extLabel = ext;
			return t_extLabel.c_str();
		}
	} // namespace

	void ContentBrowserPanel::drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
												  const AssetEntry& entry )
	{
		if ( pDrawList == nullptr )
			return;

		const ImVec2 minVec{ minPos._x, minPos._y };
		const ImVec2 maxVec{ maxPos._x, maxPos._y };

		const float32 w	 = maxPos._x - minPos._x;
		const float32 h	 = maxPos._y - minPos._y;
		const float32 cx = minPos._x + w * 0.5f;
		const float32 cy = minPos._y + h * 0.5f;

		// Card thumbnail background
		pDrawList->AddRectFilled( minVec, maxVec, IM_COL32( 22, 24, 30, 255 ), 4.0f );

		const string& ext = entry._extension;
		if ( entry._bIsDirectory )
		{
			// Golden Folder tab and body
			pDrawList->AddRectFilled( ImVec2( minPos._x + w * 0.15f, minPos._y + h * 0.20f ),
									  ImVec2( minPos._x + w * 0.50f, minPos._y + h * 0.36f ),
									  IM_COL32( 235, 175, 50, 255 ), 2.0f );
			pDrawList->AddRectFilled( ImVec2( minPos._x + w * 0.15f, minPos._y + h * 0.30f ),
									  ImVec2( minPos._x + w * 0.85f, minPos._y + h * 0.80f ),
									  IM_COL32( 245, 195, 68, 255 ), 3.0f );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, ".prefab" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".pfb" ) )
		{
			// 3D Isometric Blue Cube for Prefab
			const float32 sz		= w * 0.22f;
			ImVec2		  topPts[4] = { ImVec2( cx, cy - sz * 1.1f ), ImVec2( cx + sz * 0.9f, cy - sz * 0.55f ),
										ImVec2( cx, cy ), ImVec2( cx - sz * 0.9f, cy - sz * 0.55f ) };
			pDrawList->AddConvexPolyFilled( topPts, 4, IM_COL32( 90, 160, 255, 255 ) );

			ImVec2 leftPts[4] = { ImVec2( cx - sz * 0.9f, cy - sz * 0.55f ), ImVec2( cx, cy ),
								  ImVec2( cx, cy + sz * 0.9f ), ImVec2( cx - sz * 0.9f, cy + sz * 0.35f ) };
			pDrawList->AddConvexPolyFilled( leftPts, 4, IM_COL32( 50, 120, 230, 255 ) );

			ImVec2 rightPts[4] = { ImVec2( cx, cy ), ImVec2( cx + sz * 0.9f, cy - sz * 0.55f ),
								   ImVec2( cx + sz * 0.9f, cy + sz * 0.35f ), ImVec2( cx, cy + sz * 0.9f ) };
			pDrawList->AddConvexPolyFilled( rightPts, 4, IM_COL32( 35, 95, 195, 255 ) );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, "._material" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".mat" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".material" ) )
		{
			// 3D Sphere preview with specular shading
			const float32 r = w * 0.26f;
			pDrawList->AddCircleFilled( ImVec2( cx, cy ), r, IM_COL32( 160, 60, 220, 255 ), 24 );
			pDrawList->AddCircleFilled( ImVec2( cx - r * 0.32f, cy - r * 0.32f ), r * 0.35f,
										IM_COL32( 230, 180, 255, 200 ), 16 );
			pDrawList->AddCircleFilled( ImVec2( cx - r * 0.38f, cy - r * 0.38f ), r * 0.15f,
										IM_COL32( 255, 255, 255, 240 ), 12 );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, ".png" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".jpg" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".jpeg" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".dds" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".tga" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".bmp" ) )
		{
			// Checkerboard background
			constexpr float32 chk = 6.0f;
			for ( float32 y = minPos._y + 4.0f; y < maxPos._y - 4.0f; y += chk )
			{
				for ( float32 x = minPos._x + 4.0f; x < maxPos._x - 4.0f; x += chk )
				{
					const bool bDark =
						( static_cast<int32>( ( x - minPos._x ) / chk ) +
						  static_cast<int32>( ( y - minPos._y ) / chk ) ) %
							2 ==
						0;
					pDrawList->AddRectFilled( ImVec2( x, y ), ImVec2( x + chk, y + chk ),
											  bDark ? IM_COL32( 38, 40, 46, 255 ) : IM_COL32( 58, 62, 70, 255 ) );
				}
			}
			// Picture frame
			pDrawList->AddRect( ImVec2( minPos._x + w * 0.16f, minPos._y + h * 0.16f ),
								ImVec2( minPos._x + w * 0.84f, minPos._y + h * 0.84f ),
								IM_COL32( 255, 255, 255, 200 ), 2.0f );
			pDrawList->AddCircleFilled( ImVec2( cx + w * 0.15f, cy - h * 0.12f ), w * 0.08f,
										IM_COL32( 240, 200, 80, 230 ) );
			ImVec2 triPts[3] = { ImVec2( cx - w * 0.22f, cy + h * 0.22f ), ImVec2( cx, cy - h * 0.05f ),
								 ImVec2( cx + w * 0.22f, cy + h * 0.22f ) };
			pDrawList->AddConvexPolyFilled( triPts, 3, IM_COL32( 70, 180, 120, 230 ) );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, ".scene" ) ||
				  ( StringUtil::equalsIgnoreCase( ext, ".xml" ) && entry._name.find( ".scene" ) != string::npos ) )
		{
			// 3D Scene Compass / Horizon
			pDrawList->AddCircle( ImVec2( cx, cy ), w * 0.26f, IM_COL32( 70, 200, 140, 200 ), 18, 1.5f );
			pDrawList->AddLine( ImVec2( cx, cy - w * 0.28f ), ImVec2( cx, cy + w * 0.28f ),
								IM_COL32( 240, 80, 80, 220 ), 1.5f );
			pDrawList->AddLine( ImVec2( cx - w * 0.28f, cy ), ImVec2( cx + w * 0.28f, cy ),
								IM_COL32( 80, 160, 240, 220 ), 1.5f );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, ".hlsl" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".glsl" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".spv" ) )
		{
			// Shader Diamond / Prism
			const float32 r			= w * 0.24f;
			ImVec2		  diaPts[4] = { ImVec2( cx, cy - r ), ImVec2( cx + r * 0.85f, cy ), ImVec2( cx, cy + r ),
										ImVec2( cx - r * 0.85f, cy ) };
			pDrawList->AddConvexPolyFilled( diaPts, 4, IM_COL32( 240, 120, 50, 255 ) );
			pDrawList->AddPolyline( diaPts, 4, IM_COL32( 255, 210, 140, 255 ), ImDrawFlags_Closed, 1.5f );
		}
		else if ( StringUtil::equalsIgnoreCase( ext, ".wav" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".mp3" ) ||
				  StringUtil::equalsIgnoreCase( ext, ".ogg" ) )
		{
			// Sound wave equalizer bars
			constexpr int32	  numBars	 = 5;
			constexpr float32 heights[5] = { 0.25f, 0.55f, 0.95f, 0.65f, 0.35f };
			constexpr float32 barW		 = 3.0f;
			constexpr float32 barGap	 = 3.0f;
			constexpr float32 totalW	 = numBars * barW + ( numBars - 1 ) * barGap;
			const float32	  startX	 = cx - totalW * 0.5f;
			for ( int32 barIndex = 0; barIndex < numBars; ++barIndex )
			{
				const float32 bx = startX + static_cast<float32>( barIndex ) * ( barW + barGap );
				const float32 bh = h * 0.45f * heights[barIndex];
				pDrawList->AddRectFilled( ImVec2( bx, cy - bh * 0.5f ), ImVec2( bx + barW, cy + bh * 0.5f ),
										  IM_COL32( 80, 210, 220, 240 ), 1.0f );
			}
		}
		else
		{
			// Generic document / data file
			pDrawList->AddRectFilled( ImVec2( minPos._x + w * 0.22f, minPos._y + h * 0.16f ),
									  ImVec2( minPos._x + w * 0.78f, minPos._y + h * 0.84f ),
									  IM_COL32( 65, 70, 82, 255 ), 3.0f );
			const utf8*	 pLbl  = typeLabel( ext, false );
			const ImVec2 txtSz = ImGui::CalcTextSize( pLbl );
			pDrawList->AddText( ImVec2( cx - txtSz.x * 0.5f, cy - txtSz.y * 0.5f ),
								IM_COL32( 220, 225, 235, 230 ), pLbl );
		}

		// Sub-border
		pDrawList->AddRect( minVec, maxVec, IM_COL32( 50, 55, 65, 200 ), 4.0f );
	}

	void ContentBrowserPanel::drawAssetContextMenu( const AssetEntry& entry )
	{
		if ( ImGui::BeginPopupContextItem( "AssetCtx" ) )
		{
			if ( ImGui::MenuItem( "Show in Explorer" ) )
			{
				utf8 arrCmd[constant::kMaxBuffer512];
				formatstring( arrCmd, sizeof( arrCmd ), "explorer.exe /select,\"%s\"", entry._absolutePath.c_str() );
				system( arrCmd );
			}

			if ( ImGui::MenuItem( "Copy Relative Path" ) )
			{
				ImGui::SetClipboardText( entry._relativePath.c_str() );
			}

			if ( ImGui::MenuItem( "Copy Absolute Path" ) )
			{
				ImGui::SetClipboardText( entry._absolutePath.c_str() );
			}

			ImGui::Separator();
			if ( ImGui::MenuItem( "Delete" ) )
			{
				FileUtil::removeFile( entry._absolutePath );
			}
			ImGui::EndPopup();
		}
	}

	ContentBrowserPanel::ContentBrowserPanel() noexcept
		: _listRoot{}
		, _listEntry{}
		, _selectedFolderAbs{}
		, _breadcrumb{}
		, _selectedAssetAbs{}
		, _arrSearchBuffer{}
		, _tileSize{ 96.0f }
		, _typeFilter{ AssetTypeFilter::All }
		, _viewMode{ ViewMode::Tiles }
		, _pendingImportMutex{}
		, _listPendingImportPath{}
		, _bRootsDirty{ 1 }
		, _bFolderDirty{ 1 }
		, _reservedFlags{ 0 }
	{
	}

	void ContentBrowserPanel::drawContent()
	{
		if ( _bRootsDirty == SW_TRUE )
			refreshRoots();
		processPendingImports();
		if ( _bFolderDirty == SW_TRUE )
			refreshCurrentFolder();

		drawToolbar();
		ImGui::Separator();

		drawSourcesSection();
		ImGui::SameLine();
		drawAssetView();
	}

	void ContentBrowserPanel::refreshRoots()
	{
		_listRoot.clear();

		const auto addRoot = [this]( const utf8* pName, string_view path )
		{
			if ( path.empty() )
				return;
			if ( FileUtil::directoryExists( path ) == false )
				return;
			ContentRoot root;
			root._displayName  = pName;
			root._absolutePath = FileUtil::normalizeSeparators( path );
			_listRoot.push_back( std::move( root ) );
		};

		addRoot( "game", ResourceUtil::getGameFolderPath() );
		addRoot( "engine", ResourceUtil::getEngineFolderPath() );
		addRoot( "common", ResourceUtil::getCommonFolderPath() );
		addRoot( "editor", ResourceUtil::getEditorFolderPath() );

		if ( _selectedFolderAbs.empty() && _listRoot.empty() == false )
			selectFolder( _listRoot.front()._absolutePath, _listRoot.front()._displayName );

		_bRootsDirty = SW_FALSE;
	}

	void ContentBrowserPanel::refreshCurrentFolder()
	{
		_listEntry.clear();
		if ( _selectedFolderAbs.empty() )
		{
			_bFolderDirty = SW_FALSE;
			return;
		}

		if ( FileUtil::directoryExists( _selectedFolderAbs ) == false )
		{
			_bFolderDirty = SW_FALSE;
			return;
		}

		auto addEntry = [this]( const string& path, bool bIsDirectory )
		{
			AssetEntry item;
			item._absolutePath = FileUtil::normalizeSeparators( path );
			item._name		   = FileUtil::getFileNamePart( item._absolutePath );
			item._bIsDirectory = bIsDirectory;
			if ( item._bIsDirectory == false )
				item._extension = FileUtil::getExtension( item._name );

			const string& resourceRoot = ResourceUtil::getRootFolderPath();
			if ( resourceRoot.empty() == false )
			{
				const string rootNorm = FileUtil::normalizePath( resourceRoot );
				const string absNorm  = FileUtil::normalizePath( item._absolutePath );
				if ( absNorm.size() > rootNorm.size() && absNorm.compare( 0, rootNorm.size(), rootNorm ) == 0 && absNorm[rootNorm.size()] == '/' )
					item._relativePath = absNorm.substr( rootNorm.size() + 1 );
			}
			if ( item._relativePath.empty() )
				item._relativePath = FileUtil::normalizePath( item._name );

			if ( item._bIsDirectory == false && FileUtil::endsWithIgnoreCase( item._name, ".meta" ) )
				return;

			if ( item._bIsDirectory == false && item._relativePath.empty() == false )
				editor::getService<ResourceManager>()->getAssetDatabase().ensureMeta( item._relativePath, false );

			_listEntry.push_back( std::move( item ) );
		};

		vector<string> listFolders;
		vector<string> listFiles;
		FileUtil::collectFolders( _selectedFolderAbs, listFolders, false, false );
		FileUtil::collectFiles( _selectedFolderAbs, {}, listFiles, false, false );
		for ( const string& folder : listFolders )
		{
			addEntry( folder, true );
		}
		for ( const string& file : listFiles )
		{
			addEntry( file, false );
		}

		std::sort( _listEntry.begin(), _listEntry.end(), []( const AssetEntry& entryA, const AssetEntry& entryB )
		{
			if ( entryA._bIsDirectory != entryB._bIsDirectory )
				return entryA._bIsDirectory > entryB._bIsDirectory;
			return entryA._name < entryB._name;
		} );

		_bFolderDirty = SW_FALSE;
	}

	bool ContentBrowserPanel::passesTypeFilter( const AssetEntry& entry ) const
	{
		if ( entry._bIsDirectory )
			return true;

		const string lower	   = StringUtil::toLower( entry._extension.c_str() );
		const string lowerName = StringUtil::toLower( entry._name.c_str() );

		switch ( _typeFilter )
		{
			case AssetTypeFilter::All:
				return true;
			case AssetTypeFilter::Scenes:
				return lowerName.find( ".scene." ) != string::npos || lower == ".scene";
			case AssetTypeFilter::Prefabs:
				return lowerName.find( ".pfb" ) != string::npos;
			case AssetTypeFilter::Textures:
				return lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".dds" || lower == ".tga" || lower == ".bmp";
			case AssetTypeFilter::Shaders:
				return lower == ".hlsl" || lower == ".glsl";
			case AssetTypeFilter::Materials:
				return lower == "._material";
			case AssetTypeFilter::Audio:
				return lower == ".wav" || lower == ".mp3" || lower == ".ogg";
			case AssetTypeFilter::Data:
				return lower == ".xml" || lower == ".json" || lower == ".csv" || lower == ".ini" || lower == ".kv";
			case AssetTypeFilter::Other:
				return lower != "._material" && lower != ".hlsl" && lower != ".glsl" && lower != ".png" && lower != ".wav" && lower != ".xml";
			case AssetTypeFilter::Count:
			default:
				return true;
		}
	}

	bool ContentBrowserPanel::passesSearchFilter( const AssetEntry& entry ) const
	{
		if ( _arrSearchBuffer[0] == '\0' )
			return true;

		const string filter = StringUtil::toLower( _arrSearchBuffer );
		const string name	= StringUtil::toLower( entry._name.c_str() );
		return name.find( filter ) != string::npos;
	}

	void ContentBrowserPanel::drawToolbar()
	{
		if ( editor::beginToolbar( "##cb_toolbar" ) )
		{
			editor::drawSearchField( "##cb_search", _arrSearchBuffer, sizeof( _arrSearchBuffer ), "Search Content", 160.0f,
									 false );

			ImGui::SameLine();
			static const utf8* kArrFilterNames[] = { "All", "Scenes", "Prefabs", "Textures",
													 "Shaders", "Materials", "Audio", "Data" };
			for ( int32 filterIdx = 0; filterIdx < 8; ++filterIdx )
			{
				const bool bActive = ( static_cast<int32>( _typeFilter ) == filterIdx );
				if ( editor::drawToggleButton( kArrFilterNames[filterIdx], bActive ) )
					_typeFilter = static_cast<AssetTypeFilter>( filterIdx );
				ImGui::SameLine();
			}

			if ( ImGui::RadioButton( "Tiles", _viewMode == ViewMode::Tiles ) )
				_viewMode = ViewMode::Tiles;
			ImGui::SameLine();
			if ( ImGui::RadioButton( "List", _viewMode == ViewMode::List ) )
				_viewMode = ViewMode::List;

			if ( _viewMode == ViewMode::Tiles )
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth( 80.0f );
				ImGui::SliderFloat( "##cb_tile", &_tileSize, 64.0f, 160.0f, "%.0f" );
			}

			ImGui::SameLine();
			const bool canImport = _selectedFolderAbs.empty() == false;
			if ( canImport == false )
				ImGui::BeginDisabled();
			const bool importClicked = ImGui::Button( "Import..." );
			const bool importHovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
			if ( canImport == false )
				ImGui::EndDisabled();
			if ( importClicked )
				importFilesFromDialog();
			if ( importHovered && canImport == false )
				ImGui::SetTooltip( "Select a destination folder first." );

			ImGui::SameLine();
			if ( ImGui::Button( "Refresh" ) )
			{
				_listEntry.clear();
				_selectedAssetAbs.clear();
				refreshRoots();
				refreshCurrentFolder();
			}
		}
		editor::endToolbar();
	}

	void ContentBrowserPanel::drawSourcesSection()
	{
		editor::EditorSectionDesc sourcesDesc{};
		sourcesDesc._pId	   = "##cb_sources";
		sourcesDesc._kind	   = editor::EditorSectionKind::Child;
		sourcesDesc._childSize = float2{ 220.0f, 0.0f };
		sourcesDesc._flags	   = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
		editor::beginSection( sourcesDesc );

		editor::drawSectionHeader( "Favorites" );
		struct FavFolder
		{
			const utf8* _label;
			const utf8* _relPath;
		};
		static const FavFolder kArrFavorites[] = {
			{  "Scenes",		"Resource/game/demo/maps"},
			{ "Prefabs",	 "Resource/game/demo/prefabs"},
			{"Textures", "Resource/game/demo/textures"},
			{ "Shaders",	 "Resource/engine/shaders"},
			{	  "Data",	  "Resource/game/demo/data"}
		  };

		for ( uint32 favIdx = 0; favIdx < 5; ++favIdx )
		{
			const string fullFavPath = FileUtil::normalizeSeparators(
				FileUtil::joinPath( FileUtil::getCurrentPath(), kArrFavorites[favIdx]._relPath ) );
			const bool bSelected = FileUtil::pathsEqualNormalized( fullFavPath, _selectedFolderAbs );

			if ( ImGui::Selectable( kArrFavorites[favIdx]._label, bSelected ) )
			{
				selectFolder( fullFavPath, string{ "Favorites / " } + kArrFavorites[favIdx]._label );
			}
		}

		ImGui::Separator();
		editor::drawSectionHeader( "Sources" );

		for ( const ContentRoot& root : _listRoot )
		{
			drawFolderTreeNode( root._absolutePath.c_str(), root._displayName, 0 );
		}

		if ( _listRoot.empty() )
			editor::drawEmptyHint( "No resource roots found." );

		editor::endSection();
	}

	void ContentBrowserPanel::drawFolderTreeNode( string_view folderPath, string_view label, int32 depth )
	{
		const string absPath  = FileUtil::normalizeSeparators( folderPath );
		const bool	 selected = FileUtil::pathsEqualNormalized( absPath, _selectedFolderAbs );

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if ( selected )
			flags |= ImGuiTreeNodeFlags_Selected;
		if ( depth == 0 )
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		vector<string> listChildren;
		FileUtil::collectFolders( absPath, listChildren, false, false );
		for ( string& child : listChildren )
		{
			child = FileUtil::normalizeSeparators( child );
		}
		const bool hasChildDirs = listChildren.empty() == false;
		if ( hasChildDirs == false )
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		ImGui::PushID( absPath.c_str() );
		const string labelNt( label );
		const bool	 opened = ImGui::TreeNodeEx( labelNt.c_str(), flags );
		if ( ImGui::IsItemClicked() && ImGui::IsItemToggledOpen() == false )
		{
			// 루트 이름 + 그 아래 상대 경로로 브레드크럼을 만듭니다.
			string crumb( label );
			for ( const ContentRoot& root : _listRoot )
			{
				const string rootNorm = FileUtil::normalizePath( root._absolutePath );
				const string absNorm  = FileUtil::normalizePath( absPath );
				if ( absNorm == rootNorm || ( absNorm.size() > rootNorm.size() && absNorm.compare( 0, rootNorm.size(), rootNorm ) == 0 && absNorm[rootNorm.size()] == '/' ) )
				{
					crumb = root._displayName;
					if ( absNorm.size() > rootNorm.size() )
					{
						string rel = absNorm.substr( rootNorm.size() + 1 );
						string pretty;
						size_t start{ 0 };
						while ( start < rel.size() )
						{
							size_t slash = rel.find( '/', start );
							if ( slash == string::npos )
								slash = rel.size();
							if ( pretty.empty() == false )
								pretty += " / ";
							pretty += rel.substr( start, slash - start );
							start = slash + 1;
						}
						if ( pretty.empty() == false )
							crumb += " / " + pretty;
					}
					break;
				}
			}
			selectFolder( absPath, crumb );
		}

		if ( opened && hasChildDirs )
		{
			std::sort( listChildren.begin(), listChildren.end() );
			for ( const string& child : listChildren )
			{
				drawFolderTreeNode( child, FileUtil::getFileNamePart( child ), depth + 1 );
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void ContentBrowserPanel::drawAssetView()
	{
		vector<AssetEntry> listVisible;
		listVisible.reserve( _listEntry.size() );
		for ( const AssetEntry& entry : _listEntry )
		{
			if ( passesTypeFilter( entry ) == false || passesSearchFilter( entry ) == false )
				continue;
			listVisible.push_back( entry );
		}

		editor::EditorSectionDesc assetsDesc{};
		assetsDesc._pId	  = "##cb_assets";
		assetsDesc._kind  = editor::EditorSectionKind::Child;
		assetsDesc._flags = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::FillRemaining;
		editor::beginSection( assetsDesc );

		drawBreadcrumbs();
		ImGui::Separator();

		if ( _viewMode == ViewMode::Tiles )
			drawTilesView( listVisible );
		else
			drawListView( listVisible );

		editor::endSection();

		editor::drawCountLabel( static_cast<uint32>( listVisible.size() ), 0, "items" );
		if ( _selectedAssetAbs.empty() == false )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "|  %s", _selectedAssetAbs.c_str() );
		}
	}

	void ContentBrowserPanel::drawBreadcrumbs()
	{
		if ( _breadcrumb.empty() )
		{
			ImGui::TextDisabled( "Select a folder" );
			return;
		}

		// "Game / Shaders"를 클릭 가능한 세그먼트로 파싱합니다.
		vector<string> listParts;
		{
			string remaining = _breadcrumb;
			size_t pos{ 0 };
			while ( pos < remaining.size() )
			{
				size_t sep = remaining.find( " / ", pos );
				if ( sep == string::npos )
				{
					listParts.push_back( remaining.substr( pos ) );
					break;
				}
				listParts.push_back( remaining.substr( pos, sep - pos ) );
				pos = sep + 3;
			}
		}

		string builtCrumb;
		string builtPath;
		for ( size_t partIndex = 0; partIndex < listParts.size(); ++partIndex )
		{
			if ( partIndex > 0 )
			{
				ImGui::SameLine();
				ImGui::TextUnformatted( "/" );
				ImGui::SameLine();
			}

			if ( partIndex == 0 )
			{
				builtCrumb = listParts[0];
				for ( const ContentRoot& root : _listRoot )
				{
					if ( root._displayName == listParts[0] )
					{
						builtPath = root._absolutePath;
						break;
					}
				}
			}
			else
			{
				builtCrumb += " / " + listParts[partIndex];
				const string lowerChild = FileUtil::normalizePath( listParts[partIndex] );
				string		 next		= FileUtil::joinPath( builtPath, listParts[partIndex] );
				if ( FileUtil::directoryExists( next ) == false && builtPath.empty() == false )
				{
					vector<string> listChildren;
					FileUtil::collectFolders( builtPath, listChildren, false, false );
					for ( const string& child : listChildren )
					{
						if ( FileUtil::normalizePath( FileUtil::getFileNamePart( child ) ) == lowerChild )
						{
							next = FileUtil::normalizeSeparators( child );
							break;
						}
					}
				}
				builtPath = std::move( next );
			}

			const bool bIsLast = ( partIndex + 1 == listParts.size() );
			if ( bIsLast )
				ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.35f, 1.0f ), "%s", listParts[partIndex].c_str() );
			else if ( ImGui::SmallButton( listParts[partIndex].c_str() ) )
				selectFolder( builtPath, builtCrumb );
		}
	}

	void ContentBrowserPanel::drawTilesView( const vector<AssetEntry>& visible )
	{
		const float32 cell		 = _tileSize;
		const float32 paddingX	 = ImGui::GetStyle().ItemSpacing.x;
		const float32 paddingY	 = ImGui::GetStyle().ItemSpacing.y;
		const float32 panelWidth = ImGui::GetContentRegionAvail().x;
		int32		  columns	 = static_cast<int32>( ( panelWidth + paddingX ) / ( cell + paddingX ) );
		if ( columns < 1 )
			columns = 1;

		const int32 itemCount = static_cast<int32>( visible.size() );
		const int32 rowCount  = ( itemCount + columns - 1 ) / columns;
		// 버튼 + 줄바꿈된 이름 라인 (셀 + 텍스트 라인)
		const float32 rowHeight = cell + ImGui::GetTextLineHeightWithSpacing() + paddingY;

		ImGuiListClipper clipper;
		clipper.Begin( rowCount, rowHeight );
		while ( clipper.Step() )
		{
			for ( int32 row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row )
			{
				for ( int32 col = 0; col < columns; ++col )
				{
					const int32 index = row * columns + col;
					if ( index >= itemCount )
						break;

					const AssetEntry& entry = visible[static_cast<size_t>( index )];
					ImGui::PushID( entry._absolutePath.c_str() );

					if ( col > 0 )
						ImGui::SameLine();

					const bool selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs ) || ( entry._bIsDirectory && FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedFolderAbs ) );
					ImGui::PushStyleColor( ImGuiCol_Button, selected ? ImVec4( 0.25f, 0.40f, 0.65f, 1.0f ) : ImVec4( 0.14f, 0.14f, 0.16f, 1.0f ) );
					ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.22f, 0.28f, 0.38f, 1.0f ) );

					ImGui::BeginGroup();
					const ImVec2 cursor = ImGui::GetCursorScreenPos();
					if ( ImGui::Button( "##tile", ImVec2( cell, cell ) ) )
						selectAsset( entry );
					drawAssetContextMenu( entry );
					if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
						openAsset( entry );
					if ( entry._bIsDirectory == false )
						editor::drawAssetDragSource( entry._relativePath.c_str(), true );

					ImDrawList*		  pDrawList = ImGui::GetWindowDrawList();
					constexpr float32 inset		= 6.0f;
					drawAssetThumbnail( pDrawList, float2{ cursor.x + inset, cursor.y + inset },
										float2{ cursor.x + cell - inset, cursor.y + cell * 0.65f }, entry );

					ImGui::PushTextWrapPos( ImGui::GetCursorPos().x + cell );
					ImGui::TextUnformatted( entry._name.c_str() );
					ImGui::PopTextWrapPos();
					ImGui::EndGroup();

					ImGui::PopStyleColor( 2 );
					ImGui::PopID();
				}
			}
		}
	}

	void ContentBrowserPanel::drawListView( const vector<AssetEntry>& visible )
	{
		constexpr ImGuiTableFlags flags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

		if ( ImGui::BeginTable( "##cb_list", 3, flags, ImGui::GetContentRegionAvail() ) )
		{
			ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f );
			ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin( static_cast<int32>( visible.size() ) );
			while ( clipper.Step() )
			{
				for ( int32 itemIndex = clipper.DisplayStart; itemIndex < clipper.DisplayEnd; ++itemIndex )
				{
					const AssetEntry& entry = visible[static_cast<size_t>( itemIndex )];
					ImGui::PushID( entry._absolutePath.c_str() );
					ImGui::TableNextRow();

					const bool selected = FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs );
					ImGui::TableSetColumnIndex( 0 );
					if ( ImGui::Selectable( entry._name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
					{
						selectAsset( entry );
						if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
							openAsset( entry );
					}
					drawAssetContextMenu( entry );
					if ( entry._bIsDirectory == false )
						editor::drawAssetDragSource( entry._relativePath.c_str() );

					ImGui::TableSetColumnIndex( 1 );
					ImGui::TextColored( colorForExtension( entry._bIsDirectory ? string{} : entry._extension ),
										"%s", typeLabel( entry._extension, entry._bIsDirectory ) );

					ImGui::TableSetColumnIndex( 2 );
					ImGui::TextUnformatted( entry._relativePath.c_str() );

					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}
	}

	void ContentBrowserPanel::selectFolder( string_view absolutePath, string_view breadcrumb )
	{
		// 탐색/I/O는 실제 FS 대소문자를 쓰고, 저장 경로는 ResourceUtil::makeSavePath로 상대 세그먼트를 소문자화합니다.
		_selectedFolderAbs = FileUtil::normalizeSeparators( absolutePath );
		_breadcrumb		   = breadcrumb;
		_selectedAssetAbs.clear();
		_bFolderDirty = true;
	}

	void ContentBrowserPanel::selectAsset( const AssetEntry& entry )
	{
		_selectedAssetAbs = entry._absolutePath;
		if ( entry._bIsDirectory == false )
			EditorContext::get()->getWorkspace().setFocusedAssetPath( entry._relativePath.c_str() );
	}

	void ContentBrowserPanel::openAsset( const AssetEntry& entry )
	{
		if ( entry._bIsDirectory )
		{
			string nextCrumb = _breadcrumb.empty() ? string( entry._name ) : string( _breadcrumb + " / " + entry._name );
			selectFolder( entry._absolutePath, nextCrumb );
			return;
		}

		selectAsset( entry );
		SW_LOG_TRACE( "Open: %#", entry._relativePath.c_str() );

		if ( EditorContext::get()->getAssetEditorManager().openAssetInEditor( entry._relativePath ) )
			return;

		const string lower = StringUtil::toLower( entry._extension.c_str() );

		const bool bSceneXml = lower == ".xml" && ( FileUtil::endsWithIgnoreCase( entry._relativePath, "_scene.xml" ) || entry._relativePath.find( "scene" ) != string::npos );
		if ( bSceneXml )
			editor::getService<SceneManager>()->requestLoadAsync( entry._relativePath );
		else if ( lower == "._material" )
			EditorContext::get()->getWorkspace().setInspectMode( InspectMode::Asset );
	}

	void ContentBrowserPanel::importFilesFromDialog()
	{
		if ( _selectedFolderAbs.empty() )
		{
			SW_LOG_WARNING( "Select a destination folder before importing." );
			return;
		}

		FileDialogParams params{};
		params._type				= FileDialogParams::Type::Open;
		params._title				= "Import to Content Browser";
		params._description			= "Assets";
		params._bEnableMultiselect	= true;
		params._initialDirectory	= _selectedFolderAbs;
		params._filterExtensionList = {
			"._material",
			".hlsl",
			".glsl",
			".png",
			".jpg",
			".jpeg",
			".tga",
			".dds",
			".json",
			".txt",
		};

		FileUtil::openFileDialog( params, SW_DELEGATE_METHOD( FileDialogDelegate, &ContentBrowserPanel::onImportDialogResult, this ) );
	}

	void ContentBrowserPanel::onImportDialogResult( const vector<string>& paths )
	{
		std::scoped_lock<mutex> lock{ _pendingImportMutex };
		_listPendingImportPath.insert( _listPendingImportPath.end(), paths.begin(), paths.end() );
	}

	void ContentBrowserPanel::processPendingImports()
	{
		vector<string> listPaths;
		{
			std::scoped_lock<mutex> lock{ _pendingImportMutex };
			if ( _listPendingImportPath.empty() )
				return;
			listPaths.swap( _listPendingImportPath );
		}

		if ( _selectedFolderAbs.empty() )
		{
			SW_LOG_WARNING( "Import cancelled — no destination folder." );
			return;
		}

		uint32 copied{ 0 };
		for ( const string& sourcePath : listPaths )
		{
			if ( FileUtil::fileExists( sourcePath ) == false )
			{
				SW_LOG_WARNING( "Import skipped (missing): %#", sourcePath.c_str() );
				continue;
			}

			const string fileName = FileUtil::getFileNamePart( sourcePath );
			// 루트는 FS 대소문자를 유지하고, 상대 폴더+파일명은 저장용으로 소문자 강제합니다.
			const string destPath = ResourceUtil::makeSavePath( _selectedFolderAbs, fileName );

			if ( FileUtil::pathsEqualNormalized( sourcePath, destPath ) )
			{
				SW_LOG_TRACE( "Already in folder: %#", fileName.c_str() );
				continue;
			}

			FileUtil::createDirectory( destPath );
			if ( FileUtil::copyFile( sourcePath, destPath ) )
			{
				++copied;
				const string rel = AssetDatabase::toRelativePath( destPath );
				if ( rel.empty() == false )
					editor::getService<ResourceManager>()->getAssetDatabase().ensureMeta( rel, true );
				SW_LOG_TRACE( "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
			}
			else
				SW_LOG_ERROR( "Failed to import: %#", sourcePath.c_str() );
		}

		if ( copied > 0 )
			_bFolderDirty = true;
	}
} // namespace sw::editor

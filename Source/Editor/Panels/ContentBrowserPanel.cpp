#include "pch.h"

#include "Editor/Panels/ContentBrowserPanel.h"

#include "Core/Concurrency/mutex.h"

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

		const string lower = StringUtil::toLower( entry._extension.c_str() );

		switch ( _typeFilter )
		{
			case AssetTypeFilter::All:
				return true;
			case AssetTypeFilter::Materials:
				return lower == "._material";
			case AssetTypeFilter::Shaders:
				return lower == ".hlsl" || lower == ".glsl";
			case AssetTypeFilter::Other:
				return lower != "._material" && lower != ".hlsl" && lower != ".glsl";
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
		editor::drawSearchField( "##cb_search", _arrSearchBuffer, sizeof( _arrSearchBuffer ), "Search Content", 220.0f,
								 false );

		ImGui::SameLine();
		ImGui::SetNextItemWidth( 140.0f );
		static const utf8* kArrFilterNames[] = { "All", "Materials", "Shaders", "Other" };
		int32			   filterIndex		 = static_cast<int32>( _typeFilter );
		if ( ImGui::Combo( "##cb_type", &filterIndex, kArrFilterNames, static_cast<int32>( AssetTypeFilter::Count ) ) )
			_typeFilter = static_cast<AssetTypeFilter>( filterIndex );

		ImGui::SameLine();
		if ( ImGui::RadioButton( "Tiles", _viewMode == ViewMode::Tiles ) )
			_viewMode = ViewMode::Tiles;
		ImGui::SameLine();
		if ( ImGui::RadioButton( "List", _viewMode == ViewMode::List ) )
			_viewMode = ViewMode::List;

		if ( _viewMode == ViewMode::Tiles )
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 120.0f );
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

	void ContentBrowserPanel::drawSourcesSection()
	{
		editor::EditorSectionDesc sourcesDesc{};
		sourcesDesc._pId	   = "##cb_sources";
		sourcesDesc._kind	   = editor::EditorSectionKind::Child;
		sourcesDesc._childSize = float2{ 220.0f, 0.0f };
		sourcesDesc._flags	   = editor::EditorSectionFlags::Border | editor::EditorSectionFlags::ResizeX;
		editor::beginSection( sourcesDesc );
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

		ImGui::Text( "%d items", static_cast<int32>( listVisible.size() ) );
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
					if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
						openAsset( entry );
					if ( entry._bIsDirectory == false && ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceAllowNullID ) )
					{
						ImGui::SetDragDropPayload( "SW_ASSET_PATH", entry._relativePath.c_str(),
												   entry._relativePath.size() + 1 );
						ImGui::TextUnformatted( entry._relativePath.c_str() );
						ImGui::EndDragDropSource();
					}

					ImDrawList*		  pDrawList = ImGui::GetWindowDrawList();
					const ImVec4	  tint		= colorForExtension( entry._bIsDirectory ? string{} : entry._extension );
					constexpr float32 inset		= 8.0f;
					pDrawList->AddRectFilled(
						ImVec2( cursor.x + inset, cursor.y + inset ),
						ImVec2( cursor.x + cell - inset, cursor.y + cell * 0.62f ),
						ImGui::ColorConvertFloat4ToU32( tint ),
						4.0f );

					const utf8*	 pLabel	  = typeLabel( entry._extension, entry._bIsDirectory );
					const ImVec2 textSize = ImGui::CalcTextSize( pLabel );
					pDrawList->AddText(
						ImVec2( cursor.x + ( cell - textSize.x ) * 0.5f, cursor.y + cell * 0.30f ),
						IM_COL32( 255, 255, 255, 230 ),
						pLabel );

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
					if ( entry._bIsDirectory == false && ImGui::BeginDragDropSource() )
					{
						ImGui::SetDragDropPayload( "SW_ASSET_PATH", entry._relativePath.c_str(),
												   entry._relativePath.size() + 1 );
						ImGui::TextUnformatted( entry._relativePath.c_str() );
						ImGui::EndDragDropSource();
					}

					ImGui::TableSetColumnIndex( 1 );
					ImGui::TextUnformatted( typeLabel( entry._extension, entry._bIsDirectory ) );

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

/**
 * @file ResourceBrowserPanel.cpp
 * @brief Unreal Content Browser 스타일 리소스 브라우저
 */
#include "Panels/ResourceBrowserPanel.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include <imgui.h>

namespace sw
{
	namespace
	{
		ImVec4 colorForExtension( const std::string& ext )
		{
			std::string lower = ext;
			std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );

			if ( lower == ".material" )
				return ImVec4( 0.85f, 0.35f, 0.25f, 1.0f );
			if ( lower == ".hlsl" || lower == ".glsl" || lower == ".vert" || lower == ".frag" )
				return ImVec4( 0.25f, 0.65f, 0.90f, 1.0f );
			if ( lower == ".png" || lower == ".jpg" || lower == ".jpeg" || lower == ".tga" || lower == ".dds" )
				return ImVec4( 0.45f, 0.80f, 0.35f, 1.0f );
			if ( lower.empty() == false )
				return ImVec4( 0.55f, 0.55f, 0.60f, 1.0f );
			return ImVec4( 0.35f, 0.40f, 0.55f, 1.0f ); // folder
		}

		const char* typeLabel( const std::string& ext, bool bIsDirectory )
		{
			if ( bIsDirectory )
				return "Folder";
			std::string lower = ext;
			std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c )
			{ return static_cast<char>( std::tolower( c ) ); } );
			if ( lower == ".material" )
				return "Material";
			if ( lower == ".hlsl" )
				return "Shader";
			if ( lower.empty() )
				return "File";
			return ext.c_str();
		}
	}

	void ResourceBrowserPanel::refreshRoots()
	{
		_roots.clear();

		const auto addRoot = [this]( const char* name, const std::string& path )
		{
			if ( path.empty() )
				return;
			std::error_code ec;
			if ( std::filesystem::is_directory( path, ec ) == false )
				return;
			ContentRoot root;
			root.displayName  = name;
			root.absolutePath = FileUtil::normalizePath( path );
			_roots.push_back( std::move( root ) );
		};

		addRoot( "Game", ResourceUtil::getGameFolderPath() );
		addRoot( "Engine", ResourceUtil::getEngineFolderPath() );
		addRoot( "Common", ResourceUtil::getCommonFolderPath() );

		if ( _selectedFolderAbs.empty() && _roots.empty() == false )
			selectFolder( _roots.front().absolutePath, _roots.front().displayName );

		_bRootsDirty = false;
	}

	void ResourceBrowserPanel::selectFolder( const std::string& absolutePath, const std::string& breadcrumb )
	{
		_selectedFolderAbs = FileUtil::normalizePath( absolutePath );
		_breadcrumb		   = breadcrumb;
		_selectedAssetAbs.clear();
		_bFolderDirty = true;
	}

	void ResourceBrowserPanel::refreshCurrentFolder()
	{
		_entries.clear();
		if ( _selectedFolderAbs.empty() )
		{
			_bFolderDirty = false;
			return;
		}

		std::error_code ec;
		if ( std::filesystem::is_directory( _selectedFolderAbs, ec ) == false )
		{
			_bFolderDirty = false;
			return;
		}

		for ( const auto& entry : std::filesystem::directory_iterator( _selectedFolderAbs, ec ) )
		{
			if ( ec )
				break;

			AssetEntry item;
			item.absolutePath = FileUtil::normalizePath( entry.path().generic_string() );
			item.name		  = entry.path().filename().generic_string();
			item.bIsDirectory = entry.is_directory( ec );
			if ( item.bIsDirectory == false )
				item.extension = entry.path().extension().generic_string();

			// Relative path under Resource root if possible
			const std::string& resourceRoot = ResourceUtil::getRootFolderPath();
			if ( resourceRoot.empty() == false && item.absolutePath.size() > resourceRoot.size() )
			{
				std::string rootNorm = FileUtil::normalizePath( resourceRoot );
				std::string absLower = item.absolutePath;
				std::string rootLower = rootNorm;
				std::transform( absLower.begin(), absLower.end(), absLower.begin(), []( unsigned char c )
				{ return static_cast<char>( std::tolower( c ) ); } );
				std::transform( rootLower.begin(), rootLower.end(), rootLower.begin(), []( unsigned char c )
				{ return static_cast<char>( std::tolower( c ) ); } );
				if ( absLower.rfind( rootLower, 0 ) == 0 )
				{
					size_t offset = rootNorm.size();
					while ( offset < item.absolutePath.size() && ( item.absolutePath[offset] == '/' || item.absolutePath[offset] == '\\' ) )
						++offset;
					item.relativePath = item.absolutePath.substr( offset );
				}
			}
			if ( item.relativePath.empty() )
				item.relativePath = item.name;

			_entries.push_back( std::move( item ) );
		}

		std::sort( _entries.begin(), _entries.end(), []( const AssetEntry& a, const AssetEntry& b )
		{
			if ( a.bIsDirectory != b.bIsDirectory )
				return a.bIsDirectory > b.bIsDirectory;
			return a.name < b.name;
		} );

		_bFolderDirty = false;
	}

	bool ResourceBrowserPanel::passesTypeFilter( const AssetEntry& entry ) const
	{
		if ( entry.bIsDirectory )
			return true;

		std::string lower = entry.extension;
		std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c )
		{ return static_cast<char>( std::tolower( c ) ); } );

		switch ( _typeFilter )
		{
			case AssetTypeFilter::All:
				return true;
			case AssetTypeFilter::Materials:
				return lower == ".material";
			case AssetTypeFilter::Shaders:
				return lower == ".hlsl" || lower == ".glsl";
			case AssetTypeFilter::Other:
				return lower != ".material" && lower != ".hlsl" && lower != ".glsl";
			case AssetTypeFilter::Count:
			default:
				return true;
		}
	}

	bool ResourceBrowserPanel::passesSearchFilter( const AssetEntry& entry ) const
	{
		if ( _searchBuffer[0] == '\0' )
			return true;

		std::string filter = _searchBuffer;
		std::string name   = entry.name;
		std::transform( filter.begin(), filter.end(), filter.begin(), []( unsigned char c )
		{ return static_cast<char>( std::tolower( c ) ); } );
		std::transform( name.begin(), name.end(), name.begin(), []( unsigned char c )
		{ return static_cast<char>( std::tolower( c ) ); } );
		return name.find( filter ) != std::string::npos;
	}

	void ResourceBrowserPanel::selectAsset( const AssetEntry& entry )
	{
		_selectedAssetAbs = entry.absolutePath;
	}

	void ResourceBrowserPanel::openAsset( const AssetEntry& entry )
	{
		if ( entry.bIsDirectory )
		{
			std::string nextCrumb = _breadcrumb.empty() ? entry.name : ( _breadcrumb + " / " + entry.name );
			selectFolder( entry.absolutePath, nextCrumb );
			return;
		}

		selectAsset( entry );
		SW_LOG_INFO( "[Content Browser] Open: %#", entry.relativePath.c_str() );
	}

	void ResourceBrowserPanel::drawToolbar()
	{
		ImGui::SetNextItemWidth( 220.0f );
		if ( ImGui::InputTextWithHint( "##cb_search", "Search Content", _searchBuffer, sizeof( _searchBuffer ) ) )
		{
			// filter is applied live; no folder rescan needed
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth( 140.0f );
		static const char* kFilterNames[] = { "All", "Materials", "Shaders", "Other" };
		int				   filterIdx	  = static_cast<int>( _typeFilter );
		if ( ImGui::Combo( "##cb_type", &filterIdx, kFilterNames, static_cast<int>( AssetTypeFilter::Count ) ) )
			_typeFilter = static_cast<AssetTypeFilter>( filterIdx );

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
		if ( ImGui::Button( "Refresh" ) )
		{
			_bRootsDirty  = true;
			_bFolderDirty = true;
		}
	}

	void ResourceBrowserPanel::drawFolderTreeNode( const std::filesystem::path& folderPath, const std::string& label, int depth )
	{
		const std::string absPath = FileUtil::normalizePath( folderPath.generic_string() );
		const bool		  selected = ( absPath == _selectedFolderAbs );

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if ( selected )
			flags |= ImGuiTreeNodeFlags_Selected;
		if ( depth == 0 )
			flags |= ImGuiTreeNodeFlags_DefaultOpen;

		std::error_code ec;
		bool			hasChildDirs = false;
		for ( const auto& child : std::filesystem::directory_iterator( folderPath, ec ) )
		{
			if ( ec )
				break;
			if ( child.is_directory( ec ) )
			{
				hasChildDirs = true;
				break;
			}
		}
		if ( hasChildDirs == false )
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		ImGui::PushID( absPath.c_str() );
		const bool opened = ImGui::TreeNodeEx( label.c_str(), flags );
		if ( ImGui::IsItemClicked() && ImGui::IsItemToggledOpen() == false )
		{
			// Build breadcrumb from root name + relative path under that root
			std::string crumb = label;
			for ( const ContentRoot& root : _roots )
			{
				std::string rootAbs = root.absolutePath;
				std::string absLow	= absPath;
				std::string rootLow = rootAbs;
				std::transform( absLow.begin(), absLow.end(), absLow.begin(), []( unsigned char c )
				{ return static_cast<char>( std::tolower( c ) ); } );
				std::transform( rootLow.begin(), rootLow.end(), rootLow.begin(), []( unsigned char c )
				{ return static_cast<char>( std::tolower( c ) ); } );
				if ( absLow.rfind( rootLow, 0 ) == 0 )
				{
					crumb = root.displayName;
					if ( absPath.size() > rootAbs.size() )
					{
						size_t offset = rootAbs.size();
						while ( offset < absPath.size() && ( absPath[offset] == '/' || absPath[offset] == '\\' ) )
							++offset;
						std::string rel = absPath.substr( offset );
						for ( char& c : rel )
						{
							if ( c == '/' || c == '\\' )
								c = '/';
						}
						std::string pretty;
						size_t		start = 0;
						while ( start < rel.size() )
						{
							size_t slash = rel.find( '/', start );
							if ( slash == std::string::npos )
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
			std::vector<std::filesystem::path> children;
			for ( const auto& child : std::filesystem::directory_iterator( folderPath, ec ) )
			{
				if ( ec )
					break;
				if ( child.is_directory( ec ) )
					children.push_back( child.path() );
			}
			std::sort( children.begin(), children.end() );
			for ( const auto& child : children )
				drawFolderTreeNode( child, child.filename().generic_string(), depth + 1 );
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void ResourceBrowserPanel::drawSourcesPanel()
	{
		ImGui::BeginChild( "##cb_sources", ImVec2( 220.0f, 0.0f ), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX );
		ImGui::TextUnformatted( "Sources" );
		ImGui::Separator();

		for ( const ContentRoot& root : _roots )
			drawFolderTreeNode( root.absolutePath, root.displayName, 0 );

		if ( _roots.empty() )
			ImGui::TextDisabled( "No resource roots found." );

		ImGui::EndChild();
	}

	void ResourceBrowserPanel::drawBreadcrumbs()
	{
		if ( _breadcrumb.empty() )
		{
			ImGui::TextDisabled( "Select a folder" );
			return;
		}

		// Parse "Game / Shaders" into clickable segments
		std::vector<std::string> parts;
		{
			std::string remaining = _breadcrumb;
			size_t		pos		  = 0;
			while ( pos < remaining.size() )
			{
				size_t sep = remaining.find( " / ", pos );
				if ( sep == std::string::npos )
				{
					parts.push_back( remaining.substr( pos ) );
					break;
				}
				parts.push_back( remaining.substr( pos, sep - pos ) );
				pos = sep + 3;
			}
		}

		std::string builtCrumb;
		std::string builtPath;
		for ( size_t i = 0; i < parts.size(); ++i )
		{
			if ( i > 0 )
			{
				ImGui::SameLine();
				ImGui::TextUnformatted( "/" );
				ImGui::SameLine();
			}

			if ( i == 0 )
			{
				builtCrumb = parts[0];
				for ( const ContentRoot& root : _roots )
				{
					if ( root.displayName == parts[0] )
					{
						builtPath = root.absolutePath;
						break;
					}
				}
			}
			else
			{
				builtCrumb += " / " + parts[i];
				builtPath = FileUtil::normalizePath( ( std::filesystem::path( builtPath ) / parts[i] ).generic_string() );
			}

			const bool isLast = ( i + 1 == parts.size() );
			if ( isLast )
			{
				ImGui::TextColored( ImVec4( 1.0f, 0.85f, 0.35f, 1.0f ), "%s", parts[i].c_str() );
			}
			else if ( ImGui::SmallButton( parts[i].c_str() ) )
			{
				selectFolder( builtPath, builtCrumb );
			}
		}
	}

	void ResourceBrowserPanel::drawTilesView( const std::vector<AssetEntry>& visible )
	{
		const float cell		= _tileSize;
		const float padding		= ImGui::GetStyle().ItemSpacing.x;
		const float panelWidth	= ImGui::GetContentRegionAvail().x;
		int			columns		= static_cast<int>( ( panelWidth + padding ) / ( cell + padding ) );
		if ( columns < 1 )
			columns = 1;

		int index = 0;
		for ( const AssetEntry& entry : visible )
		{
			ImGui::PushID( entry.absolutePath.c_str() );

			if ( index > 0 && ( index % columns ) != 0 )
				ImGui::SameLine();

			const bool selected = ( entry.absolutePath == _selectedAssetAbs )
								  || ( entry.bIsDirectory && entry.absolutePath == _selectedFolderAbs );
			ImGui::PushStyleColor( ImGuiCol_Button, selected ? ImVec4( 0.25f, 0.40f, 0.65f, 1.0f ) : ImVec4( 0.14f, 0.14f, 0.16f, 1.0f ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0.22f, 0.28f, 0.38f, 1.0f ) );

			ImGui::BeginGroup();
			const ImVec2 cursor = ImGui::GetCursorScreenPos();
			if ( ImGui::Button( "##tile", ImVec2( cell, cell ) ) )
				selectAsset( entry );
			if ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				openAsset( entry );

			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const ImVec4 tint	 = colorForExtension( entry.bIsDirectory ? std::string{} : entry.extension );
			const float	 inset	 = 8.0f;
			drawList->AddRectFilled(
				ImVec2( cursor.x + inset, cursor.y + inset ),
				ImVec2( cursor.x + cell - inset, cursor.y + cell * 0.62f ),
				ImGui::ColorConvertFloat4ToU32( tint ),
				4.0f );

			const char* label = typeLabel( entry.extension, entry.bIsDirectory );
			const ImVec2 textSize = ImGui::CalcTextSize( label );
			drawList->AddText(
				ImVec2( cursor.x + ( cell - textSize.x ) * 0.5f, cursor.y + cell * 0.30f ),
				IM_COL32( 255, 255, 255, 230 ),
				label );

			ImGui::PushTextWrapPos( ImGui::GetCursorPos().x + cell );
			ImGui::TextUnformatted( entry.name.c_str() );
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();

			ImGui::PopStyleColor( 2 );
			ImGui::PopID();
			++index;
		}
	}

	void ResourceBrowserPanel::drawListView( const std::vector<AssetEntry>& visible )
	{
		const ImGuiTableFlags flags =
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		if ( ImGui::BeginTable( "##cb_list", 3, flags, ImGui::GetContentRegionAvail() ) )
		{
			ImGui::TableSetupColumn( "Name", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableSetupColumn( "Type", ImGuiTableColumnFlags_WidthFixed, 90.0f );
			ImGui::TableSetupColumn( "Path", ImGuiTableColumnFlags_WidthStretch );
			ImGui::TableHeadersRow();

			for ( const AssetEntry& entry : visible )
			{
				ImGui::PushID( entry.absolutePath.c_str() );
				ImGui::TableNextRow();

				const bool selected = ( entry.absolutePath == _selectedAssetAbs );
				ImGui::TableSetColumnIndex( 0 );
				if ( ImGui::Selectable( entry.name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick ) )
				{
					selectAsset( entry );
					if ( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
						openAsset( entry );
				}

				ImGui::TableSetColumnIndex( 1 );
				ImGui::TextUnformatted( typeLabel( entry.extension, entry.bIsDirectory ) );

				ImGui::TableSetColumnIndex( 2 );
				ImGui::TextUnformatted( entry.relativePath.c_str() );

				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	void ResourceBrowserPanel::drawAssetView()
	{
		std::vector<AssetEntry> visible;
		visible.reserve( _entries.size() );
		for ( const AssetEntry& entry : _entries )
		{
			if ( passesTypeFilter( entry ) == false || passesSearchFilter( entry ) == false )
				continue;
			visible.push_back( entry );
		}

		ImGui::BeginChild( "##cb_assets", ImVec2( 0.0f, -ImGui::GetFrameHeightWithSpacing() ), ImGuiChildFlags_Borders );

		drawBreadcrumbs();
		ImGui::Separator();

		if ( _viewMode == ViewMode::Tiles )
			drawTilesView( visible );
		else
			drawListView( visible );

		ImGui::EndChild();

		ImGui::Text( "%d items", static_cast<int>( visible.size() ) );
		if ( _selectedAssetAbs.empty() == false )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "|  %s", _selectedAssetAbs.c_str() );
		}
	}

	void ResourceBrowserPanel::draw( const EditorUIContext& /*ctx*/ )
	{
		if ( ImGui::Begin( getWindowTitle() ) == false )
		{
			ImGui::End();
			return;
		}

		if ( _bRootsDirty )
			refreshRoots();
		if ( _bFolderDirty )
			refreshCurrentFolder();

		drawToolbar();
		ImGui::Separator();

		drawSourcesPanel();
		ImGui::SameLine();
		drawAssetView();

		ImGui::End();
	}
}

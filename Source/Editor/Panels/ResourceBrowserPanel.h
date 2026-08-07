#pragma once
#include "Panels/IEditorPanel.h"
#include <filesystem>
#include <string>
#include <vector>

namespace sw
{
	class ResourceBrowserPanel : public IEditorPanel
	{
	public:
		const char* getWindowTitle() const override { return "Content Browser"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		enum class AssetTypeFilter : int
		{
			All = 0,
			Materials,
			Shaders,
			Other,
			Count
		};

		enum class ViewMode : int
		{
			Tiles = 0,
			List
		};

		struct ContentRoot
		{
			std::string displayName;
			std::string absolutePath;
		};

		struct AssetEntry
		{
			std::string name;
			std::string relativePath;
			std::string absolutePath;
			std::string extension;
			bool		bIsDirectory = false;
		};

		void refreshRoots();
		void refreshCurrentFolder();
		bool passesTypeFilter( const AssetEntry& entry ) const;
		bool passesSearchFilter( const AssetEntry& entry ) const;
		void drawToolbar();
		void drawSourcesPanel();
		void drawFolderTreeNode( const std::filesystem::path& folderPath, const std::string& label, int depth );
		void drawAssetView();
		void drawBreadcrumbs();
		void drawTilesView( const std::vector<AssetEntry>& visible );
		void drawListView( const std::vector<AssetEntry>& visible );
		void selectFolder( const std::string& absolutePath, const std::string& breadcrumb );
		void selectAsset( const AssetEntry& entry );
		void openAsset( const AssetEntry& entry );

		std::vector<ContentRoot> _roots;
		std::vector<AssetEntry>	 _entries;
		std::string				 _selectedFolderAbs;
		std::string				 _breadcrumb; // e.g. "Game / Shaders"
		std::string				 _selectedAssetAbs;
		char					 _searchBuffer[128] = {};
		AssetTypeFilter			 _typeFilter		= AssetTypeFilter::All;
		ViewMode				 _viewMode			= ViewMode::Tiles;
		float					 _tileSize			= 96.0f;
		bool					 _bRootsDirty		= true;
		bool					 _bFolderDirty		= true;
	};
}

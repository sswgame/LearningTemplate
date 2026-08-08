#pragma once
/**
 * @file ContentBrowserWindow.h
 * @brief Engine/Common/Game ë¦¬ì†Œ???¸ë¦¬Â·?ì…‹ ë¸Œë¼?°ì? ?¨ë„
 */
#include "Windows/IEditorWindow.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	/** @brief ë¦¬ì†Œ??ë£¨íŠ¸ë¥??ìƒ‰?˜ê³  ?ì…‹??? íƒÂ·?¬ëŠ” Content Browser */
	class ContentBrowserWindow : public IEditorWindow
	{
	public:
		ContentBrowserWindow() noexcept;

		const char* getWindowTitle() const override { return "Content Browser"; }
		/** @brief ?ŒìŠ¤ ?¸ë¦¬Â·?´ë°”Â·?ì…‹ ?€??ë¦¬ìŠ¤??ë·°ë? ê·¸ë¦½?ˆë‹¤. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		/** @brief ?œì‹œ???ì…‹ ì¢…ë¥˜ ?„í„° */
		enum class AssetTypeFilter : int
		{
			All = 0,
			Materials,
			Shaders,
			Other,
			Count
		};

		/** @brief ?ì…‹ ë·??œì‹œ ë°©ì‹ */
		enum class ViewMode : int
		{
			Tiles = 0,
			List
		};

		/** @brief ë¦¬ì†Œ??ë£¨íŠ¸(?œì‹œ ?´ë¦„ + ?ˆë? ê²½ë¡œ) */
		struct ContentRoot
		{
			std::string displayName;
			std::string absolutePath;
		};

		/** @brief ?´ë”/?Œì¼ ????ª© */
		struct AssetEntry
		{
			std::string name;
			std::string relativePath;
			std::string absolutePath;
			std::string extension;
			bool		bIsDirectory = false;
		};

		/** @brief Engine/Common/Game ??ë£¨íŠ¸ ëª©ë¡???¤ì‹œ ?˜ì§‘?©ë‹ˆ?? */
		void refreshRoots();
		/** @brief ?„ì¬ ?´ë”???ì‹ ??ª©???¤ì‹œ ?¤ìº”?©ë‹ˆ?? */
		void refreshCurrentFolder();
		/** @brief ?€???„í„°ë¥??µê³¼?˜ëŠ”ì§€ ê²€?¬í•©?ˆë‹¤. */
		bool passesTypeFilter( const AssetEntry& entry ) const;
		/** @brief ê²€?‰ì–´ ?„í„°ë¥??µê³¼?˜ëŠ”ì§€ ê²€?¬í•©?ˆë‹¤. */
		bool passesSearchFilter( const AssetEntry& entry ) const;
		/** @brief ê²€?‰Â·í•„?°Â·ë·° ëª¨ë“œ ?´ë°”ë¥?ê·¸ë¦½?ˆë‹¤. */
		void drawToolbar();
		/** @brief ì¢Œì¸¡ ?ŒìŠ¤/?´ë” ?¸ë¦¬ë¥?ê·¸ë¦½?ˆë‹¤. */
		void drawSourcesPanel();
		/** @brief ?´ë” ?¸ë¦¬ ?¸ë“œ ??ê°œë? ?¬ê? ê·¸ë¦½?ˆë‹¤. */
		void drawFolderTreeNode( const std::filesystem::path& folderPath, const std::string& label, int depth );
		/** @brief ?°ì¸¡ ?ì…‹ ë·??€??ë¦¬ìŠ¤??ë¥?ê·¸ë¦½?ˆë‹¤. */
		void drawAssetView();
		/** @brief ?„ì¬ ê²½ë¡œ breadcrumbë¥?ê·¸ë¦½?ˆë‹¤. */
		void drawBreadcrumbs();
		/** @brief ?€??ê·¸ë¦¬??ë·°ë? ê·¸ë¦½?ˆë‹¤. */
		void drawTilesView( const std::vector<AssetEntry>& visible );
		/** @brief ë¦¬ìŠ¤??ë·°ë? ê·¸ë¦½?ˆë‹¤. */
		void drawListView( const std::vector<AssetEntry>& visible );
		/** @brief ?„ì¬ ?´ë” ? íƒ??ë°”ê¿‰?ˆë‹¤. */
		void selectFolder( const std::string& absolutePath, const std::string& breadcrumb );
		/** @brief ?ì…‹??? íƒ ?íƒœë¡??œì‹œ?©ë‹ˆ?? */
		void selectAsset( const AssetEntry& entry );
		/** @brief ?ì…‹???½ë‹ˆ???”ë ‰?°ë¦¬ë©?ì§„ì…, ?Œì¼?´ë©´ ? íƒ). */
		void openAsset( const AssetEntry& entry );
		/** @brief ?¤ì´?°ë¸Œ ?Œì¼ ?¤ì´?¼ë¡œê·¸ë¡œ ?„ì¬ ?´ë”???Œì¼??ê°€?¸ì˜µ?ˆë‹¤. */
		void importFilesFromDialog();
		/** @brief ë°±ê·¸?¼ìš´???¤ì´?¼ë¡œê·¸ì—???ì‰??importë¥?ë©”ì¸ ?¤ë ˆ?œì—??ì²˜ë¦¬?©ë‹ˆ?? */
		void processPendingImports();

		std::vector<ContentRoot> _roots;
		std::vector<AssetEntry>	 _entries;
		std::string				 _selectedFolderAbs;
		std::string				 _breadcrumb; ///< ?? "Game / Shaders"
		std::string				 _selectedAssetAbs;
		char					 _searchBuffer[128] = {};
		float					 _tileSize			= 96.0f;
		AssetTypeFilter			 _typeFilter		= AssetTypeFilter::All;
		ViewMode				 _viewMode			= ViewMode::Tiles;
		std::mutex				 _pendingImportMutex;
		std::vector<std::string> _pendingImportPaths;
		uint8					 _bRootsDirty	: 1;
		uint8					 _bFolderDirty	: 1;
		[[maybe_unused]] uint8	 _reservedFlags : 6;
	};
} // namespace sw

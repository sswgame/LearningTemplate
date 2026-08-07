#pragma once
/**
 * @file ResourceBrowserPanel.h
 * @brief Engine/Common/Game 리소스 트리·에셋 브라우저 패널
 */
#include "Panels/IEditorPanel.h"

namespace sw
{
	/** @brief 리소스 루트를 탐색하고 에셋을 선택·여는 Content Browser */
	class ResourceBrowserPanel : public IEditorPanel
	{
	public:
		ResourceBrowserPanel() noexcept;

		const char* getWindowTitle() const override { return "Content Browser"; }
		/** @brief 소스 트리·툴바·에셋 타일/리스트 뷰를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;

	private:
		/** @brief 표시할 에셋 종류 필터 */
		enum class AssetTypeFilter : int
		{
			All = 0,
			Materials,
			Shaders,
			Other,
			Count
		};

		/** @brief 에셋 뷰 표시 방식 */
		enum class ViewMode : int
		{
			Tiles = 0,
			List
		};

		/** @brief 리소스 루트(표시 이름 + 절대 경로) */
		struct ContentRoot
		{
			std::string displayName;
			std::string absolutePath;
		};

		/** @brief 폴더/파일 한 항목 */
		struct AssetEntry
		{
			std::string name;
			std::string relativePath;
			std::string absolutePath;
			std::string extension;
			bool		bIsDirectory = false;
		};

		/** @brief Engine/Common/Game 등 루트 목록을 다시 수집합니다. */
		void refreshRoots();
		/** @brief 현재 폴더의 자식 항목을 다시 스캔합니다. */
		void refreshCurrentFolder();
		/** @brief 타입 필터를 통과하는지 검사합니다. */
		bool passesTypeFilter( const AssetEntry& entry ) const;
		/** @brief 검색어 필터를 통과하는지 검사합니다. */
		bool passesSearchFilter( const AssetEntry& entry ) const;
		/** @brief 검색·필터·뷰 모드 툴바를 그립니다. */
		void drawToolbar();
		/** @brief 좌측 소스/폴더 트리를 그립니다. */
		void drawSourcesPanel();
		/** @brief 폴더 트리 노드 한 개를 재귀 그립니다. */
		void drawFolderTreeNode( const std::filesystem::path& folderPath, const std::string& label, int depth );
		/** @brief 우측 에셋 뷰(타일/리스트)를 그립니다. */
		void drawAssetView();
		/** @brief 현재 경로 breadcrumb를 그립니다. */
		void drawBreadcrumbs();
		/** @brief 타일 그리드 뷰를 그립니다. */
		void drawTilesView( const std::vector<AssetEntry>& visible );
		/** @brief 리스트 뷰를 그립니다. */
		void drawListView( const std::vector<AssetEntry>& visible );
		/** @brief 현재 폴더 선택을 바꿉니다. */
		void selectFolder( const std::string& absolutePath, const std::string& breadcrumb );
		/** @brief 에셋을 선택 상태로 표시합니다. */
		void selectAsset( const AssetEntry& entry );
		/** @brief 에셋을 엽니다(디렉터리면 진입, 파일이면 선택). */
		void openAsset( const AssetEntry& entry );

		std::vector<ContentRoot> _roots;
		std::vector<AssetEntry>	 _entries;
		std::string				 _selectedFolderAbs;
		std::string				 _breadcrumb; ///< 예: "Game / Shaders"
		std::string				 _selectedAssetAbs;
		char					 _searchBuffer[128] = {};
		float					 _tileSize			= 96.0f;
		AssetTypeFilter			 _typeFilter		= AssetTypeFilter::All;
		ViewMode				 _viewMode			= ViewMode::Tiles;
		uint8					 _bRootsDirty	: 1;
		uint8					 _bFolderDirty	: 1;
		[[maybe_unused]] uint8	 _reservedFlags : 6;
	};
} // namespace sw

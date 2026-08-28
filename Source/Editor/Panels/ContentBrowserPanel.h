/**
 * @file ContentBrowserPanel.h
 * @brief Engine / Common / Game / Editor 애셋 트리를 탐색하는 콘텐츠 브라우저 윈도우
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"

struct ImDrawList;

namespace sw::editor
{
	/** @brief 콘텐츠 루트를 탐색하고 애셋을 선택·엽니다 */
	class ContentBrowserPanel : public IEditorPanel
	{
	public:
		/** @brief 콘텐츠 브라우저 윈도우를 생성합니다. */
		ContentBrowserPanel() noexcept;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Content Browser"; }
		/** @brief 소스 트리, 브레드크럼, 애셋 타일/리스트를 그립니다. */
		void drawContent() override;

	private:
		// ------------------------------------------------------------------------------
		// 2) 필터 · 뷰 모드 · 항목
		// ------------------------------------------------------------------------------
		/** @brief 현재 폴더 뷰의 애셋 타입 필터 */
		enum class AssetTypeFilter : int32
		{
			All = 0,
			Scenes,
			Prefabs,
			Textures,
			Shaders,
			Materials,
			Audio,
			Data,
			Other,
			Count
		};

		/** @brief 애셋 뷰 레이아웃 */
		enum class ViewMode : int32
		{
			Tiles = 0,
			List
		};

		/** @brief 이름이 있는 콘텐츠 루트 (표시 라벨 + 절대 경로) */
		struct ContentRoot
		{
			string _displayName;
			string _absolutePath;
		};

		/** @brief 현재 목록의 폴더 또는 파일 항목 */
		struct AssetEntry
		{
			string _name;
			string _relativePath;
			string _absolutePath;
			string _extension;
			bool   _bIsDirectory{ false };
		};

		// ------------------------------------------------------------------------------
		// 3) 스캔 · 필터
		// ------------------------------------------------------------------------------
		/** @brief Engine / Common / Game / Editor 루트 목록을 다시 구성합니다. */
		void refreshRoots();
		/** @brief 선택된 폴더의 항목을 다시 스캔합니다. */
		void refreshCurrentFolder();
		/** @brief 항목이 타입 필터를 통과하는지 여부를 반환합니다. */
		bool passesTypeFilter( const AssetEntry& entry ) const;
		/** @brief 항목이 검색 필터를 통과하는지 여부를 반환합니다. */
		bool passesSearchFilter( const AssetEntry& entry ) const;

		// ------------------------------------------------------------------------------
		// 4) 패널 그리기 — 툴바 / 소스 트리 / 타일·리스트
		// ------------------------------------------------------------------------------
		/** @brief 검색 / 필터 / 뷰 모드 툴바를 그립니다. */
		void drawToolbar();
		/** @brief 왼쪽 루트와 폴더 트리를 그립니다. */
		void drawSourcesSection();
		/** @brief 재귀 폴더 트리 노드 하나를 그립니다. */
		void drawFolderTreeNode( string_view folderPath, string_view label, int32 depth );
		/** @brief 오른쪽 애셋 타일 또는 리스트를 그립니다. */
		void drawAssetView();
		/** @brief 현재 폴더의 브레드크럼 경로를 그립니다. */
		void drawBreadcrumbs();
		/** @brief 보이는 항목의 타일 그리드를 그립니다. */
		void drawTilesView( const vector<AssetEntry>& visible );
		/** @brief 보이는 항목의 리스트 행을 그립니다. */
		void drawListView( const vector<AssetEntry>& visible );
		/** @brief 애셋 항목 우클릭 컨텍스트 메뉴를 그립니다. */
		void drawAssetContextMenu( const AssetEntry& entry );
		/** @brief 애셋 항목 썸네일/아이콘을 그립니다. */
		void drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
								 const AssetEntry& entry );

		// ------------------------------------------------------------------------------
		// 5) 선택 · 임포트
		//    파일 대화상자는 백그라운드, processPendingImports는 메인 스레드
		// ------------------------------------------------------------------------------
		/** @brief 폴더를 선택하고 내용을 새로고침합니다. */
		void selectFolder( string_view absolutePath, string_view breadcrumb );
		/** @brief 애셋을 선택된 상태로 표시합니다. */
		void selectAsset( const AssetEntry& entry );
		/** @brief 폴더를 열거나 파일 애셋에 포커스/오픈합니다. */
		void openAsset( const AssetEntry& entry );
		/** @brief 대화상자로 현재 폴더에 파일을 임포트합니다. */
		void importFilesFromDialog();
		/** @brief 파일 대화상자가 고른 경로를 임포트 큐에 넣습니다. */
		void onImportDialogResult( const vector<string>& paths );
		/** @brief 백그라운드 임포트 경로를 메인 스레드에서 처리합니다. */
		void processPendingImports();

	private:
		vector<ContentRoot>	   _listRoot;
		vector<AssetEntry>	   _listEntry;
		string				   _selectedFolderAbs;
		string				   _breadcrumb; /**< 예: "Game / Shaders" */
		string				   _selectedAssetAbs;
		utf8				   _arrSearchBuffer[constant::kMaxBuffer128];
		float32				   _tileSize;
		AssetTypeFilter		   _typeFilter;
		ViewMode			   _viewMode;
		mutex				   _pendingImportMutex;
		vector<string>		   _listPendingImportPath;
		uint8				   _bRootsDirty	  : 1;
		uint8				   _bFolderDirty  : 1;
		[[maybe_unused]] uint8 _reservedFlags : 6;
	};
} // namespace sw::editor

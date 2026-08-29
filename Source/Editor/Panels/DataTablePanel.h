/**
 * @file DataTablePanel.h
 * @brief 다국어 로컬라이제이션 테이블 및 게임 XML 데이터 테이블 편집기 패널
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Commands/EditorDataTableCommands.h"
#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	/**
	 * @class DataTablePanel
	 * @brief 로컬라이제이션 JSON 파일(ko_KR, en_US, ja_JP) 및 데이터 XML 파일을 실시간 검사/편집/저장하는 에디터 윈도우
	 */
	class DataTablePanel : public IEditorPanel
	{
	public:
		/** @brief 데이터 테이블 윈도우를 생성합니다. */
		DataTablePanel();
		/** @brief 소멸자. */
		virtual ~DataTablePanel() override = default;

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 윈도우 제목을 반환합니다. */
		const utf8* getPanelTitle() const override { return "Data Table Editor"; }
		/** @brief 패널 UI를 그립니다. */
		void drawContent() override;
		/** @brief 온디맨드 도구이므로 기본적으로 닫힌 채 시작합니다. */
		bool isToolPanel() const override { return true; }
		bool trySaveDirtyDocument() override;
		bool isDocumentDirty() const override;
		void discardDirtyDocument() override;

	private:
		void drawLocalizationTab();
		void drawGameDataTab();

		void reloadLocalization();
		void saveLocalization();

		void			 reloadGameDataFiles();
		void			 loadSelectedGameDataFile();
		void			 saveSelectedGameDataFile();
		void			 pollBackgroundJobs();
		void			 markLocDirty();
		void			 markGameDataDirty();
		EditorPanelFlags getPanelFlags() const override;

	private:
		int32								  _activeTab;
		fixed_string<constant::kMaxBuffer128> _locFilter;
		fixed_string<constant::kMaxBuffer128> _newKeyBuffer;
		vector<LocRecord>					  _listLocRecord;
		vector<GameDataFileEntry>			  _listGameDataFile;
		int32								  _selectedGameDataIndex;
		string								  _selectedGameDataRawText;
		string								  _savedGameDataRawText;
		EditorLocalizationLoadJob			  _locJob;
		EditorGameDataScanJob				  _gameDataJob;
		uint8								  _bLocLoaded	   : 1;
		uint8								  _bGameDataLoaded : 1;
		uint8								  _bLocDirty	   : 1;
		uint8								  _bGameDataDirty  : 1;
		[[maybe_unused]] uint8				  _reserved		   : 4;
	};
} // namespace sw::editor

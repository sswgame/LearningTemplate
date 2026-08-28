/**
 * @file DataTablePanel.h
 * @brief 다국어 로컬라이제이션 테이블 및 게임 XML 데이터 테이블 편집기 패널
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw::editor
{
	/** @brief 로컬라이제이션 문자열 다국어 레코드 */
	struct LocRecord
	{
		string _key;
		string _enUS;
		string _koKR;
		string _jaJP;
		bool   _bModified{ false };
	};

	/** @brief 게임 데이터 XML 파일 항목 */
	struct GameDataFileEntry
	{
		string _fileName;
		string _relativePath;
		string _absolutePath;
	};

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

	private:
		void drawLocalizationTab();
		void drawGameDataTab();

		void reloadLocalization();
		void saveLocalization();

		void reloadGameDataFiles();
		void loadSelectedGameDataFile();
		void saveSelectedGameDataFile();

	private:
		int32					  _activeTab;
		utf8					  _arrLocFilter[constant::kMaxBuffer128];
		utf8					  _arrNewKeyBuffer[constant::kMaxBuffer128];
		vector<LocRecord>		  _listLocRecord;
		vector<GameDataFileEntry> _listGameDataFile;
		int32					  _selectedGameDataIndex;
		string					  _selectedGameDataRawText;
		bool					  _bLocLoaded;
		bool					  _bGameDataLoaded;
	};
} // namespace sw::editor

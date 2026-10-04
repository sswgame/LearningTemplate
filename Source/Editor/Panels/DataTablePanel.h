/**
 * @file DataTablePanel.h
 * @brief 로컬라이제이션 프로젝트 표(원문 · 문화권 번역 · 낡음/검토 상태) 및 게임 XML 데이터 테이블 편집기 패널
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
     * @brief 로컬라이제이션 프로젝트(엔진 · 게임)의 원문 표와 문화권 번역을 한 표로 편집하고, 데이터 XML 파일을 살펴보고 편집 · 저장하는 에디터 창입니다.
     * @details 번역 칸의 색이 상태입니다 — 주황은 원문이 바뀐 뒤의 번역(낡음), 노랑은 검토 표시, 빨강 테두리 글은 최대 길이를 넘은 번역입니다.
     *          저장하면 실행 중인 게임이 그 프로젝트를 다시 읽습니다.
     */
    class DataTablePanel : public IEditorPanel
    {
    public:
        /** @brief 데이터 테이블 창을 만듭니다. */
        DataTablePanel();
        /** @brief 소멸자. */
        virtual ~DataTablePanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Data Table Editor"; }
        /** @brief 패널 UI를 그립니다. */
        void drawContent() override;
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool               isToolPanel() const override { return true; }
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

    private:
        void drawLocalizationTab();
        /** @brief 로컬라이즈 툴바를 그립니다. */
        void drawLocalizationToolbar();
        /** @brief 로컬라이즈 레코드 표를 그립니다. */
        void drawLocalizationTable();
        /** @brief 번역 칸 하나를 상태 색 · 길이 경고 · 맥락 툴팁과 함께 그립니다. 고쳤으면 true 입니다. */
        bool drawTranslationCell( LocalizationRecord& record, size_t cultureIndex );
        void drawGameDataTab();

        void reloadLocalization();
        void saveLocalization();

        void reloadGameDataFiles();
        void loadSelectedGameDataFile();
        void saveSelectedGameDataFile();
        void pollBackgroundJobs();
        void markLocalizationDirty();
        void markGameDataDirty();
        /** @brief 두 문서의 dirty 비트를 기반 클래스의 문서 dirty 비트에 반영합니다. */
        void syncDocumentDirty();

    private:
        fixed_string<constant::kMaxBuffer128> _localizationFilter;
        fixed_string<constant::kMaxBuffer128> _newKeyBuffer;
        LocalizationSheet                     _localizationSheet;
        vector<string>                        _listLocalizationProject;
        /** @brief 이번 프레임에 보일 행의 인덱스입니다. 프레임마다 지우고 다시 채우는 재사용 버퍼입니다. */
        vector<size_t>            _listVisibleLocalizationIndex;
        vector<GameDataFileEntry> _listGameDataFile;
        string                    _selectedGameDataRawText;
        string                    _savedGameDataRawText;
        EditorLocalizationLoadJob _localizationJob;
        EditorGameDataScanJob     _gameDataJob;
        int32                     _selectedGameDataIndex;
        int32                     _selectedProjectIndex;
        uint8                     _bLocalizationLoaded : 1;
        uint8                     _bGameDataLoaded     : 1;
        uint8                     _bLocalizationDirty  : 1;
        uint8                     _bGameDataDirty      : 1;
        [[maybe_unused]] uint8    _reserved            : 4;
    };
} // namespace sw::editor

/**
 * @file DataTablePanel.h
 * @brief 로컬라이제이션 프로젝트 표(원문 · 문화권 번역 · 낡음/검토 상태) 및 게임 XML 데이터 테이블 편집기 패널
 * @details 문서 · 목록 · 읽기 · 저장은 `DataTableModel`(Model/, ImGui 없음)이 들고, 이 창은 그리기와 입력란 글 · 원문 보기 토글 · 표 편집기만 든다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/Catalog/CatalogTableEditor.h"
#include "Editor/Panels/DataTable/Model/DataTableModel.h"

namespace sw::editor
{
    /**
     * @class DataTablePanel
     * @brief 로컬라이제이션 프로젝트(엔진 · 게임)의 원문 테이블과 문화권 번역을 한 테이블로 편집하고, 데이터 XML 파일을 원소별 표(또는 XML 원문)로 편집 · 저장하는 에디터 창입니다.
     * @details 번역 칸의 색이 상태입니다 — 주황은 원문이 바뀐 뒤의 번역(낡음), 노랑은 검토 표시, 빨강 테두리 글은 최대 길이를 넘은 번역입니다.
     *          저장하면 실행 중인 게임이 그 프로젝트를 다시 읽습니다.
     */
    class DataTablePanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "data_table";

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
        /** @brief 로컬라이즈 레코드 테이블을 그립니다. */
        void drawLocalizationTable();
        /** @brief 현지화 테이블의 한 줄(키 · 원문 · 번역 필드 · 삭제 단추)을 그립니다. 삭제 단추를 눌렀으면 true 입니다. */
        [[nodiscard]] bool drawLocalizationRow( size_t recordIndex, size_t cultureCount );
        /** @brief 번역 필드 하나를 상태 색 · 길이 경고 · 맥락 툴팁과 함께 그립니다. 고쳤으면 true 입니다. */
        bool drawTranslationCell( LocalizationRecord& record, size_t cultureIndex );
        void drawGameDataTab();
        /** @brief 모델의 두 문서 dirty 비트를 기반 클래스의 문서 dirty 비트에 반영합니다. */
        void syncDocumentDirty() override;

    private:
        DataTableModel                        _model;
        fixed_string<constant::kMaxBuffer128> _localizationFilter;
        fixed_string<constant::kMaxBuffer128> _newKeyBuffer;
        /** @brief 이번 프레임에 보일 행의 인덱스입니다. 프레임마다 지우고 다시 채우는 재사용 버퍼입니다. */
        vector<size_t>     _listVisibleLocalizationIndex;
        CatalogTableEditor _gameDataTable;
        /** @brief 표 편집기가 본 모델의 게임 데이터 읽기 횟수입니다. 다르면 표 편집기 상태(고른 칸 · 되돌리기 위치)를 지운다. */
        uint32 _seenGameDataLoadCount;
        bool   _bGameDataRaw;
    };
} // namespace sw::editor

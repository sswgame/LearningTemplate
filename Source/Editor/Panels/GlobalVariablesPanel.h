/**
 * @file GlobalVariablesPanel.h
 * @brief 전역 변수(치트, 디버그 플래그, 환경 설정 등)를 살펴보고 편집하는 에디터 창입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    struct GlobalVariableInfo;

    class GlobalVariableManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 등록된 모든 전역 변수를 나열하고 실시간으로 편집하는 에디터 도구 창입니다. */
    class GlobalVariablesPanel : public IEditorPanel
    {
    public:
        /** @brief 전역 변수 창을 만듭니다(도구 창이라 닫힌 채 시작합니다). */
        GlobalVariablesPanel();
        /** @brief 추가 해제할 GPU 리소스는 없습니다. */
        virtual ~GlobalVariablesPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Global Variables"; }
        /** @brief 전역 변수 UI를 그립니다. */
        void drawContent() override;
        /** @brief 검색·그룹화·리셋·프리셋 툴바를 그립니다. */
        void drawVariableToolbar( GlobalVariableManager& gvm );
        /** @brief 핀 고정된 즐겨찾기 변수 섹션을 그립니다. */
        void drawPinnedSection( GlobalVariableManager& gvm );
        /** @brief 모듈별 묶음 또는 평면 모드로 변수 테이블을 그립니다. */
        void drawVariableTable( const vector<GlobalVariableInfo*>& listFiltered );
        /** @brief 기본 창 크기를 반환합니다. */
        float2 getInitialPanelSize() const override { return float2{ 680.0f, 480.0f }; }
        /** @brief 필요할 때 여는 도구라 닫힌 채 시작합니다. */
        bool isToolPanel() const override { return true; }
        bool saveDocument() override;
        void revertDocument() override;

    private:
        /** @brief 단일 전역 변수의 편집 컨트롤을 그립니다. */
        void drawVariableRow( GlobalVariableInfo& info, bool bShowPin );
        void markSessionDirty();

    private:
        unordered_set<string> _uniquePinnedVar;
        EditorFileCollectJob  _presetJob;
        vector<string>        _listPresetFile;
        /** @brief 이번 프레임에 보일 변수입니다. 프레임마다 지우고 다시 채우는 재사용 버퍼입니다. */
        vector<GlobalVariableInfo*>           _listFilteredVariable;
        fixed_string<constant::kMaxBuffer128> _searchFilter;
        fixed_string<constant::kMaxBuffer64>  _presetNameBuf;
        uint8                                 _bGroupByModule   : 1;
        uint8                                 _bPresetListDirty : 1;
        [[maybe_unused]] uint8                _reserved         : 6;
    };
} // namespace sw::editor

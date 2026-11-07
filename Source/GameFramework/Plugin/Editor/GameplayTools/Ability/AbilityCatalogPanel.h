/**
 * @file AbilityCatalogPanel.h
 * @brief 어빌리티 카탈로그 편집 창 — 이펙트 · 어빌리티 · 세트 · 큐 연출을 종류별 목록으로 보고, 고른 항목이 쓰는 것(비용 · 쿨다운 · 세트 구성 · 큐 · 부모)과 쓰이는 곳을 오가며 참조를 고른다.
 * @details 상태와 명령은 `AbilityCatalogEditModel`(ImGui 없음)이 든다. 파일은 런타임이 읽는 `abilities.xml` 그대로이고(새 형식 없음), 원문 편집은 카탈로그 편집기의 문서와 같은 한 벌이라
 *          주석 · 들여쓰기 · `_base` 상속이 남는다. 저장한 뒤 런타임 로더(`AbilityCatalog::loadFromXMLText`)로 읽어 통과 여부를 보인다. 수치 칸(지속 · 수정자)은 범용 카탈로그 편집기의 표가 고친다.
 *          런타임 상태를 읽는 Ability Debugger 와 다른 창이다(저작 · 관측 분리). 언리얼 GAS 에셋 편집기 + Reference Viewer 를 한 화면에 둔 모양이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/Panel/IEditorDocumentPanel.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityCatalogEditModel.h"

namespace sw::editor
{
    /**
     * @class AbilityCatalogPanel
     * @brief 파일 머리말 참고.
     */
    class AbilityCatalogPanel final : public IEditorDocumentPanel
    {
    public:
        /** @brief 패널 id 입니다. */
        static constexpr const utf8* kPanelID = "ability_catalog";

        /** @brief 마지막 런타임 검증 결과입니다(감시 값 `Editor.AbilityCatalogValidation`). */
        enum class ValidationState : uint8
        {
            NotRun = 0,
            Passed,
            Failed,
        };

        AbilityCatalogPanel();

        const utf8*        getPanelTitle() const override { return "Ability Catalog"; }
        EditorTextExtent   getInitialPanelSize() const override { return EditorTextExtent::ofText( 110.0f, 36.0f ); }
        void               drawContent() override;
        [[nodiscard]] bool saveDocument() override;
        void               revertDocument() override;

        const AbilityCatalogEditModel& getModel() const { return _model; }
        ValidationState                getValidationState() const { return _validation; }

    private:
        /** @brief 경로 칸 · 열기 · 저장 · 되돌리기 · 실행 취소 줄과 검증 결과입니다. */
        void drawFileRow();
        /** @brief 종류별 항목 목록(거르개 · 문제가 있는 항목은 빨간 표시 하나)과 더하기 단추입니다. */
        void drawEntryList();
        /** @brief 고른 항목의 쓰는 것 · 쓰이는 곳 · 문제 · 복제 · 지우기입니다. */
        void drawEntryDetail( int32 entry );
        /** @brief 참조 하나의 줄입니다 — 이름, 대상 고르기(그 종류의 id 목록), 대상으로 가기. */
        void drawReferenceRow( int32 reference );
        /** @brief 어빌리티의 빈 비용 · 쿨다운 칸과 세트의 구성 더하기입니다. */
        void drawAddReferenceRows( int32 entry );
        /** @brief id 목록 콤보입니다. 골랐으면 true 이고 @p outID 에 고른 id 입니다. */
        bool drawIDCombo( const utf8* pID, AbilityEntryKind kind, string_view current, string& outID );
        void openFile();
        void validateText();
        /** @brief 편집 결과를 반영합니다 — 성공이면 dirty, 실패면 상태 줄에 이유. */
        void reportEdit( bool bEdited );
        void syncDirty();

    private:
        AbilityCatalogEditModel                 _model;
        DataInheritanceResourceSource           _inheritanceSource; ///< 항목 `_base` 의 부모를 읽는 곳(같은 파일 · 상속 규칙 표)
        fixed_string<constant::buffer::kMax256> _path;              ///< 열 파일(리소스 id, `game/<게임>/data/abilities.xml`)
        string                                  _openPath;          ///< 연 파일
        fixed_string<constant::buffer::kMax64>  _filter;
        string                                  _status;
        vector<int32>                           _listScratchReference;
        vector<string>                          _listScratchID;
        int32                                   _selectedEntry;
        ValidationState                         _validation;
    };
} // namespace sw::editor

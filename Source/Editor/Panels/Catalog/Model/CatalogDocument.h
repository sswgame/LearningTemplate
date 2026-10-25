/**
 * @file CatalogDocument.h
 * @brief 카탈로그 XML 한 파일을 테이블로 편집하는 문서 상태입니다 — 원문, 저장된 원문, 되돌리기 스택, 훑은 결과, 원소 이름별 표(행 · 열). ImGui 를 모릅니다.
 * @details 원문이 유일한 진실입니다. 셀을 고치면 `CatalogXMLText` 가 원문의 그 구간만 바꾸고 새 원문을 다시 훑습니다(주석 · 들여쓰기가 그대로 남는다).
 *          되돌리기는 원문 스냅숏 스택입니다 — 카탈로그 파일은 작아서 스냅숏이 싸고, 셀 · 행 편집과 글 상자 편집이 한 스택에 섭니다.
 *          언리얼 `UDataTable` 편집기가 행을 테이블로 고치고 CSV · JSON 으로 가져오고 내보내는 것에 해당하지만, 이 엔진의 카탈로그는 손으로 읽는 XML 이라 행은 리플렉션 구조체가 아니라 XML 원소입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

namespace sw::editor
{
    /**
     * @class CatalogDocument
     * @brief 테이블 편집기가 고치는 카탈로그 파일 하나입니다.
     */
    class CatalogDocument
    {
    public:
        /** @brief 같은 이름의 원소(루트의 자식)를 한 테이블로 묶은 것입니다. */
        struct Group
        {
            string         _name{};
            vector<int32>  _listRow{};    ///< 원소 인덱스(문서 순서)
            vector<string> _listColumn{}; ///< 속성 이름의 합집합(처음 나온 순서)
        };

        /** @brief 되돌리기 스택의 상한입니다. 넘으면 가장 오래된 것부터 버립니다. */
        static constexpr size_t kMaxUndoCount = 64;

        CatalogDocument() = default;

        /** @brief 원문을 읽어 문서를 처음 상태로 둡니다(저장된 원문 = 이 원문, 스택 비움). 형식이 깨졌으면 false 이고 `getError` 에 이유가 있습니다 — 원문은 글 상자로 고칠 수 있게 남습니다. */
        [[nodiscard]] bool load( string_view text );
        /** @brief 저장에 성공한 뒤 부릅니다. 지금 원문을 저장된 원문으로 삼습니다. */
        void markSaved();
        /** @brief 저장된 원문으로 되돌립니다(스택은 비웁니다). */
        void revert();

        const string&         getText() const { return _text; }
        const string&         getSavedText() const { return _savedText; }
        bool                  isDirty() const { return _text != _savedText; }
        bool                  isParsed() const { return _parsed.isParsed(); }
        const string&         getError() const { return _error; }
        const CatalogXMLText& getParsed() const { return _parsed; }
        const vector<Group>&  getGroups() const { return _listGroup; }
        /** @brief 문서 루트 원소의 이름입니다. 훑지 못했으면 빈 문자열입니다. */
        string getRootName() const;
        /** @brief 원소 하나의 속성 값입니다. 속성이 없으면 nullptr 입니다(포인터는 다음 편집까지 유효합니다). */
        const string* findCellValue( int32 elementIndex, string_view column ) const;

        // ------------------------------------------------------------------------------
        // 편집 — 성공하면 되돌리기 스택에 이전 원문을 한 단계 쌓습니다. 실패하면 아무것도 바뀌지 않고 `getError` 에 이유가 있습니다.
        // ------------------------------------------------------------------------------
        [[nodiscard]] bool setCell( int32 elementIndex, string_view column, string_view value );
        [[nodiscard]] bool unsetCell( int32 elementIndex, string_view column );
        /** @brief @p groupName 테이블의 끝에 행을 더합니다. 열은 그 테이블의 열을 따르고 `id` · `name` 은 "new" 로 채웁니다. 테이블이 없으면 속성 없는 행(`id="new"` 하나)을 더합니다. */
        [[nodiscard]] bool addRow( string_view groupName );
        [[nodiscard]] bool removeRow( int32 elementIndex );
        /** @brief 어느 깊이의 원소든 @p parentIndex 의 마지막 자식으로 `<name a="…" />` 를 더합니다(곡선 키처럼 중첩된 원소를 고치는 길). 부모가 없으면 false 입니다. */
        [[nodiscard]] bool addChild( int32 parentIndex, string_view name, const vector<CatalogXMLNewAttribute>& listAttribute );
        /** @brief 글 상자 편집입니다. 훑지 못하는 글도 받습니다(`isParsed` 가 false 가 되고 테이블은 비어 보입니다). 되돌리기 항목은 호출마다 쌓지 않고 @p bPushUndo 가 true 일 때만 쌓습니다. */
        void replaceText( string_view text, bool bPushUndo );

        bool canUndo() const { return _listUndo.empty() == false; }
        bool canRedo() const { return _listRedo.empty() == false; }
        void undo();
        void redo();

    private:
        void               rebuildFromText();
        void               pushUndo();
        [[nodiscard]] bool applyEdit( const string& editedText );

        string         _text{};
        string         _savedText{};
        string         _error{};
        CatalogXMLText _parsed{};
        vector<Group>  _listGroup{};
        vector<string> _listUndo{};
        vector<string> _listRedo{};
    };
} // namespace sw::editor

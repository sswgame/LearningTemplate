/**
 * @file ReflectedXMLDocument.h
 * @brief 리플렉션 형식 하나로 읽는 XML 에셋(효과 · 물리 에셋)의 편집 사본입니다 — 프로퍼티 그리드가 인스턴스를 고치고, 고친 프로퍼티만 원문에 다시 씁니다. ImGui 를 모릅니다.
 * @details 직렬화기로 통째로 쓰면 주석 · 칸 배치가 사라지고 적지 않은 칸까지 적힌다. 그래서 원문(`CatalogXMLText`)이 문서이고, 고친 칸의 글만 직렬화기에서 얻어 그 자리에 넣는다 —
 *          저장한 글은 엔진의 같은 직렬화기가 읽는다(카탈로그 편집기와 같은 원칙). 기본값과 같은 칸은 적혀 있지 않으면 적지 않는다.
 *          `_base` 로 물려받은 파일은 부모를 펼친 값을 고치고 부모와 다른 칸만 이 파일에 쓴다(열쇠 목록 원소마다 `ResourceComposer::makeCheckedDelta` — 부모 값으로 되돌린 칸은 빠진다).
 *          목록 원소는 열쇠(`PROPERTY( InheritKey )`)로 짚는다 — 열쇠 없는 목록은 통째로 바뀌는 칸이라 물려받은 문서에서 원소를 고치지 않는다.
 *          되돌리기는 문서 패널의 원문 단계다(`getText` · `applyText`, 편집 알림은 `DocumentEditRecorder`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Workspace/Undo/EditorDocumentState.h"
#include "Editor/Panels/Catalog/Model/CatalogXMLText.h"

#include "Engine/Reflection/ReflectedInstance.h"

namespace sw
{
    struct PropertyInfo;
    struct TypeInfo;
} // namespace sw

namespace sw::editor
{
    class DocumentEditRecorder;

    /** @class ReflectedXMLDocument @brief 파일 머리말 참고. */
    class ReflectedXMLDocument
    {
    public:
        ReflectedXMLDocument();

        /** @brief 편집 알림 창구를 겁니다 — 문서 패널이 만들 때 한 번 건다. */
        void bindDocumentEditRecorder( const DocumentEditRecorder& recorder ) { _pEditRecorder = &recorder; }

        /** @brief 원문 @p text 를 형식 @p type 의 문서로 엽니다(저장한 원문이 된다). 실패하면 이유이고 문서는 비어 있습니다. @p sourceID 는 펼치기 오류에 적는 이름입니다(대개 리소스 id). */
        [[nodiscard]] expected<void> load( const TypeInfo& type, string_view text, string_view sourceID );
        /** @brief 비웁니다. */
        void clear();

        /** @brief 고칠 수 있으면(인스턴스가 있으면) true 입니다. */
        [[nodiscard]] bool isEditable() const { return _instance.isValid(); }
        /** @brief 물려받은 부모의 리소스 id(`_base`)입니다. 부모가 없으면 빈 글입니다. */
        const string& getBaseID() const { return _baseID; }
        /** @brief 목록 @p listName 의 @p index 번째 원소가 부모에도 있으면 true 입니다 — 열쇠를 바꾸면 다른 원소가 되므로 그리드가 열쇠 칸을 감춘다. */
        [[nodiscard]] bool isInheritedItem( string_view listName, uint32 index ) const;
        [[nodiscard]] bool isDirty() const { return _state.isDirty( _text ); }
        const string&      getText() const { return _text; }
        /** @brief 저장에 성공했습니다. */
        void markSaved() { _state.markSaved( _text ); }
        /** @brief 편집할 인스턴스입니다(형식은 `load` 의 것). 고칠 수 없으면 nullptr 입니다. 원문을 다시 읽으면(`applyText` · 원소 더하기 · 빼기) 주소가 바뀐다. */
        void* getInstance() { return _instance.get(); }

        /** @brief 되돌리기 단계의 원문을 적용합니다(인스턴스를 다시 읽는다). 읽지 못하면 원문만 들고 고칠 수 없는 상태가 된다. */
        void applyText( string_view text );
        /**
         * @brief 목록 @p listName 의 @p index 번째 원소 @p pItem(형식 @p itemType)의 프로퍼티 @p property 를 원문에 씁니다. 그리드가 인스턴스를 고친 직후 부른다.
         * @return 원문이 바뀌었으면 true 이고 알립니다. 자리를 찾지 못했거나 같은 글이면 false 입니다.
         */
        bool commitItemProperty( string_view listName, uint32 index, const void* pItem, const TypeInfo& itemType, const PropertyInfo& property );
        /** @brief 목록 @p listName 의 @p index 번째 원소를 복사해 바로 뒤에 놓고 열쇠 속성 @p keyAttribute 를 겹치지 않는 이름(`<이름>_copy`)으로 바꿉니다. 바꿨으면 true 입니다. */
        bool duplicateItem( string_view listName, uint32 index, string_view keyAttribute );
        /** @brief 목록 @p listName 의 @p index 번째 원소를 뺍니다. 바꿨으면 true 입니다. */
        bool removeItem( string_view listName, uint32 index );

    private:
        [[nodiscard]] bool isInherited() const { return _baseID.empty() == false; }
        /** @brief 목록 @p listName 의 @p index 번째 원소의 원문 자리입니다. 없으면 nullptr 입니다(물려받은 문서에서는 이 파일에 적지 않은 원소도 nullptr 다). */
        const CatalogXMLElement* findItem( string_view listName, uint32 index ) const;
        /** @brief 목록 @p listName 의 열쇠 속성입니다(`PROPERTY( InheritKey )`). 없으면 빈 글입니다. */
        string findKeyAttribute( string_view listName ) const;
        /** @brief 펼친 목록 @p listName 의 @p index 번째 원소의 열쇠입니다(물려받은 문서). 없으면 빈 글입니다. */
        string findItemKey( string_view listName, uint32 index ) const;
        /** @brief 부모 문서의 열쇠 @p key 원소입니다. 없으면 nullptr 입니다. */
        const CatalogXMLElement* findParentItem( string_view listName, string_view key ) const;
        /** @brief 부모에 있는 원소의 프로퍼티를 부모와의 차이로 씁니다(`commitItemProperty` 의 물려받은 쪽). */
        bool commitInheritedProperty( string_view listName, string_view key, const CatalogXMLElement& parentItem, string_view itemText, const TypeInfo& itemType, const PropertyInfo& property );
        /** @brief 새 원문이 훑어지면 원문을 바꾸고 true 입니다. @p bReload 이면 인스턴스도 다시 읽는다 — 읽지 못하는 글은 받지 않는다. */
        [[nodiscard]] bool acceptText( string text, bool bReload );
        /** @brief 원문을 새 인스턴스로 읽습니다(물려받은 문서는 펼쳐서). 읽지 못하면 무효 인스턴스입니다. 펼친 글을 @p outFlat 에 둡니다(부모가 없으면 빈 글). */
        [[nodiscard]] ReflectedInstance readInstance( string_view text, string& outFlat ) const;
        /** @brief 글 @p text 의 부모 사슬을 펼쳐 인스턴스로 읽습니다. 조건(`_when`)은 풀지 않는다 — 원문에 남는 것이 편집 대상이다. */
        [[nodiscard]] ReflectedInstance readFlattened( string_view text, string_view sourceID, string& outFlat ) const;
        void                            notifyEdited( string_view label ) const;

    private:
        const TypeInfo*             _pType;
        const DocumentEditRecorder* _pEditRecorder;
        ReflectedInstance           _instance;
        string                      _text;
        EditorDocumentState<string> _state; ///< 저장한 글(되돌리기는 문서 패널이 글로 든다)
        CatalogXMLText              _parsed;
        string                      _sourceID;
        string                      _baseID;       ///< 물려받은 부모(비면 없음) — 아래 넷은 부모가 있을 때만 찬다
        string                      _parentText;   ///< 부모 문서를 직렬화기로 쓴 글(차이의 기준)
        CatalogXMLText              _parentParsed; ///< `_parentText` 를 훑은 것
        CatalogXMLText              _flatParsed;   ///< 펼친 글을 훑은 것(원소 번호 → 열쇠)
        string                      _flatText;
    };
} // namespace sw::editor

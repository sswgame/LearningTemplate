/**
 * @file ReflectedXMLDocument.h
 * @brief 리플렉션 형식 하나로 읽는 XML 에셋(효과 · 물리 에셋)의 편집 사본입니다 — 프로퍼티 그리드가 인스턴스를 고치고, 고친 프로퍼티만 원문에 다시 씁니다. ImGui 를 모릅니다.
 * @details 직렬화기로 통째로 쓰면 주석 · 칸 배치가 사라지고 적지 않은 칸까지 적힌다. 그래서 원문(`CatalogXMLText`)이 문서이고, 고친 칸의 글만 직렬화기에서 얻어 그 자리에 넣는다 —
 *          저장한 글은 엔진의 같은 직렬화기가 읽는다(카탈로그 편집기와 같은 원칙). 기본값과 같은 칸은 적혀 있지 않으면 적지 않는다.
 *          `_base` 로 물려받은 파일은 이 파일에 적은 것만으로 값을 다 알 수 없어 고치지 않는다(`isInherited` — 카탈로그 편집기가 고친다).
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

        /** @brief 원문 @p text 를 형식 @p type 의 문서로 엽니다(저장한 원문이 된다). 실패하면 이유이고 문서는 비어 있습니다. 물려받은 문서는 원문만 들고 인스턴스가 없습니다. */
        [[nodiscard]] expected<void> load( const TypeInfo& type, string_view text );
        /** @brief 비웁니다. */
        void clear();

        /** @brief 고칠 수 있으면(인스턴스가 있으면) true 입니다. */
        [[nodiscard]] bool isEditable() const { return _instance.isValid(); }
        /** @brief `_base` 로 물려받은 문서라 고치지 않으면 true 입니다. */
        [[nodiscard]] bool isInherited() const { return _bInherited == SW_TRUE; }
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
        /** @brief 목록 @p listName 의 @p index 번째 원소의 원문 자리입니다. 없으면 nullptr 입니다. */
        const CatalogXMLElement* findItem( string_view listName, uint32 index ) const;
        /** @brief 새 원문이 훑어지면 원문을 바꾸고 true 입니다. @p bReload 이면 인스턴스도 다시 읽는다 — 읽지 못하는 글은 받지 않는다. */
        [[nodiscard]] bool acceptText( string text, bool bReload );
        /** @brief 지금 원문을 새 인스턴스로 읽습니다. 읽지 못하면 무효 인스턴스입니다. */
        [[nodiscard]] ReflectedInstance readInstance( string_view text ) const;
        void                            notifyEdited( string_view label ) const;

    private:
        const TypeInfo*             _pType;
        const DocumentEditRecorder* _pEditRecorder;
        ReflectedInstance           _instance;
        string                      _text;
        EditorDocumentState<string> _state; ///< 저장한 글(되돌리기는 문서 패널이 글로 든다)
        CatalogXMLText              _parsed;
        uint8                       _bInherited : 1;
        [[maybe_unused]] uint8      _reserved   : 7;
    };
} // namespace sw::editor

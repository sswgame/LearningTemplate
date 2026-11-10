/**
 * @file AbilityCatalogEditModel.h
 * @brief 어빌리티 카탈로그(`abilities.xml`) 편집 모델 — 항목(이펙트 · 어빌리티 · 어빌리티 세트 · 큐) 목록, 항목 사이의 참조(쓰는 쪽 · 쓰이는 쪽), 풀리지 않은 참조 · 겹친 id, 참조 바꾸기 · 항목 더하기 · 복제. ImGui 를 모릅니다.
 * @details 원문은 카탈로그 편집기의 문서(`CatalogDocument`)가 든다 — 주석 · 들여쓰기 · `_base` 상속을 지키는 원문 편집이 한 벌이다. 형식은 런타임 로더(`AbilityCatalog`)가 읽는 그대로이고
 *          새 형식 · 사본을 두지 않는다. 참조의 뜻은 로더의 것이다: 어빌리티 `cost` · `cooldown` 은 이펙트, 세트의 `<Ability id>` 는 어빌리티 · `<Effect id>` 는 이펙트, `_base` 는 같은 종류.
 *          `<Param text>` 는 클래스가 이름으로 읽는 값이라(예: `damageEffect`) 이펙트 id 와 같을 때만 연결로 보이고 문제로 세지 않는다. 큐 태그(`<Cue tag>`)는 가장 가까운 조상 태그의
 *          `<AbilityPresentation>` 으로 이어지며 없어도 이벤트만 나가므로 문제가 아니다. 언리얼 GAS 의 GameplayEffect · Ability 에셋 편집기와 Reference Viewer 를 한 화면에 둔 것과 같다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Panels/Catalog/Model/CatalogDocument.h"

namespace sw::editor
{
    /** @brief 카탈로그 항목의 종류입니다(루트 아래 원소 이름). */
    enum class AbilityEntryKind : uint8
    {
        Effect = 0,   ///< `<AbilityEffect id>`
        Ability,      ///< `<Ability id class>`
        AbilitySet,   ///< `<AbilitySet id>`
        Presentation, ///< `<AbilityPresentation tag>`
        Count
    };

    /** @brief 항목 하나입니다. */
    struct AbilityCatalogEntry
    {
        string           _id{};    ///< id(큐는 태그)
        int32            _element; ///< 원문의 원소 번호(`CatalogXMLText`). 읽을 때 채운다
        AbilityEntryKind _kind{ AbilityEntryKind::Effect };
    };

    /** @brief 항목 사이의 참조 하나입니다. 원문의 어느 원소 · 속성에 적혔는지와 가리키는 곳입니다. */
    struct AbilityCatalogReference
    {
        string           _attribute;      ///< 적힌 속성(`cost` · `cooldown` · `id` · `text` · `_base` · `tag`)
        string           _label;          ///< 보일 이름(`cost`, `set ability`, `param damageEffect` …)
        string           _targetID;       ///< 가리키는 id(물려받은 값 포함)
        int32            _from;           ///< 쓰는 항목 번호
        int32            _element;        ///< 값이 적힌(또는 적을) 원소 번호
        optional<int32>  _target;         ///< 가리키는 항목 번호. 풀리지 않으면 비어 있다
        AbilityEntryKind _targetKind;     ///< 가리켜야 할 종류
        uint8            _bRequired  : 1; ///< 풀리지 않으면 로더가 경고 · 실패하는 참조
        uint8            _bInherited : 1; ///< 이 항목이 적지 않고 `_base` 에서 물려받은 값

        AbilityCatalogReference();
    };

    /** @brief 문제 하나입니다(영어 한 문장, 그 항목 옆에 보인다). */
    struct AbilityCatalogIssue
    {
        string _message{};
        int32  _entry; ///< 문제가 난 항목 번호(만들 때 채운다)
    };

    /**
     * @class AbilityCatalogEditModel
     * @brief 파일 머리말 참고.
     */
    class AbilityCatalogEditModel
    {
    public:
        AbilityCatalogEditModel();

        /** @brief 원문을 읽습니다. 형식이 깨졌으면 false 이고 `getDocument().getError()` 에 이유가 있습니다(원문은 남는다). */
        [[nodiscard]] bool     load( string_view text, IDataInheritanceSource* pInheritanceSource );
        CatalogDocument&       editDocument() { return _document; }
        const CatalogDocument& getDocument() const { return _document; }
        /** @brief 원문이 바뀐 뒤(편집 · 되돌리기) 항목과 참조를 다시 만듭니다. 편집 명령은 스스로 부릅니다. */
        void rebuild();

        const vector<AbilityCatalogEntry>&     getEntries() const { return _listEntry; }
        const vector<AbilityCatalogReference>& getReferences() const { return _listReference; }
        const vector<AbilityCatalogIssue>&     getIssues() const { return _listIssue; }
        /** @brief 종류의 원소 이름입니다(`AbilityEffect` …). */
        static const utf8* getElementName( AbilityEntryKind kind );
        /** @brief 종류와 id 의 항목 번호입니다. 없으면 비어 있습니다. */
        optional<int32> findEntry( AbilityEntryKind kind, string_view id ) const;
        /** @brief @p entry 가 쓰는 참조 번호들입니다(먼저 비운다). */
        void collectReferencesFrom( int32 entry, vector<int32>& outListReference ) const;
        /** @brief @p entry 를 가리키는 참조 번호들입니다(쓰이는 곳, 먼저 비운다). */
        void collectReferencesTo( int32 entry, vector<int32>& outListReference ) const;
        /** @brief 종류 @p kind 의 id 들입니다(참조 고르기 목록). */
        void collectIDs( AbilityEntryKind kind, vector<string>& outListID ) const;

        /**
         * @brief 참조 @p reference 를 @p targetID 로 바꿉니다. 빈 글이면 그 칸을 비웁니다(어빌리티의 `cost=""` 처럼 부모의 값을 지우는 것도 적힌 빈 값이다).
         * @return 바뀌었으면 true 입니다. 실패하면 `getDocument().getError()` 에 이유가 있습니다.
         */
        [[nodiscard]] bool setReference( int32 reference, string_view targetID );
        /** @brief 어빌리티에 비용 · 쿨다운 칸이 없을 때 적습니다(@p attribute 는 `cost` · `cooldown`). */
        [[nodiscard]] bool setAbilityEffectSlot( int32 entry, string_view attribute, string_view effectID );
        /** @brief 세트 @p entry 에 `<Ability id>` 또는 `<Effect id>` 를 더합니다. */
        [[nodiscard]] bool addSetMember( int32 entry, AbilityEntryKind memberKind, string_view id );
        /** @brief 종류의 새 항목(겹치지 않는 id)을 끝에 더하고 그 항목 번호를 돌려줍니다. 실패하면 이유를 돌려줍니다(문서 오류는 `getDocument().getError()`). */
        [[nodiscard]] expected<int32> addEntry( AbilityEntryKind kind );
        /** @brief 항목을 복제합니다(`<id>_copy`). 새 항목 번호입니다. */
        int32 duplicateEntry( int32 entry );
        /** @brief 항목을 지웁니다. 다른 항목이 가리키면(필수 참조) 지우지 않고 false 입니다 — 지우면 그 항목이 로드에서 풀리지 않는다. */
        [[nodiscard]] bool removeEntry( int32 entry );

    private:
        void collectEntries();
        void collectReferences();
        void collectIssues();
        /** @brief 항목의 속성 값입니다. 적지 않았으면 물려받은 값입니다. 둘 다 없으면 false 입니다. */
        [[nodiscard]] bool findValue( int32 element, string_view attribute, string& outValue, bool& outInherited ) const;
        void               addReference( int32 from, int32 element, string_view attribute, string_view label, AbilityEntryKind targetKind, bool bRequired );

    private:
        CatalogDocument                 _document;
        vector<AbilityCatalogEntry>     _listEntry;
        vector<AbilityCatalogReference> _listReference;
        vector<AbilityCatalogIssue>     _listIssue;
    };
} // namespace sw::editor

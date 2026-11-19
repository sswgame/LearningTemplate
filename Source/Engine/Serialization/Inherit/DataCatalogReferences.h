/**
 * @file DataCatalogReferences.h
 * @brief 카탈로그 안의 id 참조 규칙("이 원소의 이 속성은 저 원소의 id 다")을 데이터 표 한 벌로 적고, 런타임 · 쿠커 · 에디터가 같은 표로 봅니다.
 *
 * @details 예전에는 규칙이 카탈로그 .cpp 의 멤버 포인터 표(`XMLCatalogReference`)라 게임 모듈을 올리기 전에 도는 팩 쿠킹이 볼 수 없었고, 깨진 참조는 로그만 남긴 채
 *          로드가 성공했다. 이제 규칙은 리소스 표(`kTableAssetID`)이고 XML 원문 위에서 본다 — 쿠커(`PackCooker`)는 팩에 담기 전에, 카탈로그 읽기(`XMLCatalogLoader`)는
 *          펼친(`_base`) 문서 위에서 같은 함수(`validateDocument`)를 부른다. 필수 참조가 깨지면 로드 · 쿠킹 실패다. 선택 참조(`optional="true"`)는 경고만 하고 넘어간다 —
 *          다른 팩 · 코드 등록이 채우는 id 처럼 이 파일 안에서 다 볼 수 없는 연결에만 쓴다. 빈 값은 적지 않은 것이라 보지 않는다(필수 칸은 리플렉션 `Required` 가 본다).
 *          언리얼 데이터 검증(`UEditorValidatorSubsystem` · 쿠커의 에셋 참조 검사)이 쿠킹 전에 같은 규칙을 돌리는 자리다(기억). 리소스인 까닭은 `DataCatalogInheritance` 와 같다 —
 *          쿠킹이 모듈 코드를 볼 수 없고 배포 런타임도 읽는다.
 * @code
 *     <DataCatalogReferenceTable>
 *       <Catalog root="GhostHunt">
 *         <Reference owner="Room" attribute="ghosts" target="Ghost"/>             <!-- 목록(쉼표 · 공백 · 세미콜론) -->
 *         <Reference owner="Class/Learn" attribute="spell" target="Spell"/>       <!-- 항목의 자식 원소 -->
 *         <Reference owner="Site" attribute="garrison" target="Unit" named="true"/> <!-- `id:수` 꼴 -->
 *         <Reference owner="Quest/Stage" attribute="next" target="Stage" inEntry="true"/> <!-- 같은 퀘스트 안의 단계 -->
 *         <Unique owner="Ghost" attribute="id"/>                                    <!-- 한 파일 안에서 겹치면 오류 -->
 *       </Catalog>
 *       <Catalog root="FighterCatalog">
 *         <Reference owner="Fighter/Move" attribute="frames" defaultAttribute="id" target="Move" file="moves"/> <!-- 루트의 moves 가 가리키는 다른 파일 -->
 *       </Catalog>
 *       <CatalogKey tag="AbilitySetDef" catalog="AbilityCatalog" entry="AbilitySet"/> <!-- 항목 종류 → 카탈로그 루트 · 원소 -->
 *       <Field owner="AbilityComponent" field="_abilitySetID" tag="AbilitySetDef"/>  <!-- `CatalogKey<AbilitySetDef>` 칸 -->
 *     </DataCatalogReferenceTable>
 * @endcode
 *          `<CatalogKey>` · `<Field>` 는 카탈로그 밖(씬 · 프리팹의 컴포넌트 · 구조체 칸 `CatalogKey<Tag>`)에서 항목을 가리키는 칸이다. 쿠커는 쿠킹하는 팩들의
 *          카탈로그 항목을 모으고(`DataCatalogKeyCheck`) 칸 값이 그 안에 있는지 대조해 없으면 쿠킹 실패다. 줄이 코드와 다르면
 *          `DataCatalogReferencesTest.FieldTableMatchesReflection` 이 진다(쿠커는 게임 모듈의 리플렉션을 보지 못한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct DataCatalogReferenceRule
     * @brief 참조 규칙 한 줄입니다(표의 `<Reference>`).
     */
    struct DataCatalogReferenceRule
    {
        string         _rootName;           ///< 카탈로그 루트 원소 이름
        vector<string> _listOwnerPath;      ///< 루트의 자식(항목)에서 값을 적은 원소까지의 원소 이름들(`Class/Learn`)
        string         _attribute;          ///< id 를 적은 속성
        string         _target;             ///< 가리키는 원소 이름(루트의 자식). 겹침 규칙(`<Unique>`)은 비어 있다
        string         _targetKey;          ///< 가리키는 원소의 id 속성(기본 `id`)
        string         _defaultAttribute;   ///< `_attribute` 를 적지 않았으면 이 속성의 값을 id 로 본다(기술의 `frames` 가 없으면 자기 `id`). 비면 없음
        string         _fileAttribute;      ///< 가리키는 원소를 이 파일이 아니라 루트의 이 속성이 적은 리소스(루트의 자식)에서 찾는다. 루트가 적지 않았으면 보지 않는다(필수는 읽는 코드가 본다)
        bool           _bNamed{ false };    ///< 값이 `id:수` 꼴이다(콜론 앞이 id) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bOptional{ false }; ///< 깨져도 경고만 한다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bInEntry{ false };  ///< 가리키는 원소를 루트가 아니라 같은 항목(owner 경로의 첫 원소)의 자식에서 찾는다(퀘스트 안의 단계 · 트리 안의 스킬) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bUnique{ false };   ///< 겹침 규칙(`<Unique owner attribute>`) — owner 원소의 attribute 값이 한 파일 안에서 겹치면 오류다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };

    /**
     * @struct DataCatalogKeyTarget
     * @brief 항목 종류 한 줄입니다(표의 `<CatalogKey tag catalog entry>`) — `CatalogKey<Tag>` 의 `Tag` 이름이 가리키는 카탈로그 루트와 항목 원소.
     */
    struct DataCatalogKeyTarget
    {
        string _tag;       ///< 항목 종류 이름(이름공간 없는 C++ 형식 이름 — 프로퍼티 메타 `_catalogKeyTag`)
        string _catalog;   ///< 카탈로그 루트 원소 이름
        string _entry;     ///< 항목 원소 경로 — 루트의 자식부터 '/' 로 잇는다(`Honor/Action` · 서식지 키 `Habitat/Key`)
        string _targetKey; ///< 항목의 id 속성(기본 `id`)
    };

    /**
     * @struct DataCatalogKeyField
     * @brief 카탈로그 키 칸 한 줄입니다(표의 `<Field owner field tag>`) — 씬 · 프리팹에서 원소 `owner` 의 속성 · 목록 원소 `field` 가 항목 종류 `tag` 의 id 다.
     */
    struct DataCatalogKeyField
    {
        string _owner;     ///< 칸을 가진 리플렉션 형식 이름(씬 · 프리팹의 원소 이름 — 컴포넌트 · 구조체, 또는 `<Element>` 줄이 카탈로그 원소로 이은 정의 형식)
        string _field;     ///< 칸 이름(속성, 또는 `<item>` 을 품은 자식 원소)
        string _tag;       ///< 항목 종류 이름(`<CatalogKey tag>`)
        string _attribute; ///< 카탈로그 원소에서 이 칸을 적은 속성(`PROPERTY( Name )` 철자). 비면 칸 이름에서 `_` 를 뗀 것(`XMLFields::getAttributeName` 과 같다)
    };

    /**
     * @struct DataCatalogKeyStruct
     * @brief 구조체 칸 한 줄입니다(표의 `<Struct owner field type>`) — 씬 · 프리팹에서 구조체 칸 하나는 형식 이름이 아니라 칸 이름의 원소로 적혀(`<_eliteEnemyKind>`)
     *        쿠커가 원소 이름으로 주인을 찾을 수 없다. 이 줄이 "`owner` 원소 아래 `field` 원소는 `type` 이다" 를 알린다.
     */
    struct DataCatalogKeyStruct
    {
        string _owner; ///< 칸을 가진 리플렉션 형식 이름
        string _field; ///< 구조체 칸 이름(자식 원소 이름)
        string _type;  ///< 칸의 구조체 형식 이름(그 형식의 `<Field>` · `<Struct>` 줄이 다시 걸린다)
    };

    /**
     * @struct DataCatalogKeyElement
     * @brief 정의 형식 ↔ 카탈로그 원소 연결 한 줄입니다(표의 `<Element type catalog path>`) — 카탈로그 정의(`MechPartDef`)는 카탈로그 글에 형식 이름이 아니라 원소 이름(`<Part>`)으로
     *        적혀, 정의의 카탈로그 키 칸(무기 → 탄)을 쿠커가 찾으려면 이 연결이 필요하다. 정의의 칸은 속성으로 적히고 목록은 쉼표 목록이다(`XMLFields`).
     */
    struct DataCatalogKeyElement
    {
        string _type;    ///< 정의 형식 이름(`<Field owner>`)
        string _catalog; ///< 카탈로그 루트 원소 이름
        string _path;    ///< 원소 경로 — 루트의 자식부터 '/' 로 잇는다
    };

    /**
     * @class DataCatalogReferenceRules
     * @brief 참조 규칙 표 한 벌입니다. 읽은 뒤에는 바꾸지 않는다(`validateDocument` 는 const — 쿠킹 작업이 나란히 불러도 된다).
     */
    class SW_API DataCatalogReferenceRules
    {
    public:
        static constexpr const utf8* kTableAssetID = "engine/settings/datacatalogreferences.xml"; ///< 규칙 표(리소스 id). 코드 뿌리로 엔진 팩에 담긴다

        DataCatalogReferenceRules();

        /** @brief `kTableAssetID` 를 읽습니다. 쿠킹 작업 안이면 표도 그 작업의 입력이 된다. 읽지 못하거나 깨졌으면 오류를 남기고 false 입니다(규칙 없이 통과시키지 않는다). */
        [[nodiscard]] bool loadFromAsset();
        /** @brief 표 글을 읽습니다(테스트). 줄마다 `owner` · `attribute` · `target` 이 필수이고, 모르는 속성 · 원소는 오류입니다. */
        [[nodiscard]] bool loadFromXMLText( string_view tableText, string_view sourceName );

        /** @brief 루트 @p rootName 의 규칙이 있으면 true 입니다. */
        [[nodiscard]] bool hasRulesFor( string_view rootName ) const;
        /**
         * @brief 글 @p text 의 어딘가에 규칙이 있는 루트(`<루트`)가 적혔으면 true 입니다 — 쿠커가 파일마다 파싱하지 않고 거르는 값싼 앞검사다(거짓 양성은 파싱이 거른다).
         */
        [[nodiscard]] bool isRootMentioned( string_view text ) const;
        /**
         * @brief 문서 루트 @p root 의 규칙을 모두 봅니다. 없는 id 마다 `XMLCatalog.UnknownReference`(필수) · `XMLCatalog.UnknownOptionalReference`(선택)를,
         *        겹침 규칙의 겹친 값마다 `XMLCatalog.DuplicateID` 를 남긴다.
         * @return 필수 참조가 깨졌거나 겹침이 있으면 false 입니다. 규칙이 없는 루트는 true 입니다.
         */
        [[nodiscard]] bool validateDocument( const XMLNode& root, string_view sourceName ) const;
        /** @brief 표를 읽고(`loadFromAsset`) @p root 를 봅니다 — 카탈로그 읽기의 입구다. 표를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool validateWithTable( const XMLNode& root, string_view sourceName );

        /** @brief 읽은 규칙입니다(표 순서). */
        const vector<DataCatalogReferenceRule>& getRules() const { return _listRule; }
        /** @brief 항목 종류 줄입니다(표 순서). */
        const vector<DataCatalogKeyTarget>& getKeyTargets() const { return _listKeyTarget; }
        /** @brief 카탈로그 키 칸 줄입니다(표 순서). */
        const vector<DataCatalogKeyField>& getFieldRows() const { return _listKeyField; }
        /** @brief 항목 종류 @p tag 의 줄입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const DataCatalogKeyTarget* findKeyTarget( string_view tag ) const;
        /** @brief 형식 @p owner 의 칸 @p field 줄입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const DataCatalogKeyField* findKeyField( string_view owner, string_view field ) const;
        /** @brief 구조체 칸 줄입니다(표 순서). */
        const vector<DataCatalogKeyStruct>& getStructRows() const { return _listKeyStruct; }
        /** @brief 정의 형식 ↔ 카탈로그 원소 줄입니다(표 순서). */
        const vector<DataCatalogKeyElement>& getElementRows() const { return _listKeyElement; }
        /** @brief 형식 @p owner 의 구조체 칸 @p field 줄입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const DataCatalogKeyStruct* findKeyStruct( string_view owner, string_view field ) const;
        /** @brief 정의 형식 @p type 의 원소 줄입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const DataCatalogKeyElement* findKeyElement( string_view type ) const;
        /** @brief 칸 줄 @p field 가 카탈로그 원소에서 쓰는 속성 이름입니다(`_attribute`, 비면 칸 이름에서 `_` 를 뗀 것). */
        [[nodiscard]] static string findElementAttribute( const DataCatalogKeyField& field );
        /** @brief 카탈로그 루트 @p root 에서 항목 종류 @p target 의 항목 id 를 @p inoutListID 뒤에 더합니다(경로 `_entry` 를 따라간다, 빈 id 는 뺀다). 루트 이름이 다르면 아무것도 하지 않는다. */
        static void collectEntryIDs( const XMLNode& root, const DataCatalogKeyTarget& target, vector<string_view>& inoutListID );

    private:
        /** @brief 겹침 규칙 하나를 봅니다. 겹친 값마다 알리고, 겹침이 있으면 false 입니다. */
        [[nodiscard]] static bool validateUnique( const XMLNode& root, const DataCatalogReferenceRule& rule, string_view sourceName );

    private:
        vector<DataCatalogReferenceRule> _listRule;       ///< 표의 줄(루트 · 줄 순서)
        vector<DataCatalogKeyTarget>     _listKeyTarget;  ///< 항목 종류 줄
        vector<DataCatalogKeyField>      _listKeyField;   ///< 카탈로그 키 칸 줄
        vector<DataCatalogKeyStruct>     _listKeyStruct;  ///< 구조체 칸 줄
        vector<DataCatalogKeyElement>    _listKeyElement; ///< 정의 형식 ↔ 카탈로그 원소 줄
    };

    /**
     * @class DataCatalogKeyCheck
     * @brief 씬 · 프리팹의 카탈로그 키 칸(`<Field>`)이 쿠킹하는 팩들의 카탈로그 항목(`<CatalogKey>`)을 가리키는지 봅니다 — 쿠커가 모든 도메인의 데이터 글을 한 번씩 넘긴다.
     * @details 문서마다 `addDocument` 로 넘기면 카탈로그 루트면 항목 id 를 모으고, 칸 주인 원소(어느 깊이든)면 칸 값을 모은다. 다 넘긴 뒤 `validate` 가 대조한다 —
     *          글의 차례(씬이 카탈로그보다 먼저)와 팩 경계(게임 씬이 `common` 카탈로그를 가리킴)에 기대지 않는다. 카탈로그는 펼친(`_base`) 글을 넘긴다 — 물려받은 항목도 항목이다.
     *          대조는 "그 종류의 어느 카탈로그 파일에든 있다" 이다(칸은 id 만 들고 어느 파일인지 모른다). 쓰는 쪽이 고른 한 벌에 없는 것은 시작할 때 형식 있는 조회
     *          (`GameCatalog::require`)가 오류로 멈춘다.
     */
    class SW_API DataCatalogKeyCheck
    {
    public:
        explicit DataCatalogKeyCheck( const DataCatalogReferenceRules& rules );

        /** @brief 글 @p text 에 항목 종류의 카탈로그 루트나 칸 주인 원소(`<이름`)가 적혔으면 true 입니다 — 파일마다 파싱하지 않고 거르는 앞검사다(거짓 양성은 파싱이 거른다). */
        [[nodiscard]] bool isMentioned( string_view text ) const;
        /** @brief 문서 하나를 넘깁니다. 카탈로그 루트면 항목 id 를, 칸 주인 원소면 칸 값을 모은다. */
        void addDocument( const XMLNode& root, string_view sourceName );
        /** @brief 모은 칸 값마다 그 종류의 항목이 있는지 봅니다. 없으면 하나마다 `DataCatalogKey.UnknownEntry` 를 남기고 false 입니다. */
        [[nodiscard]] bool validate() const;

    private:
        /** @brief 칸 값 하나입니다. */
        struct KeyUse
        {
            string _tag;
            string _id;
            string _sourceName;
            string _owner;
            string _field;
        };

        /** @brief @p node 와 그 아래 원소를 모두 훑어 칸 주인 원소의 값을 모읍니다. */
        void collectUses( const XMLNode& node, string_view sourceName );
        /**
         * @brief 형식 @p typeName 의 원소 @p node 에서 칸 줄의 값을 모읍니다. 구조체 칸 줄(`<Struct>`)이면 그 자식 원소를 그 형식으로 다시 본다.
         * @param bCatalogElement 카탈로그 정의 원소(`<Element>`)다 — 칸은 속성 이름(`findElementAttribute`)으로 찾고 목록은 쉼표 목록이다.
         */
        void collectTypedUses( const XMLNode& node, string_view typeName, bool bCatalogElement, string_view sourceName );

    private:
        const DataCatalogReferenceRules&             _rules;
        unordered_map<string, unordered_set<string>> _mapTagID; ///< 항목 종류 → 모은 항목 id
        vector<KeyUse>                               _listUse;  ///< 모은 칸 값(넘긴 차례)
    };
} // namespace sw

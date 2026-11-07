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
 *       </Catalog>
 *     </DataCatalogReferenceTable>
 * @endcode
 */
#pragma once
#include "Core/Container/string.h"
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
        string         _target;             ///< 가리키는 원소 이름(루트의 자식)
        string         _targetKey;          ///< 가리키는 원소의 id 속성(기본 `id`)
        bool           _bNamed{ false };    ///< 값이 `id:수` 꼴이다(콜론 앞이 id) NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool           _bOptional{ false }; ///< 깨져도 경고만 한다 NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
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
         * @brief 문서 루트 @p root 의 규칙을 모두 봅니다. 없는 id 마다 `XMLCatalog.UnknownReference`(필수) · `XMLCatalog.UnknownOptionalReference`(선택)를 남긴다.
         * @return 필수 참조가 하나라도 깨졌으면 false 입니다. 규칙이 없는 루트는 true 입니다.
         */
        [[nodiscard]] bool validateDocument( const XMLNode& root, string_view sourceName ) const;
        /** @brief 표를 읽고(`loadFromAsset`) @p root 를 봅니다 — 카탈로그 읽기의 입구다. 표를 읽지 못하면 false 입니다. */
        [[nodiscard]] static bool validateWithTable( const XMLNode& root, string_view sourceName );

        /** @brief 읽은 규칙입니다(표 순서). */
        const vector<DataCatalogReferenceRule>& getRules() const { return _listRule; }

    private:
        vector<DataCatalogReferenceRule> _listRule; ///< 표의 줄(루트 · 줄 순서)
    };
} // namespace sw

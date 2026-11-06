/**
 * @file DataCatalogInheritance.h
 * @brief 한 파일에 항목이 여럿인 카탈로그(아이템 · 외형 · 어빌리티)의 항목 단위 상속입니다. 합치기는 `DataInheritance` 하나이고 여기는 카탈로그 모양을 그 입력으로 옮깁니다.
 * @details 항목에 `_base="<id>"`(같은 파일) · `_base="<리소스 id>#<id>"`(다른 파일)를 적고 다른 칸만 씁니다. 루트의 `_base="<리소스 id>"` 는 파일 전체를 물려받습니다.
 *          카탈로그의 자식은 래퍼 없이 같은 이름이 되풀이되고 종류도 섞인다(`<Part>` 여럿 · `<Requires>` 여럿). `DataInheritance` 는 칸마다 원소 하나를 보므로,
 *          같은 이름의 형제를 그 이름의 래퍼 하나로 묶어 넘기고 결과를 다시 풉니다. 항목 하나는 가상의 문서이고 부모는 `IDataInheritanceSource` 가 돌려주는 글이므로
 *          순환 · 없는 부모 · 깊이 초과 · 목록 합치기 규칙이 파일 상속과 같습니다.
 *
 *          언리얼 Data Asset 은 항목마다 파일 하나이고 부모 관계가 클래스 상속이라 값만 다른 변형에도 자산이 하나씩 든다. 유니티 ScriptableObject Variant 도 자산 단위다.
 *          여기는 작은 표를 한 파일에 쓰는 형식을 지키면서 항목 하나를 자산처럼 짚는다. 쿠킹이 펼치므로 런타임 경로는 하나다(`PackCooker`).
 *
 *          합치는 규칙은 `Config/Dev/DataCatalogInheritance.json` 에 있다. 팩 쿠킹은 게임 모듈을 올리기 전에 도니(`HeadlessSubsystem`) 카탈로그 코드 옆의 정적 등록을 볼 수 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw
{
    /**
     * @struct DataCatalogInheritance
     * @brief 카탈로그 문서의 펼치기입니다.
     */
    struct SW_API DataCatalogInheritance
    {
        static constexpr const utf8* kConfigPath    = "Config/Dev/DataCatalogInheritance.json"; ///< 규칙 표(프로젝트 루트 기준)
        static constexpr utf8        kFileSeparator = '#';                                      ///< `_base` 에서 파일과 항목 id 를 가른다
        static constexpr const utf8* kOpClear       = "Clear";                                  ///< 열쇠 없는 되풀이 원소: `<Requires _op="Clear" />` 는 물려받은 목록을 비운다

        /**
         * @brief 규칙 표 글에서 루트 @p rootName 의 규칙을 찾습니다. 맨 위 노드는 루트이고 `_listField` 한 칸이 항목 종류 하나입니다(`_keyAttribute` 가 항목의 id 칸).
         * @param outbListed 표에 있으면 true 입니다. 없으면 @p outSchema 는 그대로입니다.
         * @return 표가 깨졌으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool parseSchema( string_view jsonText, string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed );

        /** @brief `kConfigPath` 를 읽어 `parseSchema` 합니다. 쿠킹 작업 안이면 표도 그 작업의 입력이 됩니다. 읽지 못하면 오류입니다. */
        [[nodiscard]] static bool loadSchema( string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed );

        /**
         * @brief 파일 상속과 항목 상속을 차례로 펼쳐 `_base` 가 없는 문서를 만듭니다.
         * @details 항목의 부모 id 는 파일 상속을 합친 결과에서 찾습니다. 다른 파일의 항목은 그 파일의 파일 상속까지 펼쳐 찾고, 그 항목의 `_base` 는 그 파일 기준입니다.
         * @return 순환 · 없는 부모 · 다른 종류의 부모 · 규칙에 없는 종류의 `_base` · 깊이 초과면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool flatten( string_view text, string_view sourceName, const DataInheritanceSchema& schema, IDataInheritanceSource& source, string& outText );

        /**
         * @brief 표에 있는 카탈로그이고 루트나 항목이 `_base` 를 적었으면 펼칩니다. 아니면 @p outbFlattened 가 false 입니다(`_base` 없는 글은 글 검색 한 번으로 끝난다).
         * @return 펼치다 실패하면 false 입니다.
         */
        [[nodiscard]] static bool flattenIfListed( string_view text, string_view sourceName, IDataInheritanceSource& source, bool& outbFlattened, string& outText );

        /**
         * @brief 표의 `listDocument` 에서 문서 형식 @p rootName 의 규칙을 찾습니다. 게임 · 키트 형식은 쿠킹이 코드를 보지 못해 여기 적습니다(`ResourceComposer`).
         * @param outbListed 표에 있으면 true 입니다. `listField` 가 @p outSchema 가 됩니다(리플렉션 형식의 표 줄은 `TestDataCatalogInheritance` 가 코드와 대조합니다).
         * @return 표가 깨졌으면 오류를 남기고 false 입니다.
         */
        [[nodiscard]] static bool parseDocumentRule( string_view jsonText, string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed );
        /** @brief `kConfigPath` 를 읽어 `parseDocumentRule` 합니다. */
        [[nodiscard]] static bool loadDocumentRule( string_view rootName, DataInheritanceSchema& outSchema, bool& outbListed );
    };
} // namespace sw

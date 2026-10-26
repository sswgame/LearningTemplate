/**
 * @file XMLReadUtil.h
 * @brief 손으로 읽는 데이터 XML 의 공통 첫걸음 — 문서를 읽고 루트 원소를 찾는 것, 이름 속성을 읽는 것입니다.
 * @details 캐릭터 데이터(`CharacterDataReader`)와 게임 데이터 표(`GameDataXML`)가 같은 몸통을 각자 적던 것을 한 곳에 둡니다.
 *          알리는 방식(오류 모음 · 경고 로그)은 부르는 쪽마다 달라서, 여기서는 무엇이 실패했는지만 돌려주고 알리지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class XMLDocument;
    class XMLNode;

    /**
     * @enum XMLRootStatus
     * @brief `XMLReadUtil::loadRoot` · `parseRoot` 의 결과입니다.
     */
    enum class XMLRootStatus : uint8
    {
        Found,       ///< 루트 원소를 찾았다
        NotLoaded,   ///< 읽지 못했거나 구문 오류다 — 이유는 `XMLDocument::getLastError`
        RootMissing, ///< 문서는 읽었지만 그 이름의 루트 원소가 없다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct XMLReadUtil
     * @brief 데이터 XML 읽기 도우미입니다. 알림은 하지 않습니다 — 결과를 보고 부르는 쪽이 자기 방식으로 남깁니다.
     */
    struct SW_API XMLReadUtil
    {
        /**
         * @brief 경로(리소스 상대 또는 절대)의 문서를 읽고 루트 원소를 찾습니다.
         * @param pRootName 루트 원소 이름입니다(철자 그대로).
         * @param pOutAbsPath 있으면 실제로 읽은 경로를 받습니다(`XMLDocument::loadResource`).
         */
        [[nodiscard]] static XMLRootStatus loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot,
                                                     string* pOutAbsPath = nullptr );
        /** @brief 텍스트를 파싱하고 루트 원소를 찾습니다. @p sourceName 은 구문 오류에 적는 이름입니다. */
        [[nodiscard]] static XMLRootStatus parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                                      XMLNode& outRoot );

        /**
         * @brief **선택** 이름 속성을 앞뒤 공백을 뺀 이름으로 읽습니다. 없거나 비었으면(공백뿐이어도) 빈 이름입니다.
         * @details 필수 이름은 이 함수로 읽지 않는다 — 빈 이름이 "적지 않음"과 "잘못 적음"을 가르지 못해, 부르는 쪽이 비었는지 보는 것을 잊으면 조용히 지나간다.
         *          필수는 오류를 남기는 읽기(`GameDataXML::readRequiredName` · `AppearanceXMLUtil::readRequiredName` · `CharacterDataReader::readName( …, true )`)를 쓴다.
         *          글이 빈 이름의 철자(`none`, 대소문자 무시)이면 오류를 알리고 빈 이름을 돌려준다 — 이름은 "적지 않음"과 값 "none" 을 가를 수 없다.
         *          그 글이 뜻을 가지는 속성(열거형 · 경로)은 이름이 아니라 글(`XMLNode::findAttribute`)로 읽는다. 빈 이름의 `c_str()` 도 "None" 이라 경로로 쓰면 안 된다.
         */
        static hashed_string readOptionalName( const XMLNode& node, const utf8* pAttribute );
        /**
         * @brief 요소의 속성과 자식 요소 이름이 모두 @p listAttribute · @p listChild 안에 있는지 봅니다(대소문자까지 맞아야 한다). 모르는 것마다 @p context 와 이름을 오류로 남기고 false 를 반환합니다.
         * @details 데이터가 조용히 기본값이 되지 않게 한다. 철자가 틀린 속성 · 자식은 읽히지 않은 채 지나가기 때문이다(JSON 시절의 `AnimationJSONUtil::hasOnlyKnownKeys` 와 같다).
         */
        [[nodiscard]] static bool hasOnlyKnownNames( const XMLNode& node, std::initializer_list<string_view> listAttribute, std::initializer_list<string_view> listChild, string_view context );
        /** @brief `1,2,3` 처럼 쉼표나 공백으로 가른 숫자 글을 정확히 @p count 개 읽습니다. 개수가 다르거나 숫자가 아닌 토큰이 있으면 false 이고 @p pOutValue 는 그대로입니다. */
        [[nodiscard]] static bool readFloatList( string_view text, float32* pOutValue, uint32 count );
        /** @brief 숫자 @p count 개를 `readFloatList` 가 읽는 글(쉼표로 이은 가장 짧은 표기)로 씁니다. */
        static string makeFloatListText( const float32* pValue, uint32 count );
    };
} // namespace sw

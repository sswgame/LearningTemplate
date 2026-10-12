/**
 * @file XMLReadUtil.h
 * @brief 손으로 읽는 데이터 XML 의 공통 첫걸음 — 문서를 읽고 루트 원소를 찾는 것, 이름 속성을 읽는 것입니다.
 * @details 캐릭터 데이터(`CharacterDataReader`)와 게임 데이터 표(`GameDataXML`)가 같은 몸통을 각자 적던 것을 한 곳에 둡니다.
 *          알리는 방식(오류 모음 · 경고 로그)은 부르는 쪽마다 달라서, 여기서는 무엇이 실패했는지만 돌려주고 알리지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
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
         * @param pRootName 루트 원소 이름입니다(대소문자 무시).
         * @param pOutAbsPath 있으면 실제로 읽은 경로를 받습니다(`XMLDocument::loadPath`).
         */
        [[nodiscard]] static XMLRootStatus loadRoot( XMLDocument& outDocument, string_view path, const utf8* pRootName, XMLNode& outRoot,
                                                     string* pOutAbsPath = nullptr );
        /** @brief 텍스트를 파싱하고 루트 원소를 찾습니다. @p sourceName 은 구문 오류에 적는 이름입니다. */
        [[nodiscard]] static XMLRootStatus parseRoot( XMLDocument& outDocument, string_view xmlText, string_view sourceName, const utf8* pRootName,
                                                      XMLNode& outRoot );

        /** @brief 이름 속성을 앞뒤 공백을 뺀 이름으로 읽습니다. 없거나 비었으면(공백뿐이어도) 빈 이름입니다. */
        static hashed_string readName( const XMLNode& node, const utf8* pAttribute );
    };
} // namespace sw

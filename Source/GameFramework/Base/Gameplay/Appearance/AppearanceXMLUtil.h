/**
 * @file AppearanceXMLUtil.h
 * @brief 외형 데이터 XML 의 공통 읽기 함수입니다. 알 수 없는 속성과 원소는 오류로 처리하고, 이름 목록, 소켓 배치, 태그를 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct AppearancePlacement;

    class AppearanceLoadReport;
    class TagContainer;
    class XMLNode;

    /**
     * @struct AppearanceXMLUtil
     * @brief 외형 카탈로그들이 함께 쓰는 XML 읽기 함수입니다. 사람이 고치는 데이터이므로 **알 수 없는 이름은 모두 오류**입니다. 철자를 틀린 속성이 조용히 기본값으로 처리되는 것을 막기 위해서입니다.
     */
    struct SW_GF_API AppearanceXMLUtil
    {
        /** @brief 테이블에 없는 속성마다 오류를 추가합니다. 모두 아는 속성이면 true 입니다. */
        template <size_t Count>
        static bool reportUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], AppearanceLoadReport& report, string_view sourceName )
        {
            return reportUnknownAttributes( node, arrKnown, static_cast<uint32>( Count ), report, sourceName );
        }
        static bool reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnownName, uint32 knownCount, AppearanceLoadReport& report, string_view sourceName );
        /** @brief "알 수 없는 원소" 오류를 추가합니다. */
        static void reportUnknownChild( const XMLNode& parent, const XMLNode& child, AppearanceLoadReport& report, string_view sourceName );

        /** @brief 속성을 이름으로 읽습니다. 없으면 빈 이름입니다. */
        static hashed_string readName( const XMLNode& node, const utf8* pAttribute );
        /** @brief 쉼표나 세미콜론으로 나뉜 이름 목록을 읽어 @p outListName 에 추가합니다. */
        static void readNameList( const XMLNode& node, const utf8* pAttribute, vector<hashed_string>& outListName );
        /**
         * @brief 선택 속성을 "x y z" 정확히 세 실수로 읽습니다. 속성이 없으면 @p inoutValue 를 그대로 둡니다(호출부가 초기값을 정한다).
         * @details 있는데 정확히 세 실수가 아니면 @p report 에 오류를 더하고 @p inoutValue 는 그대로입니다. 모자란 성분을 기본값으로 메우지 않는다.
         */
        static void readOptionalFloat3( const XMLNode& node, const utf8* pAttribute, float3& inoutValue, AppearanceLoadReport& report, string_view sourceName );
        /** @brief 선택 속성을 "r g b a" 정확히 네 실수로 읽습니다. 규칙은 `readOptionalFloat3` 와 같습니다. */
        static void readOptionalFloat4( const XMLNode& node, const utf8* pAttribute, float4& inoutValue, AppearanceLoadReport& report, string_view sourceName );
        /** @brief 필수 속성을 "r g b a" 정확히 네 실수로 읽습니다. 속성이 없거나 읽히지 않으면 @p report 에 오류를 더합니다. */
        static void readRequiredFloat4( const XMLNode& node, const utf8* pAttribute, float4& outValue, AppearanceLoadReport& report, string_view sourceName );
        /** @brief @p pSocketAttribute 의 소켓 후보 목록과 `offset`, `rotation` 을 읽습니다. 적지 않은 `offset` · `rotation` 은 0 이고, 틀리게 적으면 @p report 에 오류를 더합니다. */
        static void readPlacement( const XMLNode& node, const utf8* pSocketAttribute, AppearancePlacement& outPlacement, AppearanceLoadReport& report, string_view sourceName );
        /** @brief 쉼표로 나뉜 계층 태그를 추가합니다(`Helmet.FullFace, Armor.Heavy`). */
        static void readTags( const XMLNode& node, const utf8* pAttribute, TagContainer& outTags );
        /** @brief 이름이 목록에 있으면 true 입니다. */
        static bool containsName( const vector<hashed_string>& listName, const hashed_string& name );
    };
} // namespace sw

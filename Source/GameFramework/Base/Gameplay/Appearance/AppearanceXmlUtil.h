/**
 * @file AppearanceXmlUtil.h
 * @brief 외형 데이터 XML 의 공통 읽기 — 모르는 속성 · 원소는 오류, 이름 목록, 소켓 배치, 태그입니다.
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
    class XmlNode;

    /**
     * @struct AppearanceXmlUtil
     * @brief 외형 카탈로그들이 함께 쓰는 XML 읽기입니다. 사람이 고치는 데이터라 **모르는 이름은 모두 오류**입니다 — 철자를 틀린 속성이 조용히 기본값이
     *        되지 않게 합니다.
     */
    struct SW_GF_API AppearanceXmlUtil
    {
        /** @brief 표에 없는 속성마다 오류를 더합니다. 모두 알면 true 입니다. */
        template <size_t Count>
        static bool reportUnknownAttributes( const XmlNode& node, const utf8* const ( &arrKnown )[Count], AppearanceLoadReport& report, string_view sourceName )
        {
            return reportUnknownAttributes( node, arrKnown, static_cast<uint32>( Count ), report, sourceName );
        }
        static bool reportUnknownAttributes( const XmlNode& node, const utf8* const* ppKnownName, uint32 knownCount, AppearanceLoadReport& report, string_view sourceName );
        /** @brief "모르는 원소" 오류를 더합니다. */
        static void reportUnknownChild( const XmlNode& parent, const XmlNode& child, AppearanceLoadReport& report, string_view sourceName );

        /** @brief 속성을 이름으로 읽습니다. 없으면 빈 이름입니다. */
        static hashed_string readName( const XmlNode& node, const utf8* pAttribute );
        /** @brief 쉼표 · 세미콜론으로 나뉜 이름 목록을 읽습니다(@p outListName 에 더한다). */
        static void readNameList( const XmlNode& node, const utf8* pAttribute, vector<hashed_string>& outListName );
        /** @brief "x y z" 세 실수입니다. */
        static float3 readFloat3( const XmlNode& node, const utf8* pAttribute, const float3& fallback );
        /** @brief "r g b a" 네 실수입니다. */
        static float4 readFloat4( const XmlNode& node, const utf8* pAttribute, const float4& fallback );
        /** @brief @p pSocketAttribute 의 소켓 후보 목록 + `offset` + `rotation` 을 읽습니다. */
        static void readPlacement( const XmlNode& node, const utf8* pSocketAttribute, AppearancePlacement& outPlacement );
        /** @brief 쉼표 목록의 계층 태그를 더합니다(`Helmet.FullFace, Armor.Heavy`). */
        static void readTags( const XmlNode& node, const utf8* pAttribute, TagContainer& outTags );
        /** @brief 이름이 목록에 있는가입니다. */
        static bool containsName( const vector<hashed_string>& listName, const hashed_string& name );
    };
} // namespace sw

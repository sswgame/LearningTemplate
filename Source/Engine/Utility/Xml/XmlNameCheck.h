/**
 * @file XmlNameCheck.h
 * @brief 데이터 XML 의 "모르는 이름" 검사 — 원소가 받는 속성 · 자식 원소 표에 없는 이름을 찾습니다.
 * @details 데이터 로더마다 적던 "표에 없는 속성이면 경고" 를 한 곳에 둡니다. 모르는 이름은 로드 오류라는 규칙(오타가 조용히 기본값이 되지 않게)이
 *          이것 위에 섭니다(텔레메트리 스키마 · 페이싱 감독 프로필 · 크래시 보고 설정).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    class XmlNode;

    /**
     * @struct XmlNameCheck
     * @brief 이름 비교는 대소문자를 가리지 않습니다(`XmlNode` 의 읽기와 같다).
     */
    struct SW_API XmlNameCheck
    {
        /** @brief @p node 의 속성 중 @p ppKnown 에 없는 첫 이름입니다. 모두 알면 nullptr 입니다. */
        static const utf8* findUnknownAttribute( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount );
        /** @brief @p node 의 자식 원소 중 @p ppKnown 에 없는 첫 이름입니다. 모두 알면 nullptr 입니다. */
        static const utf8* findUnknownChild( const XmlNode& node, const utf8* const* ppKnown, uint32 knownCount );

        template <size_t Count>
        static const utf8* findUnknownAttribute( const XmlNode& node, const utf8* const ( &arrKnown )[Count] )
        {
            return findUnknownAttribute( node, arrKnown, static_cast<uint32>( Count ) );
        }

        template <size_t Count>
        static const utf8* findUnknownChild( const XmlNode& node, const utf8* const ( &arrKnown )[Count] )
        {
            return findUnknownChild( node, arrKnown, static_cast<uint32>( Count ) );
        }
    };
} // namespace sw

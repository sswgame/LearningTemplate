/**
 * @file XMLNameCheck.h
 * @brief 데이터 XML 의 "모르는 이름" 검사 — 원소가 받는 속성 · 자식 원소 표에 없는 이름을 찾고 알립니다.
 * @details 데이터 로더마다 적던 "표에 없는 속성이면 알림" 을 한 곳에 둡니다. 모르는 이름이 조용히 기본값이 되지 않게 하는 규칙이 이것 위에 섭니다
 *          (텔레메트리 스키마 · 사용자 설정 스키마 · 2D 설정 · 타일셋 · 캐릭터 데이터 · 게임 데이터 표). 판정(대소문자 무시)과 알림 문구가 하나라
 *          로그를 같은 말로 거를 수 있습니다 — `<원소> has unknown attribute '이름'` · `<원소> has unknown element <이름>`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Log/LogTypes.h"

namespace sw
{
    class XMLNode;

    /**
     * @struct XMLNameCheck
     * @brief 이름 비교는 대소문자를 가리지 않습니다(`XMLNode` 의 읽기와 같다).
     */
    struct SW_API XMLNameCheck
    {
        /** @brief 표 말고도 받을 이름인지 답하는 함수입니다(일정 조건 속성처럼 열린 이름). */
        using AcceptNameFunction = bool ( * )( const utf8* pName );

        /** @brief @p pName 이 @p ppKnown 에 있는지입니다. */
        static bool isKnownName( const utf8* pName, const utf8* const* ppKnown, uint32 knownCount );

        /** @brief @p node 의 속성 중 표(와 @p pfnAlsoKnown)에 없는 이름을 @p outListName 뒤에 붙입니다. 붙인 수입니다. */
        static uint32 collectUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, uint32 knownCount, vector<const utf8*>& outListName,
                                                AcceptNameFunction pfnAlsoKnown = nullptr );
        /** @brief @p node 의 자식 원소 중 표에 없는 이름을 @p outListName 뒤에 붙입니다. 붙인 수입니다. */
        static uint32 collectUnknownChildren( const XMLNode& node, const utf8* const* ppKnown, uint32 knownCount, vector<const utf8*>& outListName );

        /**
         * @brief 표에 없는 속성마다 `<sourceName>: <원소> has unknown attribute '이름'` 을 @p level 로 남깁니다.
         * @return 모두 알면 true 입니다. 첫 것에서 멈추지 않고 모두 알립니다.
         */
        [[nodiscard]] static bool reportUnknownAttributes( const XMLNode& node, const utf8* const* ppKnown, uint32 knownCount, string_view sourceName,
                                                           LogLevel level = LogLevel::Error, AcceptNameFunction pfnAlsoKnown = nullptr );
        /** @brief 표에 없는 자식 원소마다 `<sourceName>: <원소> has unknown element <이름>` 을 @p level 로 남깁니다. 모두 알면 true 입니다. */
        [[nodiscard]] static bool reportUnknownChildren( const XMLNode& node, const utf8* const* ppKnown, uint32 knownCount, string_view sourceName,
                                                         LogLevel level = LogLevel::Error );

        template <size_t Count>
        static bool isKnownName( const utf8* pName, const utf8* const ( &arrKnown )[Count] )
        {
            return isKnownName( pName, arrKnown, static_cast<uint32>( Count ) );
        }

        template <size_t Count>
        static uint32 collectUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], vector<const utf8*>& outListName,
                                                AcceptNameFunction pfnAlsoKnown = nullptr )
        {
            return collectUnknownAttributes( node, arrKnown, static_cast<uint32>( Count ), outListName, pfnAlsoKnown );
        }

        template <size_t Count>
        [[nodiscard]] static bool reportUnknownAttributes( const XMLNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName,
                                                           LogLevel level = LogLevel::Error, AcceptNameFunction pfnAlsoKnown = nullptr )
        {
            return reportUnknownAttributes( node, arrKnown, static_cast<uint32>( Count ), sourceName, level, pfnAlsoKnown );
        }

        template <size_t Count>
        [[nodiscard]] static bool reportUnknownChildren( const XMLNode& node, const utf8* const ( &arrKnown )[Count], string_view sourceName,
                                                         LogLevel level = LogLevel::Error )
        {
            return reportUnknownChildren( node, arrKnown, static_cast<uint32>( Count ), sourceName, level );
        }
    };
} // namespace sw

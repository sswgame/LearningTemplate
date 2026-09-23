/**
 * @file EditorListFilter.h
 * @brief 목록형 패널의 검색 필터 판정입니다(ImGui 에 의존하지 않아 테스트를 붙일 수 있습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include <initializer_list>

namespace sw::editor
{
    /**
     * @class EditorListFilter
     * @brief 검색 문자열을 한 번 정규화해 들고, 항목마다 부분 일치만 판정합니다.
     * @details 검색 가능한 패널마다 같은 것을 다시 만들고 있었습니다. "필터가 비면 모두 통과" 가드와, 필드마다 이어 붙인
     *          `stristr` 체인입니다. 판정 규칙이 패널마다 조금씩 달랐고, 새 패널은 그것을 또 복사해야 했습니다.
     *
     *          빈 가드를 빼먹으면 조용히 반대로 동작합니다. `StringUtil::stristr( x, "" )` 는 nullptr 을 반환하므로,
     *          **필터가 비었을 때 목록이 모두 사라집니다.** 매번 손으로 막아야 할 함정을 구조로 없앱니다.
     *
     *          `drawSearchField` 의 짝이지만 ImGui 에 의존하지 않습니다. 그래서 "무엇이 걸러지는가" 는 단위 테스트가 보고,
     *          "화면에 보이는가" 는 실제 기동이 봅니다.
     * @note 필터 문자열을 **복사하지 않습니다.** 생성할 때 넘긴 버퍼가 이 객체보다 오래 살아야 합니다.
     *       패널 멤버 버퍼를 프레임마다 감싸 쓰는 용도입니다.
     */
    class EditorListFilter
    {
    public:
        /** @brief 검색 문자열을 받아 앞뒤 공백을 뗀 상태로 보관합니다. nullptr 은 빈 필터입니다. */
        explicit EditorListFilter( const utf8* pFilter );
        /** @brief string_view 오버로드입니다. */
        explicit EditorListFilter( string_view filter );

        /** @brief 필터가 실제로 걸려 있는지 반환합니다. 공백만 입력한 것은 걸리지 않은 것으로 봅니다. */
        bool isActive() const { return _filter.empty() == false; }

        /** @brief 앞뒤 공백을 뗀 필터 문자열입니다. 0건 안내에 그대로 쓸 수 있습니다. */
        string_view getText() const { return _filter; }

        /**
         * @brief 필드가 필터를 대소문자 무시로 포함하는지 판정합니다.
         * @return 필터가 비어 있으면 **항상 true** (= 전부 통과).
         */
        bool matches( string_view field ) const;

        /** @brief 필드 중 하나라도 일치하면 true 입니다. 필터가 비어 있으면 항상 true 입니다. */
        bool matchesAny( std::initializer_list<string_view> listField ) const;

    private:
        string_view _filter;
    };
} // namespace sw::editor

/**
 * @file CompareUtil.h
 * @brief 정렬 · 이진 탐색에 넘기는 비교 함수 객체(`Ascending` · `Descending` · `Lexicographic`)입니다. 멤버 포인터로 키를 고릅니다.
 *
 * @details 정렬마다 `[]( const A& a, const A& b ) { return a._x < b._x; }` 를 손으로 적지 않습니다. 손으로 적으면 한쪽만 고치거나
 *          `<` 를 `>` 로 잘못 적어도 컴파일되고, 여러 키 비교에서 "앞 키가 같을 때만 다음 키" 를 빠뜨리기 쉽습니다.
 *          키는 데이터 멤버(`&T::_x`)나 인자 없는 const 멤버 함수(`&T::getX`)이고 `std::invoke` 로 읽으므로 원소가 값 · 원시 포인터 ·
 *          `unique_ptr` 어느 것이어도 같은 비교자를 씁니다.
 *          `operator<` 가 없는 키(`hashed_string`)는 컴파일되지 않습니다 — 그 키는 `HashedStringLexicalLess` 를 씁니다.
 *
 * @code
 *   std::sort( listItem.begin(), listItem.end(), Ascending<&Item::_order>{} );
 *   std::sort( listItem.begin(), listItem.end(), Ascending<&Item::_layer, &Item::_order>{} );                  // 두 키 오름차순
 *   std::sort( listItem.begin(), listItem.end(), Lexicographic<Descending<&Item::_priority>, Ascending<&Item::_id>>{} );
 * @endcode
 */
#pragma once
#include "Core/Common/StdHeaders.h"

namespace sw
{
    /**
     * @struct CompareUtil
     * @brief 비교자들이 함께 쓰는 키 읽기입니다. 직접 부르지 않고 `Ascending` · `Descending` · `Lexicographic` 을 씁니다.
     */
    struct CompareUtil
    {
        /** @brief 멤버 키들을 앞에서부터 비교해 @p lhs 가 앞이면 true 입니다(앞 키가 같을 때만 다음 키를 봅니다). */
        template <auto First, auto... Rest, typename TLeft, typename TRight>
        [[nodiscard]] static bool isLessByMember( const TLeft& lhs, const TRight& rhs )
        {
            static_assert( std::is_member_pointer_v<decltype( First )>, "키는 멤버 포인터(&T::_member 또는 &T::getX)여야 합니다." );
            const auto& left  = std::invoke( First, lhs );
            const auto& right = std::invoke( First, rhs );
            if ( left < right )
                return true;
            if ( right < left )
                return false;
            if constexpr ( sizeof...( Rest ) > 0 )
                return isLessByMember<Rest...>( lhs, rhs );
            else
                return false;
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct Ascending
     * @brief 멤버 키 오름차순 비교자입니다. 키를 여럿 주면 앞 키부터 차례로 봅니다(`Ascending<&T::_layer, &T::_order>`).
     */
    template <auto... Member>
    struct Ascending
    {
        static_assert( sizeof...( Member ) > 0, "키 멤버를 하나 이상 주십시오." );

        /** @brief @p lhs 의 키가 @p rhs 보다 작으면 true 입니다. */
        template <typename TLeft, typename TRight>
        [[nodiscard]] bool operator()( const TLeft& lhs, const TRight& rhs ) const
        {
            return CompareUtil::isLessByMember<Member...>( lhs, rhs );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct Descending
     * @brief 멤버 키 내림차순 비교자입니다. 키를 여럿 주면 앞 키부터 차례로, 모두 내림차순으로 봅니다.
     */
    template <auto... Member>
    struct Descending
    {
        static_assert( sizeof...( Member ) > 0, "키 멤버를 하나 이상 주십시오." );

        /** @brief @p lhs 의 키가 @p rhs 보다 크면 true 입니다. */
        template <typename TLeft, typename TRight>
        [[nodiscard]] bool operator()( const TLeft& lhs, const TRight& rhs ) const
        {
            return CompareUtil::isLessByMember<Member...>( rhs, lhs );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct Lexicographic
     * @brief 비교자를 차례로 잇는 비교자입니다. 앞 비교자가 두 원소를 같다고 볼 때만 다음 비교자를 봅니다.
     * @details 방향이 섞인 여러 키 정렬에 씁니다(`Lexicographic<Descending<&T::_priority>, Ascending<&T::_id>>`). 방향이 모두 같으면
     *          `Ascending<&T::_a, &T::_b>` 가 짧습니다. 각 비교자는 엄격한 약순서(strict weak ordering)여야 합니다.
     */
    template <typename... TCompare>
    struct Lexicographic
    {
        static_assert( sizeof...( TCompare ) > 0, "비교자를 하나 이상 주십시오." );

        /** @brief 비교자를 차례로 적용해 @p lhs 가 앞이면 true 입니다. */
        template <typename TLeft, typename TRight>
        [[nodiscard]] bool operator()( const TLeft& lhs, const TRight& rhs ) const
        {
            return isLess<TCompare...>( lhs, rhs );
        }

    private:
        template <typename TFirst, typename... TRest, typename TLeft, typename TRight>
        [[nodiscard]] static bool isLess( const TLeft& lhs, const TRight& rhs )
        {
            const TFirst compare{};
            if ( compare( lhs, rhs ) )
                return true;
            if ( compare( rhs, lhs ) )
                return false;
            if constexpr ( sizeof...( TRest ) > 0 )
                return isLess<TRest...>( lhs, rhs );
            else
                return false;
        }
    };
} // namespace sw

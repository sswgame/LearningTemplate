/**
 * @file VectorUtil.h
 * @brief 연속 배열(`sw::vector` · `std::vector` · `small_vector`)에 쓰는 도우미입니다. 전부 static 템플릿이고 특정 vector 타입에 묶이지 않습니다.
 *
 * @note 표준 vector 에 없는 연산만 둡니다. `vector.h` 안에 두면 `SW_ENABLE_STL_CONTAINER` 로 표준 vector 를 쓸 때 "이 함수는 어느 vector 의 것인가" 가
 *       흐려지므로, `StringUtil` · `FileUtil` 처럼 따로 둔 도구 클래스로 부릅니다. 인자는 `size()` · `back()` · `pop_back()` · `operator[]` 만 씁니다
 *       (`removeIf` · `sortUnique` 는 `begin()` · `end()` · `erase()` 도 씁니다).
 *
 * @details 멤버 기준 찾기(`findBy<&T::_member>`)는 "원소를 돌며 멤버를 비교하고 맞으면 주소를 돌려준다" 는 손 루프를 대신합니다. 키는 데이터
 *          멤버나 인자 없는 const 멤버 함수이고 `std::invoke` 로 읽으므로, 원소가 값 · 원시 포인터 · `unique_ptr` 어느 것이어도 됩니다.
 *          찾은 결과는 **키를 가진 객체의 포인터**입니다(원소가 값이면 원소의 주소, 포인터 · `unique_ptr` 이면 가리키는 객체).
 *          배열은 lvalue 로만 받습니다 — 임시 배열 안을 가리키는 포인터는 컴파일되지 않습니다.
 *          정렬 비교자는 `CompareUtil.h`(`Ascending<&T::_member>`)에 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"

namespace sw
{
    /**
     * @struct VectorUtil
     * @brief 연속 배열의 순서 없는 삭제 · 멤버 기준 찾기 같은, 표준 vector 에 없는 연산을 모은 도우미입니다.
     */
    struct VectorUtil
    {
        // `findBy` 의 반환 타입(뒤따르는 반환 타입)이 쓰므로 공개 함수보다 먼저 선언한다 — 반환 타입에서는 뒤에 선언한 멤버가 보이지 않는다.
    private:
        /** @brief 멤버 포인터 `M C::*` 의 클래스 `C` 입니다(데이터 멤버 · 멤버 함수 둘 다). */
        template <typename TMember>
        struct MemberOwner;
        template <typename TMember, typename TOwner>
        struct MemberOwner<TMember TOwner::*>
        {
            using Type = TOwner;
        };

        /** @brief 원소에서 키 `Member` 를 가진 객체의 주소입니다. 원소가 그 객체(또는 파생)면 원소의 주소, 아니면 원소가 가리키는 객체입니다. */
        template <auto Member, typename TElement>
        static auto* addressOfKeyOwner( TElement& element )
        {
            using TOwner = typename MemberOwner<decltype( Member )>::Type;
            if constexpr ( std::is_base_of_v<TOwner, std::remove_cv_t<TElement>> )
                return std::addressof( element );
            else
                return &*element;
        }

    public:
        /**
         * @brief `index` 위치의 원소를 지우고 마지막 원소를 그 위치로 옮깁니다. 순서를 지키지 않는 O(1) 삭제입니다(언리얼 `TArray::RemoveAtSwap`).
         * @details 옮겨 온 원소는 이제 `index` 에 있습니다. 원소가 제 위치를 따로 기억한다면 부르는 쪽이 그것을 고칩니다. 마지막 원소를 지울 때는
         *          옮기지 않습니다. 옮기기는 이동이라 이동만 되는 원소(`unique_ptr`)도 됩니다.
         */
        template <typename TVector>
        static void removeAtSwap( TVector& list, size_t index )
        {
            SW_ASSERT( index < list.size() );
            if ( index + 1 < list.size() )
                list[index] = std::move( list.back() );
            list.pop_back();
        }

        /**
         * @brief `predicate` 가 true 인 첫 원소의 주소입니다. 없으면 nullptr 입니다(언리얼 `TArray::FindByPredicate`).
         * @details 조건이 둘 이상이라 `findBy` 의 멤버 키로 표현할 수 없는 찾기에 씁니다. 원소가 값인 배열에만 씁니다(포인터 원소는 가리키는 객체가 아니라 원소의 주소가 나온다).
         *          const 배열이면 const 포인터로 나옵니다. 배열은 lvalue 로만 받습니다.
         */
        template <typename TVector, typename TPredicate>
        [[nodiscard]] static auto findIf( TVector& list, TPredicate predicate ) -> decltype( std::addressof( list[0] ) )
        {
            for ( size_t index = 0; index < list.size(); ++index )
            {
                if ( predicate( list[index] ) )
                    return std::addressof( list[index] );
            }
            return nullptr;
        }

        /** @brief `predicate` 가 true 인 원소가 있으면 true 입니다(언리얼 `TArray::ContainsByPredicate`). */
        template <typename TVector, typename TPredicate>
        [[nodiscard]] static bool containsIf( const TVector& list, TPredicate predicate )
        {
            for ( const auto& element : list )
            {
                if ( predicate( element ) )
                    return true;
            }
            return false;
        }

        /** @brief `value` 와 같은 첫 원소의 인덱스입니다. 없으면 빈 값입니다(언리얼 `TArray::Find`). `findIndexBy` 는 멤버 키로 찾고, 이것은 원소 값 자체로 찾습니다. */
        template <typename TVector, typename TValue>
        [[nodiscard]] static std::optional<size_t> findIndex( const TVector& list, const TValue& value )
        {
            for ( size_t index = 0; index < list.size(); ++index )
            {
                if ( list[index] == value )
                    return index;
            }
            return std::nullopt;
        }

        /**
         * @brief `value` 와 같은 첫 원소를 지우고 지운 개수(0 또는 1)를 돌려줍니다. 남은 원소의 순서는 지킵니다(언리얼 `TArray::RemoveSingle`).
         * @details 찾아서 지우는 `find` + `erase( it )` 손 코드를 대신합니다. 지운 개수는 알림이라 버려도 됩니다(`[[nodiscard]]` 가 아닙니다). 순서가 상관없으면 `removeSingleSwap` 이 원소를 덜 옮깁니다.
         */
        template <typename TVector, typename TValue>
        static size_t removeFirst( TVector& list, const TValue& value )
        {
            const std::optional<size_t> index = findIndex( list, value );
            if ( index.has_value() == false )
                return 0;
            list.erase( list.begin() + static_cast<ptrdiff_t>( *index ) );
            return 1;
        }

        /**
         * @brief `value` 와 같은 첫 원소를 `removeAtSwap` 으로 지웁니다(언리얼 `TArray::RemoveSingleSwap`). 찾아서 지웠으면 true 입니다.
         * @details "찾기 → 마지막 원소로 덮기 → pop_back" 을 손으로 적지 않고 이것을 씁니다.
         */
        template <typename TVector, typename TValue>
        [[nodiscard]] static bool removeSingleSwap( TVector& list, const TValue& value )
        {
            for ( size_t index = 0; index < list.size(); ++index )
            {
                if ( list[index] == value )
                {
                    removeAtSwap( list, index );
                    return true;
                }
            }
            return false;
        }

        /** @brief `value` 와 같은 원소가 있으면 true 입니다(언리얼 `TArray::Contains`). 앞에서부터 선형으로 찾습니다(C 배열도 됩니다). */
        template <typename TVector, typename TValue>
        [[nodiscard]] static bool contains( const TVector& list, const TValue& value )
        {
            for ( const auto& element : list )
            {
                if ( element == value )
                    return true;
            }
            return false;
        }

        /**
         * @brief 키 `Member` 가 `value` 와 같은 첫 원소의 인덱스입니다. 없으면 빈 값입니다(언리얼 `TArray::IndexOfByKey`).
         * @details 무효 인덱스 센티넬 대신 `optional` 을 돌려주므로, 확인 없이 `list[index]` 로 쓰는 실수가 컴파일되지 않습니다.
         *          `if ( const auto index = VectorUtil::findIndexBy<&T::_id>( list, id ) ) VectorUtil::removeAtSwap( list, *index );`
         */
        template <auto Member, typename TVector, typename TValue>
        [[nodiscard]] static std::optional<size_t> findIndexBy( const TVector& list, const TValue& value )
        {
            for ( size_t index = 0; index < list.size(); ++index )
            {
                if ( std::invoke( Member, list[index] ) == value )
                    return index;
            }
            return std::nullopt;
        }

        /**
         * @brief 키 `Member` 가 `value` 와 같은 첫 원소가 가리키는 객체의 포인터입니다. 없으면 nullptr 입니다(언리얼 `TArray::FindByKey`).
         * @details 원소가 값이면 원소의 주소, 원시 포인터 · `unique_ptr` 이면 가리키는 객체입니다. const 배열이면 값 원소는 const 포인터로 나옵니다.
         *          배열은 lvalue 로만 받습니다(임시 배열 안의 주소를 돌려주지 않도록).
         */
        template <auto Member, typename TVector, typename TValue>
        [[nodiscard]] static auto findBy( TVector& list, const TValue& value ) -> decltype( addressOfKeyOwner<Member>( list[0] ) )
        {
            for ( size_t index = 0; index < list.size(); ++index )
            {
                if ( std::invoke( Member, list[index] ) == value )
                    return addressOfKeyOwner<Member>( list[index] );
            }
            return nullptr;
        }

        /** @brief 키 `Member` 가 `value` 와 같은 원소가 있으면 true 입니다. */
        template <auto Member, typename TVector, typename TValue>
        [[nodiscard]] static bool containsBy( const TVector& list, const TValue& value )
        {
            return findIndexBy<Member>( list, value ).has_value();
        }

        /**
         * @brief `predicate` 가 true 인 원소를 모두 지우고 지운 개수를 돌려줍니다. 남은 원소의 순서는 지킵니다(erase-remove, C++20 `std::erase_if`).
         * @details `list.erase( std::remove_if( ... ), list.end() )` 에서 `erase` 의 둘째 인자를 빠뜨리면 한 개만 지워지는데도 컴파일됩니다.
         *          그 형태를 손으로 적지 않고 이것을 씁니다. 순서가 상관없으면 `removeAtSwap` 이 원소를 덜 옮깁니다.
         *          지운 개수는 알림이라 버려도 됩니다(`[[nodiscard]]` 가 아닙니다).
         */
        template <typename TVector, typename TPredicate>
        static size_t removeIf( TVector& list, TPredicate predicate )
        {
            const auto   newEnd      = std::remove_if( list.begin(), list.end(), predicate );
            const size_t removeCount = static_cast<size_t>( list.end() - newEnd );
            list.erase( newEnd, list.end() );
            return removeCount;
        }

        /**
         * @brief `value` 와 같은 원소를 모두 지우고 지운 개수를 돌려줍니다. 남은 원소의 순서는 지킵니다.
         * @details 값 하나를 지우는 `erase( std::remove( ... ), end )` 도 `erase` 의 둘째 인자를 빠뜨리기 쉬워서 이것을 씁니다. 지운 개수는 알림이라 버려도 됩니다.
         */
        template <typename TVector, typename TValue>
        static size_t removeValue( TVector& list, const TValue& value )
        {
            const auto   newEnd      = std::remove( list.begin(), list.end(), value );
            const size_t removeCount = static_cast<size_t>( list.end() - newEnd );
            list.erase( newEnd, list.end() );
            return removeCount;
        }

        /** @brief 오름차순(`operator<`)으로 정렬하고 같은(`operator==`) 이웃을 하나만 남깁니다. */
        template <typename TVector>
        static void sortUnique( TVector& list )
        {
            std::sort( list.begin(), list.end() );
            list.erase( std::unique( list.begin(), list.end() ), list.end() );
        }

        /**
         * @brief `compare` 로 정렬하고 `compare` 가 같다고 보는 이웃을 하나만 남깁니다(`sortUnique( list, Ascending<&T::_id>{} )`).
         * @details 같음은 `compare` 에서 끌어냅니다(어느 쪽도 앞이 아니면 같다). 정렬 기준과 중복 기준이 어긋날 수 없습니다.
         */
        template <typename TVector, typename TCompare>
        static void sortUnique( TVector& list, TCompare compare )
        {
            std::sort( list.begin(), list.end(), compare );
            const auto isEquivalent = [&compare]( const auto& lhs, const auto& rhs )
            { return compare( lhs, rhs ) == false && compare( rhs, lhs ) == false; };
            list.erase( std::unique( list.begin(), list.end(), isEquivalent ), list.end() );
        }
    };
} // namespace sw

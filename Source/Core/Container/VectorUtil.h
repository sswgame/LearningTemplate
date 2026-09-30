/**
 * @file VectorUtil.h
 * @brief 연속 배열(`sw::vector` · `std::vector` · `small_vector`)에 쓰는 도우미입니다. 전부 static 템플릿이고 특정 vector 타입에 묶이지 않습니다.
 *
 * @note 표준 vector 에 없는 연산만 둡니다. `vector.h` 안에 두면 `SW_ENABLE_STL_CONTAINER` 로 표준 vector 를 쓸 때 "이 함수는 어느 vector 의 것인가" 가
 *       흐려지므로, `StringUtil` · `FileUtil` 처럼 따로 둔 도구 클래스로 부릅니다. 인자는 `size()` · `back()` · `pop_back()` · `operator[]` 만 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"

#include <cstddef>
#include <utility>

namespace sw
{
    /**
     * @struct VectorUtil
     * @brief 연속 배열의 순서 없는 삭제 같은, 표준 vector 에 없는 연산을 모은 도우미입니다.
     */
    struct VectorUtil
    {
        /**
         * @brief `index` 자리의 원소를 지우고 마지막 원소를 그 자리로 옮깁니다. 순서를 지키지 않는 O(1) 삭제입니다(언리얼 `TArray::RemoveAtSwap`).
         * @details 옮겨 온 원소는 이제 `index` 에 있습니다. 원소가 제 자리를 따로 기억한다면 부르는 쪽이 그것을 고칩니다. 마지막 원소를 지울 때는
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
         * @brief `value` 와 같은 첫 원소를 `removeAtSwap` 으로 지웁니다(언리얼 `TArray::RemoveSingleSwap`). 찾아서 지웠으면 true 입니다.
         * @details 여러 곳이 "찾기 → 마지막 원소로 덮기 → pop_back" 여덟 줄을 각자 들고 있었습니다.
         */
        template <typename TVector, typename TValue>
        static bool removeSingleSwap( TVector& list, const TValue& value )
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
    };
} // namespace sw

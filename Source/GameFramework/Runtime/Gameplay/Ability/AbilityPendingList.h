/**
 * @file AbilityPendingList.h
 * @brief 어빌리티 컴포넌트의 스펙 · 활성 이펙트 컨테이너가 함께 쓰는 형입니다 — 주소가 바뀌지 않는 원소 목록과 지우기 표시의 늦은 정리.
 * @details 언리얼 GAS 의 `FGameplayAbilitySpecContainer` · `FActiveGameplayEffectsContainer` 자리다. 순회 중(잠금 안)에 지우라고 표시만 하고,
 *          잠금이 풀리면 표시된 원소를 목록 밖으로 옮겨 낸다(`takePendingRemovals`) — 소멸자가 컴포넌트를 다시 불러도 목록은 이미 정리돼 있다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    /**
     * @class AbilityPendingList
     * @brief `unique_ptr<T>` 목록입니다. @p T 는 지우기 표시 칸 `_bPendingRemove`(SW_TRUE 면 지운다)를 가진다.
     */
    template <typename T>
    class AbilityPendingList
    {
    public:
        using Iterator = typename vector<unique_ptr<T>>::const_iterator;

        size_t                       size() const { return _list.size(); }
        bool                         empty() const { return _list.empty(); }
        const unique_ptr<T>&         operator[]( size_t index ) const { return _list[index]; }
        Iterator                     begin() const { return _list.begin(); }
        Iterator                     end() const { return _list.end(); }
        const vector<unique_ptr<T>>& getList() const { return _list; }

        /** @brief 끝에 더합니다. 주소는 지워질 때까지 그대로다. */
        void add( unique_ptr<T> pElement ) { _list.push_back( std::move( pElement ) ); }
        void clear() { _list.clear(); }

        /** @brief 지우기 표시된 원소를 목록에서 빼 @p outListRemoved 끝에 옮깁니다(뒤에서부터). 부른 쪽이 잠근 채 버린다. */
        void takePendingRemovals( vector<unique_ptr<T>>& outListRemoved )
        {
            for ( size_t index = _list.size(); index > 0; --index )
            {
                unique_ptr<T>& pElement = _list[index - 1];
                if ( pElement->_bPendingRemove != SW_TRUE )
                    continue;
                outListRemoved.push_back( std::move( pElement ) );
                _list.erase( _list.begin() + static_cast<ptrdiff_t>( index - 1 ) );
            }
        }

    private:
        vector<unique_ptr<T>> _list;
    };
} // namespace sw

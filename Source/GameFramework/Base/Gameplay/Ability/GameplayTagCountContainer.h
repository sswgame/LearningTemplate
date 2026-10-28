/**
 * @file GameplayTagCountContainer.h
 * @brief 개수로 세는 태그 집합입니다. 이펙트 둘이 같은 태그를 주면 둘 다 해제돼야 사라집니다(언리얼 `FGameplayTagCountContainer`).
 * @details 어빌리티 시스템 컴포넌트가 가진 태그(`getOwnedTagCounts`)와 막힌 어빌리티 태그가 각각 이 집합 하나입니다. 알림은 태그가 생기거나(개수 0 → 1)
 *          사라질 때(1 → 0)만 보냅니다. 같은 태그를 주는 이펙트가 하나 더 적용돼도 "생겼다" 고 알리지 않습니다(언리얼의 NewOrRemoved).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"

#include "Engine/Object/Component/TagSystem.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class GameplayTagCountContainer
     * @brief 태그마다 개수를 세고, 개수 1 이상인 태그의 목록(`getTags`)을 함께 듭니다.
     */
    class SW_GF_API GameplayTagCountContainer
    {
    public:
        using TagChangedDelegate = Delegate<void( TagID, int32 )>;

        GameplayTagCountContainer();
        ~GameplayTagCountContainer();

        GameplayTagCountContainer( const GameplayTagCountContainer& )            = delete;
        GameplayTagCountContainer& operator=( const GameplayTagCountContainer& ) = delete;

        /** @brief 개수 1 이상인 태그의 목록입니다. */
        const TagContainer& getTags() const { return _tags; }
        /** @brief @p tag 나 그 하위 태그를 가지고 있으면 true 입니다("Cooldown" 은 "Cooldown.Fireball" 과 일치). */
        bool hasMatchingTag( TagID tag ) const;
        /** @brief @p tags 를 모두 가지고 있으면 true 입니다. @p tags 가 비어 있으면 true 입니다. */
        bool hasAllMatchingTags( const TagContainer& tags ) const;
        /** @brief @p tags 중 하나라도 가지고 있으면 true 입니다. @p tags 가 비어 있으면 false 입니다. */
        bool hasAnyMatchingTags( const TagContainer& tags ) const;
        /** @brief @p tag 와 **같은** 태그의 개수입니다. 하위 태그는 세지 않습니다. */
        int32 getCount( TagID tag ) const;

        /** @brief @p tag 의 개수를 @p delta 만큼 바꿉니다. 0 아래로 내려가지 않습니다. 태그가 생기거나 사라지면 알립니다. */
        void updateCount( TagID tag, int32 delta );
        /** @brief 집합의 태그마다 `updateCount` 를 부릅니다. */
        void updateCounts( const TagContainer& tags, int32 delta );

        /** @brief 태그가 새로 생기거나(개수 0 → 1) 사라질 때(1 → 0) 부를 델리게이트를 겁니다. 인자는 태그와 새 개수입니다. 돌려준 구독이 사라지면 풉니다. */
        [[nodiscard]] Subscription registerChanged( const TagChangedDelegate& delegate );
        /** @brief 모든 구독을 풉니다(소유자가 사라질 때). 구독은 무효가 됩니다. */
        void unregisterAllChanged();
        /** @brief [@p pBegin, @p pEnd) 안의 코드를 가리키는 구독을 풉니다(모듈 언로드). 푼 수입니다. */
        uint32 unregisterChangedWithin( const void* pBegin, const void* pEnd );

    private:
        struct CountEntry
        {
            TagID _tag{};
            int32 _count{ 0 };
        };

        unordered_map<uint64, CountEntry>       _mapCount; ///< 태그 id → 개수
        TagContainer                            _tags;     ///< 개수 1 이상인 태그
        MulticastDelegate<void( TagID, int32 )> _changedMulticast;
    };
} // namespace sw

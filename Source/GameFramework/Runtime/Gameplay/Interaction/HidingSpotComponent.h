/**
 * @file HidingSpotComponent.h
 * @brief 숨는 곳(사물함 · 옷장)입니다. 스마트 오브젝트 슬롯을 차지하면 숨음 태그를 붙입니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class HidingSpotComponent
     * @brief 숨는 곳입니다(사물함, 옷장, 침대 밑). 같은 오브젝트의 `SmartObjectComponent` 슬롯(Activity.Hide)을 차지하면 `State.Hidden` 태그를 붙입니다.
     *        AI 감각은 그 태그를 보고 대상을 보지 않습니다(`isHidden`). 상호작용(Hide)을 끝내면 들어가고, 다시 끝내면 나옵니다(완료 알림).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Hiding Spot", Tooltip = "Claims a smart object slot and tags the occupant as hidden" )
    class SW_GF_API HidingSpotComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostPhysics; ///< 이번 프레임에 움직인 자리로 본다(상호작용과 같다)

        HidingSpotComponent();
        virtual ~HidingSpotComponent() override = default;

        /** @brief 같은 오브젝트의 상호작용 완료를 듣습니다. */
        void onBeginPlay() override;
        /** @brief 숨은 쪽을 모두 내보냅니다 — 부서지거나 스트리밍으로 사라진 곳에 숨었던 쪽이 숨은 채 남지 않는다. */
        void onEndPlay() override;
        /** @brief 걸어 나간 쪽(상호작용이 닿는 거리 밖)을 내보냅니다. 숨은 쪽이 있는 동안만 돕니다. */
        void onTick( TickContext& context ) override;
        /** @brief 숨깁니다. 슬롯이 없으면 false 입니다. */
        [[nodiscard]] bool enter( GameObject& who );
        /** @brief 나옵니다. 숨어 있지 않았으면 false 입니다. */
        bool exit( GameObject& who );
        /** @brief 숨음 태그를 가지고 있으면 true 입니다. AI 감각이 이 값을 묻습니다. */
        static bool isHidden( const GameObject& object );

    private:
        /** @brief 상호작용(Hide)을 끝낸 쪽이 숨었으면 나오고, 아니면 들어갑니다. */
        void handleUsed( const GameObject& user );

        /** @brief @p who 가 숨은 자리에서 걸어 나갔으면 true 입니다(상호작용 정의가 있으면 그 거리의 여유 배율 밖). */
        bool hasWalkedAway( const GameObject& who ) const;
        /** @brief 걸어 나갔거나 사라진 쪽을 내보냅니다(틱 뒤 — 남의 태그를 고친다). */
        void releaseGoneOccupants();

    private:
        PROPERTY( RuntimeState )
        vector<GameObjectHandle> _listOccupant;     ///< 숨은 쪽 — 숨음 태그를 붙였고 아직 떼지 않았다
        Subscription             _usedSubscription; ///< 상호작용 완료 구독(`handleUsed`)
    };
} // namespace sw

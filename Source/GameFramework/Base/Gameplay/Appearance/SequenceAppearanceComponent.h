/**
 * @file SequenceAppearanceComponent.h
 * @brief 시퀀스 플레이어의 외형 항목(`SequenceItemKind::Appearance`)을 받아 대상 오브젝트의 외형 프리셋을 바꾸는 컴포넌트를 정의합니다.
 * @details 엔진의 시퀀서는 게임 프레임워크의 외형(`CharacterAppearanceComponent`)을 모른다. 그래서 시퀀서는 시작 프레임을 지날 때 이벤트로만 알리고
 *          (`SequencePlayerComponent::registerSequenceEvent`, 항목 종류 표의 `_bFiresOnCross`), 이 컴포넌트가 들어서 프리셋을 바꾼다.
 *          카메라 컷을 `CinematicCameraComponent` 가 받는 것과 같은 나눔이다. 언리얼 Sequencer 의 Event 트랙이 블루프린트로 장비를 바꾸는 일을 데이터 항목으로 만든 셈이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct SequenceTrackItem;

    class GameObjectManager;

    /**
     * @class SequenceAppearanceComponent
     * @brief 같은 오브젝트의 `SequencePlayerComponent` 가 알리는 외형 항목을 받아, 항목의 대상 오브젝트가 입은 외형 프리셋을 `_preset` 으로 바꿉니다.
     * @details 시드는 대상이 쥔 값을 그대로 둔다(같은 캐릭터가 프리셋만 갈아입는다). 대상이 없거나 외형 컴포넌트가 없으면 오류를 남기고 아무것도 바꾸지 않는다 —
     *          항목은 읽을 때 대상 · 프리셋 이름이 비어 있지 않음을 확인했지만(`SequenceAsset`), 대상이 씬에 있는지는 재생할 때 알 수 있다.
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Appearance", Tooltip = "Routes the sequence player's appearance items to the target object's character appearance preset" )
    class SW_GF_API SequenceAppearanceComponent : public Component
    {
    public:
        REFLECT_BODY();

        SequenceAppearanceComponent();
        virtual ~SequenceAppearanceComponent() override = default;

        /** @brief 같은 오브젝트의 시퀀스 플레이어에 이벤트 델리게이트를 등록합니다. */
        void onBeginPlay() override;
        /** @brief 이벤트 델리게이트를 뗍니다. */
        void onEndPlay() override;

        /** @brief 이벤트 항목 하나를 처리합니다. 외형 항목이 아니면 무시합니다. 시퀀스 플레이어가 부릅니다. */
        void handleSequenceEvent( const SequenceTrackItem& item );
        /**
         * @brief 외형 항목 하나를 @p manager 의 씬에 적용합니다(대상 오브젝트의 외형 컴포넌트에 프리셋을 줍니다). 컴포넌트를 거치지 않고 테스트가 부릅니다.
         * @return 외형 항목이 아니거나 대상 · 외형 컴포넌트를 찾지 못하면(오류를 남깁니다) false 입니다.
         */
        [[nodiscard]] static bool applyAppearanceItem( GameObjectManager& manager, const SequenceTrackItem& item );

    private:
        Subscription _eventSubscription; ///< 시퀀스 플레이어의 이벤트 구독. 플레이어가 먼저 사라지면 무효가 된다
    };
} // namespace sw

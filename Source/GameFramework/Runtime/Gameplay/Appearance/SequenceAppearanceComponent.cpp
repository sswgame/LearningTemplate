#include "pch.h"

#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/Logger.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterAppearanceComponent.h"

namespace sw
{
    struct SequenceFiredEvent;

    class GameObjectManager;

    /**
     * @class SequenceAppearanceComponent
     * @brief 같은 오브젝트의 `SequencePlayerComponent` 가 알리는 외형 항목을 받아, 항목의 대상 오브젝트가 입은 외형 프리셋을 `_preset` 으로 바꿉니다.
     * @details 시드는 대상이 쥔 값을 그대로 둔다(같은 캐릭터가 프리셋만 갈아입는다). 대상은 항목의 바인딩이 묶인 오브젝트다(이벤트가 싣고 온다).
     *          대상이 없거나 외형 컴포넌트가 없으면 오류를 남기고 아무것도 바꾸지 않는다 — 항목은 읽을 때 바인딩 · 프리셋이 있음을 확인했지만(`SequenceAsset`),
     *          대상이 살아 있는지는 재생할 때 알 수 있다.
     */
    REFLECT( Category = "Cinematics", DisplayName = "Sequence Appearance", Tooltip = "Routes the sequence player's appearance items to the target object's character appearance preset" )
    class SequenceAppearanceComponent : public Component
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
        void handleSequenceEvent( const SequenceFiredEvent& event );
        /**
         * @brief 외형 항목 하나를 @p manager 의 씬에 적용합니다(대상 오브젝트의 외형 컴포넌트에 프리셋을 줍니다). 컴포넌트를 거치지 않고 테스트가 부릅니다.
         * @return 외형 항목이 아니거나 대상 · 외형 컴포넌트를 찾지 못하면(오류를 남깁니다) false 입니다.
         */
        [[nodiscard]] static bool applyAppearanceItem( GameObjectManager& manager, const SequenceFiredEvent& event );

    private:
        Subscription _eventSubscription; ///< 시퀀스 플레이어의 이벤트 구독. 플레이어가 먼저 사라지면 무효가 된다
    };

    SequenceAppearanceComponent::SequenceAppearanceComponent()
        : _eventSubscription{}
    {
    }

    void SequenceAppearanceComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject*              pOwner  = getOwner();
        SequencePlayerComponent* pPlayer = pOwner != nullptr ? pOwner->getComponent<SequencePlayerComponent>() : nullptr;
        if ( pPlayer == nullptr )
        {
            SW_LOG_WARNING_ID( "SequenceAppearance.SequenceAppearanceNoSequencePlayerComponent", "Sequence appearance on '%#' has no SequencePlayerComponent next to it - it hears nothing", pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            return;
        }
        _eventSubscription = pPlayer->registerSequenceEvent(
            SW_DELEGATE_METHOD( OnSequenceEventDelegate, &SequenceAppearanceComponent::handleSequenceEvent, this ) );
    }

    void SequenceAppearanceComponent::onEndPlay()
    {
        _eventSubscription.reset();
        Component::onEndPlay();
    }

    void SequenceAppearanceComponent::handleSequenceEvent( const SequenceFiredEvent& event )
    {
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            (void)applyAppearanceItem( *pManager, event ); // 실패는 안에서 오류로 남겼다
    }

    bool SequenceAppearanceComponent::applyAppearanceItem( GameObjectManager& manager, const SequenceFiredEvent& event )
    {
        const SequenceTrackItem& item = event._item;
        if ( item._kind != SequenceItemKind::Appearance )
            return false;
        GameObject*                   pTarget     = manager.resolveGameObject( event._target );
        CharacterAppearanceComponent* pAppearance = pTarget != nullptr ? pTarget->getComponent<CharacterAppearanceComponent>() : nullptr;
        if ( pAppearance == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceAppearance.SequenceItemWantsPreset", "Sequence item '%#' wants preset '%#' on binding %# but the object %#", item._name.c_str(), item._preset.c_str(), item._binding,
                             pTarget == nullptr ? "is not bound or no longer exists" : "has no CharacterAppearanceComponent" );
            return false;
        }
        pAppearance->setPreset( hashed_string{ item._preset.c_str() }, static_cast<uint32>( pAppearance->getSeed() ) );
        return true;
    }
} // namespace sw

#include "SequenceAppearanceComponent.cpp.gen.inl"

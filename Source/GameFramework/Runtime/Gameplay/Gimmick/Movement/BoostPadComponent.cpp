#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Movement/BoostPadComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"

#include "GameFramework/Runtime/Character/Movement/IBoostReceiver.h"

namespace sw
{
    namespace
    {
        struct BoostPadComponentInternal
        {
            static bool canAccept( const OverlapInfo& overlap, const TagContainer& requiredTags )
            {
                return overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                       ( requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( requiredTags ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BoostPadComponent::BoostPadComponent()
        : _duration{ 1.0f }
        , _requiredTags{}
        , _boostCount{ 0 }
    {
    }

    void BoostPadComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&BoostPadComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void BoostPadComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        if ( BoostPadComponentInternal::canAccept( overlap, _requiredTags ) )
            boost( *overlap._pOther );
    }

    void BoostPadComponent::boost( GameObject& target )
    {
        // 받는 쪽은 밟은 몸의 이동 컴포넌트 하나라 이벤트 없이 계약(`IBoostReceiver`)으로 직접 부른다. 세기는 받는 쪽 튜닝이 정한다(마리오 카트 대시 판).
        IBoostReceiver* pReceiver = target.findInterface<IBoostReceiver>();
        if ( pReceiver == nullptr )
            return; // 부스트를 받지 않는 몸(걷는 캐릭터 · 상자)은 지나간다 — 태그로 거르지 않은 패드에서는 흔한 일이라 경고하지 않는다
        if ( pReceiver->receiveBoost( _duration ) )
            ++_boostCount;
        else
            SW_LOG_WARNING_ID( "BoostPadComponent.BoostRefused", "Boost pad '%#': '%#' refused a %.2f s boost (boost disabled in its tuning)",
                               getOwner() != nullptr ? getOwner()->getName().c_str() : "", target.getName().c_str(), _duration );
    }
} // namespace sw

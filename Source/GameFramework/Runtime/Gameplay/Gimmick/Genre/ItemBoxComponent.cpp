#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Genre/ItemBoxComponent.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/Framework/GameEventUtil.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickEvents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"

namespace sw
{
    namespace
    {
        struct ItemBoxComponentInternal
        {
            struct ChoiceWeight
            {
                float32 operator()( const ItemBoxChoice& choice ) const { return choice._weight; }
            };

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
    ItemBoxComponent::ItemBoxComponent()
        : _listChoice{}
        , _requiredTags{}
        , _respawnDelay{ 3.0f }
        , _seed{ 1u }
        , _openCount{ 0 }
        , _respawnStepsLeft{ 0 }
        , _clock{}
    {
    }

    void ItemBoxComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&ItemBoxComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void ItemBoxComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        if ( ItemBoxComponentInternal::canAccept( overlap, _requiredTags ) )
            (void)open( *overlap._pOther ); // 뽑힌 이름은 쓰지 않는다. 비어 있으면 열리지 않을 뿐이다
    }

    hashed_string ItemBoxComponent::open( GameObject& target )
    {
        GameObject* pOwner      = getOwner();
        const bool  bCannotOpen = pOwner == nullptr || isAvailable() == false || _listChoice.empty();
        if ( bCannotOpen )
            return hashed_string{};
        // 시드와 연 횟수로 뽑는다. 같은 순서로 열면 같은 것이 나온다.
        GameRandom            random( HashUtil::hashCoord( _openCount, 0, _seed ) );
        const optional<int32> index = random.pickWeightedIndex( _listChoice, ItemBoxComponentInternal::ChoiceWeight{} );
        ++_openCount;
        if ( index.has_value() == false )
            return hashed_string{};
        const ItemBoxChoice& choice = _listChoice[static_cast<size_t>( *index )];
        GimmickItemEvent     event;
        event._target = target.getHandle();
        event._source = pOwner->getHandle();
        event._item   = choice._item;
        event._count  = choice._count;
        GameEventUtil::send( event );
        if ( _respawnDelay > 0.0f )
        {
            _respawnStepsLeft = _clock.secondsToSteps( _respawnDelay, 1 );
            GimmickUtil::setBodyActive( *pOwner, false, this );
        }
        return choice._item;
    }

    void ItemBoxComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = _clock.consume( context.getDeltaTime() );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepOnce();
        }
    }

    void ItemBoxComponent::stepOnce()
    {
        if ( _respawnStepsLeft <= 0 )
            return;
        --_respawnStepsLeft;
        GameObject* pOwner = getOwner();
        if ( _respawnStepsLeft == 0 && pOwner != nullptr )
            GimmickUtil::setBodyActive( *pOwner, true, this );
    }
} // namespace sw

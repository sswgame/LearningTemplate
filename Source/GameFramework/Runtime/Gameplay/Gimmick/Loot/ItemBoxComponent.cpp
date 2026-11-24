#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/ItemBoxComponent.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/Tick/TickInterval.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
#include "GameFramework/Runtime/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/IItemReceiver.h"

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

            static bool hasTags( const GameObject& target, const TagContainer& requiredTags )
            {
                return requiredTags.getTagCount() == 0 || target.getTags().hasAllTags( requiredTags );
            }

            static bool canAccept( const OverlapInfo& overlap, const TagContainer& requiredTags )
            {
                // 받을 가방이 없는 몸(발판 · 통 · 탄)은 닿아도 줄 대상이 아니다 — 조용히 건너뛴다(언리얼 줍기 아이템이 폰만 받는 자리, docs/agents/Gameplay.md 지역 효과 예외).
                return overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE && hasTags( *overlap._pOther, requiredTags ) &&
                       IItemReceiver::findForTarget( *overlap._pOther ) != nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ItemBoxComponent::ItemBoxComponent()
        : _openBy{ ItemBoxOpenBy::Touch }
        , _maxOpenCount{ 0 }
        , _bHideWhenOpened{ true }
        , _listChoice{}
        , _requiredTags{}
        , _respawnDelay{ 3.0f }
        , _seed{ 1u }
        , _openCount{ 0 }
        , _respawnStepsLeft{ 0 }
    {
    }

    void ItemBoxComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        _overlapSubscription = GameObjectEvents::listen<&ItemBoxComponent::handleOverlapBegin>( *this, &GameObjectEvents::_overlapBegin );
    }

    void ItemBoxComponent::handleOverlapBegin( const OverlapInfo& overlap )
    {
        if ( _openBy == ItemBoxOpenBy::Touch && ItemBoxComponentInternal::canAccept( overlap, _requiredTags ) )
            (void)open( *overlap._pOther ); // 뽑힌 이름은 쓰지 않는다. 비어 있으면 열리지 않을 뿐이다
    }

    void ItemBoxComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        if ( _openBy != ItemBoxOpenBy::Use )
            return;
        _usedSubscription = InteractableComponent::listenCompleted<&ItemBoxComponent::handleUsed>( *this );
        if ( _usedSubscription.isActive() == false )
            SW_LOG_ERROR_ID( "ItemBox.UseNeedsInteractable", "Item box '%#' opens by use but has no InteractableComponent - it never opens", getOwner() != nullptr ? getOwner()->getName().c_str() : "" );
    }

    void ItemBoxComponent::handleUsed( const GameObject& user )
    {
        if ( ItemBoxComponentInternal::hasTags( user, _requiredTags ) )
            (void)open( user ); // 못 주면 giveItem 이 경고를 남긴다
    }

    hashed_string ItemBoxComponent::open( const GameObject& target )
    {
        GameObject* pOwner      = getOwner();
        const bool  bCannotOpen = pOwner == nullptr || isAvailable() == false || _listChoice.empty();
        if ( bCannotOpen )
            return hashed_string{};
        // 시드와 연 횟수로 뽑는다. 같은 순서로 열면 같은 것이 나온다.
        GameRandom            random( HashUtil::hashCoord( _openCount, 0, _seed ) );
        const optional<int32> index = random.pickWeightedIndex( _listChoice, ItemBoxComponentInternal::ChoiceWeight{} );
        if ( index.has_value() == false )
        {
            ++_openCount;
            return hashed_string{};
        }
        const ItemBoxChoice& choice = _listChoice[static_cast<size_t>( *index )];
        // 받는 쪽이 없거나 가방이 차면 열지 않는다 — 아이템을 버리지 않고, 연 횟수도 그대로라 다음에 같은 것이 나온다(경고는 giveItem 이 남긴다).
        if ( GimmickUtil::giveItem( *pOwner, target, choice._item, choice._count ) == false )
            return hashed_string{};
        ++_openCount;
        const bool bSpent = _maxOpenCount > 0 && _openCount >= _maxOpenCount;
        if ( bSpent == false && _respawnDelay > 0.0f )
            _respawnStepsLeft = TickIntervalUtil::secondsToCount( kTickInterval, _respawnDelay, 1 );
        if ( isAvailable() )
            return choice._item; // 늘 있는 상자(낚시터)
        if ( _bHideWhenOpened )
            GimmickUtil::setBodyActive( *pOwner, false, this );
        GimmickUtil::setInteractableEnabled( *pOwner, false );
        return choice._item;
    }

    void ItemBoxComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        const int32 stepCount = context.getIntervalCount();
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
        if ( _respawnStepsLeft > 0 || pOwner == nullptr )
            return;
        GimmickUtil::setBodyActive( *pOwner, true, this );
        GimmickUtil::setInteractableEnabled( *pOwner, true );
    }
} // namespace sw

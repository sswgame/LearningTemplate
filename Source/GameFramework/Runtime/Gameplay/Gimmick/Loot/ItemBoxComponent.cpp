#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/Loot/ItemBoxComponent.h"

#include "Core/Hash/HashUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectEvents.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickUtil.h"
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

            static bool canAccept( const OverlapInfo& overlap, const TagContainer& requiredTags )
            {
                // 받을 가방이 없는 몸(발판 · 통 · 탄)은 닿아도 줄 대상이 아니다 — 조용히 건너뛴다(언리얼 줍기 아이템이 폰만 받는 자리, docs/agents/Gameplay.md 지역 효과 예외).
                return overlap._pOther != nullptr && overlap._bOtherTrigger == SW_FALSE &&
                       ( requiredTags.getTagCount() == 0 || overlap._pOther->getTags().hasAllTags( requiredTags ) ) &&
                       IItemReceiver::findForTarget( *overlap._pOther ) != nullptr;
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

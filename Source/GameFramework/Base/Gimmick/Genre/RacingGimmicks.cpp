#include "pch.h"

#include "GameFramework/Base/Gimmick/Genre/RacingGimmicks.h"

#include "Engine/Object/GameObject/GameObject.h"

#include "GameFramework/Base/Framework/GameEventUtil.h"
#include "GameFramework/Base/Gimmick/Genre/GenreGimmickUtil.h"
#include "GameFramework/Base/Utility/GameRandom.h"

namespace sw
{
    namespace
    {
        struct RacingGimmicksInternal
        {
            struct ChoiceWeight
            {
                float32 operator()( const GimmickItemChoice& choice ) const { return choice._weight; }
            };

            static bool accepts( const OverlapInfo& overlap, const TagContainer& requiredTags )
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
        , _strength{ 1.5f }
        , _requiredTags{}
        , _boostCount{ 0 }
    {
    }

    void BoostPadComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        if ( RacingGimmicksInternal::accepts( overlap, _requiredTags ) == false )
            return;
        ++_boostCount;
        GimmickBoostEvent event;
        event._target   = overlap._pOther->getHandle();
        event._duration = _duration;
        event._strength = _strength;
        GameEventUtil::send( event );
    }

    ItemBoxComponent::ItemBoxComponent()
        : _listChoice{}
        , _requiredTags{}
        , _respawnDelay{ 3.0f }
        , _seed{ 1u }
        , _openCount{ 0 }
        , _respawnStepsLeft{ 0 }
        , _clock{ GenreGimmickUtil::makeClock() }
    {
    }

    void ItemBoxComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        if ( RacingGimmicksInternal::accepts( overlap, _requiredTags ) )
            (void)open( *overlap._pOther ); // 뽑힌 이름은 쓰지 않는다 — 비어 있으면 열리지 않을 뿐이다
    }

    hashed_string ItemBoxComponent::open( GameObject& target )
    {
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || isAvailable() == false || _listChoice.empty() )
            return hashed_string{};
        // 씨앗 + 연 횟수로 뽑는다 — 같은 순서로 열면 같은 것이 나온다.
        GameRandom  random( GameHash::hashCoord( _openCount, 0, _seed ) );
        const int32 index = random.pickWeightedIndex( _listChoice, RacingGimmicksInternal::ChoiceWeight{} );
        ++_openCount;
        if ( index < 0 )
            return hashed_string{};
        const GimmickItemChoice& choice = _listChoice[static_cast<size_t>( index )];
        GimmickItemEvent         event;
        event._target = target.getHandle();
        event._source = pOwner->getHandle();
        event._item   = choice._item;
        event._count  = choice._count;
        GameEventUtil::send( event );
        if ( _respawnDelay > 0.0f )
        {
            _respawnStepsLeft = GenreGimmickUtil::toSteps( _respawnDelay, 1 );
            GenreGimmickUtil::setBodyActive( *pOwner, false, this );
        }
        return choice._item;
    }

    void ItemBoxComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const int32 stepCount = _clock.consume( deltaTime );
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
            GenreGimmickUtil::setBodyActive( *pOwner, true, this );
    }
} // namespace sw

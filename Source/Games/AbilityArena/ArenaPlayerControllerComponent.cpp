#include "pch.h"

#include "Games/AbilityArena/ArenaPlayerControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"

#include "GameFramework/Base/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Ability/CombatAttributeSet.h"
#include "GameFramework/Base/Framework/GameService.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct ArenaPlayerControllerComponentInternal
        {
            /** @brief 입력 맵 액션 하나 → 어빌리티 입력 번호입니다(키는 `data/arena.input.xml`). */
            struct ActionBinding
            {
                const utf8* _pAction;
                int32       _inputId;
            };

            static constexpr const utf8*   kMoveAction         = "Arena.Move";
            static constexpr ActionBinding kArrActionBinding[] = {
                {   "Arena.Melee",    ArenaDirectorComponent::kInputMelee},
                {"Arena.Fireball", ArenaDirectorComponent::kInputFireball},
                {    "Arena.Heal",     ArenaDirectorComponent::kInputHeal},
                {    "Arena.Dash",     ArenaDirectorComponent::kInputDash},
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    ArenaPlayerControllerComponent::ArenaPlayerControllerComponent()
        : _crowdRadius{ 2.5f }
        , _crowdCount{ 3 }
        , _meleeRange{ 2.0f }
    {
    }

    void ArenaPlayerControllerComponent::tickController( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem,
                                                         float3& inoutPosition )
    {
        const InputManager* pInput = game::getService<InputManager>();
        if ( director.isAutoPlayOn() || pInput == nullptr )
        {
            tickAutoPlay( deltaTime, director, abilitySystem, inoutPosition );
            return;
        }
        tickInput( deltaTime, *pInput, abilitySystem, inoutPosition );
    }

    void ArenaPlayerControllerComponent::tickInput( float32 deltaTime, const InputManager& input, AbilitySystemComponent& abilitySystem, float3& inoutPosition )
    {
        // 입력 → 어빌리티 입력 번호. 눌림 · 뗌을 그대로 넘긴다(차지 · 콤보 어빌리티가 뗌을 받는다).
        const InputMap& inputMap = input.getInputMap();
        for ( const ArenaPlayerControllerComponentInternal::ActionBinding& binding : ArenaPlayerControllerComponentInternal::kArrActionBinding )
        {
            const hashed_string action( binding._pAction );
            if ( inputMap.wasActionPressed( action ) )
                abilitySystem.abilityInputPressed( binding._inputId );
            if ( inputMap.wasActionReleased( action ) )
                abilitySystem.abilityInputReleased( binding._inputId );
        }

        const float2 move = inputMap.getVector2D( hashed_string( ArenaPlayerControllerComponentInternal::kMoveAction ) );
        float3       direction{ move._x, 0.0f, move._y };

        if ( abilitySystem.hasMatchingTag( "State.Dashing"_tag ) )
            direction = getFacing();
        if ( direction.getLengthSquared() > 0.0f )
            moveTowards( abilitySystem, direction, deltaTime, inoutPosition );
    }

    void ArenaPlayerControllerComponent::tickAutoPlay( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem,
                                                       float3& inoutPosition )
    {
        const float32 healthRatio = abilitySystem.getAttributeValue( CombatAttributes::health() ) /
                                    MathUtil::max( 1.0f, abilitySystem.getAttributeValue( CombatAttributes::maxHealth() ) );
        if ( healthRatio < 0.5f )
            tapInput( abilitySystem, ArenaDirectorComponent::kInputHeal );

        const GameObjectHandle self    = getOwner()->getHandle();
        const ArenaUnitView*   pTarget = director.findNearestHostileView( self, 100.0f );
        if ( pTarget == nullptr )
            return;
        const float3  toTarget = pTarget->_position - inoutPosition;
        const float32 distance = toTarget.getLength();
        setFacing( flattenDirection( toTarget, getFacing() ) );

        // 같은 그룹의 다른 유닛과 함께 읽는다 — 첨자 대신 포인터로.
        const vector<ArenaUnitView>& listView         = director.getUnitViews();
        const ArenaUnitView*         pView            = listView.data();
        uint32                       nearbyEnemyCount = 0;
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& view    = pView[viewIndex];
            const bool           bNearby = view._kind != ArenaUnitKind::Player && view._bAlive == SW_TRUE &&
                                 float3::getDistance( view._position, inoutPosition ) < _crowdRadius;
            if ( bNearby )
                ++nearbyEnemyCount;
        }
        if ( nearbyEnemyCount >= _crowdCount )
        {
            setFacing( float3{ 0.0f, 0.0f, 0.0f } - getFacing() );
            tapInput( abilitySystem, ArenaDirectorComponent::kInputDash );
        }

        if ( abilitySystem.hasMatchingTag( "State.Dashing"_tag ) )
        {
            moveTowards( abilitySystem, getFacing(), deltaTime, inoutPosition );
            return;
        }
        if ( distance > _meleeRange )
        {
            tapInput( abilitySystem, ArenaDirectorComponent::kInputFireball );
            moveTowards( abilitySystem, toTarget, deltaTime, inoutPosition );
        }
        else
        {
            tapInput( abilitySystem, ArenaDirectorComponent::kInputMelee );
        }
    }
} // namespace sw

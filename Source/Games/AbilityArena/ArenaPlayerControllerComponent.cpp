#include "pch.h"

#include "Games/AbilityArena/ArenaPlayerControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Input/InputManager.h"

#include "GameFramework/Ability/AbilitySystemComponent.h"
#include "GameFramework/Ability/CombatAttributeSet.h"
#include "GameFramework/Framework/GameService.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    namespace
    {
        struct ArenaPlayerControllerComponentInternal
        {
            /** @brief 키 하나 → 입력 번호입니다. 같은 번호에 키가 둘이면 어느 쪽이든 누르면 눌림이다. */
            struct KeyBinding
            {
                Key   _key;
                int32 _inputId;
            };

            static constexpr KeyBinding kArrKeyBinding[] = {
                {        Key::J,    ArenaDirectorComponent::kInputMelee},
                {    Key::Space,    ArenaDirectorComponent::kInputMelee},
                {        Key::K, ArenaDirectorComponent::kInputFireball},
                {   Key::Digit2, ArenaDirectorComponent::kInputFireball},
                {        Key::L,     ArenaDirectorComponent::kInputHeal},
                {   Key::Digit3,     ArenaDirectorComponent::kInputHeal},
                {Key::LeftShift,     ArenaDirectorComponent::kInputDash},
                {   Key::Digit4,     ArenaDirectorComponent::kInputDash},
            };

            static constexpr float32 kCrowdRadius = 2.5f; ///< 자동 전투가 "둘러싸였다" 고 보는 거리(m)
            static constexpr uint32  kCrowdCount  = 3;
            static constexpr float32 kMeleeRange  = 2.0f; ///< 자동 전투가 근접으로 바꾸는 거리(m)
        };
    } // namespace
} // namespace sw

namespace sw
{
    ArenaPlayerControllerComponent::ArenaPlayerControllerComponent() = default;

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
        for ( const ArenaPlayerControllerComponentInternal::KeyBinding& binding : ArenaPlayerControllerComponentInternal::kArrKeyBinding )
        {
            if ( input.wasKeyPressed( binding._key ) )
                abilitySystem.abilityInputPressed( binding._inputId );
            if ( input.wasKeyReleased( binding._key ) )
                abilitySystem.abilityInputReleased( binding._inputId );
        }

        float3 direction{ 0.0f, 0.0f, 0.0f };
        if ( input.isKeyDown( Key::W ) || input.isKeyDown( Key::Up ) )
            direction._z += 1.0f;
        if ( input.isKeyDown( Key::S ) || input.isKeyDown( Key::Down ) )
            direction._z -= 1.0f;
        if ( input.isKeyDown( Key::D ) || input.isKeyDown( Key::Right ) )
            direction._x += 1.0f;
        if ( input.isKeyDown( Key::A ) || input.isKeyDown( Key::Left ) )
            direction._x -= 1.0f;

        if ( abilitySystem.hasMatchingTag( "State.Dashing"_tag ) )
            direction = getFacing();
        if ( direction.getLengthSquared() > 0.0f )
            moveTowards( abilitySystem, direction, deltaTime, inoutPosition );
    }

    void ArenaPlayerControllerComponent::tickAutoPlay( float32 deltaTime, const ArenaDirectorComponent& director, AbilitySystemComponent& abilitySystem,
                                                       float3& inoutPosition )
    {
        using Internal            = ArenaPlayerControllerComponentInternal;
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
                                 float3::getDistance( view._position, inoutPosition ) < Internal::kCrowdRadius;
            if ( bNearby )
                ++nearbyEnemyCount;
        }
        if ( nearbyEnemyCount >= Internal::kCrowdCount )
        {
            setFacing( float3{ 0.0f, 0.0f, 0.0f } - getFacing() );
            tapInput( abilitySystem, ArenaDirectorComponent::kInputDash );
        }

        if ( abilitySystem.hasMatchingTag( "State.Dashing"_tag ) )
        {
            moveTowards( abilitySystem, getFacing(), deltaTime, inoutPosition );
            return;
        }
        if ( distance > Internal::kMeleeRange )
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

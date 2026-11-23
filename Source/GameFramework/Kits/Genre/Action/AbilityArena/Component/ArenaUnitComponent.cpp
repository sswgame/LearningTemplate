#include "pch.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaUnitComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"
#include "Engine/Object/GameObject/WorldOrigin.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilitySystemComponent.h"
#include "GameFramework/Runtime/Gameplay/Ability/CombatAttributeSet.h"
#include "GameFramework/Runtime/Gameplay/Ability/MovementConstraint.h"

namespace sw
{
    ArenaUnitComponent::ArenaUnitComponent()
        : _kind{ ArenaUnitKind::Grunt }
        , _facingMode{ PawnFacingMode::ControlYaw }
        , _unitRadius{ 0.5f }
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _deathRemainRatio{ 1.0f }
        , _arrButtonIndex{}
        , _previousButtonDown{ 0 }
        , _bButtonsResolved{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    void ArenaUnitComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bButtonsResolved   = SW_FALSE;
        _previousButtonDown = 0;
    }

    SpawnStateReset ArenaUnitComponent::resetToSpawnState()
    {
        _deathRemainRatio   = 1.0f;
        _previousButtonDown = 0;
        return SpawnStateReset::Reset;
    }

    void ArenaUnitComponent::assignGameMode( GameObjectHandle gameMode, const float3& facing )
    {
        bindGameMode( gameMode );
        _facing = facing;
    }

    float3 ArenaUnitComponent::flattenDirection( const float3& direction, const float3& fallback )
    {
        const float3  flat   = float3{ direction._x, 0.0f, direction._z };
        const float32 length = flat.getLength();
        if ( length < MathUtil::kLooseEpsilon )
            return fallback;
        return float3{ flat._x / length, 0.0f, flat._z / length };
    }

    void ArenaUnitComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        GameObject*             pOwner             = getOwner();
        MeshComponent*          pMesh              = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent* pAbilitySystem     = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const PawnComponent*    pPawn              = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        const bool              bMissingComponents = pMesh == nullptr || pAbilitySystem == nullptr || pPawn == nullptr || context.getDeltaTime() <= 0.0f;
        if ( bMissingComponents )
            return;
        const ArenaGameModeComponent* pGameMode = findGameMode<ArenaGameModeComponent>();
        if ( pGameMode == nullptr )
            return;
        if ( _deathRemainRatio < 1.0f || pAbilitySystem->getOwnedTagCounts().hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
        {
            _deathRemainRatio = MathUtil::max( 0.0f, _deathRemainRatio - context.getDeltaTime() / pGameMode->getDeathLifeSpan() ); // 같은 시간 뒤 디렉터가 걷는다
            return;
        }

        // (1) 바라보는 쪽 — 어빌리티(투사체 방향)가 이번 틱의 쪽을 보게 버튼보다 먼저.
        const ControlIntent& intent = pPawn->getIntent();
        const float3         move   = flattenDirection( intent.computeWorldMove(), float3{} );
        if ( _facingMode == PawnFacingMode::ControlYaw )
            _facing = float3{ MathUtil::sin( intent._controlYaw ), 0.0f, MathUtil::cos( intent._controlYaw ) };
        else if ( move.getLengthSquared() > 0.0f )
            _facing = move;

        // (2) 버튼 → 어빌리티 입력 번호.
        pressAbilityButtons( *pPawn, *pAbilitySystem );

        // (3) 이동 — 이동 속도는 어트리뷰트의 current 다(대시 ×3 · 둔화 이펙트가 그대로 반영된다).
        // 경계 · 유닛 스냅샷은 절대 자리다 — 월드 원점이 옮겨져도 아레나가 따라 밀리지 않는다.
        const WorldOrigin& worldOrigin = pOwner->getManager()->getWorldOrigin();
        float3             position    = worldOrigin.toAbsolute( pMesh->getLocalPosition() );
        const float3       direction   = pAbilitySystem->getOwnedTagCounts().hasMatchingTag( "State.Dashing"_tag ) ? _facing : move;
        // 이동 속도 어트리뷰트가 없는 유닛은 움직이지 않는다(0 을 지어내지 않고 그 뜻을 여기서 정한다).
        const optional<float32> speed = pAbilitySystem->getAttributeValue( CombatAttributes::moveSpeed() );
        // 속박 · 기절 · 둔화 배율은 공용 이동기와 같은 계약으로 읽는다(`MovementConstraint`).
        const MovementConstraint constraint = MovementConstraint::read( *pOwner );
        if ( direction.getLengthSquared() > 0.0f && speed.has_value() && constraint._bMoveBlocked == SW_FALSE )
            position = position + direction * ( *speed * constraint._speedScale * context.getDeltaTime() );
        if ( _kind != ArenaUnitKind::Player )
            separateFromEnemies( *pGameMode, position );
        const float32 halfSize = pGameMode->getArenaHalfSize();
        position._x            = MathUtil::clamp( position._x, -halfSize, halfSize );
        position._z            = MathUtil::clamp( position._z, -halfSize, halfSize );
        pMesh->setLocalPosition( worldOrigin.toLocal( position ) );
        // 모델의 앞(+Z)을 바라보는 쪽으로 — AI 는 움직이지 않고도 몸을 돌린다.
        pMesh->setLocalRotation( float3{ 0.0f, MathUtil::atan2( _facing._x, _facing._z ), 0.0f } );
    }

    void ArenaUnitComponent::pressAbilityButtons( const PawnComponent& pawn, AbilitySystemComponent& abilitySystem )
    {
        if ( _bButtonsResolved == SW_FALSE )
        {
            for ( int32 buttonIndex = 0; buttonIndex < kAbilityButtonCount; ++buttonIndex )
            {
                _arrButtonIndex[buttonIndex] = pawn.findButton( hashed_string( kArrAbilityButton[buttonIndex]._pName ) );
            }
            _bButtonsResolved = SW_TRUE;
        }
        // 발동은 누름, 지난 틱에 누르고 있던 버튼을 놓으면 뗌(차지 · 콤보 어빌리티가 뗌을 받는다). AI 의 한 번 누름은 다음 틱에 뗌이 된다.
        const ControlIntent& intent = pawn.getIntent();
        for ( int32 buttonIndex = 0; buttonIndex < kAbilityButtonCount; ++buttonIndex )
        {
            const optional<int32> slot    = _arrButtonIndex[buttonIndex];
            const int32           inputID = kArrAbilityButton[buttonIndex]._inputID;
            if ( ControlIntent::isInRange( slot ) == false )
                continue;
            const bool bWasDown = ( _previousButtonDown & ( 1u << *slot ) ) != 0;
            if ( intent.wasTriggered( slot ) )
                abilitySystem.abilityInputPressed( inputID );
            if ( bWasDown && intent.isDown( slot ) == false )
                abilitySystem.abilityInputReleased( inputID );
        }
        _previousButtonDown = intent._buttonDown;
    }

    void ArenaUnitComponent::separateFromEnemies( const ArenaGameModeComponent& gameMode, float3& inoutPosition ) const
    {
        const GameObjectHandle       self     = getOwner()->getHandle();
        const vector<ArenaUnitView>& listView = gameMode.getUnitViews();
        const ArenaUnitView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& other = pView[viewIndex];
            if ( other._object == self || other._kind == ArenaUnitKind::Player )
                continue;
            const float3  apart    = inoutPosition - other._position;
            const float32 distance = apart.getLength();
            const float32 overlap  = _unitRadius * 2.0f - distance;
            if ( overlap <= 0.0f || distance < MathUtil::kLooseEpsilon )
                continue;
            inoutPosition = inoutPosition + apart * ( 0.5f * overlap / distance );
        }
    }
} // namespace sw

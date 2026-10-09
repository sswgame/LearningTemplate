#include "pch.h"

#include "Games/AbilityArena/ArenaUnitComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Ability/AbilitySystemComponent.h"
#include "GameFramework/Base/Ability/CombatAttributeSet.h"
#include "GameFramework/Base/Control/ControlIntent.h"
#include "GameFramework/Base/Control/PawnComponent.h"

#include "Games/AbilityArena/ArenaDirectorComponent.h"

namespace sw
{
    ArenaUnitComponent::ArenaUnitComponent()
        : _director{}
        , _kind{ ArenaUnitKind::Grunt }
        , _facingMode{ PawnFacingMode::ControlYaw }
        , _unitRadius{ 0.5f }
        , _facing{ 0.0f, 0.0f, 1.0f }
        , _deathScale{ 1.0f, 1.0f, 1.0f }
        , _deathTimer{ -1.0f }
        , _arrButtonIndex{ -1, -1, -1, -1 }
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

    void ArenaUnitComponent::assignDirector( GameObjectHandle director, const float3& facing )
    {
        _director = director;
        _facing   = facing;
    }

    float3 ArenaUnitComponent::flattenDirection( const float3& direction, const float3& fallback )
    {
        const float3  flat   = float3{ direction._x, 0.0f, direction._z };
        const float32 length = flat.getLength();
        if ( length < 1.0e-4f )
            return fallback;
        return float3{ flat._x / length, 0.0f, flat._z / length };
    }

    void ArenaUnitComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*             pOwner         = getOwner();
        GameObjectManager*      pManager       = pOwner != nullptr ? pOwner->getManager() : nullptr;
        MeshComponent*          pMesh          = pOwner != nullptr ? pOwner->getComponent<MeshComponent>() : nullptr;
        AbilitySystemComponent* pAbilitySystem = pOwner != nullptr ? pOwner->getComponent<AbilitySystemComponent>() : nullptr;
        const PawnComponent*    pPawn          = pOwner != nullptr ? pOwner->getComponent<PawnComponent>() : nullptr;
        if ( pManager == nullptr || pMesh == nullptr || pAbilitySystem == nullptr || pPawn == nullptr || deltaTime <= 0.0f )
            return;
        if ( _deathTimer >= 0.0f || pAbilitySystem->hasMatchingTag( CombatAttributeSet::getDeadTag() ) )
        {
            tickDeath( deltaTime, *pMesh );
            return;
        }
        const ArenaDirectorComponent* pDirector = GameDirectorComponent::resolve<ArenaDirectorComponent>( *pManager, _director );
        if ( pDirector == nullptr )
            return;

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
        float3       position  = pMesh->getLocalPosition();
        const float3 direction = pAbilitySystem->hasMatchingTag( "State.Dashing"_tag ) ? _facing : move;
        if ( direction.getLengthSquared() > 0.0f )
        {
            const float32 speed = pAbilitySystem->getAttributeValue( CombatAttributes::moveSpeed() );
            position            = position + direction * ( speed * deltaTime );
        }
        if ( _kind != ArenaUnitKind::Player )
            separateFromEnemies( *pDirector, position );
        const float32 halfSize = pDirector->getArenaHalfSize();
        position._x            = MathUtil::clamp( position._x, -halfSize, halfSize );
        position._z            = MathUtil::clamp( position._z, -halfSize, halfSize );
        pMesh->setLocalPosition( position );
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
            const int32 slot    = _arrButtonIndex[buttonIndex];
            const int32 inputId = kArrAbilityButton[buttonIndex]._inputId;
            if ( slot < 0 )
                continue;
            const bool bWasDown = ( _previousButtonDown & ( 1u << slot ) ) != 0;
            if ( intent.wasTriggered( slot ) )
                abilitySystem.abilityInputPressed( inputId );
            if ( bWasDown && intent.isDown( slot ) == false )
                abilitySystem.abilityInputReleased( inputId );
        }
        _previousButtonDown = intent._buttonDown;
    }

    void ArenaUnitComponent::separateFromEnemies( const ArenaDirectorComponent& director, float3& inoutPosition ) const
    {
        const GameObjectHandle       self     = getOwner()->getHandle();
        const vector<ArenaUnitView>& listView = director.getUnitViews();
        const ArenaUnitView*         pView    = listView.data();
        for ( size_t viewIndex = 0; viewIndex < listView.size(); ++viewIndex )
        {
            const ArenaUnitView& other = pView[viewIndex];
            if ( other._object == self || other._kind == ArenaUnitKind::Player )
                continue;
            const float3  apart    = inoutPosition - other._position;
            const float32 distance = apart.getLength();
            const float32 overlap  = _unitRadius * 2.0f - distance;
            if ( overlap <= 0.0f || distance < 1.0e-4f )
                continue;
            inoutPosition = inoutPosition + apart * ( 0.5f * overlap / distance );
        }
    }

    void ArenaUnitComponent::tickDeath( float32 deltaTime, MeshComponent& mesh )
    {
        // 쓰러진 유닛은 납작해지다가 디렉터가 걷는다(같은 시간 — `ArenaDirectorComponent::kDeathLinger`).
        if ( _deathTimer < 0.0f )
        {
            _deathTimer = ArenaDirectorComponent::kDeathLinger;
            _deathScale = mesh.getLocalScale();
        }
        _deathTimer          = MathUtil::max( 0.0f, _deathTimer - deltaTime );
        const float32 squash = MathUtil::max( 0.05f, _deathTimer / ArenaDirectorComponent::kDeathLinger );
        mesh.setLocalScale( float3{ _deathScale._x, _deathScale._y * squash, _deathScale._z } );
    }
} // namespace sw

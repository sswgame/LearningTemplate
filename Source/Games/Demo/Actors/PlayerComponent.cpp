#include "pch.h"

#include "Games/Demo/Actors/PlayerComponent.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/TagSystem.h"

#include "RuntimeAPI/GameService.h"

namespace sw
{
	void PlayerComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Player"_tag );

		dashCount	  = maxDashCount;
		dashTimer	  = 0.0f;
		dashCoolTimer = 0.0f;
		currentState  = PlayerMoveState::Idle;
	}

	void PlayerComponent::onEndPlay()
	{
	}

	void PlayerComponent::onTick( float32 deltaTime )
	{
		if ( dashTimer > 0.0f )
		{
			dashTimer -= deltaTime;
			if ( dashTimer <= 0.0f )
			{
				dashTimer	 = 0.0f;
				currentState = PlayerMoveState::Idle;
			}
		}

		if ( dashCount < maxDashCount )
		{
			dashCoolTimer += deltaTime;
			while ( dashCoolTimer >= dashCoolTime && dashCount < maxDashCount )
			{
				dashCoolTimer -= dashCoolTime;
				dashCount++;
			}
			if ( dashCount >= maxDashCount )
				dashCoolTimer = 0.0f;
		}

		handleInput( deltaTime );

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		float3 localPos = pSceneComp->getLocalPosition();
		if ( dashTimer > 0.0f )
			localPos._x += static_cast<float32>( moveDir ) * dashSpeed * deltaTime;
		else if ( currentState == PlayerMoveState::Run )
			localPos._x += static_cast<float32>( moveDir ) * speed * deltaTime;
		pSceneComp->setLocalPosition( localPos );
	}

	void PlayerComponent::handleInput( float32 deltaTime )
	{
		(void)deltaTime;
		InputManager& inputManager = *game::getService<InputManager>();

		if ( dashTimer > 0.0f )
			return;

		bool bMoving{ false };
		if ( inputManager.isKeyDown( Key::A ) || inputManager.isKeyDown( Key::Left ) )
		{
			moveDir = -1;
			bMoving = true;
		}
		else if ( inputManager.isKeyDown( Key::D ) || inputManager.isKeyDown( Key::Right ) )
		{
			moveDir = 1;
			bMoving = true;
		}

		if ( inputManager.wasKeyPressed( Key::Space ) )
			tryJump();

		if ( inputManager.wasKeyPressed( Key::LeftShift ) || inputManager.wasKeyPressed( Key::RightShift ) )
			tryDash();

		if ( dashTimer <= 0.0f )
			currentState = bMoving ? PlayerMoveState::Run : PlayerMoveState::Idle;
	}

	void PlayerComponent::tryDash()
	{
		if ( dashCount > 0 && dashTimer <= 0.0f )
		{
			dashCount--;
			dashTimer	 = dashTime;
			currentState = PlayerMoveState::Dash;
		}
	}

	void PlayerComponent::tryJump()
	{
		currentState = PlayerMoveState::Jump;
	}
} // namespace sw

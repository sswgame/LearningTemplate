#include "pch.h"

#include "Games/Demo/Actors/PlayerComponent.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/Component/TagSystem.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void PlayerComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::DuringPhysics );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "Player"_tag );

		_dashCount	   = _maxDashCount;
		_dashTimer	   = 0.0f;
		_dashCoolTimer = 0.0f;
		_currentState  = PlayerMoveState::Idle;
	}

	void PlayerComponent::onEndPlay()
	{
	}

	void PlayerComponent::onTick( float32 deltaTime )
	{
		if ( _dashTimer > 0.0f )
		{
			_dashTimer -= deltaTime;
			if ( _dashTimer <= 0.0f )
			{
				_dashTimer	  = 0.0f;
				_currentState = PlayerMoveState::Idle;
			}
		}

		if ( _dashCount < _maxDashCount )
		{
			_dashCoolTimer += deltaTime;
			while ( _dashCoolTimer >= _dashCoolTime && _dashCount < _maxDashCount )
			{
				_dashCoolTimer -= _dashCoolTime;
				_dashCount++;
			}
			if ( _dashCount >= _maxDashCount )
				_dashCoolTimer = 0.0f;
		}

		handleInput( deltaTime );

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		float3 localPos = pSceneComp->getLocalPosition();
		if ( _dashTimer > 0.0f )
			localPos._x += static_cast<float32>( _moveDir ) * _dashSpeed * deltaTime;
		else if ( _currentState == PlayerMoveState::Run )
			localPos._x += static_cast<float32>( _moveDir ) * _speed * deltaTime;
		pSceneComp->setLocalPosition( localPos );
	}

	void PlayerComponent::handleInput( float32 deltaTime )
	{
		(void)deltaTime;
		InputManager& inputManager = *game::getService<InputManager>();

		if ( _dashTimer > 0.0f )
			return;

		bool bMoving{ false };
		if ( inputManager.isKeyDown( Key::A ) || inputManager.isKeyDown( Key::Left ) )
		{
			_moveDir = -1;
			bMoving	 = true;
		}
		else if ( inputManager.isKeyDown( Key::D ) || inputManager.isKeyDown( Key::Right ) )
		{
			_moveDir = 1;
			bMoving	 = true;
		}

		if ( inputManager.wasKeyPressed( Key::Space ) )
			tryJump();

		if ( inputManager.wasKeyPressed( Key::LeftShift ) || inputManager.wasKeyPressed( Key::RightShift ) )
			tryDash();

		if ( _dashTimer <= 0.0f )
			_currentState = bMoving ? PlayerMoveState::Run : PlayerMoveState::Idle;
	}

	void PlayerComponent::tryDash()
	{
		if ( _dashCount > 0 && _dashTimer <= 0.0f )
		{
			_dashCount--;
			_dashTimer	  = _dashTime;
			_currentState = PlayerMoveState::Dash;
		}
	}

	void PlayerComponent::tryJump()
	{
		_currentState = PlayerMoveState::Jump;
	}
} // namespace sw

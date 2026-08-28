#include "pch.h"

#include "Games/Demo/Actors/MouseComponent.h"

#include "Engine/Input/InputManager.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void MouseComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PrePhysics );
		_bIsLeftDown  = false;
		_bIsRightDown = false;
	}

	void MouseComponent::onEndPlay()
	{
	}

	void MouseComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
		updatePosition();
	}

	void MouseComponent::updatePosition()
	{
		InputManager& inputManager = *game::getService<InputManager>();
		int32		  mousePosX{ 0 };
		int32		  mousePosY{ 0 };
		inputManager.getMousePosition( mousePosX, mousePosY );
		_mouseScreenPos._x = static_cast<float32>( mousePosX );
		_mouseScreenPos._y = static_cast<float32>( mousePosY );
		_mouseWorldPos	   = _mouseScreenPos;
		_bIsLeftDown	   = inputManager.isMouseButtonDown( MouseButton::Left );
		_bIsRightDown	   = inputManager.isMouseButtonDown( MouseButton::Right );
	}

	bool MouseComponent::isPointInside( float2 minBound, float2 maxBound ) const
	{
		const bool bWithinX = ( _mouseWorldPos._x >= minBound._x && _mouseWorldPos._x <= maxBound._x );
		const bool bWithinY = ( _mouseWorldPos._y >= minBound._y && _mouseWorldPos._y <= maxBound._y );
		return bWithinX && bWithinY;
	}
} // namespace sw

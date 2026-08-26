#include "pch.h"

#include "Games/Demo/Actors/MouseComponent.h"

#include "Engine/Input/InputManager.h"

#include "RuntimeAPI/Service/GameService.h"

namespace sw
{
	void MouseComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PrePhysics );
		bIsLeftDown	 = false;
		bIsRightDown = false;
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
		mouseScreenPos._x = static_cast<float32>( mousePosX );
		mouseScreenPos._y = static_cast<float32>( mousePosY );
		mouseWorldPos	  = mouseScreenPos;
		bIsLeftDown		  = inputManager.isMouseButtonDown( MouseButton::Left );
		bIsRightDown	  = inputManager.isMouseButtonDown( MouseButton::Right );
	}

	bool MouseComponent::isPointInside( float2 minBound, float2 maxBound ) const
	{
		const bool bWithinX = ( mouseWorldPos._x >= minBound._x && mouseWorldPos._x <= maxBound._x );
		const bool bWithinY = ( mouseWorldPos._y >= minBound._y && mouseWorldPos._y <= maxBound._y );
		return bWithinX && bWithinY;
	}
} // namespace sw

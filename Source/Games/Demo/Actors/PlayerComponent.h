#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	ENUM()
	enum class PlayerMoveState : uint8
	{
		Idle = 0,
		Run,
		Dash,
		Jump
	};

	REFLECT()
	class PlayerComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float32 _speed{ 0.0f };

		PROPERTY()
		float32 _jumpPower{ 0.0f };

		PROPERTY()
		float32 _dashSpeed{ 0.0f };

		PROPERTY()
		float32 _dashTime{ 0.0f };

		PROPERTY()
		float32 _dashTimer{ 0.0f };

		PROPERTY()
		float32 _dashCoolTime{ 0.0f };

		PROPERTY()
		float32 _dashCoolTimer{ 0.0f };

		PROPERTY()
		int32 _dashCount{ 0 };

		PROPERTY()
		int32 _maxDashCount{ 0 };

		PROPERTY()
		int32 _moveDir{ 1 }; // 1 = right, -1 = left

		PROPERTY()
		PlayerMoveState _currentState{ PlayerMoveState::Idle };

		PROPERTY()
		string _currentCostume{ "basic_" };

		PlayerComponent()										 = default;
		virtual ~PlayerComponent() override						 = default;
		PlayerComponent( PlayerComponent&& ) noexcept			 = default;
		PlayerComponent& operator=( PlayerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void handleInput( float32 deltaTime );
		void tryDash();
		void tryJump();
	};
} // namespace sw

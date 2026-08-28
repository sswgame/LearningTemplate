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
		float32 speed{ 0.0f };

		PROPERTY()
		float32 jumpPower{ 0.0f };

		PROPERTY()
		float32 dashSpeed{ 0.0f };

		PROPERTY()
		float32 dashTime{ 0.0f };

		PROPERTY()
		float32 dashTimer{ 0.0f };

		PROPERTY()
		float32 dashCoolTime{ 0.0f };

		PROPERTY()
		float32 dashCoolTimer{ 0.0f };

		PROPERTY()
		int32 dashCount{ 0 };

		PROPERTY()
		int32 maxDashCount{ 0 };

		PROPERTY()
		int32 moveDir{ 1 }; // 1 = right, -1 = left

		PROPERTY()
		PlayerMoveState currentState{ PlayerMoveState::Idle };

		PROPERTY()
		string currentCostume{ "basic_" };

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

#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class MouseComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float2 mouseScreenPos{ 0.0f, 0.0f };

		PROPERTY()
		float2 mouseWorldPos{ 0.0f, 0.0f };

		PROPERTY()
		bool bIsLeftDown{ false };

		PROPERTY()
		bool bIsRightDown{ false };

		PROPERTY()
		bool bMouseHovered{ false };

		MouseComponent()									   = default;
		virtual ~MouseComponent() override					   = default;
		MouseComponent( MouseComponent&& ) noexcept			   = default;
		MouseComponent& operator=( MouseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void updatePosition();
		bool isPointInside( float2 minBound, float2 maxBound ) const;
	};
} // namespace sw

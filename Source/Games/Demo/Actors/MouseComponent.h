#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class MouseComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float2 _mouseScreenPos{ 0.0f, 0.0f };

		PROPERTY()
		float2 _mouseWorldPos{ 0.0f, 0.0f };

		PROPERTY()
		bool _bIsLeftDown{ false };

		PROPERTY()
		bool _bIsRightDown{ false };

		PROPERTY()
		bool _bMouseHovered{ false };

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

#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class UIManagerComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		bool _bHudVisible{ true };

		PROPERTY()
		bool _bPauseMenuOpen{ false };

		PROPERTY()
		bool _bInventoryOpen{ false };

		UIManagerComponent()										   = default;
		virtual ~UIManagerComponent() override						   = default;
		UIManagerComponent( UIManagerComponent&& ) noexcept			   = default;
		UIManagerComponent& operator=( UIManagerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void setHudVisible( bool bVisible );
	};
} // namespace sw

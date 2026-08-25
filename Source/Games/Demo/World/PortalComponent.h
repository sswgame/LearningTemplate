#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class PortalComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string targetScenePath{};

		PROPERTY()
		bool bIsOpen{ true };

		PROPERTY()
		float32 triggerRadius{ 0.0f };

		PortalComponent()										 = default;
		virtual ~PortalComponent() override						 = default;
		PortalComponent( PortalComponent&& ) noexcept			 = default;
		PortalComponent& operator=( PortalComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw

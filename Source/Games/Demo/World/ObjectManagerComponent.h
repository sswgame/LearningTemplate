#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class ObjectManagerComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 activeObjectCount{ 0 };

		PROPERTY()
		bool bInitialized{ true };

		ObjectManagerComponent()											   = default;
		virtual ~ObjectManagerComponent() override							   = default;
		ObjectManagerComponent( ObjectManagerComponent&& ) noexcept			   = default;
		ObjectManagerComponent& operator=( ObjectManagerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;
	};
} // namespace sw

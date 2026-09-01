#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	namespace generated
	{
		struct sw_DamageUIComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_GF_API DamageUIComponent : public Component
	{
		friend struct ::sw::generated::sw_DamageUIComponent_Registrar;

	public:
		REFLECT_BODY();
		DamageUIComponent();
		virtual ~DamageUIComponent() override						 = default;
		DamageUIComponent( DamageUIComponent&& ) noexcept			 = default;
		DamageUIComponent& operator=( DamageUIComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

	private:
		PROPERTY( Alias = "damageValue" )
		int32 _damageValue;
		PROPERTY( Alias = "lifeTime" )
		float32 _lifeTime;
		PROPERTY( Alias = "currentLife" )
		float32 _currentLife;
		PROPERTY( Alias = "floatSpeed" )
		float32 _floatSpeed;
		PROPERTY( Alias = "alpha" )
		float32 _alpha;
	};
} // namespace sw

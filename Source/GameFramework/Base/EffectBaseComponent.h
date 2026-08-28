#pragma once
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	namespace generated
	{
		struct sw_EffectBaseComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_GF_API EffectBaseComponent : public Component
	{
		friend struct ::sw::generated::sw_EffectBaseComponent_Registrar;

	public:
		REFLECT_BODY();
		EffectBaseComponent();
		virtual ~EffectBaseComponent() override							 = default;
		EffectBaseComponent( EffectBaseComponent&& ) noexcept			 = default;
		EffectBaseComponent& operator=( EffectBaseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		float32 getDuration() const;
		float32 getCurrentTimer() const;
		float32 getCurrentAlpha() const;
		void	setCurrentAlpha( float32 alpha );

	private:
		PROPERTY( Alias="duration" )
		float32 _duration;
		PROPERTY( Alias="currentTimer" )
		float32 _currentTimer;
		PROPERTY( Alias="currentAlpha" )
		float32 _currentAlpha;
	};
} // namespace sw

#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	namespace generated
	{
		struct sw_CameraControllerComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_GF_API CameraControllerComponent : public Component
	{
		friend struct ::sw::generated::sw_CameraControllerComponent_Registrar;

	public:
		REFLECT_BODY();
		CameraControllerComponent();
		virtual ~CameraControllerComponent() override								 = default;
		CameraControllerComponent( CameraControllerComponent&& ) noexcept			 = default;
		CameraControllerComponent& operator=( CameraControllerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void shake( float32 intensity, float32 duration );

	private:
		PROPERTY( Alias="targetPos" )
		float2 _targetPos;
		PROPERTY( Alias="currentPos" )
		float2 _currentPos;
		PROPERTY( Alias="followSpeed" )
		float32 _followSpeed;
		PROPERTY( Alias="shakeIntensity" )
		float32 _shakeIntensity;
		PROPERTY( Alias="shakeDuration" )
		float32 _shakeDuration;
		PROPERTY( Alias="shakeFrequency" )
		float32 _shakeFrequency;
	};
} // namespace sw

#pragma once
#include "Engine/Object/Component/Component.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for Camera Controller
	 */
	REFLECT()
	struct SW_GF_API CameraControllerData
	{
		REFLECT_BODY();
		PROPERTY()
		float2 targetPos{ 0.0f, 0.0f };
		PROPERTY()
		float2 currentPos{ 0.0f, 0.0f };
		PROPERTY()
		float32 followSpeed{ 0.0f };
		PROPERTY()
		float32 shakeIntensity{ 0.0f };
		PROPERTY()
		float32 shakeDuration{ 0.0f };
		PROPERTY()
		float32 shakeFrequency{ 0.0f };
	};

	REFLECT()
	class SW_GF_API CameraControllerComponent : public Component
	{
	public:
		REFLECT_BODY();
		CameraControllerComponent()													 = default;
		virtual ~CameraControllerComponent() override								 = default;
		CameraControllerComponent( CameraControllerComponent&& ) noexcept			 = default;
		CameraControllerComponent& operator=( CameraControllerComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		CameraControllerData* getControllerData() const;
		CameraControllerData* ensureControllerData();

		void shake( float32 intensity, float32 duration );
	};
} // namespace sw

#pragma once
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/EffectBaseComponent.h"

namespace sw
{
	ENUM()
	enum class VFXType : uint8
	{
		AlphaFade = 0,
		AfterImage,
		WarningBlink
	};

	REFLECT_SCRIPT()
	class VFXComponent : public EffectBaseComponent
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		VFXType vfxType{ VFXType::AlphaFade };

		PROPERTY()
		float32 blinkRate{ 0.0f };

		PROPERTY()
		float32 ghostAlpha{ 0.0f };

		VFXComponent()									   = default;
		virtual ~VFXComponent() override				   = default;
		VFXComponent( VFXComponent&& ) noexcept			   = default;
		VFXComponent& operator=( VFXComponent&& ) noexcept = default;

		void onTick( float32 deltaTime ) override;
	};
} // namespace sw

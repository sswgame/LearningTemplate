#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
	namespace generated
	{
		struct sw_HPBarBaseComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_GF_API HPBarBaseComponent : public Component
	{
		friend struct ::sw::generated::sw_HPBarBaseComponent_Registrar;

	public:
		REFLECT_BODY();
		HPBarBaseComponent();
		virtual ~HPBarBaseComponent() override						   = default;
		HPBarBaseComponent( HPBarBaseComponent&& ) noexcept			   = default;
		HPBarBaseComponent& operator=( HPBarBaseComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void setTargetRatio( float32 ratio );

	private:
		PROPERTY( Alias="hpRatio" )
		float32 _hpRatio;
		PROPERTY( Alias="remainRatio" )
		float32 _remainRatio;
		PROPERTY( Alias="targetRatio" )
		float32 _targetRatio;
		PROPERTY( Alias="lerpSpeed" )
		float32 _lerpSpeed;
		PROPERTY( Alias="offsetPos" )
		float2 _offsetPos;
		PROPERTY( Alias="bVisible" )
		bool _bVisible;
	};
} // namespace sw

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

	REFLECT( Category = "UI", DisplayName = "HP Bar Base Component", Tooltip = "Smooth lerping HP Bar floating UI component" )
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
		PROPERTY( Category = "Health", DisplayName = "HP Ratio", Tooltip = "Current displayed HP ratio (0..1)", Min = 0.0, Max = 1.0, Meta = "Slider, Units=%", Alias = "hpRatio" )
		float32 _hpRatio;
		PROPERTY( Category = "Health", DisplayName = "Remain Ratio", Tooltip = "Delayed damage trail ratio (0..1)", Min = 0.0, Max = 1.0, Meta = "Slider, Units=%", Alias = "remainRatio" )
		float32 _remainRatio;
		PROPERTY( Category = "Health", DisplayName = "Target Ratio", Tooltip = "Target HP ratio to lerp towards (0..1)", Min = 0.0, Max = 1.0, Meta = "Slider, Units=%", Alias = "targetRatio" )
		float32 _targetRatio;
		PROPERTY( Category = "Animation", DisplayName = "Lerp Speed", Tooltip = "Speed of HP bar transition", Min = 0.1, Max = 20.0, Alias = "lerpSpeed" )
		float32 _lerpSpeed;
		PROPERTY( Category = "Layout", DisplayName = "Offset Position", Tooltip = "Offset from attached entity position", Alias = "offsetPos" )
		float2 _offsetPos;
		PROPERTY( Category = "Layout", DisplayName = "Visible", Tooltip = "Toggle HP bar visibility", Alias = "bVisible" )
		bool _bVisible;
	};
} // namespace sw

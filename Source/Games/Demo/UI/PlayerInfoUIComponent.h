#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class PlayerInfoUIComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 playerLevel;

		PROPERTY()
		int32 playerHp;

		PROPERTY()
		int32 playerMaxHp;

		PROPERTY()
		int32 playerMp;

		PlayerInfoUIComponent();
		virtual ~PlayerInfoUIComponent() override							 = default;
		PlayerInfoUIComponent( PlayerInfoUIComponent&& ) noexcept			 = default;
		PlayerInfoUIComponent& operator=( PlayerInfoUIComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void updateStats( int32 hp, int32 maxHp );
	};
} // namespace sw

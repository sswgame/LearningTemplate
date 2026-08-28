#pragma once
#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class PlayerInfoUIComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		int32 _playerLevel;

		PROPERTY()
		int32 _playerHp;

		PROPERTY()
		int32 _playerMaxHp;

		PROPERTY()
		int32 _playerMp;

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

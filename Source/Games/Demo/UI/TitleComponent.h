#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class TitleComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float32 frontCloudSpeed{ 0.0f };

		PROPERTY()
		float32 startFrontPosX{ 0.0f };

		PROPERTY()
		float32 maxFrontPosX{ 0.0f };

		PROPERTY()
		float32 backCloudSpeed{ 0.0f };

		PROPERTY()
		float32 startBackCloudPosX{ 0.0f };

		PROPERTY()
		float32 maxBackCloudPosX{ 0.0f };

		PROPERTY()
		string nextSceneName{};

		PROPERTY()
		int32 selectIndex{ 0 };

		PROPERTY()
		float32 frontCloudCurrentX{ 0.0f };

		PROPERTY()
		float32 backCloudCurrentX{ 0.0f };

		TitleComponent()									   = default;
		virtual ~TitleComponent() override					   = default;
		TitleComponent( TitleComponent&& ) noexcept			   = default;
		TitleComponent& operator=( TitleComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void moveClouds( float32 deltaTime );
		void checkMenuSelection();
		void doAction( int32 menuIndex );
	};
} // namespace sw

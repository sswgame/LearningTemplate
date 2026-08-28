#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT()
	class TitleComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		float32 _frontCloudSpeed{ 0.0f };

		PROPERTY()
		float32 _startFrontPosX{ 0.0f };

		PROPERTY()
		float32 _maxFrontPosX{ 0.0f };

		PROPERTY()
		float32 _backCloudSpeed{ 0.0f };

		PROPERTY()
		float32 _startBackCloudPosX{ 0.0f };

		PROPERTY()
		float32 _maxBackCloudPosX{ 0.0f };

		PROPERTY()
		string _nextSceneName{};

		PROPERTY()
		int32 _selectIndex{ 0 };

		PROPERTY()
		float32 _frontCloudCurrentX{ 0.0f };

		PROPERTY()
		float32 _backCloudCurrentX{ 0.0f };

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

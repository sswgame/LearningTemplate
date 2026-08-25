#pragma once
#include "Core/Container/string.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
	REFLECT_SCRIPT()
	class TextComponent : public Component
	{
	public:
		REFLECT_BODY();

		PROPERTY()
		string text{ "Text" };

		PROPERTY()
		float4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };

		PROPERTY()
		float32 fontSize{ 0.0f };

		TextComponent()										 = default;
		virtual ~TextComponent() override					 = default;
		TextComponent( TextComponent&& ) noexcept			 = default;
		TextComponent& operator=( TextComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void setText( const string& newText );
	};
} // namespace sw

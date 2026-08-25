#include "pch.h"

#include "Games/Demo/UI/TextComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void TextComponent::onBeginPlay()
	{
		setTickGroup( TickGroup::PostUpdate );
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void TextComponent::onEndPlay()
	{
	}

	void TextComponent::onTick( float32 deltaTime )
	{
		(void)deltaTime;
	}

	void TextComponent::setText( const string& newText )
	{
		text = newText;
	}
} // namespace sw

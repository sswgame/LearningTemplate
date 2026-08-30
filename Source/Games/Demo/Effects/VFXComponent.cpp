#include "pch.h"

#include "Games/Demo/Effects/VFXComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	void VFXComponent::onTick( float32 deltaTime )
	{
		EffectBaseComponent::onTick( deltaTime );

		switch ( _vfxType )
		{
			case VFXType::AlphaFade:
			{
				if ( getDuration() > 0.0f )
					setCurrentAlpha( 1.0f - ( getCurrentTimer() / getDuration() ) );
				break;
			}
			case VFXType::AfterImage:
			{
				if ( getDuration() > 0.0f )
					setCurrentAlpha( _ghostAlpha * ( 1.0f - ( getCurrentTimer() / getDuration() ) ) );
				break;
			}
			case VFXType::WarningBlink:
			{
				const float32 freq	 = _blinkRate;
				const float32 sinVal = MathUtil::sin( getCurrentTimer() * freq * ( MathUtil::Pi * 2.0f ) );
				setCurrentAlpha( ( sinVal > 0.0f ) ? 1.0f : 0.2f );
				break;
			}
			default:
				break;
		}

		setCurrentAlpha( MathUtil::max( getCurrentAlpha(), 0.0f ) );
	}
} // namespace sw

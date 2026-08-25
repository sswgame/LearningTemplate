#include "pch.h"

#include "Games/Demo/Effects/VFXComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	void VFXComponent::onTick( float32 deltaTime )
	{
		EffectBaseComponent::onTick( deltaTime );

		EffectBaseData* pData = ensureEffectData();
		if ( pData == nullptr )
			return;

		switch ( vfxType )
		{
			case VFXType::AlphaFade:
			{
				if ( pData->duration > 0.0f )
					pData->currentAlpha = 1.0f - ( pData->currentTimer / pData->duration );
				break;
			}
			case VFXType::AfterImage:
			{
				if ( pData->duration > 0.0f )
					pData->currentAlpha = ghostAlpha * ( 1.0f - ( pData->currentTimer / pData->duration ) );
				break;
			}
			case VFXType::WarningBlink:
			{
				const float32 freq	 = blinkRate;
				const float32 sinVal = MathUtil::sin( pData->currentTimer * freq * ( MathUtil::Pi * 2.0f ) );
				pData->currentAlpha	 = ( sinVal > 0.0f ) ? 1.0f : 0.2f;
				break;
			}
		}

		pData->currentAlpha = MathUtil::max( pData->currentAlpha, 0.0f );
	}
} // namespace sw

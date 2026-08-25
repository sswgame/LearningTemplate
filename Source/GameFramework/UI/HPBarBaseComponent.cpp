#include "pch.h"

#include "GameFramework/UI/HPBarBaseComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/EcsDataUtil.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void HPBarBaseComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostUpdate );

		HPBarBaseData* pData = ensureHPBarData();
		if ( pData != nullptr )
		{
			pData->remainRatio = pData->hpRatio;
			pData->targetRatio = pData->hpRatio;
		}

		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			pOwner->addTag( "UI"_tag );
	}

	void HPBarBaseComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void HPBarBaseComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		HPBarBaseData* pData = ensureHPBarData();
		if ( pData == nullptr )
			return;

		pData->remainRatio = MathUtil::lerp( pData->remainRatio, pData->targetRatio, MathUtil::clamp( pData->lerpSpeed * deltaTime, 0.0f, 1.0f ) );
	}

	Component::EcsDataView HPBarBaseComponent::ensureEcsData()
	{
		HPBarBaseData* pData = ensureHPBarData();
		return { pData, HPBarBaseData::StaticType() };
	}

	Component::EcsDataView HPBarBaseComponent::getEcsData() const
	{
		return { ( getHPBarData() ), HPBarBaseData::StaticType() };
	}

	HPBarBaseData* HPBarBaseComponent::getHPBarData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner != nullptr )
			return pOwner->getComponent<HPBarBaseData>().get();
		return nullptr;
	}

	HPBarBaseData* HPBarBaseComponent::ensureHPBarData()
	{
		return sw::ensureEcsData<HPBarBaseData>( getOwner(), getTypeInfo() );
	}
} // namespace sw

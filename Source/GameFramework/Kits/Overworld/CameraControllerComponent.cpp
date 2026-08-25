#include "pch.h"

#include "GameFramework/Kits/Overworld/CameraControllerComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/EcsDataUtil.h"

namespace sw
{
	void CameraControllerComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PrePhysics );
		ensureControllerData();
	}

	void CameraControllerComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void CameraControllerComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );

		CameraControllerData* pData = ensureControllerData();
		if ( pData == nullptr )
			return;

		pData->currentPos._x = MathUtil::lerp( pData->currentPos._x, pData->targetPos._x, MathUtil::clamp( pData->followSpeed * deltaTime, 0.0f, 1.0f ) );
		pData->currentPos._y = MathUtil::lerp( pData->currentPos._y, pData->targetPos._y, MathUtil::clamp( pData->followSpeed * deltaTime, 0.0f, 1.0f ) );

		float2 shakeOffset{ 0.0f, 0.0f };
		if ( pData->shakeDuration > 0.0f )
		{
			pData->shakeDuration -= deltaTime;
			if ( pData->shakeDuration < 0.0f )
				pData->shakeDuration = 0.0f;
			const float32 freq = pData->shakeFrequency;
			shakeOffset._x	   = MathUtil::sin( pData->shakeDuration * freq ) * pData->shakeIntensity;
			shakeOffset._y	   = MathUtil::cos( pData->shakeDuration * ( freq * 1.3f ) ) * ( pData->shakeIntensity * 0.75f );
		}

		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;

		SceneComponent* pSceneComp = pOwner->getPrimarySceneComponent();
		if ( pSceneComp == nullptr )
			return;

		float3 pos = pSceneComp->getLocalPosition();
		pos._x	   = pData->currentPos._x + shakeOffset._x;
		pos._y	   = pData->currentPos._y + shakeOffset._y;
		pSceneComp->setLocalPosition( pos );
	}

	Component::EcsDataView CameraControllerComponent::ensureEcsData()
	{
		CameraControllerData* pData = ensureControllerData();
		return { pData, CameraControllerData::StaticType() };
	}

	Component::EcsDataView CameraControllerComponent::getEcsData() const
	{
		return { ( getControllerData() ), CameraControllerData::StaticType() };
	}

	CameraControllerData* CameraControllerComponent::getControllerData() const
	{
		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return nullptr;
		return pOwner->getComponent<CameraControllerData>().get();
	}

	CameraControllerData* CameraControllerComponent::ensureControllerData()
	{
		return sw::ensureEcsData<CameraControllerData>( getOwner(), getTypeInfo() );
	}

	void CameraControllerComponent::shake( float32 intensity, float32 duration )
	{
		CameraControllerData* pData = ensureControllerData();
		if ( pData == nullptr )
			return;

		pData->shakeIntensity = intensity;
		pData->shakeDuration  = duration;
	}
} // namespace sw

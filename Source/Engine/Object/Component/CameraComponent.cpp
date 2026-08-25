#include "pch.h"

#include "Engine/Object/Component/CameraComponent.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/EcsDataUtil.h"

namespace sw
{
	void CameraComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		ensureCameraData();
	}

	Component::EcsDataView CameraComponent::ensureEcsData()
	{
		CameraData* pData = ensureCameraData();
		return { pData, CameraData::StaticType() };
	}

	Component::EcsDataView CameraComponent::getEcsData() const
	{
		return { getCameraData(), CameraData::StaticType() };
	}

	CameraData* CameraComponent::getCameraData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<CameraData>().get();
		return nullptr;
	}

	CameraData* CameraComponent::ensureCameraData()
	{
		return sw::ensureEcsData<CameraData>( getOwner(), getTypeInfo() );
	}

	void CameraComponent::setRole( CameraRole role )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->role = role;
	}

	CameraRole CameraComponent::getRole() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->role;
		return CameraRole::Game;
	}

	void CameraComponent::setFieldOfViewY( float32 fovRadians )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->fovY = fovRadians;
	}

	float32 CameraComponent::getFieldOfViewY() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->fovY;
		return 0.70f;
	}

	void CameraComponent::setNearPlane( float32 nearZ )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->nearZ = nearZ;
	}

	float32 CameraComponent::getNearPlane() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->nearZ;
		return 0.1f;
	}

	void CameraComponent::setFarPlane( float32 farZ )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->farZ = farZ;
	}

	float32 CameraComponent::getFarPlane() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->farZ;
		return 100.0f;
	}

	void CameraComponent::setOrthoHeight( float32 height )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->orthoHeight = height;
	}

	float32 CameraComponent::getOrthoHeight() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->orthoHeight;
		return 10.0f;
	}

	void CameraComponent::setOrthographic( bool bOrtho )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->bOrthographic = bOrtho;
	}

	bool CameraComponent::isOrthographic() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->bOrthographic;
		return false;
	}

	void CameraComponent::setPriority( int32 priority )
	{
		CameraData* pData = ensureCameraData();
		if ( pData != nullptr )
			pData->priority = priority;
	}

	int32 CameraComponent::getPriority() const
	{
		const CameraData* pData = getCameraData();
		if ( pData != nullptr )
			return pData->priority;
		return 0;
	}

	void CameraComponent::lookAt( const float3& target, const float3& up )
	{
		const float3  eye	  = getWorldPosition();
		float3		  forward = ( target - eye );
		const float32 lenSq	  = forward.getLengthSquared();
		if ( lenSq < 1e-10f )
			return;
		forward = forward.normalize();

		const float32 yaw	= MathUtil::atan2( forward._x, forward._z );
		const float32 pitch = MathUtil::asin( MathUtil::clamp( forward._y, -1.0f, 1.0f ) );
		(void)up;
		setLocalRotation( float3( pitch, yaw, 0.0f ) );
	}

	float4x4 CameraComponent::getViewMatrix() const
	{
		const float3 eye = getWorldPosition();
		const float3 rot = getLocalRotation();

		const float4x4 rotMat  = float4x4::createFromYawPitchRoll( rot._y, rot._x, rot._z );
		const float3   forward = float3::transformNormal( float3( 0.0f, 0.0f, 1.0f ), rotMat );
		const float3   target  = eye + forward;
		return float4x4::createLookAt( eye, target, float3( 0.0f, 1.0f, 0.0f ) );
	}

	float4x4 CameraComponent::getProjectionMatrix( float32 aspectRatio ) const
	{
		const CameraData* pData	 = getCameraData();
		const float32	  fovY	 = pData != nullptr ? pData->fovY : 0.70f;
		const float32	  nearZ	 = pData != nullptr ? pData->nearZ : 0.1f;
		const float32	  farZ	 = pData != nullptr ? pData->farZ : 100.0f;
		const float32	  aspect = aspectRatio > 1e-4f ? aspectRatio : ( 16.0f / 9.0f );
		if ( pData != nullptr && pData->bOrthographic )
		{
			const float32 height = pData->orthoHeight > 1e-4f ? pData->orthoHeight : 10.0f;
			const float32 width	 = height * aspect;
			return float4x4::createOrthographic( width, height, nearZ, farZ );
		}
		return float4x4::createPerspectiveFieldOfView( fovY, aspect, nearZ, farZ );
	}

	float4x4 CameraComponent::getViewProjectionMatrix( float32 aspectRatio ) const
	{
		return getViewMatrix() * getProjectionMatrix( aspectRatio );
	}

	void CameraComponent::getCameraPosition( float32 arrOutPos[3] ) const
	{
		if ( arrOutPos == nullptr )
			return;
		const float3 pos = getWorldPosition();
		arrOutPos[0]	 = pos._x;
		arrOutPos[1]	 = pos._y;
		arrOutPos[2]	 = pos._z;
	}
} // namespace sw

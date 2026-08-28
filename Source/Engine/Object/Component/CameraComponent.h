/**
 * @file CameraComponent.h
 * @brief 렌더용 뷰/투영 행렬을 만드는 SceneComponent입니다.
 */
#pragma once
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	namespace generated
	{
		struct sw_CameraComponent_Registrar;
	} // namespace generated

	/// @brief 카메라 Role 종류를 정의하는 열거형입니다.
	ENUM()
	enum class CameraRole : uint8
	{
		Game   = 0, ///< Playing 중 활성
		Editor = 1, ///< Editor 뷰포트에서 활성
		Custom = 2, ///< 수동 선택만
	};

	/**
	 * @class CameraComponent
	 * @brief GameObject에 붙는 카메라. 트랜스폼은 SceneComponent에서 옵니다.
	 */
	REFLECT()
	class SW_API CameraComponent : public SceneComponent
	{
		friend struct ::sw::generated::sw_CameraComponent_Registrar;

	public:
		REFLECT_BODY();

		/** @brief 기본 카메라를 만듭니다. */
		CameraComponent();
		/** @brief 카메라를 해제합니다. */
		virtual ~CameraComponent() override = default;

		/** @brief 이동 생성자입니다. */
		CameraComponent( CameraComponent&& ) noexcept = default;
		/** @brief 이동 대입입니다. */
		CameraComponent& operator=( CameraComponent&& ) noexcept = default;

		void onBeginPlay() override;

		/** @brief 카메라 역할을 설정합니다. */
		void setRole( CameraRole role ) { _role = role; }
		/** @brief 카메라 역할을 반환합니다. */
		CameraRole getRole() const { return _role; }

		/** @brief 수직 시야각(라디안)을 설정합니다. */
		void setFieldOfViewY( float32 fovRadians ) { _fovY = fovRadians; }
		/** @brief 수직 시야각(라디안)을 반환합니다. */
		float32 getFieldOfViewY() const { return _fovY; }

		/** @brief 근평면을 설정합니다. */
		void setNearPlane( float32 nearZ ) { _nearZ = nearZ; }
		/** @brief 근평면을 반환합니다. */
		float32 getNearPlane() const { return _nearZ; }

		/** @brief 원평면을 설정합니다. */
		void setFarPlane( float32 farZ ) { _farZ = farZ; }
		/** @brief 원평면을 반환합니다. */
		float32 getFarPlane() const { return _farZ; }

		/** @brief 직교 투영 높이를 설정합니다. */
		void setOrthoHeight( float32 height ) { _orthoHeight = height; }
		/** @brief 직교 투영 높이를 반환합니다. */
		float32 getOrthoHeight() const { return _orthoHeight; }

		/** @brief 직교 투영 여부를 설정합니다. */
		void setOrthographic( bool bOrtho ) { _bOrthographic = bOrtho; }
		/** @brief 직교 투영인지 반환합니다. */
		bool isOrthographic() const { return _bOrthographic; }

		/** @brief 우선순위를 설정합니다. */
		void setPriority( int32 priority ) { _priority = priority; }
		/** @brief 우선순위를 반환합니다. */
		int32 getPriority() const { return _priority; }

		/** @brief 월드 공간 타깃을 바라봅니다 (로컬 회전 갱신). */
		void lookAt( const float3& target, const float3& up = float3( 0.0f, 1.0f, 0.0f ) );

		/** @brief 뷰 행렬을 반환합니다. */
		float4x4 getViewMatrix() const;
		/** @brief 투영 행렬을 반환합니다. */
		float4x4 getProjectionMatrix( float32 aspectRatio ) const;
		/** @brief 뷰-투영 행렬을 반환합니다. */
		float4x4 getViewProjectionMatrix( float32 aspectRatio ) const;

		/** @brief 카메라 월드 위치를 반환합니다. */
		void getCameraPosition( float32 outPos[3] ) const;

	private:
		PROPERTY()
		float32 _fovY;
		PROPERTY()
		float32 _nearZ;
		PROPERTY()
		float32 _farZ;
		PROPERTY()
		float32 _orthoHeight;
		PROPERTY()
		int32 _priority;
		PROPERTY()
		CameraRole _role;
		PROPERTY()
		bool _bOrthographic;
	};
} // namespace sw

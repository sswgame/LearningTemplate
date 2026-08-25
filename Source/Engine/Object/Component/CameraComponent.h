/**
 * @file CameraComponent.h
 * @brief 렌더용 뷰/투영 행렬을 만드는 SceneComponent입니다.
 */
#pragma once
#include "Engine/Object/Component/SceneComponent.h"

namespace sw
{
	/// @brief 카메라 Role 종류를 정의하는 열거형입니다.
	ENUM()
	enum class CameraRole : uint8
	{
		Game   = 0, ///< Playing 중 활성
		Editor = 1, ///< Editor 뷰포트에서 활성
		Custom = 2, ///< 수동 선택만
	};

	/**
	 * @brief 카메라 투영·역할 ECS 데이터
	 */
	REFLECT()
	struct SW_API CameraData
	{
		REFLECT_BODY();
		PROPERTY()
		float32 fovY = 0.70f;
		PROPERTY()
		float32 nearZ = 0.1f;
		PROPERTY()
		float32 farZ = 100.0f;
		PROPERTY()
		float32 orthoHeight = 10.0f;
		PROPERTY()
		int32 priority{ 0 };
		PROPERTY()
		CameraRole role = CameraRole::Game;
		PROPERTY()
		bool bOrthographic{ false };
	};

	/**
	 * @class CameraComponent
	 * @brief GameObject에 붙는 카메라. 트랜스폼은 SceneComponent에서 옵니다.
	 */
	REFLECT()
	class SW_API CameraComponent : public SceneComponent
	{
	public:
		REFLECT_BODY();

		/** @brief 기본 카메라를 만듭니다. */
		CameraComponent() = default;
		/** @brief 카메라를 해제합니다. */
		virtual ~CameraComponent() override = default;

		/** @brief 이동 생성자입니다. */
		CameraComponent( CameraComponent&& ) noexcept = default;
		/** @brief 이동 대입입니다. */
		CameraComponent& operator=( CameraComponent&& ) noexcept = default;

		void onBeginPlay() override;

		EcsDataView ensureEcsData() override;
		EcsDataView getEcsData() const override;

		/** @brief 카메라 ECS 데이터. 없으면 nullptr. */
		CameraData* getCameraData() const;
		/** @brief 카메라 ECS 데이터를 확보합니다. */
		CameraData* ensureCameraData();

		/** @brief 카메라 역할을 설정합니다. */
		void setRole( CameraRole role );
		/** @brief 카메라 역할을 반환합니다. */
		CameraRole getRole() const;

		/** @brief 수직 시야각(라디안)을 설정합니다. */
		void setFieldOfViewY( float32 fovRadians );
		/** @brief 수직 시야각(라디안)을 반환합니다. */
		float32 getFieldOfViewY() const;

		/** @brief 근평면을 설정합니다. */
		void setNearPlane( float32 nearZ );
		/** @brief 근평면을 반환합니다. */
		float32 getNearPlane() const;

		/** @brief 원평면을 설정합니다. */
		void setFarPlane( float32 farZ );
		/** @brief 원평면을 반환합니다. */
		float32 getFarPlane() const;

		/** @brief 직교 투영 높이를 설정합니다. */
		void setOrthoHeight( float32 height );
		/** @brief 직교 투영 높이를 반환합니다. */
		float32 getOrthoHeight() const;

		/** @brief 직교 투영 여부를 설정합니다. */
		void setOrthographic( bool bOrtho );
		/** @brief 직교 투영인지 반환합니다. */
		bool isOrthographic() const;

		/** @brief 우선순위를 설정합니다. */
		void setPriority( int32 priority );
		/** @brief 우선순위를 반환합니다. */
		int32 getPriority() const;

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
	};
} // namespace sw

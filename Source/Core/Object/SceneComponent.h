#pragma once

#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"
#include "Core/Object/Component.h"
#include "Core/Utility/Math/VectorMath.h"
#include "Core/Utility/Math/MatrixMath.h"

/**
 * @file SceneComponent.h
 * @brief 트랜스폼(위치/회전/크기) 및 부모-자식 계층 트리를 지원하는 SceneComponent 클래스 정의
 */

namespace sw
{
	/**
	 * @class SceneComponent
	 * @brief 로컬 트랜스폼·부모-자식 계층, float→double 누적 월드 위치(LWC), 카메라 상대 행렬을 제공하는 씬 컴포넌트
	 */
	class SW_API SceneComponent : public Component
	{
	public:
		SceneComponent();
		virtual ~SceneComponent() override;

		const TypeInfo* getTypeInfo() const override;

		/** @brief 로컬 위치 설정 */
		void setLocalPosition( const float3& pos );
		/** @brief 로컬 위치 반환 */
		float3 getLocalPosition() const { return _localPosition; }

		/** @brief 로컬 오일러 회전(피치/요/롤) 설정. getCameraRelativeWorldMatrix에 반영 */
		void setLocalRotation( const float3& rot );
		/** @brief 로컬 회전각 반환 */
		float3 getLocalRotation() const { return _localRotation; }

		/** @brief 로컬 스케일 설정. getCameraRelativeWorldMatrix에 반영 */
		void setLocalScale( const float3& scale );
		/** @brief 로컬 스케일 반환 */
		float3 getLocalScale() const { return _localScale; }

		/** @brief 계층을 반영한 float32 월드 위치(캐시). LWC를 float로 내린 값 */
		float3 getWorldPosition() const;

		/**
		 * @brief 계층 위치 합을 double로 누적한 월드 좌표(LWC)
		 * @details 로컬 포즈는 float3이며, 부모 LWC에 로컬 오프셋을 double로 더합니다.
		 */
		double3 getWorldPositionLWC() const;

		/**
		 * @brief 계층 번역만 합성한 4x4 월드 행렬(캐시)
		 * @details 현재 구현은 translation만 포함합니다. 회전·스케일은 getCameraRelativeWorldMatrix를 사용하세요.
		 *          병렬 tick 구간(beginParallelTransformReadOnly)에서는 캐시만 반환합니다.
		 */
		float4x4 getWorldMatrix() const;

		/**
		 * @brief 카메라 기준 상대 위치 + 로컬 회전·스케일 행렬
		 * @param cameraWorldPos 카메라 월드 위치(LWC)
		 */
		float4x4 getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const;

		/**
		 * @brief 부모 캐시가 이미 유효하다고 가정하고 이 노드의 월드 캐시를 갱신합니다.
		 * @details GameObjectManager::flushSceneTransforms 가 루트→자식 순으로 호출합니다.
		 */
		void updateWorldTransformFromParent();

		/** @brief 부모 SceneComponent에 계층적으로 부착 */
		bool attachToComponent( SceneComponent* parent );

		/** @brief 부모 컴포넌트로부터 부착 해제 */
		void detachFromComponent();

		/** @brief 부모 SceneComponent 포인터 반환 */
		SceneComponent* getParent() const { return _parent; }

		/** @brief 자식 SceneComponent 포인터 목록 반환 */
		const std::vector<SceneComponent*>& getChildren() const { return _children; }

		/** @brief 트랜스폼 변경 시 행렬 캐시 재계산 더티 마킹 */
		void markTransformDirty();

		/**
		 * @brief 병렬 tick 구간: getWorld* 는 캐시만 읽고, markTransformDirty 는 자식 전파를 생략합니다.
		 * @details 메인 스레드에서 parallel region 진입 전/대기 후 에만 호출하세요.
		 */
		static void beginParallelTransformReadOnly();
		static void endParallelTransformReadOnly();
		static bool isParallelTransformReadOnly();

	protected:
		mutable float4x4 _cachedWorldMatrix{ float4x4::Identity }; ///< 캐시된 번역-only 월드 행렬
		mutable double3	 _cachedWorldPositionLWC{ 0.0, 0.0, 0.0 }; ///< 캐시된 double 누적 월드 위치

		float3 _localPosition{ 0.0f, 0.0f, 0.0f }; ///< 로컬 X, Y, Z 오프셋

		float3 _localRotation{ 0.0f, 0.0f, 0.0f }; ///< 로컬 롤/피치/요 오일러 각도

		float3 _localScale{ 1.0f, 1.0f, 1.0f }; ///< 로컬 스케일 배율

		mutable float3 _cachedWorldPosition{ 0.0f, 0.0f, 0.0f }; ///< 캐시된 float 월드 위치

		SceneComponent*				 _parent = nullptr; ///< 부모 컴포넌트 참조
		std::vector<SceneComponent*> _children;			///< 자식 컴포넌트 리스트

		mutable uint8				   _bIsTransformDirty : 1; ///< 트랜스폼 캐시 재계산 더티 비트
		[[maybe_unused]] mutable uint8 _reservedFlags	  : 7;
	};
} // namespace sw

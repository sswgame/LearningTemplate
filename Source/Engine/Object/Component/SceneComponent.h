/**
 * @file SceneComponent.h
 * @brief 트랜스폼(위치/회전/크기) 및 부모-자식 계층 트리를 지원하는 SceneComponent 클래스 정의
 */
#pragma once
#include "Engine/ECS/Entity.h"
#include "Engine/Object/Component/Component.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
	/**

	 * @brief 트랜스폼 ECS 데이터
	 */
	REFLECT()
	struct SW_API TransformData
	{
		REFLECT_BODY();
		float3 localPosition{ 0.0f, 0.0f, 0.0f };
		float3 localRotation{ 0.0f, 0.0f, 0.0f };
		float3 localScale{ 1.0f, 1.0f, 1.0f };

		float4x4 cachedWorldMatrix{ float4x4::Identity };
		double3	 cachedWorldPositionLWC{ 0.0, 0.0, 0.0 };
		float3	 cachedWorldPosition{ 0.0f, 0.0f, 0.0f };

		uint8 bIsTransformDirty	  : 1;
		uint8 bHasDirtyDescendant : 1;
		uint8 reserved			  : 6;

		TransformData()
			: bIsTransformDirty{ 1 }
			, bHasDirtyDescendant{ 0 }
			, reserved{ 0 }
		{
		}
	};

	/**
	 * @brief 계층 ECS 데이터
	 */
	REFLECT()
	struct SW_API HierarchyData
	{
		REFLECT_BODY();
		sw::Entity		   parentEntity;
		vector<sw::Entity> listChildEntities;

		HierarchyData()
			: parentEntity{ sw::kNullEntity }
			, listChildEntities{}
		{
		}
	};

	/**
	 * @class SceneComponent
	 * @brief 로컬 트랜스폼·부모-자식 계층, float32→float64 누적 월드 위치(LWC), 카메라 상대 행렬을 제공하는 씬 컴포넌트
	 */
	REFLECT()
	class SW_API SceneComponent : public Component
	{
	public:
		REFLECT_BODY();

		/** @brief 로컬 트랜스폼 항등, 계층 없음. */
		SceneComponent();
		/** @brief 트랜스폼 계층에서 자신을 뗍니다. */
		virtual ~SceneComponent() override;

		/** @brief 계층 포인터를 이동합니다. */
		SceneComponent( SceneComponent&& ) noexcept = default;
		/** @brief 이동 대입입니다. */
		SceneComponent& operator=( SceneComponent&& ) noexcept = default;

		/** @brief 플레이 시작 시 월드 행렬을 맞춥니다. */
		void onBeginPlay() override;
		/** @brief 더티면 월드 행렬을 다시 계산합니다. */
		void onTick( float32 deltaTime ) override;

		/** @brief 로컬 위치 설정 */
		void setLocalPosition( const float3& pos );
		/** @brief 로컬 위치 반환 */
		float3 getLocalPosition() const;

		/** @brief 로컬 오일러 회전(피치/요/롤) 설정. getCameraRelativeWorldMatrix에 반영 */
		void setLocalRotation( const float3& rot );
		/** @brief 로컬 회전각 반환 */
		float3 getLocalRotation() const;

		/** @brief 로컬 스케일 설정. getCameraRelativeWorldMatrix에 반영 */
		void setLocalScale( const float3& scale );
		/** @brief 로컬 스케일 반환 */
		float3 getLocalScale() const;

		/** @brief 계층을 반영한 float32 월드 위치(캐시). LWC를 float로 내린 값 */
		float3 getWorldPosition() const;

		/**
		 * @brief 계층 위치 합을 double로 누적한 월드 좌표(LWC)
		 * @details 로컬 포즈는 float3이며, 부모 LWC에 로컬 오프셋을 double로 더합니다.
		 */
		double3 getWorldPositionLWC() const;

		/**
		 * @brief 계층 TRS를 합성한 4x4 월드 행렬(캐시)
		 * @details localTRS * parentWorld (row-vector). 병렬 tick 구간에서는 캐시만 반환합니다.
		 */
		float4x4 getWorldMatrix() const;

		/**
		 * @brief 카메라 기준 상대 위치 + 계층 월드 회전·스케일 행렬
		 * @param cameraWorldPos 카메라 월드 위치(LWC)
		 * @details 위치는 (worldLWC - camera)를 float로 내린 값, 회전/스케일은 계층 월드 포즈를 사용합니다.
		 */
		float4x4 getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const;

		/**
		 * @brief 부모 캐시가 이미 유효하다고 가정하고 이 노드의 월드 캐시를 갱신합니다.
		 * @details GameObjectManager::flushSceneTransforms 가 루트→자식 순으로 호출합니다.
		 */
		void updateWorldTransformFromParent();

		/** @brief 부모 SceneComponent에 계층적으로 부착 */
		bool attachToComponent( SceneComponent* pParent );

		/** @brief 부모 컴포넌트로부터 부착 해제 */
		void detachFromComponent();

		/** @brief 부모 SceneComponent 포인터 반환 (엔티티 ID로 조회) */
		SceneComponent* getParent() const;

		/** @brief 자식 SceneComponent 포인터 목록 반환 (엔티티 ID로 조회) */
		vector<SceneComponent*> getChildren() const;

		/** @brief 트랜스폼 변경 시 행렬 캐시 재계산 더티 마킹 */
		void markTransformDirty();

		/** @brief 트랜스폼 캐시가 더티면 true. */
		bool isTransformDirty() const;
		/** @brief 더티 자손이 있으면 true. */
		bool hasDirtyDescendant() const;
		/** @brief 더티 자손 플래그를 지웁니다. */
		void clearDirtyDescendant();
	};

	/** @brief SceneComponent면 this, 아니면 nullptr. */
	inline class SceneComponent* Component::asSceneComponent() { return ( _bIsSceneComponent != 0 ) ? static_cast<class SceneComponent*>( this ) : nullptr; }

	/** @brief SceneComponent면 this, 아니면 nullptr. */
	inline const class SceneComponent* Component::asSceneComponent() const { return ( _bIsSceneComponent != 0 ) ? static_cast<const class SceneComponent*>( this ) : nullptr; }
} // namespace sw

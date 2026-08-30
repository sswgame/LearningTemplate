/**
 * @file SceneComponent.h
 * @brief 트랜스폼(위치/회전/크기) 및 부모-자식 계층 트리를 지원하는 SceneComponent 클래스 정의
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	namespace generated
	{
		struct sw_SceneComponent_Registrar;
	} // namespace generated

	/**
	 * @class SceneComponent
	 * @brief 로컬 트랜스폼·부모-자식 계층, float32→float64 누적 월드 위치(LWC), 카메라 상대 행렬을 제공하는 씬 컴포넌트
	 */
	REFLECT( Category = "Transform", DisplayName = "Scene Component", Tooltip = "Provides Transform (Position, Rotation, Scale) and Hierarchy" )
	class SW_API SceneComponent : public Component
	{
		friend struct ::sw::generated::sw_SceneComponent_Registrar;

	public:
		REFLECT_BODY();

		/** @brief 로컬 트랜스폼 항등, 계층 없음. */
		SceneComponent();
		/** @brief 트랜스폼 계층에서 자신을 뗍니다. */
		virtual ~SceneComponent() override;

		/** @brief 계층 포인터를 이동합니다. */
		SceneComponent( SceneComponent&& ) noexcept;
		/** @brief 이동 대입입니다. */
		SceneComponent& operator=( SceneComponent&& ) noexcept;

		/** @brief 플레이 시작 시 월드 행렬을 맞춥니다. */
		void onBeginPlay() override;
		/** @brief 더티면 월드 행렬을 다시 계산합니다. */
		void onTick( float32 deltaTime ) override;
		/** @brief 로컬 TRS PROPERTY 변경 시 월드 캐시를 더티로 표시합니다. */
		void onPropertyChanged( hashed_string propertyName ) override;

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

		/** @brief 부모 SceneComponent 포인터 반환 */
		SceneComponent* getParent() const { return _pParent; }

		/** @brief 자식 SceneComponent 포인터 목록 반환 */
		const vector<SceneComponent*>& getChildren() const { return _listChild; }

		/** @brief 트랜스폼 변경 시 행렬 캐시 재계산 더티 마킹 */
		void markTransformDirty();

		/** @brief 트랜스폼 캐시가 더티면 true. */
		bool isTransformDirty() const { return _bIsTransformDirty == SW_TRUE; }
		/** @brief 더티 자손이 있으면 true. */
		bool hasDirtyDescendant() const { return _bHasDirtyDescendant == SW_TRUE; }
		/** @brief 더티 자손 플래그를 지웁니다. */
		void clearDirtyDescendant() { _bHasDirtyDescendant = SW_FALSE; }

		/** @brief `_pParent`에서 Attach 직렬화 필드를 채웁니다. */
		void syncAttachSerializeFields() const;
		/** @brief 로드된 Attach 필드로 `_pParent`를 복원합니다. 부모 GO가 아직 없으면 no-op. */
		void applyAttachSerializeFields();

	private:
		PROPERTY( Category = "Transform", DisplayName = "Position", Tooltip = "Local translation vector", Meta = "Units=m" )
		float3 _localPosition;
		PROPERTY( Category = "Transform", DisplayName = "Rotation", Tooltip = "Local Euler angles (Pitch, Yaw, Roll)", Meta = "Units=deg" )
		float3 _localRotation;
		PROPERTY( Category = "Transform", DisplayName = "Scale", Tooltip = "Local scale vector" )
		float3 _localScale;
		PROPERTY( HideInInspector )
		mutable hashed_string _attachOwner;
		PROPERTY( HideInInspector )
		mutable hashed_string	_attachComponent;
		float3					_cachedWorldPosition;
		float4x4				_cachedWorldMatrix;
		double3					_cachedWorldPositionLWC;
		SceneComponent*			_pParent;
		vector<SceneComponent*> _listChild;
		uint8					_bIsTransformDirty	 : 1;
		uint8					_bHasDirtyDescendant : 1;
		uint8					_reservedTransform	 : 6;
	};
} // namespace sw

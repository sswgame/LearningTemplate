/**
 * @file SceneComponent.h
 * @brief 트랜스폼(위치 · 회전 · 크기)과 부모-자식 계층을 가진 SceneComponent 입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct SceneTransformWrite;

    class GameObjectManager;

    /**
     * @class SceneComponent
     * @brief 로컬 트랜스폼 · 부모-자식 계층, float64 로 누적한 월드 위치(LWC), 카메라 상대 행렬을 제공하는 씬 컴포넌트입니다.
     */
    REFLECT( Category = "Transform", DisplayName = "Scene Component", Tooltip = "Provides Transform (Position, Rotation, Scale) and Hierarchy" )
    class SW_API SceneComponent : public Component
    {
        friend class SceneTransformHierarchy;

    public:
        REFLECT_BODY();

        /** @brief 항등 로컬 트랜스폼, 계층 없음으로 만듭니다. */
        SceneComponent();
        /** @brief 트랜스폼 계층에서 자신을 뗍니다. */
        virtual ~SceneComponent() override;

        /**
         * @brief **옮기지 않습니다.** 이 클래스는 자기 주소로 얽혀 있는 계층의 노드입니다.
         * @details 자식들의 `_pParent`, 부모의 `_listChild` 항목, 매니저의 루트 등록부가 모두
         *          이 객체의 **주소**를 들고 있습니다. 옮기려면 그 셋을 모두 새 주소로 고쳐야
         *          하는데, 예전 이동 연산은 하나도 하지 않았습니다(이동 대입은 방금 옮겨 온
         *          `_listChild` 를 그 자리에서 비우기까지 했습니다). 컴포넌트는 풀 안의 제자리에서
         *          만들고 없애므로 실제로 옮겨지는 일이 없습니다. 그래서 고치는 대신 막습니다.
         */
        SceneComponent( SceneComponent&& )            = delete;
        SceneComponent& operator=( SceneComponent&& ) = delete;

        /** @brief 플레이가 시작될 때 월드 행렬을 맞춥니다. */
        void onBeginPlay() override;
        /** @brief 더티면 월드 행렬을 다시 계산합니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 로컬 TRS PROPERTY 가 바뀌면 월드 캐시를 더티로 표시합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 부모가 없으면 루트 SceneComponent 캐시에 자기를 넣습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 루트 SceneComponent 캐시에서 자기를 뺍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /**
         * @brief 월드 행렬이 실제로 다시 계산된 직후 불립니다.
         * @details 트랜스폼이 바뀐 컴포넌트를 정확히 한 번 짚어 주는 유일한 지점입니다.
         *          렌더 프리미티브는 여기서 자기를 더티로 표시합니다. 그래서 프레임마다 전부 훑어
         *          "행렬이 바뀌었나" 되묻지 않아도 됩니다.
         */
        virtual void onWorldTransformUpdated() {}

        /** @brief 로컬 위치를 설정합니다. */
        void setLocalPosition( const float3& pos );
        /** @brief 로컬 위치를 반환합니다. */
        float3 getLocalPosition() const;

        /** @brief 로컬 오일러 회전(피치/요/롤)을 설정합니다. getCameraRelativeWorldMatrix 에 반영됩니다. */
        void setLocalRotation( const float3& rot );
        /** @brief 로컬 회전각을 반환합니다. */
        float3 getLocalRotation() const;

        /** @brief 로컬 스케일을 설정합니다. getCameraRelativeWorldMatrix 에 반영됩니다. */
        void setLocalScale( const float3& scale );
        /** @brief 로컬 스케일을 반환합니다. */
        float3 getLocalScale() const;

        /** @brief 계층을 반영한 float32 월드 위치(캐시)입니다. LWC 를 float 로 내린 값입니다. */
        float3 getWorldPosition() const;

        /**
         * @brief 계층 위치 합을 double 로 누적한 월드 좌표(LWC)입니다.
         * @details 로컬 포즈는 float3 이며, 부모 LWC 에 로컬 오프셋을 double 로 더합니다.
         */
        double3 getWorldPositionLwc() const;

        /**
         * @brief 계층 TRS 를 합성한 4x4 월드 행렬(캐시)입니다.
         * @details localTRS * parentWorld (행 벡터). 병렬 틱 구간에서는 캐시만 반환합니다.
         */
        float4x4 getWorldMatrix() const;

        /**
         * @brief 카메라 기준 상대 위치 + 계층 월드 회전 · 스케일로 만든 행렬입니다.
         * @param cameraWorldPos 카메라 월드 위치(LWC)
         * @details 위치는 (worldLWC - camera)를 float 로 내린 값이고, 회전/스케일은 계층 월드 포즈를 씁니다.
         */
        float4x4 getCameraRelativeWorldMatrix( const double3& cameraWorldPos ) const;

        /**
         * @brief 부모 캐시가 이미 유효하다고 보고 이 노드의 월드 캐시를 갱신합니다.
         * @details 트랜스폼 계층 플러시(`SceneTransformHierarchy::flushSubtree`)가 루트 → 자식 순으로 부릅니다.
         *          잎 루트는 `applyTransformWrite` 가 그 자리에서 부릅니다.
         */
        void updateWorldTransformFromParent();

        /** @brief 부모 SceneComponent 에 붙입니다. */
        bool attachToComponent( SceneComponent* pParent );

        /** @brief 부모 컴포넌트에서 뗍니다(틱 중이면 미룹니다). */
        void detachFromComponent();

        /** @brief 부모 SceneComponent 를 반환합니다. */
        SceneComponent* getParent() const { return _pParent; }

        /** @brief 자식 SceneComponent 목록을 반환합니다. */
        const vector<SceneComponent*>& getChildren() const { return _listChild; }

        /** @brief 트랜스폼이 바뀌었을 때 행렬 캐시를 다시 계산하도록 더티로 표시합니다. */
        void markTransformDirty();
        /**
         * @brief 배치 쓰기 한 건을 적용합니다. **워커에서 불립니다**(`GameObjectManager::applyTransformBatch` 전용).
         * @details 값이 같으면 아무것도 하지 않고 false 를 반환합니다. 바뀌면 필드를 쓰고 더티를 표시합니다. 세대는 올리지 않습니다(배치가
         *          끝에 한 번 올립니다). 더티 표시는 **바이트 저장**뿐이라(부모의 자손 더티 · 자식의 더티) 여러 워커가 같은
         *          바이트에 TRUE 를 겹쳐 써도 무해합니다. 그래서 이 두 플래그는 비트필드가 아닙니다(비트필드는 이웃 비트를
         *          같이 씁니다). 구조 변경(attach · detach)이 없는 구간에서만 부릅니다. 배치가 그 전제를 단언합니다.
         */
        bool applyTransformWrite( const SceneTransformWrite& write );
        // 잎 루트(부모도 자식도 없음)는 위 함수가 월드 행렬까지 그 자리에서 만들고 더티 목록에 올리지 않는다.

        /** @brief 트랜스폼 캐시가 더티면 true 입니다. */
        bool isTransformDirty() const { return _bIsTransformDirty == SW_TRUE; }
        /** @brief 더티 자손이 있으면 true 입니다. */
        bool hasDirtyDescendant() const { return _bHasDirtyDescendant == SW_TRUE; }
        /** @brief 더티 자손 플래그를 지웁니다. */
        void clearDirtyDescendant() { _bHasDirtyDescendant = SW_FALSE; }

        /** @brief `_pParent` 에서 Attach 직렬화 필드를 채웁니다. */
        void syncAttachSerializeFields() const;
        /** @brief 로드된 Attach 필드로 `_pParent` 를 복원합니다. 부모 GameObject 가 아직 없으면 아무것도 하지 않습니다. */
        void applyAttachSerializeFields();

    private:
        /** @brief 매니저가 병렬 틱 중(트랜스폼 읽기 전용)인지 반환합니다. 이때 세터는 바로 쓰지 않고 큐에 올립니다. */
        bool isInParallelTick() const;
        /**
         * @brief 틱 중의 세터 한 건을 자기 스레드 슬롯의 큐에 올립니다. 핸들과 대상 포인터는 여기서 채웁니다.
         * @details 세 세터가 같은 열 줄을 각자 들고 있었고, 스케일만 `_pTarget` 을 빠뜨려 적용 쪽이 핸들을 다시 풀었습니다
         *          ("한 곳에 넣은 고침이 형제에게 안 갔다" 의 자리). 큐에 쌓인 건은 같은 `tick()` 안에서 적용되므로
         *          대상 포인터를 믿어도 됩니다(파괴는 틱 밖에서만 메모리를 놓습니다).
         */
        void queueTickWrite( SceneTransformWrite& write );
        /**
         * @brief 병렬 틱이 끝난 뒤 이 컴포넌트의 @p pMethod 를 다시 부르도록 미룹니다. 핸들로 되찾으므로 그 사이에 파괴돼도 안전합니다.
         * @details `detachFromComponent` · `markTransformDirty` 가 같은 여덟 줄을 각자 들고 있었습니다. 인자가 있는
         *          `attachToComponent` 는 부모 핸들도 되찾아야 해서 따로 둡니다.
         */
        void deferSelfCall( void ( SceneComponent::*pMethod )() );
        /**
         * @brief 부모에서 **지금 당장** 뗍니다(미루지 않습니다).
         * @details `detachFromComponent` 는 틱 중이면 일을 미루고 그냥 돌아옵니다. 소멸자가 그
         *          경로를 타면 `_listChild` 가 줄지 않아 루프가 끝나지 않고, 미룬 일이 큐에
         *          무한히 쌓입니다. 소멸 중에는 미룰 대상(나중에 핸들로 되찾을 자기 자신)이
         *          없으므로 소멸자는 반드시 이쪽을 씁니다.
         */
        void detachFromParentImmediate();
        /** @brief 이 노드와 자손의 더티를 바이트 저장으로 세웁니다(`applyTransformWrite` 의 자식 쪽). 세대는 건드리지 않습니다. */
        void markDirtySubtree();

        PROPERTY( Category = "Transform", DisplayName = "Position", Tooltip = "Local translation vector", Meta = "Units=m" )
        float3 _localPosition;
        PROPERTY( Category = "Transform", DisplayName = "Rotation", Tooltip = "Local Euler angles (Pitch, Yaw, Roll)", Meta = "Units=deg" )
        float3 _localRotation;
        PROPERTY( Category = "Transform", DisplayName = "Scale", Tooltip = "Local scale vector" )
        float3 _localScale;
        PROPERTY( HideInInspector )
        mutable hashed_string _attachOwner;
        PROPERTY( HideInInspector )
        mutable hashed_string _attachComponent;
        float3                _cachedWorldPosition;
        float4x4              _cachedWorldMatrix;
        double3               _cachedWorldPositionLWC;
        /**
         * @brief 이 컴포넌트가 속한 매니저입니다. 등록 시점에 받아 둡니다.
         * @details 쓸 때마다 `getOwner()->getManager()` 로 두 단계 거슬러 찾던 것을 대체합니다.
         *          언리얼 컴포넌트가 등록 시점에 `GetWorld()` 를 잡아 두는 것과 같은 자리입니다.
         *          씬에 붙지 않았으면 nullptr 이고, 그때는 지연 없이 바로 반영하는 경로를 탑니다.
         */
        GameObjectManager*      _pManager;
        SceneComponent*         _pParent;
        vector<SceneComponent*> _listChild;
        /// @brief 비트필드가 **아닙니다.** 배치 쓰기의 워커들이 이 둘을 바이트 저장으로 같이 세웁니다(`applyTransformWrite`).
        uint8 _bIsTransformDirty;
        uint8 _bHasDirtyDescendant;
        /**
         * @brief 루트일 때 계층의 더티 루트 목록에 올라 있는지 나타냅니다. `SceneTransformHierarchy` 만 만집니다.
         * @details 원자인 이유: 배치 쓰기의 워커 둘이 같은 루트 아래의 자식을 써서 동시에 올리려 할 때 한 번만 오르게 합니다(exchange).
         */
        atomic<uint8> _bQueuedDirtyRoot;
        /**
         * @brief 계층의 루트 목록 · 더티 루트 목록에서의 자기 자리입니다. 목록에 없으면 `kNotInList` 입니다. `SceneTransformHierarchy` 만 만집니다.
         * @details 등록이 중복 검사로 목록 전체를 훑고 해제가 선형으로 찾던 것을 O(1) swap-remove 로 바꿨습니다. 8000 개를 만들고 지우면
         *          각각 3200만 번 비교였습니다(파괴 개당 2 µs). 더티 목록 자리는 플러시가 비웁니다.
         */
        static constexpr uint32 kNotInList = 0xFFFFFFFFu;
        uint32                  _rootIndex;
        uint32                  _dirtyRootIndex;
    };
} // namespace sw

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
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/SceneTransformStorage.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    enum class ObjectIdSpace : uint8;

    struct AABB;
    struct ObjectSaveOptions;
    struct SceneTransformWrite;

    class GameObjectManager;

    /**
     * @struct SceneAttachReference
     * @brief 상태에 적힌 부착 하나 — 부모가 어느 오브젝트의 어느 컴포넌트인가입니다. 부착 필드(`_attachOwner` · `_attachOwnerId` ·
     *        `_attachComponent`)를 한데 묶은 값이고, 묶음(`ObjectStateBatch`)이 이것으로 부모를 찾습니다.
     */
    struct SceneAttachReference
    {
        /** @brief 부모 오브젝트의 이름입니다. **비면 같은 오브젝트**입니다. id 가 없는 데이터는 다른 오브젝트도 이것만 있습니다. */
        hashed_string _ownerName{};
        /** @brief 다른 오브젝트의 id 입니다(`_idSpace` 의 것). 0 이면 같은 오브젝트이거나 id 가 없는 데이터입니다. */
        uint64 _ownerId{ 0 };
        /** @brief 부모 컴포넌트의 안정 키(`ComponentStableKey`)입니다. 비면 루트입니다. */
        hashed_string _componentKey{};
        /** @brief `_ownerId` 의 공간입니다. 찾지 못한 참조를 남길 때만 뜻이 있습니다(다른 공간으로 저장할 때는 id 를 비우고 이름만 남깁니다). */
        ObjectIdSpace _idSpace{};
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneComponent
     * @brief 로컬 트랜스폼 · 부모-자식 계층, float64 로 누적한 월드 위치(LWC), 카메라 상대 행렬을 제공하는 씬 컴포넌트입니다.
     * @details **트랜스폼 값은 이 객체 안에 없습니다.** 로컬 TRS · 월드 행렬 · LWC 는 전역 `SceneTransformStorage` 의 칸에 있고, 이 객체는
     *          칸 번호와 페이지만 듭니다. 틱 뒤 적용 · 렌더 수집이 값만 연달아 읽게 하려는 것입니다(280 B → 칸 번호). 리플렉션 이름
     *          (`_localPosition` · `_localRotation` · `_localScale`)은 그대로이고 값 접근자가 칸을 찾으므로, 씬 파일 · 인스펙터 · 직렬화는
     *          바뀐 것이 없습니다. 계층(부모 · 자식)과 더티 표시는 여기 남아 있습니다.
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
         *          이 객체의 **주소**를 들고 있어 옮기려면 그 셋을 모두 새 주소로 고쳐야 합니다.
         *          컴포넌트는 풀 안의 제자리에서 만들고 없애므로 실제로 옮겨지는 일이 없습니다. 그래서 막습니다.
         */
        SceneComponent( SceneComponent&& )            = delete;
        SceneComponent& operator=( SceneComponent&& ) = delete;

        /** @brief 플레이가 시작될 때 월드 행렬을 맞춥니다. */
        void onBeginPlay() override;
        /** @brief 로컬 TRS PROPERTY 가 바뀌면 월드 캐시를 더티로 표시합니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 부모가 없으면 루트 SceneComponent 캐시에 자기를 넣습니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 루트 SceneComponent 캐시에서 자기를 뺍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /**
         * @brief 월드 행렬이 실제로 다시 계산된 직후 불립니다.
         * @details 트랜스폼이 바뀐 컴포넌트를 정확히 한 번 짚어 주는 지점입니다. 플러시 · 지연 합성(`getWorldMatrix` 등) · 틱 뒤 적용과
         *          배치 쓰기의 잎 루트 합성이 모두 `SceneTransformHierarchy::notifyWorldUpdated` 를 거쳐 부릅니다. 칸의 알림 비트가 꺼진
         *          파생은 불리지 않습니다(`setWorldTransformNotify`) — 메시 컴포넌트가 그렇고, 렌더 더티는 칸에 적힌 프리미티브 번호로
         *          등록부에 바로 찍힙니다. 그래서 프레임마다 전부 훑어 "행렬이 바뀌었나" 되묻지 않아도 됩니다.
         */
        virtual void onWorldTransformUpdated() {}

        /**
         * @brief 이 컴포넌트(또는 조상)가 순간이동했습니다(`teleportTo`). 물리 바디를 가진 파생이 다음 step 에서 그 사이를 쓸지 않게 표시합니다.
         * @details 틱 중에도 불립니다(자식 오브젝트가 붙어 있으면 그 오브젝트의 컴포넌트에도). 원자적으로 표시만 하고, 물리는 step 직전에 읽습니다.
         */
        virtual void onTeleported() {}

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
         * @brief 월드 위치를 정합니다. 부모 아래에 있으면 부모 월드의 역으로 로컬 위치를 구해 씁니다(회전 · 스케일은 그대로).
         * @details 언리얼 `SetWorldLocation`, 유니티 `Transform.position` 의 자리입니다. 월드 자리를 바꾸는 곳은 이것을 씁니다 — 월드 값을 로컬 칸에
         *          쓰거나 월드 축의 차이를 로컬 축에 더하면 부모가 돌았거나 커졌을 때 엉뚱한 자리로 간다.
         */
        void setWorldPosition( const float3& worldPosition );
        /**
         * @brief 월드 위치로 **순간이동**합니다 — `setWorldPosition` 과 같은 자리로 가지만, 연속 충돌 콜라이더가 옛 자리에서 새 자리까지를 쓸지 않습니다.
         * @details 언리얼 `SetActorLocation( …, ETeleportType::TeleportPhysics )` · 유니티 `Rigidbody.position` 대입의 자리입니다. 이 컴포넌트와 그
         *          아래에 붙은 모두(자식 오브젝트 포함)가 순간이동한 것으로 칩니다(`onTeleported`). 그냥 옮기면(`setWorldPosition`) 빠른 바디는 그 길에
         *          놓인 것과 겹칩니다 — 리스폰 · 문 통과처럼 실제로 지나가지 않은 이동은 이것으로 합니다.
         */
        void teleportTo( const float3& worldPosition );
        /**
         * @brief 월드 트랜스폼(행렬)을 정합니다. 부모 기준으로 분해해 로컬 위치 · 회전 · 스케일을 씁니다(언리얼 `SetWorldTransform`).
         * @details 회전은 이 엔진의 오일러 규칙(`quaternion::getEulerAngles` — `createFromYawPitchRoll` 의 역)으로 적습니다. 주의: ImGuizmo 의 XYZ
         *          오일러로 분해해 넣으면 두 축 이상이 섞인 회전이 다른 회전으로 들어간다. 부모의 부등 스케일과 회전이 만든 기울임은
         *          TRS 로 나타낼 수 없어 버립니다(언리얼 · 유니티도 같습니다).
         */
        void setWorldTransform( const float4x4& worldMatrix );

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
         * @brief 이 컴포넌트가 차지하는 월드 공간의 경계 구입니다. 경계가 없는 컴포넌트(빈 트랜스폼 · 빛 · 카메라)는 false 입니다.
         * @details 경계는 **컴포넌트가 선언**합니다 — 언리얼 `USceneComponent::CalcBounds( LocalToWorld )`, 유니티 `Renderer.bounds` ·
         *          `Collider.bounds` 의 자리입니다. 에디터 피킹과 GPU 컬링이 같은 답을 씁니다 — 종류마다 경계를 따로 세면 **로컬** 스케일만 봐서
         *          부모가 키운 물체에서 클릭이 빗나간다.
         */
        virtual bool getWorldBounds( float3& outCenter, float32& outRadius ) const;
        /**
         * @brief 그리거나 부딪히는 것의 **월드** 축 정렬 상자입니다. 크기가 없는 컴포넌트(빈 씬 컴포넌트 · 카메라 · 빛)는 false 입니다.
         * @details 크기를 재는 쪽(에디터의 바닥 · 표면 붙이기 · 프레이밍)은 이것 하나를 본다. 자리마다 "메시면 로컬 스케일 × 단위 상자,
         *          콜라이더면 오프셋 크기" 를 따로 세면 부모가 키운 오브젝트 · 단위 상자가 아닌 메시 · 키운 콜라이더를 틀리게 잰다.
         */
        virtual bool getWorldBox( AABB& outBox ) const;

        /**
         * @brief 부모 캐시가 이미 유효하다고 보고 이 노드의 월드 캐시를 갱신합니다.
         * @details 트랜스폼 계층 플러시(`SceneTransformHierarchy::flushSubtree`)와 지연 합성(`ensureWorldCache`)이 부릅니다. 틱 뒤 적용 ·
         *          배치 쓰기의 잎 루트는 이 함수를 거치지 않고 칸에서 바로 합성합니다(`SceneTransformHierarchy::applyLocalChange`) —
         *          합성과 알림은 어느 쪽이든 `SceneTransformStorage::composeWorld` · `SceneTransformHierarchy::notifyWorldUpdated` 입니다.
         */
        void updateWorldTransformFromParent();

        /**
         * @brief 부모 SceneComponent 에 붙입니다. 붙일 수 없는 부모(`canAttachTo` 가 false)면 아무것도 하지 않고 false 입니다.
         * @param rule 로컬을 지킬지(기본) 월드 자리를 지킬지(`AttachRule`)입니다. 틱 중이면 규칙째 미뤄 그때의 월드를 지킵니다.
         */
        [[nodiscard]] bool attachToComponent( SceneComponent* pParent, AttachRule rule = AttachRule::KeepRelative );
        /**
         * @brief @p pParent 에 붙일 수 있는지 봅니다. 붙이는 길(직접 · 틱 중 미룸 · 오브젝트 단위)이 모두 이것을 먼저 묻습니다.
         * @details 안 되는 것: 자기 자신 · null · **다른 매니저(씬)의 부모**(계층이 두 매니저에 걸치면 한쪽의 더티 루트 목록에 다른 쪽
         *          루트가 오르고, 그 루트가 파괴되면 남은 포인터를 플러시가 읽는다) · 파괴 대기 중인 부모 · 컴포넌트 사슬의 순환 · **오브젝트
         *          사슬의 순환**(primary 가 아닌 컴포넌트(소켓)를 거치면 컴포넌트 사슬은 끊겨 있어도 오브젝트는 서로의 부모가 될 수 있다 —
         *          그러면 `GameObject::isDescendantOf` 가 끝나지 않는다). 언리얼 `AttachToComponent` 가 자기 · 순환을 거절하고, 유니티는
         *          계층이 씬을 넘지 않게 하는 것과 같은 규칙입니다.
         */
        bool canAttachTo( const SceneComponent* pParent ) const;

        /** @brief 부모 컴포넌트에서 뗍니다(틱 중이면 미룹니다). `AttachRule::KeepWorld` 면 뗀 뒤에도 월드 자리를 지킵니다. */
        void detachFromComponent( AttachRule rule = AttachRule::KeepRelative );

        /** @brief 부모 SceneComponent 를 반환합니다. */
        SceneComponent* getParent() const { return _pParent; }

        /** @brief 자식 SceneComponent 목록을 반환합니다. */
        const vector<SceneComponent*>& getChildren() const { return _listChild; }

        /** @brief 트랜스폼이 바뀌었을 때 행렬 캐시를 다시 계산하도록 더티로 표시합니다. */
        void markTransformDirty();

        /** @brief 트랜스폼 저장소의 칸 번호입니다. 컴포넌트가 사는 동안 바뀌지 않습니다. */
        uint32 getTransformSlot() const { return _transformSlot; }

        /** @brief 트랜스폼 캐시가 더티면 true 입니다. */
        bool isTransformDirty() const { return _bIsTransformDirty.load( std::memory_order_relaxed ) == SW_TRUE; }
        /** @brief 더티 자손이 있으면 true 입니다. */
        bool hasDirtyDescendant() const { return _bHasDirtyDescendant.load( std::memory_order_relaxed ) == SW_TRUE; }
        /** @brief 더티 자손 플래그를 지웁니다. */
        void clearDirtyDescendant() { _bHasDirtyDescendant.store( SW_FALSE, std::memory_order_relaxed ); }

        /**
         * @brief `_pParent` 에서 부착 직렬화 필드를 채웁니다. 같은 오브젝트의 부모는 소유자 칸을 비우고, 다른 오브젝트는 이름과 id 를 적습니다.
         * @details 부모가 없는데 찾지 못한 참조를 남겨 두었으면(`keepUnresolvedAttach`) 그것을 그대로 적습니다 — 지우지 않습니다. 남겨 둔 id 가
         *          지금 쓰는 공간(`options._pSavedIdMap` 유무)과 다르면 id 를 비우고 이름 · 컴포넌트 키만 적습니다.
         */
        void syncAttachSerializeFields( const ObjectSaveOptions& options ) const;
        /** @brief 상태에서 읽은 부착 필드입니다. 공간은 묶음이 압니다(`_idSpace` 는 채우지 않습니다). */
        SceneAttachReference getLoadedAttachReference() const;
        /**
         * @brief 찾지 못한 부착 참조를 남깁니다. 다음 저장이 살아 있는 부모(없음)에서 필드를 다시 만들어 연결을 지우지 않게 합니다.
         * @details 프리팹이 없는 엔티티의 자식 · 아직 없는 부모처럼 나중에 풀릴 수 있는 참조입니다(유니티의 missing reference 와 같은 자리).
         *          붙이거나 떼면(`attachToComponent` · `detachFromComponent`) 잊습니다.
         */
        void keepUnresolvedAttach( const SceneAttachReference& reference );
        /** @brief 남겨 둔(찾지 못한) 부착 참조가 있으면 true 입니다. */
        bool hasUnresolvedAttach() const { return _unresolvedAttach != nullptr; }

    protected:
        /**
         * @brief 월드가 바뀔 때 `onWorldTransformUpdated` 를 부를지 정합니다. 기본은 부릅니다.
         * @details 메시 컴포넌트는 끕니다 — 칸에 적힌 프리미티브 번호로 등록부에 바로 더티를 찍으므로, 틱 뒤 적용이 컴포넌트를 건너다니지 않습니다.
         */
        void setWorldTransformNotify( bool bNotify );
        /** @brief 칸에 렌더 프리미티브 번호를 적습니다(`PrimitiveRegistry` 가 준 번호, 없으면 `SceneTransformStorage::kNoPrimitive`). */
        void setTransformPrimitiveIndex( uint32 primitiveIndex );

    private:
        /** @brief 매니저가 병렬 틱 중(트랜스폼 읽기 전용)인지 반환합니다. 이때 세터는 바로 쓰지 않고 틱 뒤에 적용합니다. */
        bool isInParallelTick() const;
        /**
         * @brief 세터 셋의 몸통입니다. 비트 하나(`SceneTransformPage::LocalValueBit`)의 로컬 값을 씁니다.
         * @details 병렬 틱 중이면 `writeTickTransform` 으로 가고, 아니면 칸에 쓰고(거의 같은 값이면 건너뜀) 더티를 표시합니다.
         */
        void setLocalValue( uint8 bit, const float3& value );
        /**
         * @brief 병렬 틱 중의 세터 한 건입니다. 이 컴포넌트의 오브젝트를 틱하는 스레드면 칸의 대기 자리에 바로 쓰고, 아니면 쓰기 큐에 올립니다.
         * @details 대기 자리는 칸의 주인 오브젝트를 틱하는 스레드만 씁니다(한 오브젝트의 항목은 한 워커가 돈다). 그래서 잠금 없이 쓰고, 칸이
         *          처음 대기에 들 때만 그 스레드의 대기 목록에 번호를 올립니다. 틱 중에 다른 스레드가 읽는 로컬 · 월드 값은 그대로입니다(틱 전 값).
         *          다른 오브젝트의 컴포넌트에 쓰는 것은 두 스레드가 한 칸에 쓸 수 있어 쓰기 큐로 갑니다.
         */
        void writeTickTransform( uint8 bit, const float3& value );
        /** @brief 페이지 안의 자리입니다. */
        uint32 getPageIndex() const { return _transformSlot & SceneTransformPage::kSlotMask; }
        /** @brief 월드가 다시 합성된 직후입니다. 칸의 프리미티브에 렌더 더티를 찍고, 알림이 켜져 있으면 `onWorldTransformUpdated` 를 부릅니다. */
        void notifyWorldTransformUpdated();
        /** @brief 칸 플래그 비트 하나를 켜거나 끕니다(게임 스레드, 구조 변경 때). */
        void setTransformFlag( uint8 flag, bool bOn );
        /**
         * @brief 틱 중의 쓰기 한 건을 자기 스레드 슬롯의 쓰기 큐에 올립니다(`writeTickTransform` 의 다른 오브젝트 길). 핸들과 대상 포인터는 여기서 채웁니다.
         * @details 큐에 쌓인 건은 같은 `tick()` 안에서 적용되므로 대상 포인터를 믿어도 됩니다(파괴는 틱 밖에서만 메모리를 놓습니다).
         */
        void queueTickWrite( SceneTransformWrite& write );
        /**
         * @brief 병렬 틱이 끝난 뒤 이 컴포넌트의 @p pMethod 를 다시 부르도록 미룹니다. 핸들로 되찾으므로 그 사이에 파괴돼도 안전합니다.
         * @details 인자 없는 메서드(`markTransformDirty`)용입니다. 인자가 있는 `attachToComponent` · `detachFromComponent` 는 부모 핸들 · 부착 규칙을
         *          함께 싣느라 따로 둡니다.
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
        /** @brief 부모가 바뀐 직후 소유 오브젝트의 계층 활성을 다시 맞춥니다(소유자가 없거나 삭제 대기면 건너뜁니다). */
        void refreshOwnerActiveInHierarchy();
        /**
         * @brief 월드 캐시가 더티면 더티인 조상 사슬부터 위에서 아래로 합성합니다. 병렬 틱 중이면 아무것도 하지 않습니다.
         * @details 합성은 `updateWorldTransformFromParent` 를 지나므로 `onWorldTransformUpdated` 가 여기서도 불립니다.
         */
        void ensureWorldCache() const;
        /**
         * @brief 자기를 더티로, 조상에 "자손 더티" 를 세웁니다. 더티 루트 목록에 올려야 할 루트를 돌려줍니다(이미 올라 있으면 nullptr).
         * @details 바이트 저장뿐이라 워커(`markHierarchyDirtyParallel`)와 직렬(`markTransformDirty`)이 같이 씁니다. 목록에 올리는 쪽만 다릅니다.
         */
        SceneComponent* markSelfAndAncestorsDirty();
        /** @brief 자손 전부를 더티로, 자식이 있는 노드에 "자손 더티" 를 세웁니다. 반복문이고 세대는 건드리지 않습니다. */
        void markDescendantsDirty();
        /**
         * @brief 계층이 있는 노드의 로컬 값이 워커에서 바뀐 뒤 부릅니다. 자기 · 조상 · 자손을 더티로 세우고 루트를 워커 스크래치에 올립니다.
         * @details 틱 뒤 적용 · 배치 쓰기의 뒤처리(`SceneTransformHierarchy::applyLocalChange`)가 계층이 있는 칸에 부릅니다.
         */
        void markHierarchyDirtyParallel();

        /** @brief 로컬 위치 값의 자리(칸)입니다. 리플렉션은 이 함수로 `_localPosition` 을 찾습니다. */
        PROPERTY( Name = "_localPosition", Category = "Transform", DisplayName = "Position", Tooltip = "Local translation vector", Meta = "Units=m" )
        float3& getLocalPositionRef() { return _pTransformPage->_arrLocalPosition[getPageIndex()]; }
        /** @brief 로컬 회전 값의 자리(칸)입니다. 리플렉션은 이 함수로 `_localRotation` 을 찾습니다. */
        PROPERTY( Name = "_localRotation", Category = "Transform", DisplayName = "Rotation", Tooltip = "Local Euler angles (Pitch, Yaw, Roll)", Meta = "Units=rad" )
        float3& getLocalRotationRef() { return _pTransformPage->_arrLocalRotation[getPageIndex()]; }
        /** @brief 로컬 스케일 값의 자리(칸)입니다. 리플렉션은 이 함수로 `_localScale` 을 찾습니다. */
        PROPERTY( Name = "_localScale", Category = "Transform", DisplayName = "Scale", Tooltip = "Local scale vector" )
        float3& getLocalScaleRef() { return _pTransformPage->_arrLocalScale[getPageIndex()]; }

        /** @brief 부모 오브젝트의 이름입니다. 같은 오브젝트면 비어 있습니다. 사람이 읽는 값이고, 다른 오브젝트는 `_attachOwnerId` 로 찾습니다. */
        PROPERTY( HideInInspector )
        mutable hashed_string _attachOwner;
        /**
         * @brief 다른 오브젝트인 부모의 id 입니다. 씬 파일에서는 파일 id, 스냅샷에서는 런타임 id 입니다(`ObjectIdSpace`). 0 이면 같은 오브젝트이거나 id 가 없는 데이터입니다.
         * @details 주의: 이름만으로 찾지 말 것 — 매니저는 이름을 유일하게 바꾸므로(`X` → `X_2`) 복제본 · 두 번 놓은 프리팹 · 되돌린 오브젝트가
         *          **같은 이름의 다른 오브젝트**(원본 · 자동으로 생긴 카메라)에 붙는다.
         */
        PROPERTY( HideInInspector )
        mutable uint64 _attachOwnerId;
        PROPERTY( HideInInspector )
        mutable hashed_string _attachComponent;
        /**
         * @brief 트랜스폼 칸이 든 페이지입니다. 페이지는 옮기지 않으므로 표를 거치지 않고 찾습니다.
         * @details 이 셋(페이지 · 칸 번호 · 매니저)은 세터가 매번 읽는다. 한 캐시 줄(Component 뒤 첫 줄)에 붙여 둔다 — 칸 번호가 계층
         *          필드 뒤에 있으면 세터 하나가 이 객체의 줄 셋을 건드린다.
         */
        SceneTransformPage* _pTransformPage;
        /** @brief 트랜스폼 저장소의 칸 번호입니다. 만들 때 받고 소멸할 때 놓습니다. */
        uint32 _transformSlot;
        /**
         * @brief 계층의 더티 루트 목록에서의 자기 자리입니다. 목록에 없으면 `kNotInList` 입니다. `SceneTransformHierarchy` 만 만집니다.
         * @details 해제가 이 자리로 O(1) swap-remove 합니다(선형으로 찾으면 8000 개를 지울 때 3200만 번 비교다).
         *          자리는 플러시가 비웁니다.
         */
        static constexpr uint32 kNotInList = 0xFFFFFFFFu;
        uint32                  _dirtyRootIndex;
        /**
         * @brief 이 컴포넌트가 속한 매니저입니다. 등록 시점에 받아 둡니다.
         * @details 언리얼 컴포넌트가 등록 시점에 `GetWorld()` 를 잡아 두는 것과 같은 자리입니다.
         *          씬에 붙지 않았으면 nullptr 이고, 그때는 지연 없이 바로 반영하는 경로를 탑니다.
         */
        GameObjectManager*      _pManager;
        SceneComponent*         _pParent;
        vector<SceneComponent*> _listChild;
        /** @brief 찾지 못한 부착 참조입니다(`keepUnresolvedAttach`). 거의 늘 비어 있어 따로 둡니다(컴포넌트마다 40 바이트를 들지 않게). */
        unique_ptr<SceneAttachReference> _unresolvedAttach;
        /// @brief 비트필드가 **아닙니다.** 적용 · 배치 쓰기의 워커들이 이 둘을 같은 조상 · 자손에 겹쳐 세웁니다(`markHierarchyDirtyParallel`) —
        /// 비트필드면 이웃 비트까지 쓴다. 같은 값을 쓰니 결과는 무해하지만 평범한 바이트면 데이터 경쟁(미정의 동작)이라 ThreadSanitizer 가
        /// 짚었다 — relaxed 원자로 둔다(x86 에서 같은 명령).
        atomic<uint8> _bIsTransformDirty;
        atomic<uint8> _bHasDirtyDescendant;
        /**
         * @brief 루트일 때 계층의 더티 루트 목록에 올라 있는지 나타냅니다. `SceneTransformHierarchy` 만 만집니다.
         * @details 원자인 이유: 배치 쓰기의 워커 둘이 같은 루트 아래의 자식을 써서 동시에 올리려 할 때 한 번만 오르게 합니다(exchange).
         */
        atomic<uint8> _bQueuedDirtyRoot;
    };
} // namespace sw

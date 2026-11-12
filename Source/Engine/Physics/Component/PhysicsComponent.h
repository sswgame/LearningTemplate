/**
 * @file PhysicsComponent.h
 * @brief 강체 물리 컴포넌트(바디 · 관절 · 캐릭터, 2D · 3D)의 기반입니다 — 씬의 물리(`ScenePhysics`)에 등록되고, 프레임마다 정해진 순서로 불립니다.
 * @details 컴포넌트는 틱하지 않습니다(병렬 틱에서 물리 씬을 만지면 같은 그룹의 다른 오브젝트가 스케줄에 따라 다른 자리를 본다). 매니저가 틱 ·
 *          트랜스폼 적용이 끝난 뒤 게임 스레드에서 `ScenePhysics::step` 을 부르고, 그것이 등록된 컴포넌트를 단계(바디 → 관절 → 캐릭터) 순으로 돕니다:
 *
 *          1. `beginPhysicsFrame` — 시작 전이면 아무것도 하지 않고, 시작했으면 바디를 만들거나(처음) 트랜스폼의 바깥 변화(코드가 옮김 ·
 *             `teleportTo`)를 바디로 옮깁니다.
 *          2. 고정 스텝마다 `prePhysicsStep`(키네마틱 목표 · 캐릭터 이동) → 씬 step → 그 스텝에 깨어 있던 강체만 자세 기록(`ScenePhysics` 가
 *             백엔드의 `getMovedBodies` 를 돈다 — 정적 · 잠든 바디는 부르지 않는다) → `postPhysicsStep`.
 *          3. `endPhysicsFrame( alpha )` — 마지막 두 스텝 사이를 보간한 자세를 트랜스폼에 씁니다(유니티 Rigidbody interpolation).
 *
 *          바디는 플레이 중에만 있습니다(편집 중에는 시뮬레이션하지 않는다). 컴포넌트를 끄거나 플레이가 끝나면 백엔드 객체를 놓습니다(`releasePhysics`).
 *          핸들은 런타임 값이라 저장하지 않습니다 — 핫 리로드 · 되돌리기로 다시 만든 컴포넌트는 새 바디를 만듭니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Physics/Collision/PhysicsShape.h"
#include "Engine/Physics/PhysicsDesc.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct SpatialGridLayerDesc;
    class GameObjectManager;
    class ScenePhysics;

    /** @brief 물리 컴포넌트가 도는 단계입니다. 관절은 바디가 있어야 만들 수 있어 그 뒤입니다. */
    enum class PhysicsComponentPhase : uint8
    {
        Body = 0,
        Joint,
        Character,
        Count
    };

    REFLECT( Abstract, Category = "Physics", DisplayName = "Physics Component", Tooltip = "Base of rigid body, joint and character components", Meta = "Icon=Physics, IconColor=Physics" )
    class SW_API PhysicsComponent : public SceneComponent
    {
        friend class ScenePhysics; ///< 등록 자리(`_physicsIndex`)를 적고 단계 콜백을 부른다

    public:
        // 물리 컴포넌트가 함께 쓰는 자세 · 셰이프 도우미입니다.
        /** @brief 월드 행렬을 자리 · 회전 · 배율로 풉니다. */
        static void readWorldPose( const SceneComponent& component, float3& outPosition, quaternion& outRotation, float3& outScale );
        /**
         * @brief 자리 · 회전 · 배율을 월드 트랜스폼으로 씁니다. 부모가 없으면 행렬을 만들어 다시 풀지 않고 로컬 값에 바로 씁니다.
         * @details 배율은 부르는 쪽이 들고 있는 값이다(트랜스폼 변경 번호가 같으면 지난번에 읽은 배율 그대로). 쓴 값을 다시 읽어 비교하지 않는다 —
         *          바깥 이동은 변경 번호가 알린다(`PhysicsComponent::consumeTransformChange`).
         */
        static void writeWorldPose( SceneComponent& component, const float3& position, const quaternion& rotation, const float3& scale );
        /** @brief Z 축 둘레 각입니다(2D 물리의 회전). */
        static float32 getAngle2D( const quaternion& rotation );
        /** @brief Z 축 둘레 각의 회전입니다. */
        static quaternion makeRotation2D( float32 angle );
        /** @brief 셰이프를 월드 배율로 키웁니다(상자 · 자리 · 점은 축마다, 반지름은 가로 축 중 큰 쪽, 캡슐 높이는 Y). */
        static PhysicsShapeDesc3D makeScaledShape( const PhysicsShapeDesc3D& shape, const float3& scale );
        /** @brief 2D 셰이프를 월드 배율(XY)로 키웁니다. */
        static PhysicsShapeDesc2D makeScaledShape( const PhysicsShapeDesc2D& shape, const float3& scale );

        REFLECT_BODY();
        /** @brief 등록되지 않았다는 표시입니다. */
        static constexpr uint32 kNotRegistered = constant::invalid_index::kUint32;

        explicit PhysicsComponent( PhysicsComponentPhase phase );
        ~PhysicsComponent() override = default;

        /**
         * @brief 3D 캐릭터 컨트롤러 · 3D 강체가 등록 동안 올리는 씬 공간 색인 층입니다(`SceneSpatialGrid`, XZ, 자리는 이 컴포넌트의 월드 자리).
         * @details 거르기(움직이는 강체인지, 캐릭터와 한 오브젝트인지)는 읽는 쪽이 지금 상태로 한다 — 바디 종류가 바뀌어도 항목을 옮기지 않는다. 사용자(지역 볼륨)가 하나라도 있으면 맞춘다.
         */
        static const SpatialGridLayerDesc& getCharacterSpatialLayer();
        static const SpatialGridLayerDesc& getBodySpatialLayer();

        /** @brief 씬의 물리에 등록합니다(바디는 시작한 뒤 첫 물리 프레임에 생긴다). */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 백엔드 객체를 놓고 등록을 풉니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 플레이가 끝나면 백엔드 객체를 놓습니다. */
        void onEndPlay() override;
        /** @brief 다음 물리 프레임에 바디를 그 자리로 순간이동시키고 보간을 끊습니다. */
        void onTeleported() override;
        /** @brief 값이 바뀌면(에디터 · 코드) 다음 물리 프레임에 백엔드 객체를 다시 만듭니다. */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 등록된 씬의 물리입니다(등록 전이면 nullptr). */
        ScenePhysics* getScenePhysics() const { return _pScenePhysics; }
        /** @brief 이 컴포넌트의 단계입니다. */
        PhysicsComponentPhase getPhysicsPhase() const { return _phase; }

    protected:
        /** @brief 시뮬레이션에 들 상태인지입니다 — 시작했고, 켜졌고, 지워지는 중이 아니다. */
        bool isSimulated() const;
        /** @brief 순간이동 표시를 읽고 지웁니다. */
        bool consumeTeleport() { return _bTeleportPending.exchange( false, std::memory_order_acq_rel ); }
        /** @brief 다음 물리 프레임에 백엔드 객체를 다시 만들게 합니다(게임 스레드 — 세터 · 속성 변경). */
        void requestRebuild() { _bRebuildPending = true; }
        /** @brief 다시 만들라는 표시를 읽고 지웁니다. */
        bool consumeRebuild()
        {
            const bool bRebuild = _bRebuildPending;
            _bRebuildPending    = false;
            return bRebuild;
        }
        /**
         * @brief 마지막으로 맞춘 뒤 월드 트랜스폼이 바뀌었는지(칸의 변경 번호, `getWorldTransformVersion`) 보고 지금 번호로 맞춥니다.
         * @details 바뀌지 않았으면 월드 행렬을 풀지 않는다 — 정적 · 잠든 바디 대부분이 이 비교 하나로 끝난다. 자기 쓰기는 `markTransformSynced` 로
         *          맞춰 두므로 바깥(코드 · 부모)이 옮긴 것만 참이다.
         */
        bool consumeTransformChange()
        {
            const uint32 version    = getWorldTransformVersion();
            const bool   bChanged   = version != _syncedTransformVersion;
            _syncedTransformVersion = version;
            return bChanged;
        }
        /** @brief 이 컴포넌트가 트랜스폼에 쓴 뒤 부릅니다 — 자기 쓰기를 다음 프레임에 바깥 이동으로 보지 않게. */
        void markTransformSynced() { _syncedTransformVersion = getWorldTransformVersion(); }
        /**
         * @brief 백엔드 셰이프를 지은 월드 배율과 @p worldScale 이 다른지입니다(축마다 상대 허용치 — 회전한 행렬을 풀 때의 반올림은 같다고 본다).
         * @details 셰이프는 지을 때 배율을 굽는다. 배율은 호출 순서(세터 · 재생성 요청)가 아니라 값으로 다룬다 — 물리 프레임 시작 훅이 마지막으로 읽은
         *          배율과 지은 배율을 비교해 다르면 다시 짓는다(언리얼 `UpdateBodyScale` 은 트랜스폼 갱신마다 셰이프를 바꾸고, Godot 은 바디 배율을 경고만 한다).
         */
        bool isShapeScaleStale( const float3& worldScale ) const;
        /** @brief 백엔드 셰이프를 @p scale 로 지었다고 적습니다(바디 · 캐릭터를 만든 직후). */
        void markShapeScale( const float3& scale ) { _shapeScale = scale; }

        /**
         * @brief 물리 프레임 시작 — 백엔드 객체를 만들거나 트랜스폼의 바깥 변화를 옮깁니다.
         * @details 네 훅의 기본 구현은 아무것도 하지 않는다. 재정의 여부는 `PhysicsHooksOf<T>`(`PhysicsHookRegistry.h`)가 `&T::훅` 의 형식으로 컴파일에서 정하고,
         *          `ScenePhysics` 는 재정의한 타입만 타입별 일괄 함수로 부른다(등록이 없는 타입만 가상 호출).
         */
        virtual void beginPhysicsFrame( ScenePhysics& physics );
        /** @brief 고정 스텝 직전 — @p stepIndex 는 이 프레임의 몇 번째 스텝인지(0 부터), @p stepCount 는 이 프레임의 스텝 수입니다. */
        virtual void prePhysicsStep( ScenePhysics& physics, float32 fixedDeltaTime, uint32 stepIndex, uint32 stepCount );
        /** @brief 고정 스텝 직후 — 자세를 기록합니다. */
        virtual void postPhysicsStep( ScenePhysics& physics );
        /** @brief 물리 프레임 끝 — 보간 비 @p alpha 로 자세를 트랜스폼에 씁니다. */
        virtual void endPhysicsFrame( ScenePhysics& physics, float32 alpha );
        /** @brief 백엔드 객체(바디 · 관절 · 캐릭터)를 놓습니다. 몇 번 불려도 됩니다. */
        virtual void releasePhysics( ScenePhysics& physics ) { (void)physics; }
        /**
         * @brief 지금 트랜스폼 자리의 3D 정적 바디(막는 것, 트리거 아님)를 서술해 @p outListBody 에 붙입니다. 시작 전(편집 중)에도 불립니다.
         * @details `ScenePhysics::createStaticScene3D` 가 오프라인 베이크(라이트 프로브)의 가림 씬을 이것으로 세운다. 움직이는 바디는 내지 않는다 —
         *          베이크에는 정적인 것만 든다(유니티 Contribute GI · 언리얼 Lightmass 의 Static 과 같은 나눔).
         */
        virtual void appendStaticBodies3D( const ScenePhysics& physics, vector<PhysicsBodyDesc3D>& outListBody ) const
        {
            (void)physics;
            (void)outListBody;
        }

    private:
        ScenePhysics*         _pScenePhysics;
        uint32                _physicsIndex;           ///< `ScenePhysics` 의 단계 목록 자리. 없으면 `kNotRegistered`
        uint32                _syncedTransformVersion; ///< 마지막으로 맞춘 월드 변경 번호(`consumeTransformChange`). 처음 0 — 칸의 번호는 받을 때 1 이상이라 첫 비교는 "바뀜"
        float3                _shapeScale;             ///< 백엔드 셰이프를 지은 월드 배율(`isShapeScaleStale`). 바디 · 캐릭터가 없으면 뜻이 없다
        PhysicsComponentPhase _phase;
        bool                  _bRebuildPending;  ///< 게임 스레드에서만 읽고 쓴다(속성 변경 → 다음 물리 프레임)
        atomic<bool>          _bTeleportPending; ///< 틱 중 다른 워커가 세울 수 있어 원자값이다
        SlotHandle            _spatialEntry;     ///< 씬 공간 색인 항목(3D 캐릭터 · 강체만, 등록 ~ 해제)
    };
} // namespace sw

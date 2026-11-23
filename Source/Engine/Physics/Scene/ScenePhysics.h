/**
 * @file ScenePhysics.h
 * @brief 씬 하나의 강체 물리 — 3D · 2D 물리 씬(처음 쓸 때 만든다), 고정 스텝 누적기, 물리 컴포넌트 레지스트리, 접촉 이벤트를 컴포넌트에 나눠 주기입니다.
 * @details 씬 서비스(`ISceneService`)입니다. 이 .cpp 의 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생기고(`manager.getSubsystem<sw::ScenePhysics>()`),
 *          매니저는 프레임 표의 `Physics` 단계(`SceneFrameStepList.xxx`)에서 게임 스레드로 `update` 를 한 번 부릅니다. 물리 서비스(`PhysicsSystem`)는
 *          매니저의 서비스 묶음(`SceneEngineServices`)에서 받습니다. 2D 콜라이더(`BoxCollider2DComponent`)도 같은 2D 씬의 Box2D 센서 바디이고(유니티 트리거 ·
 *          Godot Area2D 처럼 물리 월드 하나), 에디터 시각화가 훑을 콜라이더 목록(`getColliders2D`)과 연속 콜라이더가 쓸어 낸 겹침(`queueSweptOverlap2D`)을 이 서비스가 듭니다.
 *
 *          프레임 하나: 누적기가 스텝 수 n 을 정한다 → 고정 스텝 그룹 `FrameBegin`(컴포넌트 `onPhysicsFrameBegin` — 틱 스케줄러 `tickFixedStepGroup`) → 연속 콜라이더 쓸기(`sweepContinuousColliders2D` — 모든 콜라이더가
 *          이번 자리로 옮겨진 뒤라 상대의 이동을 안다) → n 번(`onPrePhysicsStep` → 3D · 2D 씬 step → 그 스텝에 깨어 있던 바디의 주인 강체만
 *          자세를 읽는다(`getMovedBodies` · `bindBodyOwner` — 백엔드 일괄 읽기 `readBodyStates` 를 워커 구간으로 나눠 부른다) → `onPostPhysicsStep`,
 *          이벤트를 모은다) → `onPhysicsFrameEnd`(보간 비 `FixedStepContext::getAlpha` — 보간한 자세 — 루트 바디는 `queuePoseWrite` 로 모아 `applyTransformBatch` 한 번에 쓴다).
 *          모은 이벤트는 이 단계에서 나눠 주지 않는다 — `FlushPostPhysics` 단계의 `ScenePhysicsEvents` 가 트랜스폼을 펼친 뒤 두 오브젝트의 켜진 컴포넌트에
 *          나눠 준다(막는 접촉은 바디 쌍마다 `onCollision*`, 트리거나 겹침 전용 바디가 낀 겹침은 `PhysicsOverlapTracker` 가 오브젝트 쌍으로 모은 진입 · 이탈만 `onOverlap*` — 이탈 먼저; 언리얼 Chaos 의
 *          지연 충돌 알림 `DispatchPendingCollisionNotifies` · 유니티 `OnCollision*` 이 시뮬레이션 뒤 한 자리에서 도는 것과 같다). 이벤트의 사용자 값은 오브젝트 id 입니다 — 컴포넌트가 아닌 코드(래그돌 빌더)가
 *          만든 바디도 사용자 값에 오브젝트 id 를 실으면 그 오브젝트가 이벤트를 받습니다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Time/FixedStepTimer.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/Collision/PhysicsQuery.h"
#include "Engine/Physics/IPhysicsBodyScene.h"
#include "Engine/Physics/IPhysicsCharacterScene.h"
#include "Engine/Physics/IPhysicsSceneFwd.h"
#include "Engine/Physics/Scene/DebrisBudget.h"
#include "Engine/Physics/Scene/PhysicsOverlapTracker.h"

namespace sw
{
    enum class PhysicsComponentPhase : uint8;

    struct PhysicsSettings;

    struct CollisionInfo;
    struct OverlapInfo;

    class BoxCollider2DComponent;
    class Component;
    class GameObject;
    class GameObjectManager;
    class IPhysicsDebugRenderer;
    class PhysicsComponent;
    class PhysicsSystem;
    class RigidBody2DComponent;
    class RigidBodyComponent;

    /**
     * @struct ContactStayListener
     * @brief 접촉 · 겹침 유지(Stay)를 받는 함수 둘입니다. `ScenePhysics::registerContactStayListener` 로 올린 컴포넌트만 유지를 받습니다(감사 S-1).
     * @details 유지는 닿은 쌍마다 스텝마다 나와 거의 아무도 듣지 않는데 비싸다 — 언리얼 `bNotifyRigidBodyCollision` · Godot `contact_monitor` 처럼 켠 쪽만 만든다.
     *          `Component` 의 가상 함수로 두지 않아 올리지 않고 재정의만 해서 조용히 못 받는 모양을 쓸 수 없다. 인터페이스 기반이 아니라 함수인 까닭은
     *          REFLECT 타입이 두 번째 기반을 가질 수 없어서다. 시작 · 끝은 지금처럼 `Component::onCollision*` · `onOverlap*` 이다.
     */
    struct ContactStayListener
    {
        void ( *_pOnCollisionStay )( Component& self, const CollisionInfo& collision ){ nullptr }; ///< 막는 접촉이 스텝 하나 이어졌다(유니티 `OnCollisionStay`). 안 들으면 nullptr
        void ( *_pOnOverlapStay )( Component& self, const OverlapInfo& overlap ){ nullptr };       ///< 겹친 채로 스텝 하나가 지났다(유니티 `OnTriggerStay`, 쓸어 낸 겹침은 없다). 안 들으면 nullptr
    };

    /**
     * @brief 연속 콜라이더가 한 프레임에 쓸고 지나간 상대 하나입니다(`BoxCollider2DComponent` 의 연속 판정).
     * @details Box2D 센서는 연속 충돌이 없다 — 지난 자리에서 지금 자리까지 셰이프를 쓸어(`shapeCastAll`) 지나친 것을 이것으로 알린다. 지금 자리에서도
     *          겹친 것은 센서가 시작을 내므로 여기 넣지 않는다. 나눠 줄 때 시작과 끝을 함께 낸다(지나쳤다).
     */
    struct SweptOverlap2D
    {
        PhysicsBodyHandle _selfBody{};
        PhysicsBodyHandle _otherBody{};
        uint64            _selfUserData{ 0 };
        uint64            _otherUserData{ 0 };
        float32           _hitFraction{ 1.0f };    ///< 쓸린 길에서 닿은 시점(0..1)
        bool              _bSelfTrigger{ false };  ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        bool              _bOtherTrigger{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 접촉 · 겹침의 시작과 끝 하나입니다(물리 접촉 디버거가 읽는 고정 용량 기록 — `ScenePhysics::collectContactLog`). 유지는 남기지 않는다(스텝마다 쏟아진다). */
    struct PhysicsContactLogEntry
    {
        uint64              _stepIndex{ 0 };  ///< 이벤트가 나온 프레임의 마지막 스텝 번호
        uint64              _objectA{ 0 };    ///< 바디 A 주인 오브젝트 id(바디 사용자 값)
        uint64              _objectB{ 0 };    ///< 바디 B 주인 오브젝트 id
        float3              _point{};         ///< 대표 접촉점(월드, 2D 는 Z=0)
        float3              _normal{};        ///< A 에서 B 쪽 법선
        float32             _impulse{ 0.0f }; ///< 시작 충격량(뉴턴초, 겹침은 0)
        PhysicsContactPhase _phase{ PhysicsContactPhase::Begin };
        uint8               _bOverlap{ SW_FALSE }; ///< 막지 않는 겹침(트리거 · 센서)
        uint8               _bIs2D{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    class ScenePhysics;
    template <typename TOwner>
    class WorkerReadFence; ///< `Engine/Foundation/WorkerReadFence.h` — 선언만 쓴다(헤더 포함 상한)

    /**
     * @class PhysicsReadScope
     * @brief 물리 씬을 워커가 함께 읽는 구간의 표입니다(이동 전용) — 스텝 밖에서만 열리고, 열린 동안 씬은 질의 창(`const IPhysicsQueryScene*`)으로만 보인다.
     * @details 씬 하나는 한 스레드가 다룬다는 계약(`IPhysicsScene.h`)에 읽기 구간을 더한다: Box2D 3.1.1 은 스텝 밖 읽기 전용 질의를 여러 스레드에서 불러도 되고
     *          (스텝 중 읽기는 단언으로 막는다 — `b2World_CastRay`), Jolt 는 잠금 있는 인터페이스(`GetNarrowPhaseQuery`)만 쓴다. 가림 · 시야 · 발자국 질의를 워커에 넘길 때
     *          워커에는 이 표가 내준 질의 창만 건넨다. 닫기(`ScenePhysics::closeReadScope`)는 워커를 기다린 뒤 하고, 그 답인 증표(`WorkerReadFence<ScenePhysics>`)가 있어야
     *          씬을 고치는 길(스텝)이 돈다 — 경로 줄의 `Fence` 와 같은 형식이다. 닫지 않고 버리면 검사 구성에서 멈춘다.
     *          유니티 `RaycastCommand`(잡 질의) · 언리얼 물리 씬 읽기 잠금 자리이고, 일괄 질의 API 는 쓰는 곳이 늘면 그 위에 얹는다.
     */
    class SW_API PhysicsReadScope
    {
    public:
        PhysicsReadScope( PhysicsReadScope&& other ) noexcept;
        PhysicsReadScope& operator=( PhysicsReadScope&& )      = delete;
        PhysicsReadScope( const PhysicsReadScope& )            = delete;
        PhysicsReadScope& operator=( const PhysicsReadScope& ) = delete;
        ~PhysicsReadScope();

        /** @brief 3D 질의 창입니다(3D 씬이 없으면 nullptr). 워커가 함께 불러도 된다. */
        const IPhysicsQueryScene3D* findQuery3D() const { return _pQuery3D; }
        /** @brief 2D 질의 창입니다(2D 씬이 없으면 nullptr). */
        const IPhysicsQueryScene2D* findQuery2D() const { return _pQuery2D; }

    private:
        friend class ScenePhysics;
        PhysicsReadScope( ScenePhysics& owner, const IPhysicsQueryScene3D* pQuery3D, const IPhysicsQueryScene2D* pQuery2D );

    private:
        ScenePhysics*               _pOwner; ///< 닫으면 nullptr
        const IPhysicsQueryScene3D* _pQuery3D;
        const IPhysicsQueryScene2D* _pQuery2D;
    };

    /** @class ScenePhysics @brief 씬 하나의 강체 물리입니다. 파일 머리말 참고. */
    class SW_API ScenePhysics final : public ISceneService
    {
    public:
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ScenePhysics";
        /** @brief 할당 용도입니다 — 만들 때 · 틱 · 프레임 일 동안 매니저가 건다(`Subsystem::getSubsystemMemoryTag`). */
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::Physics;
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 매니저 @p manager 의 서비스 묶음에 든 물리 서비스로 씬을 만들 물리를 둡니다(소유하지 않음).
         * @details 물리 서비스가 없으면(nullptr) 물리 씬을 만들지 않습니다(설정 표도 없다). 그러면 2D 콜라이더도 겹침을 내지 않습니다.
         */
        explicit ScenePhysics( GameObjectManager& manager );
        ~ScenePhysics() override;

        ScenePhysics( const ScenePhysics& )            = delete;
        ScenePhysics& operator=( const ScenePhysics& ) = delete;

        /** @brief 물리 씬을 모두 내립니다(바디 · 관절 · 캐릭터가 함께 사라진다). 매니저가 비울 때 부릅니다. */
        void onSceneCleared() override;
        /** @brief 바디를 모두 옮깁니다(`IPhysicsBodyScene::shiftOrigin`) — 컴포넌트가 주인이 아닌 래그돌 · 파편 · 천 바디까지. */
        void onWorldOriginShifted( const float3& delta ) override;
        /** @brief 물리를 진행합니다(`Physics` 단계, 게임 스레드). */
        void update( float32 deltaTime ) override;
        /** @brief 미리보기 정책의 고정 스텝(`ScenePreviewPolicy::_fixedStepOverride`)을 적용합니다. */
        void applyPreviewPolicy( const ScenePreviewPolicy& policy ) override;

        /**
         * @brief 프레임 시간만큼 물리를 진행하고 이벤트를 모읍니다(나눠 주기는 `deliverFrameEvents`). 파일 머리말의 순서입니다.
         */
        void step( float32 deltaTime );
        /** @brief 3D · 2D 씬의 바디 · 캐릭터를 선으로 냅니다. */
        void drawDebug( IPhysicsDebugRenderer& renderer ) const;

        /** @brief 2D 콜라이더를 목록에 넣습니다(`BoxCollider2DComponent::onRegister`). 이미 있으면 아무것도 하지 않습니다. */
        void registerCollider2D( BoxCollider2DComponent* pCollider );
        /** @brief 2D 콜라이더를 목록에서 뺍니다. 멱등입니다. */
        void unregisterCollider2D( BoxCollider2DComponent* pCollider );
        /** @brief 등록된 2D 콜라이더 목록입니다(순서 없음, 편집 중에도 있다). 에디터 시각화가 씬 전체를 훑지 않고 이것을 봅니다. */
        const vector<BoxCollider2DComponent*>& getColliders2D() const { return _listCollider2D; }
        /** @brief 연속 콜라이더가 쓸고 지나간 겹침을 이번 프레임에 나눠 줄 목록에 더합니다(`sweepContinuousColliders2D` 안, 콜라이더마다 닿은 시점 순서로). */
        void queueSweptOverlap2D( const SweptOverlap2D& overlap ) { _listSweptOverlap2D.push_back( overlap ); }

        /** @brief 3D 씬입니다. 처음 부를 때 생성자로 받은 물리 서비스(`PhysicsSystem`)로 만듭니다. 서비스가 없으면 nullptr 입니다. */
        IPhysicsScene3D* getScene3D();
        /** @brief 2D 씬입니다. 처음 부를 때 만듭니다. */
        IPhysicsScene2D* getScene2D();
        /** @brief 3D 씬이 이미 있으면 그것을, 없으면 nullptr 입니다(만들지 않는다). */
        IPhysicsScene3D* findScene3D() const { return _pScene3D.get(); }
        /** @brief 2D 씬이 이미 있으면 그것을, 없으면 nullptr 입니다. */
        IPhysicsScene2D* findScene2D() const { return _pScene2D.get(); }
        /**
         * @brief 등록된 컴포넌트의 3D 정적 바디(`PhysicsComponent::appendStaticBodies3D`)만 담은 새 3D 씬을 만듭니다. 플레이 씬(`getScene3D`)과 따로이고 편집 중에도 됩니다.
         * @details 오프라인 베이크(라이트 프로브)의 가림 씬이다 — 바디는 시작 뒤에만 생기므로 편집 중에는 플레이 씬이 없고, 있더라도 움직이는 바디가 섞인다.
         *          만든 뒤의 편집은 이 씬에 들지 않는다(베이크하는 동안의 사본). 물리 서비스가 없으면 nullptr 입니다.
         */
        [[nodiscard]] unique_ptr<IPhysicsScene3D> createStaticScene3D() const;
        /** @brief 물리 서비스의 설정 표입니다. 서비스가 없으면 nullptr 입니다. */
        const PhysicsSettings* findSettings() const;
        /** @brief 충돌 프로필 이름의 레이어 번호입니다. 없는 이름이면 오류를 남기고 0(첫 프로필)입니다. 이름이 비면 0 입니다. */
        uint8 resolveLayer( const hashed_string& layerName ) const;
        /**
         * @brief 트레이스 채널 @p traceChannel 로 거르는 질의 필터입니다 — 그 채널을 막는(Block) 질의가 켜진 프로필의 바디만 맞는다(언리얼 `LineTraceByChannel`).
         * @details 설정이 없거나 모르는 · 오브젝트 채널이면 오류를 남기고 nullopt 입니다(모두 맞는 필터로 대신하지 않는다). 쏘는 쪽 무시 칸은 부르는 쪽이 채운다.
         */
        [[nodiscard]] optional<PhysicsQueryFilter> makeTraceFilter( const hashed_string& traceChannel ) const;

        /**
         * @brief 이 씬의 고정 스텝(초)을 엔진 설정 대신 @p seconds 로 둡니다. 0 이면 엔진 설정으로 돌아갑니다. 다음 스텝 전에 누적기를 다시 맞춥니다.
         * @details 미리보기 씬 정책(`ScenePreviewPolicy::_fixedStepOverride`)이 부른다. 서브스텝 수 · 프레임당 상한은 그대로 설정 표의 것이다. 음수는 거절하고 오류를 알린다.
         */
        void setFixedStepOverride( float32 seconds );
        /** @brief 덮어쓴 고정 스텝(초)입니다. 0 이면 엔진 설정을 씁니다. */
        float32 getFixedStepOverride() const { return _fixedStepOverride; }
        /** @brief 고정 스텝 누적기입니다. 스텝 크기 · 프레임당 상한은 설정 표에서 옵니다. */
        const FixedStepTimer& getStepTimer() const { return _stepTimer; }
        /** @brief 마지막 프레임의 보간 비(0..1)입니다. */
        float32 getInterpolationAlpha() const { return _stepTimer.getAlpha(); }
        /** @brief 지금까지 돈 고정 스텝 수입니다. */
        uint64 getStepCount() const { return _stepCount; }
        /**
         * @brief (게임 스레드, 스텝 밖) 워커가 함께 읽는 구간을 엽니다(`PhysicsReadScope`). 열린 동안 스텝은 돌지 않는다(검사 구성 단언).
         * @details 씬을 만들지 않는다 — 아직 없는 차원은 질의 창이 nullptr 이다.
         */
        [[nodiscard]] PhysicsReadScope openReadScope();
        /** @brief (게임 스레드) 워커를 기다린 뒤 구간을 닫고 "읽는 워커 없음" 증표를 돌려줍니다 — 씬을 고치는 길이 이 증표를 받는다. */
        [[nodiscard]] WorkerReadFence<ScenePhysics> closeReadScope( PhysicsReadScope&& scope );
        /** @brief 지금 열린 읽기 구간 수입니다. */
        uint32 getOpenReadScopeCount() const { return _openReadScopeCount; }
        /** @brief 마지막 프레임이 낸 3D 접촉 이벤트입니다(스텝 순서). 다음 `step` 까지 그대로입니다. */
        const vector<PhysicsContactEvent3D>& getFrameEvents3D() const { return _listFrameEvent3D; }
        /** @brief 마지막 프레임이 낸 2D 접촉 이벤트입니다. */
        const vector<PhysicsContactEvent2D>& getFrameEvents2D() const { return _listFrameEvent2D; }
        /** @brief 겹침 추적기입니다 — 오브젝트 쌍의 진입 · 이탈 계약(`PhysicsOverlapTracker`). 감시 값 · 테스트가 읽는다. */
        const PhysicsOverlapTracker& getOverlapTracker() const { return _overlapTracker; }
        /** @brief 접촉 기록의 칸 수입니다(고정 용량). */
        static constexpr uint32 kContactLogCapacity = 32;
        /**
         * @brief 최근 접촉 · 겹침의 시작과 끝(오래된 것부터)을 @p outListEntry 에 채웁니다(먼저 비운다). 고정 용량 고리 — 넘으면 오래된 것이 밀린다.
         * @details 디버그 HUD 는 4 Hz 라 이번 프레임 이벤트만으로는 스친 트리거를 놓친다. 시작 · 끝만 남겨(유지는 빼고) 비용은 그 이벤트의 복사 하나다.
         */
        void collectContactLog( vector<PhysicsContactLogEntry>& outListEntry ) const;

        /**
         * @brief @p component 의 오브젝트에 오는 유지 이벤트를 @p listener 의 함수로 받게 올립니다(보통 `onRegister`). 오브젝트가 없거나, 함수가 둘 다 없거나, 이미 올렸으면 오류입니다.
         * @details 오브젝트의 첫 듣는 쪽이면 물리 씬에 그 오브젝트 id 를 듣는 사용자 값으로 건다(`IPhysicsScene::setContactStayListener`) — 그 오브젝트가 낀 쌍만
         *          유지를 만든다. 나눠 줄 때는 켜진 컴포넌트에만 준다(시작 · 끝과 같은 규칙).
         */
        void registerContactStayListener( Component& component, const ContactStayListener& listener );
        /** @brief 올린 듣는 쪽을 내립니다(`onUnregister`). 오브젝트의 마지막이면 물리 씬의 듣기도 끈다. 올리지 않았으면 아무것도 하지 않습니다. */
        void unregisterContactStayListener( Component& component );
        /** @brief 오브젝트 @p objectID 에 올린 유지 듣는 쪽 수입니다(테스트 · 진단). */
        uint32 getContactStayListenerCount( uint64 objectID ) const;

        /** @brief 물리 컴포넌트를 단계 목록에 넣습니다(`PhysicsComponent::onRegister`). */
        void registerComponent( PhysicsComponent* pComponent );
        /** @brief 물리 컴포넌트를 뺍니다. */
        void unregisterComponent( PhysicsComponent* pComponent );
        /** @brief 등록된 물리 컴포넌트 수입니다(진단 · 테스트). */
        uint32 getComponentCount() const;
        /** @brief 단계 @p phase 에 등록된 물리 컴포넌트 목록입니다(순서 없음, 편집 중에도 있다). 에디터 시각화가 씬 전체를 훑지 않고 이것을 봅니다. */
        const vector<PhysicsComponent*>& getComponents( PhysicsComponentPhase phase ) const { return _arrListComponent[static_cast<uint32>( phase )]; }

        /**
         * @brief 강체 컴포넌트를 자기 바디(`getBodyHandle`)의 주인으로 적습니다. 바디를 만든 직후에 부릅니다.
         * @details 스텝마다 백엔드가 깨어 있던 바디 목록(`getMovedBodies`)을 주면 그 바디의 강체 칸(`PhysicsPoseStore`)에만 자세를 쓴다 — 정적 · 잠든 바디는
         *          스텝마다 건드리지 않는다. 표는 바디 칸 번호 → 강체 칸 번호이고, 강체 칸의 바디 핸들과 세대까지 같을 때만 쓴다.
         */
        void bindBodyOwner( RigidBodyComponent& owner );
        void bindBodyOwner( RigidBody2DComponent& owner );
        /** @brief 바디를 놓기 전에 주인 표에서 뺍니다. 그 칸의 주인이 @p owner 가 아니면 아무것도 하지 않습니다. */
        void unbindBodyOwner( const RigidBodyComponent& owner );
        void unbindBodyOwner( const RigidBody2DComponent& owner );

        /**
         * @brief 물리 컴포넌트 @p writer 가 이번 프레임 끝 월드 자세를 트랜스폼에 쓰겠다고 넣습니다(`onPhysicsFrameEnd` 안에서만).
         * @details 프레임 훅이 끝나면 부모 없는 쓰기를 `GameObjectManager::applyTransformBatch` 한 번으로 적용하고 펼친 뒤, 부모 있는 쓰기를 펼친 부모 위에
         *          풀어 쓰고(`PhysicsComponent::writeWorldPose`), 쓴 컴포넌트마다 변경 번호를 맞춘다(`markTransformSynced`) — 바디마다 세터 셋 · 월드 합성
         *          하나를 부르지 않고, 부모 바디가 목록에서 뒤에 있어도 자식이 지난 자리의 부모로 풀지 않는다(유니티 `TransformAccessArray` · 언리얼 Chaos 의 결과 일괄 반영).
         */
        void queuePoseWrite( PhysicsComponent& writer, const float3& position, const quaternion& rotation, const float3& scale );
        /**
         * @brief 이번 스텝의 3D 캐릭터 이동 하나를 넣습니다(`onPrePhysicsStep` 안에서만). 모든 단계의 `onPrePhysicsStep` 뒤, 씬 step 앞에 한 번에 움직인다
         *        (`IPhysicsCharacterScene3D::moveCharacters`, 감사 S-8) — 결과는 `onPostPhysicsStep` 에서 `getCharacterState` 로 읽는다.
         */
        void queueCharacterMove( PhysicsCharacterHandle character, const float3& velocity );

        /**
         * @brief 마지막 `step` 이 모은 접촉 이벤트를 나눠 줍니다(`ScenePhysicsEvents::update`, `FlushPostPhysics` 단계). 나눠 줄 것이 없거나 이미 나눠 줬으면 아무것도 하지 않습니다.
         * @details 처리가 트랜스폼을 옮기면 PostPhysics 틱 전에 펼친다 — 병렬 틱은 더티 월드를 합성하지 않고 캐시만 읽는다.
         */
        void deliverFrameEvents();

        /**
         * @brief 이 씬의 파괴 오브젝트들이 든 떨어진 덩어리 바디 장부입니다(물리 설정 `_maxDebrisBodyCount` 예산 — `DebrisBudget`).
         * @details 씬마다 따로 든다 — 한 프로세스에 월드가 여럿이면(가상 서버 하니스) 한 월드의 파편이 다른 월드의 예산을 먹지 않게. 프레임 끝 훅 앞에 한 번 고른다.
         */
        DebrisBudget&       getDebrisBudget() { return _debrisBudget; }
        const DebrisBudget& getDebrisBudget() const { return _debrisBudget; }

    private:
        friend class PhysicsComponent; ///< 첫 바디를 알린다(`markObjectSpawned`) — 나타남 표시는 바디를 만드는 쪽만 적는다

        /** @brief 첫 바디가 생긴 오브젝트 하나입니다(`markObjectSpawned`). */
        struct SpawnedObject
        {
            uint64 _objectID{ 0 };
            uint64 _stepCount{ 0 }; ///< 첫 바디가 생길 때의 스텝 번호 — 그 바디가 든 첫 스텝은 이 다음이다
        };

        /**
         * @brief 오브젝트 @p objectID 의 첫 바디(등록 뒤 처음 만든 바디 · 캐릭터)가 막 생겼다고 적습니다(`PhysicsComponent::markBodyCreated`).
         * @details 그 바디가 든 첫 스텝의 겹침 진입은 "안에서 생김" 이다(`OverlapInfo::_bOtherSpawnedInside`). 프레임 수가 아니라 스텝으로 재므로 스텝이 없는 프레임(높은 프레임률)이
         *          몇이 끼어도 표시가 남는다. 다시 켜짐 · 다시 짓기 · 스트리밍 쥠 풀림은 첫 바디가 아니라 적지 않는다.
         */
        void markObjectSpawned( uint64 objectID );
        /** @brief 오브젝트 @p objectID 의 첫 바디가 이번 프레임의 스텝에서 처음 시뮬레이션에 들었으면 true 입니다(나눠 주는 동안 — `dispatchEvents`). */
        bool isSpawnedThisFrame( uint64 objectID ) const;
        /** @brief 열린 읽기 구간이 없음을 확인하고(검사 구성 단언) 씬을 고쳐도 된다는 증표를 만듭니다 — 스텝이 이것을 받는다. */
        WorkerReadFence<ScenePhysics> makeWriteFence() const;
        /** @brief 3D · 2D 월드를 고정 스텝 하나만큼 돌립니다. 읽는 워커가 없다는 증표가 있어야 부를 수 있다. */
        void stepWorlds( const WorkerReadFence<ScenePhysics>& fence, float32 fixedStep );
        /** @brief 3D · 2D 이벤트를 두 오브젝트의 켜진 컴포넌트에 나눠 줍니다. */
        void dispatchEvents( GameObjectManager& manager );
        /** @brief 이벤트 하나를 한 쪽(@p selfUserData)에서 본 것으로 그 오브젝트의 컴포넌트에 줍니다. */
        void deliverEvent( GameObjectManager& manager, uint64 selfUserData, uint64 otherUserData, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody,
                           const float3& point, const float3& normal, float32 impulse, PhysicsContactPhase phase, bool bSelfTrigger, bool bOtherTrigger,
                           bool bOverlap, bool bIs2D, float32 hitFraction, bool bOtherSpawnedInside );
        /** @brief 유지 하나를 그 오브젝트에 올린 듣는 쪽(`registerContactStayListener`)에만 줍니다. 오브젝트가 켜졌는지는 부른 쪽이 봤다. */
        void deliverStay( uint64 selfUserData, GameObject* pOther, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody, const float3& point, const float3& normal,
                          float32 impulse, bool bSelfTrigger, bool bOtherTrigger, bool bOverlap, bool bIs2D );
        /**
         * @brief 이번 프레임에 움직인 2D 콜라이더를 모으고, 그 가운데 연속 콜라이더가 지나친 것을 쓸어 냅니다(`BoxCollider2DComponent::sweepPassedBodies`).
         * @details 모든 콜라이더의 `onPhysicsFrameBegin` 뒤에 돈다 — 콜라이더 하나의 `onPhysicsFrameBegin` 안에서 쓸면 상대가 아직 지난 자리인지 이번 자리인지가
         *          목록 순서로 갈리고, 함께 움직인 상대의 이동을 뺄 수 없다(길을 가로질러 건너간 유닛을 놓친다).
         */
        void sweepContinuousColliders2D();
        /** @brief 설정 표의 스텝 크기 · 상한을 누적기에 한 번 맞춥니다. */
        void configureOnce();
        /** @brief 움직인 바디의 자세를 읽는 프레임 버퍼입니다(용량을 프레임마다 재사용한다). 세 목록은 같은 자리끼리 짝이다. */
        template <typename TDimension>
        struct PoseReadBuffer
        {
            vector<PhysicsBodyHandle>            _listBody;
            vector<uint32>                       _listPoseSlot; ///< 강체 칸 번호(`PhysicsPoseStore`)
            vector<PhysicsBodyState<TDimension>> _listState;
        };
        /**
         * @brief 씬 @p scene 이 마지막 스텝에 움직였다고 한 바디 가운데 강체 칸(@p listPoseSlot)이 확인된 것을 모아, 워커 구간마다 백엔드 일괄 읽기(`readBodyStates`)와
         *        강체 칸 쓰기(`PhysicsBodyPage::applySteppedPose`)를 합니다(.cpp 에서만 쓴다). 칸끼리 겹치지 않아 구간끼리 겹치지 않는다 — 컴포넌트는 건드리지 않는다.
         */
        template <typename TDimension>
        void readMovedBodies( const vector<uint32>& listPoseSlot, const IPhysicsBodyScene<TDimension>& scene, PoseReadBuffer<TDimension>& buffer );
        /** @brief `queuePoseWrite` 로 모은 쓰기를 적용합니다(그 머리말의 순서). */
        void applyPoseWrites();
        /**
         * @brief 바닥(스트리밍 셀)이 없는 자리의 동적 바디 · 캐릭터를 쥐고, 셀이 오른 자리의 것을 풉니다(`PhysicsComponent::isStreamingHeld`, `SceneStreamingSubsystem::isFloorMissing`).
         *        셀이 내려간 후보만 몸 모양을 아래로 쓸어(`PhysicsSettings::_streamingFloorProbeDistance`, 땅 질의 층) 받쳐 줄 충돌(위를 보는 면)이 있으면 쥐지 않는다.
         * @details 스트리밍은 물리보다 아래 층이라 물리가 묻는다. 셀을 스트리밍하지 않는 씬은 쥔 것이 없으면 아무것도 하지 않는다.
         */
        void updateStreamingHolds();
        /** @brief `queueCharacterMove` 로 모은 이동을 3D 씬에 한 번에 넘기고 비웁니다(스텝마다 `onPrePhysicsStep` 뒤). */
        void flushCharacterMoves( float32 fixedDeltaTime );

        /** @brief 유지 듣는 쪽 하나입니다(`registerContactStayListener`) — 켜짐 · 파괴 대기는 컴포넌트로 본다. */
        struct ContactStayListenerEntry
        {
            Component*          _pComponent{ nullptr };
            ContactStayListener _listener{};
        };
        /** @brief 지금 있는 물리 씬에 오브젝트 @p objectID 의 유지 듣기를 겁니다. */
        void applyContactStayListener( uint64 objectID, bool bListen );

        /** @brief 프레임 끝 월드 자세 쓰기 하나입니다(`queuePoseWrite`). */
        struct PoseWrite
        {
            PhysicsComponent* _pWriter{ nullptr };
            float3            _position{};
            quaternion        _rotation{};
            float3            _scale{ 1.0f, 1.0f, 1.0f };
        };
        /** @brief 받은 물리 서비스가 있고 올라와 있으면 그것, 아니면 nullptr 입니다. */
        PhysicsSystem* findInitializedPhysicsSystem() const;

    private:
        GameObjectManager&                                      _manager;             ///< 이 서비스를 가진 매니저(이벤트의 오브젝트 id 를 푼다)
        PhysicsSystem*                                          _pPhysicsSystem;      ///< 매니저의 서비스 묶음에서 받은 물리 서비스(빌림)
        vector<BoxCollider2DComponent*>                         _listCollider2D;      ///< 콜라이더가 자기 자리(`_colliderIndex`)를 든다
        vector<BoxCollider2DComponent*>                         _listMovedCollider2D; ///< 이번 프레임에 움직인 콜라이더(연속 판정의 상대 운동, `sweepContinuousColliders2D` 가 다시 채운다)
        vector<SweptOverlap2D>                                  _listSweptOverlap2D;  ///< 이번 프레임에 연속 콜라이더가 쓸고 지나간 겹침(`step` 이 비운다)
        unique_ptr<IPhysicsScene3D>                             _pScene3D;
        unique_ptr<IPhysicsScene2D>                             _pScene2D;
        FixedStepTimer                                          _stepTimer;
        vector<PhysicsComponent*>                               _arrListComponent[3]; ///< 단계(`PhysicsComponentPhase`)마다 하나. 컴포넌트가 자기 자리(`_physicsIndex`)를 든다
        vector<uint32>                                          _listBodyPoseSlot3D;  ///< 3D 바디 핸들의 칸 번호 → 강체 칸 번호(없으면 `PhysicsPoseStore::kInvalidSlot`, `bindBodyOwner`)
        vector<uint32>                                          _listBodyPoseSlot2D;  ///< 2D 바디 핸들의 칸 번호 → 강체 칸 번호
        vector<PhysicsContactEvent3D>                           _listFrameEvent3D;
        vector<PhysicsContactEvent2D>                           _listFrameEvent2D;
        PoseReadBuffer<PhysicsDimension3D>                      _poseRead3D;                         ///< 스텝마다 움직인 3D 바디의 자세 읽기
        PoseReadBuffer<PhysicsDimension2D>                      _poseRead2D;                         ///< 위와 같다(2D)
        vector<PoseWrite>                                       _listPoseWrite;                      ///< 이번 프레임 끝 자세 쓰기(`queuePoseWrite`)
        vector<PhysicsCharacterMove3D>                          _listCharacterMove3D;                ///< 이번 스텝의 캐릭터 이동(`queueCharacterMove`, 재사용)
        vector<SceneTransformWrite>                             _listTransformWrite;                 ///< 그 가운데 부모 없는 것의 일괄 쓰기(`applyPoseWrites` 가 채운다, 재사용)
        vector<PhysicsContactEvent3D>                           _listDispatchEvent3D;                ///< `dispatchEvents` 가 처리하는 동안의 사본(재사용)
        vector<PhysicsContactEvent2D>                           _listDispatchEvent2D;                ///< 위와 같다
        vector<SweptOverlap2D>                                  _listDispatchSwept;                  ///< 위와 같다
        PhysicsOverlapTracker                                   _overlapTracker;                     ///< 바디 쌍 겹침 → 오브젝트 쌍 진입 · 이탈 · 유지(겹침을 나누는 유일한 길)
        vector<SpawnedObject>                                   _listSpawnedObject;                  ///< 첫 바디가 생겨 아직 첫 스텝의 나눠 주기를 지나지 않은 오브젝트(`markObjectSpawned`)
        vector<PhysicsOverlapEvent>                             _listOverlapTransition;              ///< 이번 프레임 진입 · 이탈(`dispatchEvents` 재사용)
        vector<PhysicsOverlapEvent>                             _listOverlapStay;                    ///< 이번 프레임 겹침 유지(올린 듣는 쪽이 있을 때만, 재사용)
        unordered_map<uint64, vector<ContactStayListenerEntry>> _mapStayListener;                    ///< 오브젝트 id → 유지 듣는 쪽(`registerContactStayListener`)
        vector<ContactStayListenerEntry>                        _listStayTarget;                     ///< 유지를 나눠 주는 동안의 듣는 쪽 사본(재사용)
        PhysicsContactLogEntry                                  _arrContactLog[kContactLogCapacity]; ///< 시작 · 끝 접촉 고리(진단 — `collectContactLog`)
        uint64                                                  _contactLogCount;                    ///< 기록한 누계(다음 칸은 `% kContactLogCapacity`)
        uint64                                                  _stepCount;
        uint32                                                  _openReadScopeCount; ///< 열린 읽기 구간 수(`openReadScope` · `closeReadScope`, 게임 스레드)
        DebrisBudget                                            _debrisBudget;
        float32                                                 _fixedStepOverride; ///< 0 이면 엔진 설정의 고정 스텝(`setFixedStepOverride`)
        uint8                                                   _bConfigured         : 1;
        uint8                                                   _bFrameEventsPending : 1; ///< 마지막 `step` 의 이벤트를 아직 나눠 주지 않았다(`deliverFrameEvents`)
        uint8                                                   _bStreamingHolds     : 1; ///< 스트리밍이 바닥 없는 바디를 쥐고 있을 수 있다(`updateStreamingHolds` — 스트리밍이 끝나면 한 번 모두 푼다)
    };

    /**
     * @class ScenePhysicsEvents
     * @brief 물리 접촉 이벤트를 `FlushPostPhysics` 단계(물리가 쓴 트랜스폼을 펼친 뒤)에서 나눠 주는 씬 서비스입니다(`ScenePhysics::deliverFrameEvents`).
     * @details 물리 단계 안의 동기 가상 호출을 시뮬레이션 밖 한 자리로 뺀다 — 처리는 이번 프레임의 펼친 자리를 보고, 그 뒤 PostPhysics 틱이 처리 결과를 본다.
     */
    class SW_API ScenePhysicsEvents final : public ISceneService
    {
    public:
        static constexpr const utf8*           kSubsystemName = "ScenePhysicsEvents";
        static const SceneServiceRegistration& getSceneServiceRegistration();
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }
        /** @brief 물리 서비스(`ScenePhysics`)가 먼저 선다. */
        span<const utf8* const> getSubsystemDependencies() const override;

        explicit ScenePhysicsEvents( GameObjectManager& manager );
        /** @brief 마지막 물리 프레임의 이벤트를 나눠 줍니다. */
        void update( float32 deltaTime ) override;
        /** @brief 들고 있는 것이 없다 — 남은 이벤트는 `ScenePhysics` 가 든다. */
        void onSceneCleared() override {}

    private:
        GameObjectManager& _manager;
    };
} // namespace sw

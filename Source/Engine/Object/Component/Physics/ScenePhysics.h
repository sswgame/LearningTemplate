/**
 * @file ScenePhysics.h
 * @brief 씬 하나의 강체 물리 — 3D · 2D 물리 씬(처음 쓸 때 만든다), 고정 스텝 누적기, 물리 컴포넌트 레지스트리, 접촉 이벤트를 컴포넌트에 나눠 주기입니다.
 * @details 씬 서비스(`ISceneService`)입니다. 이 .cpp 의 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생기고(`manager.getSubsystem<sw::ScenePhysics>()`),
 *          매니저는 프레임 표의 `Physics` 단계(`SceneFrameStepList.xxx`)에서 게임 스레드로 `update` 를 한 번 부릅니다. 물리 서비스(`PhysicsSystem`)는
 *          매니저의 서비스 묶음(`SceneEngineServices`)에서 받습니다. 겹침만 측정하는 겹침 월드(`SceneOverlapWorld2D` — AABB · 연속 쓸기 ·
 *          `BoxCollider2DComponent` · 키트의 투사체 · 근접 판정)도 이 서비스가 들고, `update` 가 겹침 → 강체 순서로 진행합니다 — 강체 씬과 같은 레이어 표를 받습니다.
 *
 *          프레임 하나: 누적기가 스텝 수 n 을 정한다 → 컴포넌트 `beginPhysicsFrame` → n 번(`prePhysicsStep` → 3D · 2D 씬 step → `postPhysicsStep`,
 *          이벤트를 모은다) → `endPhysicsFrame( alpha )`(보간한 자세를 트랜스폼에 쓴다) → 모은 이벤트를 두 오브젝트의 켜진 컴포넌트에
 *          나눠 준다(막는 접촉은 `onCollision*`, 트리거는 `onOverlap*`). 이벤트의 사용자 값은 오브젝트 id 입니다 — 컴포넌트가 아닌 코드(래그돌 빌더)가
 *          만든 바디도 사용자 값에 오브젝트 id 를 실으면 그 오브젝트가 이벤트를 받습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Engine/Object/Component/Physics/SceneOverlapWorld2D.h"
#include "Engine/Object/GameObject/ISceneService.h"
#include "Engine/Physics/Collision/PhysicsContact.h"
#include "Engine/Physics/FixedStepAccumulator.h"
#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    struct PhysicsSettings;

    class GameObjectManager;
    class IPhysicsDebugRenderer;
    class PhysicsComponent;
    class PhysicsSystem;

    /** @class ScenePhysics @brief 씬 하나의 강체 물리입니다. 파일 머리말 참고. */
    class SW_API ScenePhysics final : public ISceneService
    {
    public:
        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ScenePhysics";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 매니저 @p manager 의 서비스 묶음에 든 물리 서비스로 씬을 만들 물리를 둡니다(소유하지 않음).
         * @details 물리 서비스가 없으면(nullptr) 물리 씬을 만들지 않습니다(설정 표도 없다). 겹침 월드는 그래도 돕니다.
         */
        explicit ScenePhysics( GameObjectManager& manager );
        ~ScenePhysics() override;

        ScenePhysics( const ScenePhysics& )            = delete;
        ScenePhysics& operator=( const ScenePhysics& ) = delete;

        /** @brief 물리 씬을 모두 내립니다(바디 · 관절 · 캐릭터가 함께 사라진다). 매니저가 비울 때 부릅니다. */
        void onSceneCleared() override;
        /** @brief 겹침 월드를 진행한 뒤 강체 물리를 진행합니다(`Physics` 단계, 게임 스레드). */
        void update( float32 deltaTime ) override;

        /**
         * @brief 프레임 시간만큼 물리를 진행하고 이벤트를 나눠 줍니다. 파일 머리말의 순서입니다.
         * @param manager 이벤트의 오브젝트 id 를 풀 매니저
         */
        void step( GameObjectManager& manager, float32 deltaTime );
        /** @brief 3D · 2D 씬의 바디 · 캐릭터를 선으로 냅니다. */
        void drawDebug( IPhysicsDebugRenderer& renderer ) const;

        /** @brief 이 씬의 겹침 월드입니다(AABB 질의 월드 · 2D 콜라이더 등록 · 겹침 이벤트). 질의는 `getOverlapWorld2D().getPhysicsOverlapWorld()` 입니다. */
        SceneOverlapWorld2D& getOverlapWorld2D() { return _overlapWorld2D; }
        /** @brief 이 씬의 겹침 월드입니다. */
        const SceneOverlapWorld2D& getOverlapWorld2D() const { return _overlapWorld2D; }

        /** @brief 3D 씬입니다. 처음 부를 때 생성자로 받은 물리 서비스(`PhysicsSystem`)로 만듭니다. 서비스가 없으면 nullptr 입니다. */
        IPhysicsScene3D* getScene3D();
        /** @brief 2D 씬입니다. 처음 부를 때 만듭니다. */
        IPhysicsScene2D* getScene2D();
        /** @brief 3D 씬이 이미 있으면 그것을, 없으면 nullptr 입니다(만들지 않는다). */
        IPhysicsScene3D* findScene3D() const { return _pScene3D.get(); }
        /** @brief 2D 씬이 이미 있으면 그것을, 없으면 nullptr 입니다. */
        IPhysicsScene2D* findScene2D() const { return _pScene2D.get(); }
        /** @brief 물리 서비스의 설정 표입니다. 서비스가 없으면 nullptr 입니다. */
        const PhysicsSettings* findSettings() const;
        /** @brief 레이어 이름의 번호입니다. 없는 이름이면 오류를 남기고 0(첫 레이어)입니다. 이름이 비면 0 입니다. */
        uint8 resolveLayer( const hashed_string& layerName ) const;

        /** @brief 고정 스텝 누적기입니다. 스텝 크기 · 프레임당 상한은 설정 표에서 옵니다. */
        const FixedStepAccumulator& getAccumulator() const { return _accumulator; }
        /** @brief 마지막 프레임의 보간 비(0..1)입니다. */
        float32 getInterpolationAlpha() const { return _accumulator.getAlpha(); }
        /** @brief 지금까지 돈 고정 스텝 수입니다. */
        uint64 getStepCount() const { return _stepCount; }
        /** @brief 마지막 프레임이 낸 3D 접촉 이벤트입니다(스텝 순서). 다음 `step` 까지 그대로입니다. */
        const vector<PhysicsContactEvent3D>& getFrameEvents3D() const { return _listFrameEvent3D; }
        /** @brief 마지막 프레임이 낸 2D 접촉 이벤트입니다. */
        const vector<PhysicsContactEvent2D>& getFrameEvents2D() const { return _listFrameEvent2D; }

        /** @brief 물리 컴포넌트를 단계 목록에 넣습니다(`PhysicsComponent::onRegister`). */
        void registerComponent( PhysicsComponent* pComponent );
        /** @brief 물리 컴포넌트를 뺍니다. */
        void unregisterComponent( PhysicsComponent* pComponent );
        /** @brief 등록된 물리 컴포넌트 수입니다(진단 · 테스트). */
        uint32 getComponentCount() const;

        /**
         * @brief 이 씬의 파괴 오브젝트들이 든 떨어진 덩어리 바디 수입니다(`gv_destructionMaxDebrisBodies` 예산).
         * @details 씬마다 따로 센다 — 한 프로세스에 월드가 여럿이면(가상 서버 하니스) 한 월드의 파편이 다른 월드의 예산을 먹지 않게.
         */
        uint32 getDebrisBodyCount() const { return _debrisBodyCount; }
        /** @brief 위 수를 @p delta 만큼 바꿉니다(파괴 컴포넌트가 바디를 만들고 지울 때). 0 아래로 내려가지 않는다. */
        void changeDebrisBodyCount( int32 delta );

    private:
        /** @brief 3D · 2D 이벤트를 두 오브젝트의 켜진 컴포넌트에 나눠 줍니다. */
        void dispatchEvents( GameObjectManager& manager );
        /** @brief 이벤트 하나를 한 쪽(@p selfUserData)에서 본 것으로 그 오브젝트의 컴포넌트에 줍니다. */
        void deliverEvent( GameObjectManager& manager, uint64 selfUserData, uint64 otherUserData, PhysicsBodyHandle selfBody, PhysicsBodyHandle otherBody,
                           const float3& point, const float3& normal, float32 impulse, PhysicsContactPhase phase, bool bSelfTrigger, bool bOtherTrigger,
                           bool bIs2D );
        /** @brief 설정 표의 스텝 크기 · 상한을 누적기에 한 번 맞춥니다. */
        void configureOnce();
        /** @brief 받은 물리 서비스가 있고 올라와 있으면 그것, 아니면 nullptr 입니다. */
        PhysicsSystem* findInitializedPhysicsSystem() const;

        GameObjectManager&            _manager;        ///< 이 서비스를 가진 매니저(이벤트의 오브젝트 id 를 푼다)
        PhysicsSystem*                _pPhysicsSystem; ///< 매니저의 서비스 묶음에서 받은 물리 서비스(빌림)
        SceneOverlapWorld2D           _overlapWorld2D; ///< 겹침 월드 — `update` 가 강체보다 먼저 진행한다
        unique_ptr<IPhysicsScene3D>   _pScene3D;
        unique_ptr<IPhysicsScene2D>   _pScene2D;
        FixedStepAccumulator          _accumulator;
        vector<PhysicsComponent*>     _arrListComponent[3]; ///< 단계(`PhysicsComponentPhase`)마다 하나. 컴포넌트가 자기 자리(`_physicsIndex`)를 든다
        vector<PhysicsContactEvent3D> _listFrameEvent3D;
        vector<PhysicsContactEvent2D> _listFrameEvent2D;
        uint64                        _stepCount;
        uint32                        _debrisBodyCount;
        bool                          _bConfigured;
    };
} // namespace sw

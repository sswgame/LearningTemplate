/**
 * @file GameObjectManager.h
 * @brief 씬 하나 — 오브젝트 저장소 · 틱 디스패치 · 틱 중 규칙 · 등록부 · 물리를 소유하고, 게임이 부르는 API 를 전달합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/CameraRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectStore.h"
#include "Engine/Object/GameObject/LightRegistry.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/SceneAudio.h"
#include "Engine/Object/GameObject/SceneFrameStep.h"
#include "Engine/Object/GameObject/SceneNavigation.h"
#include "Engine/Object/GameObject/SceneOverlapWorld2D.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Object/GameObject/SceneTickScheduler.h"
#include "Engine/Object/GameObject/StructuralChangeBuffer.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class SceneComponent;

    /// @brief 씬 하나의 얼굴입니다 — 저장소(`GameObjectStore`) · 틱 디스패치(`SceneTickScheduler`) · 틱 중 규칙(`StructuralChangeBuffer`) · 등록부 · 물리를 소유하고
    ///        `tick` 의 단계를 정합니다. 게임이 부르는 API 는 같은 이름으로 각 단위에 전달합니다.
    class SW_API GameObjectManager
    {
        friend class GameObject;
        friend class StructuralChangeBuffer; ///< `drain` 이 틱 뒤 시작 줄(`dispatchPendingBeginPlay`)을 돌린다

    public:
        /** @brief 엔진과 모듈의 컴포넌트 팩토리를 등록하며 만듭니다. 오브젝트는 없는 채로 시작합니다. */
        GameObjectManager();
        /** @brief 등록된 오브젝트를 모두 파괴합니다. */
        ~GameObjectManager();

        /** @brief 새 GameObject 를 만들고 등록합니다. */
        GameObject* createGameObject( hashed_string name = hashed_string( "GameObject" ) ) { return _store.createGameObject( name ); }

        /**
         * @brief 앞서 발급한 objectId 를 그대로 써서 오브젝트를 다시 만듭니다. 되돌리기 · 플레이 세션 복원 · 핫 리로드가 씁니다.
         * @details 핸들(`GameObjectHandle` · `ComponentHandle`)은 objectId 로 대상을 찾으므로, 되살린 오브젝트가 같은 id 를 받아야
         *          그 너머로도 핸들이 이어집니다. 그 id 로 등록된 오브젝트가 아직 있으면(삭제 대기 포함) 새 id 를 쓰고 경고를 남깁니다.
         *          옛 오브젝트의 지연 파괴가 나중에 id 로 정리하는 항목(슬롯 표 · 에디터 GUID 맵 등)이 새 오브젝트 몫까지 지우지
         *          않게 하기 위해서입니다. 발급 카운터는 그 id 뒤로 밀어 앞으로의 발급과 겹치지 않게 합니다.
         * @return 만든 오브젝트입니다. 실제로 받은 id 는 `getObjectId()` 로 확인합니다.
         */
        GameObject* createGameObjectWithId( hashed_string name, uint64 objectId ) { return _store.createGameObjectWithId( name, objectId ); }

        /**
         * @brief 등록된 GameObject 의 이름이 바뀐 것을 이름 맵에 반영합니다.
         * @details GameObject::setName 이 부릅니다.
         */
        void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName ) { _store.notifyNameChanged( pObj, oldName, newName ); }

        /** @brief 이름으로 GameObject 를 찾습니다. */
        GameObject* findGameObjectByName( hashed_string name ) const { return _store.findGameObjectByName( name ); }

        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 락이 없습니다(칸이 그 id 를 들고 있으면). */
        GameObject* findGameObjectById( uint64 objectId ) const { return _store.findGameObjectById( objectId ); }

        /**
         * @brief 락 없는 id 표의 칸 수입니다. 칸은 id 의 아래 비트(`id % kObjectSlotCount`)입니다.
         * @details 이만큼 떨어진 id 둘이 함께 살아 있을 때만 뒤에 온 것이 맵(잠금 + 해시)으로 갑니다 — `getOverflowObjectCount`.
         */
        static constexpr uint64 kObjectSlotCount = GameObjectStore::kObjectSlotCount;
        /** @brief 표의 칸을 다른 오브젝트가 써서 맵에 든 오브젝트 수입니다. 진단 · 회귀 테스트용입니다(보통 0). */
        uint32 getOverflowObjectCount() const { return _store.getOverflowObjectCount(); }

        /**
         * @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용).
         * @details 같은 클래스의 findGameObjectsByTag 와 같은 규약입니다. 값 반환형은 호출마다 씬
         *          전체를 새로 할당 · 복사하므로, 매 프레임 도는 곳이나 두 번 이상 쓰는 곳은 이쪽을
         *          씁니다. 순회만 하면 되는 곳은 forEachGameObject 가 복사조차 하지 않습니다.
         */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const { _store.getAllGameObjects( outListGameObject ); }

        /** @brief 살아 있는 오브젝트 목록을 새 벡터로 반환합니다(한 번만 쓰는 곳 전용). */
        vector<GameObject*> getAllGameObjects() const
        {
            vector<GameObject*> listAllGameObject;
            _store.getAllGameObjects( listAllGameObject );
            return listAllGameObject;
        }

        /** @brief `forEachGameObject` 가 공유 잠금을 쥔 동안의 표시입니다(`GameObjectStore::WalkScope`). */
        using WalkScope = GameObjectStore::WalkScope;

        /**
         * @brief 힙 할당 없이 등록된 모든 유효한 GameObject 를 순회합니다.
         * @note 공유 잠금을 쥔 채 콜백을 부릅니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 안 됩니다(`WalkScope`) —
         *       그런 일을 하는 순회는 `getAllGameObjects( out )` 로 목록을 받아 잠금 없이 돕니다(`beginPlay` 처럼).
         */
        template <typename Func>
        void forEachGameObject( Func&& func ) const
        {
            _store.forEachGameObject( std::forward<Func>( func ) );
        }

        /** @brief 힙 할당 없이 씬의 모든 유효한 Component 를 순회합니다. */
        template <typename Func>
        void forEachComponent( Func&& func ) const
        {
            forEachGameObject( [&func]( GameObject* pObj )
            {
                pObj->forEachComponent( func );
            } );
        }

        /** @brief 힙 할당 없이 씬에서 TComponent 와 그 파생 컴포넌트를 순회합니다. TypeInfo 는 씬 전체에 한 번만 구합니다. */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( Func&& func ) const
        {
            const TypeInfo* pToType = findStaticType<TComponent>();
            forEachGameObject( [&func, pToType]( GameObject* pObj )
            {
                pObj->forEachComponentOfType<TComponent>( pToType, func );
            } );
        }

        /** @brief 태그를 가진 첫 GameObject 를 반환합니다. 없으면 nullptr 입니다. */
        GameObject* findGameObjectByTag( TagID tag ) const { return _store.findGameObjectByTag( tag ); }

        /** @brief 태그를 가진 GameObject 를 outListGameObject 에 넣습니다. */
        void findGameObjectsByTag( TagID tag, vector<GameObject*>& outListGameObject ) const { _store.findGameObjectsByTag( tag, outListGameObject ); }

        /**
         * @brief 플레이를 시작합니다 — 살아 있는 오브젝트의 컴포넌트마다 onBeginPlay 를 한 번 부르고, 이후 붙는 컴포넌트는 다음 틱 단계에서 시작합니다.
         * @details 보통은 직접 부르지 않고 `SceneManager::setWorldPlaying` · 활성 씬 교체가 부릅니다. 두 번 불러도 컴포넌트마다 한 번입니다.
         */
        void beginPlay() { _store.beginPlay(); }

        /** @brief 플레이를 끝냅니다 — 시작했던 컴포넌트마다 onEndPlay 를 한 번 부르고, 시작을 기다리던 줄을 비웁니다. */
        void endPlay() { _store.endPlay(); }

        /** @brief 플레이 중(`beginPlay` 뒤, `endPlay` 전)이면 true 입니다. 이때 붙는 컴포넌트는 다음 틱 단계에서 onBeginPlay 를 받습니다. */
        bool hasBegunPlay() const { return _store.hasBegunPlay(); }

        /**
         * @brief 씬 한 프레임을 진행합니다 — 표(`SceneFrameStepList.xxx`)의 단계를 줄 순서대로 돕니다.
         * @details 순서와 각 단계가 하는 일은 그 표 하나에 있습니다. 단계마다 `GT.Scene.tick.*` 프로파일 스코프가 있습니다.
         */
        void tick( float32 deltaTime );

        using FrameStepObserver = Delegate<void( SceneFrameStep )>;
        /**
         * @brief 단계마다 돌기 **직전에** 부를 관찰자를 겁니다(게임 스레드). 묶이지 않은 델리게이트를 주면 풉니다.
         * @details 진단 · 시험용입니다 — 단계 사이의 상태를 보거나 순서를 기록합니다. 관찰자 안에서 구조를 바꾸지 마십시오.
         */
        void setFrameStepObserver( FrameStepObserver observer ) { _frameStepObserver = std::move( observer ); }

        /**
         * @brief 트랜스폼 계층입니다(루트 목록 · 더티 세대 · 플러시 알고리즘). 매니저는 소유하고 tick 의 단계만 정합니다.
         * @details `PhysicsWorld` · `PrimitiveRegistry` 와 같은 자리입니다. 능력은 별도 타입이 갖고, 매니저는 순서만 정합니다. 병렬 시스템을
         *          하나 더하려면 이런 타입 하나와 tick 의 한 줄이면 됩니다. 매니저가 그 알고리즘을 알 필요가 없습니다.
         */
        SceneTransformHierarchy&       getTransformHierarchy() { return _transformHierarchy; }
        const SceneTransformHierarchy& getTransformHierarchy() const { return _transformHierarchy; }

        /** @brief 더티 루트의 SceneComponent 월드 캐시를 계층 순으로 갱신합니다. `getTransformHierarchy().flush()` 와 같습니다. */
        void flushSceneTransforms() { _transformHierarchy.flush(); }

        /** @brief 플러시를 기다리는 더티 루트가 하나라도 있으면 true 입니다. */
        bool hasDirtySceneTransforms() const { return _transformHierarchy.hasDirty(); }

        /**
         * @brief 트랜스폼 쓰기 여러 건을 한 번에 적용합니다. 건수가 많으면 워커에 나눕니다.
         * @details 세터를 컴포넌트마다 부르는 것과 결과가 같습니다(값이 같으면 건너뛰고, 바뀌면 더티 표시). 다른 것은
         *          (1) 핸들 해석 · 필드 쓰기 · 더티 표시가 워커에서 나란히 돌고 (2) 세대는 배치 끝에 한 번 오른다는 것입니다.
         *          핸들이 씬 컴포넌트가 아니거나 죽었으면 그 건은 건너뜁니다.
         *          **틱 중에는 부를 수 없습니다.** 워커가 트랜스폼을 읽는 구간이라 쓰면 안 되고, 구조 변경이 미뤄지는 구간이라
         *          부모 사슬이 흔들립니다. 그때는 건마다 세터로 돌립니다(세터가 틱 중 쓰기 길을 탑니다). 알고리즘은
         *          `SceneTransformHierarchy::applyBatch` 에 있습니다.
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count );

        /**
         * @brief 컴포넌트 틱 중이면 true 입니다. 이때 구조 변경(GameObject 생성 · addComponent · attach · detach)은 미뤄지고,
         *        트랜스폼은 읽기 전용이라 세터가 쓰기 큐로 갑니다.
         * @details 구조 동결과 트랜스폼 읽기 전용은 같은 구간이라 플래그 하나가 둘 다 답합니다(`StructuralChangeBuffer::isFrozen`).
         */
        bool isStructuralMutationFrozen() const { return _structuralChangeBuffer.isFrozen(); }

        using StructuralChangeDelegate = StructuralChangeBuffer::Callback;
        using PostTickDelegate         = StructuralChangeBuffer::Callback;

        /**
         * @brief 틱 중의 구조 변경(컴포넌트 추가 · 부착 · 떼기 · 태그 · 활성)을 **부른 순서대로** 지연 큐에 넣습니다. 틱 직후 가장 먼저 돕니다.
         * @details 구조 변경은 이 큐 하나다(`StructuralChangeBuffer` 머리말의 주의).
         */
        void deferStructuralChange( StructuralChangeDelegate func ) { _structuralChangeBuffer.deferStructuralChange( std::move( func ) ); }
        /**
         * @brief 틱 중의 계층 변경(attach · detach)을 구조 변경 큐에 넣고, 이번 단계에 계층 변경이 미뤄졌다고 적습니다.
         * @details 그 뒤로는 스테이지 경계의 트랜스폼 적용(`SceneTickScheduler`)을 하지 않습니다(`StructuralChangeBuffer::deferHierarchyChange`).
         */
        void deferHierarchyChange( StructuralChangeDelegate func ) { _structuralChangeBuffer.deferHierarchyChange( std::move( func ) ); }

        /**
         * @brief 병렬 틱 중의 트랜스폼 쓰기 한 건을 슬롯 큐에 올립니다. 세터가 `isStructuralMutationFrozen()` 일 때 부릅니다.
         * @details 큐는 `SceneTransformHierarchy` 의 것이고, 틱 뒤 `SceneTransformHierarchy::applyTickWrites` 가 `applyTransformBatch` 와 같은
         *          병렬 적용을 돕니다. 스크래치 슬롯을 받지 못한 스레드(도우미 칸이 다 찬 드문 경우)만 지연 델리게이트로 갑니다.
         */
        void queueTransformWrite( const SceneTransformWrite& write );

        /**
         * @brief 이 스레드가 지금 틱하고 있는 오브젝트입니다. 오브젝트 그룹 틱 · 선행 조건 스테이지가 항목을 도는 동안 채워지고, 그 밖에서는 nullptr 입니다.
         * @details 씬 컴포넌트의 세터가 "내 오브젝트를 틱하는 스레드인가" 를 묻습니다. 그렇다면 한 오브젝트의 항목은 동시에 한 워커만 도므로
         *          칸의 대기 자리에 잠금 없이 바로 쓰고, 아니면(다른 오브젝트의 컴포넌트) 쓰기 큐로 갑니다.
         */
        static const GameObject* getTickingObject() { return StructuralChangeBuffer::getTickingObject(); }

        /**
         * @brief 병렬 틱이 끝난 뒤 메인 스레드에서 실행할 작업을 넣습니다.
         * @details GameObject 생성 · addComponent · 데미지 · 태그 변경 같은 구조 · 공유 상태 변경에 씁니다.
         */
        void deferPostTick( PostTickDelegate func ) { _structuralChangeBuffer.deferPostTick( std::move( func ) ); }

        /**
         * @brief 구조 변경이 얼어 있으면 deferPostTick 으로 미루고, 아니면 바로 실행합니다.
         * @details createGameObject + addComponent + 초기화를 한 람다로 묶을 때 씁니다.
         */
        void executeOrDeferPostTick( PostTickDelegate func ) { _structuralChangeBuffer.executeOrDeferPostTick( std::move( func ) ); }

        /** @brief SceneComponent 가 루트가 됐음을 트랜스폼 계층에 알립니다(더티면 플러시 목록에 오릅니다). */
        void registerRootSceneComponent( SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent 를 트랜스폼 계층의 플러시 목록에서 뺍니다. */
        void unregisterRootSceneComponent( SceneComponent* pComp );

        /**
         * @brief 그릴 수 있는 컴포넌트의 등록부입니다.
         * @details PhysicsWorld 와 같은 자리입니다. 능력은 별도 타입으로 두고 매니저는 그것을 소유만
         *          합니다. 컴포넌트는 등록 시점에 이것을 받아 들고 있으므로, 매니저 전체를 알 필요가 없습니다.
         */
        PrimitiveRegistry& getPrimitiveRegistry() { return _primitiveRegistry; }
        /** @brief 그릴 수 있는 컴포넌트의 등록부입니다. */
        const PrimitiveRegistry& getPrimitiveRegistry() const { return _primitiveRegistry; }

        /**
         * @brief 애니메이션 유닛(`SkeletalMeshComponent`)을 평가하는 시스템입니다.
         * @details PrimitiveRegistry 와 같은 자리입니다 — 유닛은 등록 때 이것을 받고, 매니저는 tick 의 단계(틱 뒤 · 트랜스폼 플러시 앞)만 정합니다.
         */
        AnimationSystem&       getAnimationSystem() { return _animationSystem; }
        const AnimationSystem& getAnimationSystem() const { return _animationSystem; }

        /**
         * @brief 빛 컴포넌트의 등록부입니다.
         * @details 프리미티브와 같은 이유로 있습니다. 매 프레임 씬을 뒤져 빛을 **찾지** 않고, 빛이 붙을
         *          때 **등록받습니다**. 자세한 사연은 LightRegistry.h 에 있습니다.
         */
        LightRegistry& getLightRegistry() { return _lightRegistry; }
        /** @brief 빛 컴포넌트의 등록부입니다. */
        const LightRegistry& getLightRegistry() const { return _lightRegistry; }

        /**
         * @brief 카메라 컴포넌트의 등록부입니다. 역할 · 우선순위로 고르는 규칙(`selectCamera`)도 여기 하나입니다.
         * @details 빛과 같은 이유로 있습니다 — 게임 · 에디터 카메라 선택이 씬 전체를 훑지 않습니다. 자세한 사연은 CameraRegistry.h 에 있습니다.
         */
        CameraRegistry& getCameraRegistry() { return _cameraRegistry; }
        /** @brief 카메라 컴포넌트의 등록부입니다. */
        const CameraRegistry& getCameraRegistry() const { return _cameraRegistry; }

        /**
         * @brief 틱에 참여하는 오브젝트의 등록부입니다(언리얼 `FTickTaskManager` 의 자리). 자세한 사연은 TickRegistry.h 에 있습니다.
         * @details 틱 디스패치(`SceneTickScheduler`)가 소유합니다. 컴포넌트가 틱을 켜고 끄면 소유 오브젝트가 여기에 표시하고, 디스패치 전에
         *          표시된 오브젝트만 다시 훑습니다.
         */
        TickRegistry& getTickRegistry() { return _tickScheduler.getTickRegistry(); }
        /** @brief 틱에 참여하는 오브젝트의 등록부입니다. */
        const TickRegistry& getTickRegistry() const { return _tickScheduler.getTickRegistry(); }

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. */
        Component* resolveComponent( ComponentHandle handle ) { return _store.resolveComponent( handle ); }

        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기면 nullptr 입니다. 락을 잡지 않습니다(`findGameObjectById`). */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return _store.resolveGameObject( handle ); }

        /**
         * @brief 이 씬의 강체 물리입니다(3D · 2D 물리 씬 · 고정 스텝 · 물리 컴포넌트 · 접촉 이벤트). 처음 쓸 때 씬을 만듭니다.
         * @details 겹침 월드(`getOverlapWorld2D`)와 따로 돕니다 — 둘 다 `stepPhysics` 에서 이 순서(겹침 → 강체)로 한 번씩 진행합니다.
         */
        ScenePhysics& getScenePhysics() { return _scenePhysics; }
        /** @brief 씬 오디오(리스너 · 에미터 · 가림 · 리버브 존을 엔진에 넣는 자리)입니다. `Scene::tick` 이 틱 뒤에 `update` 를 부릅니다. */
        SceneAudio& getSceneAudio() { return _sceneAudio; }
        /** @brief 이 씬의 강체 물리입니다. */
        const ScenePhysics& getScenePhysics() const { return _scenePhysics; }
        /**
         * @brief 이 씬의 내비게이션(에이전트 종류마다 내비메시 · 군중, 베이크 · 재베이크, 에이전트 갱신)입니다. 물리처럼 소유만 하고 `tick` 의 한 줄만 정합니다 —
         *        물리 앞 틱 결과를 적용한 뒤, 애니메이션 · 물리 앞(에이전트가 캐릭터 컨트롤러에 넘긴 속도가 이번 물리 프레임에 든다).
         */
        SceneNavigation& getSceneNavigation() { return _sceneNavigation; }
        /** @brief 이 씬의 내비게이션입니다. */
        const SceneNavigation& getSceneNavigation() const { return _sceneNavigation; }

        /** @brief 이 씬의 겹침 월드입니다(AABB 질의 월드 · 2D 콜라이더 등록 · 겹침 이벤트). 질의는 `getOverlapWorld2D().getPhysicsWorld()` 입니다. */
        SceneOverlapWorld2D& getOverlapWorld2D() { return _overlapWorld2D; }
        /** @brief 이 씬의 겹침 월드입니다. */
        const SceneOverlapWorld2D& getOverlapWorld2D() const { return _overlapWorld2D; }

        /**
         * @brief GameObject 를 지연 삭제 큐에 넣습니다.
         * @param pObj 삭제할 게임 오브젝트
         * @param bDestroyChildren 자식 오브젝트도 함께 지울지 여부
         */
        void destroyObject( GameObject* pObj, bool bDestroyChildren = true ) { _store.destroyObject( pObj, bDestroyChildren ); }

        /** @brief Component 를 지연 삭제 큐에 넣습니다. 처리 때 핸들로 다시 찾으므로, 그 사이 다른 경로가 먼저 해제해도 안전합니다. */
        void destroyComponent( Component* pComp ) { _store.destroyComponent( pComp ); }

        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다: `onUnregister`(정확히 한 번) → `onDestroy` → 소유자 끊기 → 소멸 → 풀 · 힙 반납.
         * @details 컴포넌트 해체는 이 함수 하나를 지납니다. 목록에서 빼는 일은 `GameObject::removeComponent` · `clearComponents` 가 합니다.
         */
        void destroyComponentInstance( Component* pComp ) { _store.destroyComponentInstance( pComp ); }

        /** @brief 지연 삭제 큐의 오브젝트 · 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction() { _store.processDeferredDestruction(); }

        /** @brief 등록·대기 목록을 모두 비웁니다. */
        void clear();

        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds() { _store.mergePendingAdds(); }

#if !defined( SW_SHIPPING )
        /**
         * @brief 모듈이 내려가기 전에, 그 모듈이 정의한 컴포넌트 타입의 **살아 있는 인스턴스**를 모두 지웁니다.
         * @details 팩토리 · 타입 · 전역 변수는 등록 해제되지만 씬은 엔진이 소유해 모듈보다 오래 삽니다. 인스턴스가 남으면
         *          vtable 이 사라진 객체가 씬에 남아 다음 틱 · 소멸에서 없는 코드로 뛰어듭니다. 지연 파괴 목록에 남은 것도
         *          그 소멸이 모듈 코드이므로 먼저 지금 처리합니다. 틱 밖에서만 부릅니다(구조 변경이 얼려 있으면 미뤄질 뿐입니다).
         * @return 지운 컴포넌트 수입니다.
         */
        uint32 destroyComponentsOfModule( string_view moduleName ) { return _store.destroyComponentsOfModule( moduleName ); }
#endif

        /**
         * @brief 타입 이름(짧은 이름 · FQN · 옛 이름 별칭, `이름#번호` 꼬리는 무시)으로 컴포넌트를 만들어 붙입니다. 에디터 · 직렬화 전용이고, 게임은 addComponent<T> 를 씁니다.
         * @details 생성 함수는 리플렉션 표 하나에서 찾습니다(`TypeRegistry::findType` → `TypeInfo::_addComponent`). 매니저가 따로 든 팩토리 표는 없으므로
         *          모듈이 올라오거나 내려가면 모든 매니저가 그 즉시 같은 답을 냅니다.
         */
        Component* addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning = true );

        /** @brief 모든 오브젝트의 틱 항목을 다음 틱 전에 다시 짓게 합니다(타입 재바인딩 · 씬 초기화). */
        void markTickStagesDirty() { _tickScheduler.getTickRegistry().markAllDirty(); }
        /**
         * @brief 틱 등록부가 오브젝트 항목을 다시 지은 틱의 수입니다. 진단 · 회귀 테스트용입니다.
         * @details 틱에 참여하는 컴포넌트(`Component::hasTickWork`)가 생기거나 없어지거나 순서가 바뀔 때만 올라야 합니다.
         *          틱하지 않는 MeshComponent 를 붙였다 떼는 것으로는 오르지 않습니다. 다시 지을 때도 바뀐 오브젝트의 컴포넌트 몇 개만 훑습니다.
         */
        uint32 getTickStageBuildCount() const { return _tickScheduler.getStageBuildCount(); }
        /** @brief 선행 조건 스테이지 경계에서 틱 중 트랜스폼 쓰기를 적용한 횟수(누적)입니다. 진단 · 회귀 테스트용입니다. */
        uint32 getStageTransformApplyCount() const { return _tickScheduler.getStageTransformApplyCount(); }

        /** @brief 이름으로 만들 수 있는 컴포넌트 타입(리플렉션 표에서 `_addComponent` 가 있는 타입)의 짧은 이름 목록입니다. 에디터의 "Add Component" 가 씁니다. */
        static vector<hashed_string> getRegisteredComponentTypeNames();

        /** @brief 트랜스폼이 바뀌었음을 알려 세대를 올립니다(`getTransformHierarchy().notifyDirtied()`). */
        void notifyTransformDirtied() { _transformHierarchy.notifyDirtied(); }
        /** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
        uint64 getTransformGeneration() const { return _transformHierarchy.getGeneration(); }

    private:
        /** @brief 플레이 중에 붙어 줄을 선 컴포넌트의 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 도는 중에 선 것은 다음 번에 돕니다. */
        void dispatchPendingBeginPlay() { _store.dispatchPendingBeginPlay(); }
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 줄에 세웁니다(`GameObject::attachCreatedComponent`). 핸들로 들어 그새 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle ) { _store.queueBeginPlay( handle ); }
        /** @brief 타입별 컴포넌트 풀을 얻거나 만듭니다(`GameObjectStore::getOrCreateComponentPool`). */
        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize ) { return _store.getOrCreateComponentPool( pTypeInfo, typeSize ); }

        /**
         * @brief 겹침 월드 → 강체 물리를 한 번씩 진행합니다. 틱 · 트랜스폼 적용 뒤, 게임 스레드에서.
         * @details 유니티는 물리 갱신 뒤 OnTrigger 를, 언리얼은 움직임이 끝난 뒤 Begin/EndOverlap 을 부른다. 여기서는 그 프레임에 적용된 월드 자리로 잰다.
         */
        void stepPhysics( float32 deltaTime );

        // 프레임 단계 본문 — 표(`SceneFrameStepList.xxx`)의 줄마다 하나. `tick` 이 줄 순서대로 부른다.
#define SW_SCENE_FRAME_STEP( Name ) void runFrameStep##Name( float32 deltaTime );
#include "Engine/Object/GameObject/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP

        // 단위들은 소유만 한다. 선언 순서가 생성 순서다 — 뒤 단위가 앞 단위를 참조로 받는다(틱 디스패치 → 계층 · 등록부 · 틱 중 규칙,
        // 저장소 → 틱 등록부 · 틱 중 규칙). 오브젝트는 소멸자의 `clear` 가 먼저 지우므로 단위가 사라지는 순서에 기대지 않는다.

        /** @brief 틱 중 규칙(동결 플래그 · 구조 변경 큐 · 틱 뒤 큐 · 비우는 순서)입니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        StructuralChangeBuffer _structuralChangeBuffer;
        /** @brief 트랜스폼 계층입니다. */
        SceneTransformHierarchy _transformHierarchy;
        /** @brief 그릴 수 있는 컴포넌트의 등록부입니다. */
        PrimitiveRegistry _primitiveRegistry;
        /** @brief 빛 컴포넌트의 등록부입니다. */
        LightRegistry _lightRegistry;
        /** @brief 카메라 컴포넌트의 등록부입니다. */
        CameraRegistry _cameraRegistry;
        /** @brief 애니메이션 시스템입니다. */
        AnimationSystem _animationSystem;
        /** @brief 컴포넌트 틱 디스패치(틱 등록부 · 그룹 포크-조인 · 선행 조건 스테이지)입니다. */
        SceneTickScheduler _tickScheduler;
        /** @brief 오브젝트 저장소(생성 · 이름 · id 표 · 조회 · 지연 파괴 · 컴포넌트 풀 · 시작 줄)입니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        GameObjectStore _store;
        /** @brief 겹침 월드입니다. */
        SceneOverlapWorld2D _overlapWorld2D;
        /** @brief 강체 물리입니다. 컴포넌트의 해제가 바디를 놓는다. */
        ScenePhysics _scenePhysics;
        /** @brief 오디오 컴포넌트 등록부와 엔진 묶기입니다. */
        SceneAudio _sceneAudio;
        /** @brief 내비게이션입니다. 컴포넌트(에이전트 · 장애물 · 표면)의 해제가 등록을 뺀다. */
        SceneNavigation _sceneNavigation;
        /** @brief 단계마다 돌기 직전에 부르는 관찰자(`setFrameStepObserver`)입니다. 보통 비어 있습니다. */
        FrameStepObserver _frameStepObserver;
    };
} // namespace sw

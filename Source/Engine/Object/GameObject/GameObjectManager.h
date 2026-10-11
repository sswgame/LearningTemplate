/**
 * @file GameObjectManager.h
 * @brief 씬 하나를 관리하는 매니저입니다. 오브젝트 저장소, 틱 디스패치, 틱 중 규칙, 레지스트리, 물리를 소유하고 게임이 부르는 API 를 각 단위에 전달합니다.
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
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectStore.h"
#include "Engine/Object/GameObject/ISceneFrameSystem.h"
#include "Engine/Object/GameObject/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/SceneAudio.h"
#include "Engine/Object/GameObject/SceneFrameStep.h"
#include "Engine/Object/GameObject/SceneOverlapWorld2D.h"
#include "Engine/Object/GameObject/ScenePhysics.h"
#include "Engine/Object/GameObject/SceneTickScheduler.h"
#include "Engine/Object/GameObject/StructuralChangeBuffer.h"

namespace sw
{
    class Component;
    class GameObjectManager;
    class SceneComponent;
    class SceneNavigation;

    /// @brief 씬 하나를 관리하는 클래스입니다. 오브젝트 저장소(`GameObjectStore`), 틱 디스패치(`SceneTickScheduler`), 틱 중 규칙(`StructuralChangeBuffer`), 레지스트리, 물리를 소유하고
    ///        `tick` 의 단계 순서를 정합니다. 게임이 부르는 API 는 같은 이름으로 각 단위에 전달합니다.
    class SW_API GameObjectManager
    {
        friend class GameObject;
        friend class StructuralChangeBuffer; ///< `drain` 이 틱 뒤 시작 대기열(`dispatchPendingBeginPlay`)을 처리한다

    public:
        /** @brief 엔진과 모듈의 컴포넌트 팩토리를 등록하며 만듭니다. 오브젝트는 없는 채로 시작합니다. */
        GameObjectManager();
        /** @brief 등록된 오브젝트를 모두 파괴합니다. */
        ~GameObjectManager();

        /** @brief 새 GameObject 를 만들고 등록합니다. */
        GameObject* createGameObject( hashed_string name = hashed_string( GameObject::kDefaultName ) ) { return _store.createGameObject( name ); }

        /**
         * @brief 앞서 발급한 objectID 를 그대로 써서 오브젝트를 다시 만듭니다. 되돌리기, 플레이 세션 복원, 핫 리로드가 씁니다.
         * @details 핸들(`GameObjectHandle`, `ComponentHandle`)은 objectID 로 대상을 찾으므로, 복원한 오브젝트가 같은 id 를 받아야
         *          기존 핸들이 계속 유효하다. 그 id 로 등록된 오브젝트가 아직 있으면(삭제 대기 포함) 새 id 를 쓰고 경고를 남긴다.
         *          옛 오브젝트의 지연 파괴가 나중에 id 로 정리하는 항목(슬롯 테이블, 에디터 GUID 맵 등)이 새 오브젝트의 항목까지 지우지
         *          않게 하기 위해서다. 발급 카운터는 그 id 뒤로 밀어서 앞으로의 발급과 겹치지 않게 한다.
         * @return 만든 오브젝트입니다. 실제로 받은 id 는 `getObjectID()` 로 확인합니다.
         */
        GameObject* createGameObjectWithID( hashed_string name, uint64 objectID ) { return _store.createGameObjectWithID( name, objectID ); }

        /**
         * @brief 등록된 GameObject 의 이름이 바뀐 것을 이름 맵에 반영합니다.
         * @details GameObject::setName 이 부릅니다.
         */
        void notifyNameChanged( GameObject* pObj, hashed_string oldName, hashed_string newName ) { _store.notifyNameChanged( pObj, oldName, newName ); }

        /** @brief 이름으로 GameObject 를 찾습니다. */
        GameObject* findGameObjectByName( hashed_string name ) const { return _store.findGameObjectByName( name ); }

        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 잠금을 쓰지 않으며, 슬롯이 그 id 를 가지고 있을 때 찾습니다. */
        GameObject* findGameObjectByID( uint64 objectID ) const { return _store.findGameObjectByID( objectID ); }

        /**
         * @brief 잠금 없는 id 테이블의 슬롯 수입니다. 슬롯은 id 의 아래 비트(`id % kObjectSlotCount`)로 정합니다.
         * @details 이 값만큼 떨어진 id 둘이 함께 살아 있을 때만 나중에 온 것이 맵(잠금과 해시)으로 간다. `getOverflowObjectCount` 를 참고한다.
         */
        static constexpr uint64 kObjectSlotCount = GameObjectStore::kObjectSlotCount;
        /** @brief 테이블의 슬롯을 다른 오브젝트가 써서 맵에 들어간 오브젝트 수입니다. 진단과 회귀 테스트용이며, 보통 0 입니다. */
        uint32 getOverflowObjectCount() const { return _store.getOverflowObjectCount(); }

        /**
         * @brief 살아 있는 오브젝트를 outList 에 채웁니다(부르는 쪽 버퍼 재사용).
         * @details 같은 클래스의 findGameObjectsByTag 와 같은 규약이다. 값 반환형은 호출마다 씬
         *          전체를 새로 할당하고 복사하므로, 매 프레임 도는 곳이나 두 번 이상 쓰는 곳은 이쪽을
         *          쓴다. 순회만 하면 되는 곳은 forEachGameObject 가 복사조차 하지 않는다.
         */
        void getAllGameObjects( vector<GameObject*>& outListGameObject ) const { _store.getAllGameObjects( outListGameObject ); }

        /** @brief 살아 있는 오브젝트 목록을 새 벡터로 반환합니다(한 번만 쓰는 곳 전용). */
        vector<GameObject*> getAllGameObjects() const
        {
            vector<GameObject*> listAllGameObject;
            _store.getAllGameObjects( listAllGameObject );
            return listAllGameObject;
        }

        /** @brief `forEachGameObject` 가 공유 잠금을 쥔 동안 쓰는 표식 타입입니다(`GameObjectStore::WalkScope`). */
        using WalkScope = GameObjectStore::WalkScope;

        /**
         * @brief 힙 할당 없이 등록된 모든 유효한 GameObject 를 순회합니다.
         * @note 공유 잠금을 쥔 채 콜백을 부릅니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 안 됩니다(`WalkScope`).
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
         * @brief 플레이를 시작합니다. 살아 있는 오브젝트의 컴포넌트마다 onBeginPlay 를 한 번 부르고, 이후 붙는 컴포넌트는 다음 틱 단계에서 시작합니다.
         * @details 보통은 직접 부르지 않고 `SceneManager::setWorldPlaying` 이나 활성 씬 교체가 부릅니다. 두 번 불러도 컴포넌트마다 한 번만 시작합니다.
         */
        void beginPlay() { _store.beginPlay(); }

        /** @brief 플레이를 끝냅니다. 시작했던 컴포넌트마다 onEndPlay 를 한 번 부르고, 시작을 기다리던 대기열을 비웁니다. */
        void endPlay() { _store.endPlay(); }

        /** @brief 플레이 중(`beginPlay` 뒤, `endPlay` 전)이면 true 입니다. 이때 붙는 컴포넌트는 다음 틱 단계에서 onBeginPlay 를 받습니다. */
        bool hasBegunPlay() const { return _store.hasBegunPlay(); }

        /**
         * @brief 씬 한 프레임을 진행합니다. 프레임 단계 목록(`SceneFrameStepList.xxx`)의 단계를 위에서부터 차례로 실행합니다.
         * @details 순서와 각 단계가 하는 일은 그 목록 한 곳에 있다. 단계마다 `GT.Scene.tick.*` 프로파일 스코프가 있다.
         */
        void tick( float32 deltaTime );

        using FrameStepObserver = Delegate<void( SceneFrameStep )>;
        /**
         * @brief 단계가 실행되기 **직전에** 부를 관찰자를 등록합니다(게임 스레드). 바인딩되지 않은 델리게이트를 주면 등록을 해제합니다.
         * @details 진단과 테스트용이다. 단계 사이의 상태를 보거나 실행 순서를 기록한다. 관찰자 안에서 구조를 바꾸면 안 된다.
         */
        void setFrameStepObserver( FrameStepObserver observer ) { _frameStepObserver = std::move( observer ); }

        /** @brief 이름으로 프레임 시스템(`ISceneFrameSystem`)을 찾습니다. 없으면 nullptr 입니다. */
        ISceneFrameSystem* findFrameSystem( const hashed_string& key ) const;
        /**
         * @brief 프레임 시스템을 붙입니다(소유권을 넘겨받음). 같은 이름이 있거나 nullptr 이면 거절하고 false 를 반환합니다. 붙인 순서가 `FrameSystems` 단계의 실행 순서입니다.
         * @details 게임 스레드에서, 틱 밖에서 부른다. 단계가 실행되는 중에 붙인 것은 다음 프레임부터 실행한다.
         */
        [[nodiscard]] bool addFrameSystem( const hashed_string& key, unique_ptr<ISceneFrameSystem> pSystem );
        /** @brief 프레임 시스템을 제거합니다. 없으면 아무것도 하지 않습니다. 단계가 실행 중이면 그 단계가 끝난 뒤 해제하므로, 실행 중인 시스템이 자기 자신을 제거해도 됩니다. */
        void removeFrameSystem( const hashed_string& key );

        /**
         * @brief 트랜스폼 계층입니다. 루트 목록, 더티 세대, 플러시 알고리즘을 담고 있으며, 매니저는 이것을 소유하고 tick 의 단계만 정합니다.
         * @details `PhysicsWorld` 와 `PrimitiveRegistry` 도 같은 구조다. 기능은 별도 타입이 갖고, 매니저는 순서만 정한다. 병렬 시스템을
         *          하나 추가하려면 이런 타입 하나와 tick 의 한 줄이면 된다. 매니저가 그 알고리즘을 알 필요가 없다.
         */
        SceneTransformHierarchy&       getTransformHierarchy() { return _transformHierarchy; }
        const SceneTransformHierarchy& getTransformHierarchy() const { return _transformHierarchy; }

        /** @brief 더티 루트의 SceneComponent 월드 캐시를 계층 순서로 갱신합니다. `getTransformHierarchy().flush()` 와 같습니다. */
        void flushSceneTransforms() { _transformHierarchy.flush(); }

        /** @brief 플러시를 기다리는 더티 루트가 하나라도 있으면 true 입니다. */
        bool hasDirtySceneTransforms() const { return _transformHierarchy.hasDirty(); }

        /**
         * @brief 트랜스폼 쓰기 여러 건을 한 번에 적용합니다. 건수가 많으면 워커에 나눕니다.
         * @details 컴포넌트마다 세터를 부르는 것과 결과가 같다(값이 같으면 건너뛰고, 바뀌면 더티로 표시한다). 차이는 두 가지다.
         *          (1) 핸들 해석, 필드 쓰기, 더티 표시를 워커들이 나란히 실행한다. (2) 세대는 배치가 끝날 때 한 번만 오른다.
         *          핸들이 씬 컴포넌트가 아니거나 이미 죽었으면 그 건은 건너뛴다.
         *          **틱 중에는 부를 수 없다.** 틱 중에는 워커가 트랜스폼을 읽으므로 쓰면 안 되고, 구조 변경이 미뤄지는 구간이라
         *          부모 체인이 흔들린다. 그때는 건마다 세터를 부른다(세터가 틱 중 쓰기 경로를 탄다). 알고리즘은
         *          `SceneTransformHierarchy::applyBatch` 에 있다.
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyTransformBatch( const SceneTransformWrite* pWrite, uint32 count );

        /**
         * @brief 컴포넌트 틱 중이면 true 입니다. 이때 구조 변경(GameObject 생성, addComponent, attach, detach)은 미뤄지고,
         *        트랜스폼은 읽기 전용이라 세터가 쓰기 큐로 갑니다.
         * @details 구조 동결과 트랜스폼 읽기 전용은 같은 구간이라 플래그 하나로 둘 다 판정한다(`StructuralChangeBuffer::isFrozen`).
         */
        bool isStructuralMutationFrozen() const { return _structuralChangeBuffer.isFrozen(); }

        using StructuralChangeDelegate = StructuralChangeBuffer::Callback;
        using PostTickDelegate         = StructuralChangeBuffer::Callback;

        /**
         * @brief 틱 중의 구조 변경(컴포넌트 추가, 부착, 분리, 태그, 활성)을 **부른 순서대로** 지연 큐에 넣습니다. 틱 직후에 가장 먼저 실행됩니다.
         * @details 구조 변경은 이 큐 하나를 쓴다(`StructuralChangeBuffer` 머리말의 주의 참고).
         */
        void deferStructuralChange( StructuralChangeDelegate func ) { _structuralChangeBuffer.deferStructuralChange( std::move( func ) ); }
        /**
         * @brief 틱 중의 계층 변경(attach, detach)을 구조 변경 큐에 넣고, 이번 단계에 계층 변경이 미뤄졌다고 기록합니다.
         * @details 그 뒤에는 스테이지 경계의 트랜스폼 적용(`SceneTickScheduler`)을 하지 않는다(`StructuralChangeBuffer::deferHierarchyChange`).
         */
        void deferHierarchyChange( StructuralChangeDelegate func ) { _structuralChangeBuffer.deferHierarchyChange( std::move( func ) ); }

        /**
         * @brief 병렬 틱 중의 트랜스폼 쓰기 한 건을 슬롯 큐에 넣습니다. 세터가 `isStructuralMutationFrozen()` 일 때 부릅니다.
         * @details 큐는 `SceneTransformHierarchy` 가 가지고 있고, 틱 뒤에 `SceneTransformHierarchy::applyTickWrites` 가 `applyTransformBatch` 와 같은
         *          병렬 적용을 실행한다. 스크래치 슬롯을 받지 못한 스레드(도우미 슬롯이 다 찬 드문 경우)만 지연 델리게이트로 간다.
         */
        void queueTransformWrite( const SceneTransformWrite& write );

        /**
         * @brief 이 스레드가 지금 틱하고 있는 오브젝트입니다. 오브젝트 그룹 틱과 선행 조건 스테이지가 항목을 실행하는 동안 채워지고, 그 밖에서는 nullptr 입니다.
         * @details 씬 컴포넌트의 세터가 "내 오브젝트를 틱하는 스레드인가"를 묻는다. 그렇다면 한 오브젝트의 항목은 동시에 한 워커만 실행하므로
         *          슬롯의 대기 위치에 잠금 없이 바로 쓰고, 아니면(다른 오브젝트의 컴포넌트) 쓰기 큐로 보낸다.
         */
        static const GameObject* getTickingObject() { return StructuralChangeBuffer::getTickingObject(); }

        /**
         * @brief 병렬 틱이 끝난 뒤 메인 스레드에서 실행할 작업을 넣습니다.
         * @details GameObject 생성, addComponent, 데미지, 태그 변경처럼 구조나 공유 상태를 바꾸는 일에 쓴다.
         */
        void deferPostTick( PostTickDelegate func ) { _structuralChangeBuffer.deferPostTick( std::move( func ) ); }

        /**
         * @brief 구조 변경이 막혀 있으면 deferPostTick 으로 미루고, 아니면 바로 실행합니다.
         * @details createGameObject, addComponent, 초기화를 한 람다로 묶을 때 쓴다.
         */
        void executeOrDeferPostTick( PostTickDelegate func ) { _structuralChangeBuffer.executeOrDeferPostTick( std::move( func ) ); }

        /** @brief SceneComponent 가 루트가 되었음을 트랜스폼 계층에 알립니다. 더티이면 플러시 목록에 올라갑니다. */
        void registerRootSceneComponent( SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent 를 트랜스폼 계층의 플러시 목록에서 제거합니다. */
        void unregisterRootSceneComponent( SceneComponent* pComp );

        /**
         * @brief 그릴 수 있는 컴포넌트의 레지스트리입니다.
         * @details PhysicsWorld 와 같은 구조다. 기능은 별도 타입으로 두고 매니저는 그것을 소유만
         *          한다. 컴포넌트는 등록할 때 이 레지스트리를 받아 보관하므로 매니저 전체를 알 필요가 없다.
         */
        PrimitiveRegistry& getPrimitiveRegistry() { return _primitiveRegistry; }
        /** @brief 그릴 수 있는 컴포넌트의 레지스트리입니다. */
        const PrimitiveRegistry& getPrimitiveRegistry() const { return _primitiveRegistry; }

        /**
         * @brief 애니메이션 유닛(`SkeletalMeshComponent`)을 평가하는 시스템입니다.
         * @details PrimitiveRegistry 와 같은 구조다. 유닛은 등록할 때 이 시스템을 받고, 매니저는 tick 의 단계(틱 뒤, 트랜스폼 플러시 앞)만 정한다.
         */
        AnimationSystem&       getAnimationSystem() { return _animationSystem; }
        const AnimationSystem& getAnimationSystem() const { return _animationSystem; }

        /**
         * @brief 타입별 컴포넌트 레지스트리입니다(라이트, 2D 그림자 가림막, 상호작용 대상, 환경 질의 대상 등).
         * @details 프리미티브와 같은 이유로 있다. 매 프레임 씬을 뒤져 **찾지** 않고, 컴포넌트가 붙을 때 **등록받는다**.
         *          `forEachComponentOfType` 은 모든 오브젝트를 훑으므로 프레임 경로에 쓰지 않는다. 자세한 배경은 ComponentRegistry.h 에 있다.
         */
        ComponentRegistry& getComponentRegistry() { return _componentRegistry; }
        /** @brief 타입별 컴포넌트 레지스트리입니다. */
        const ComponentRegistry& getComponentRegistry() const { return _componentRegistry; }

        /**
         * @brief 카메라 컴포넌트의 레지스트리입니다. 역할과 우선순위로 카메라를 고르는 규칙(`selectCamera`)도 여기 한 곳에 있습니다.
         * @details 라이트와 같은 이유로 있다. 게임과 에디터의 카메라 선택이 씬 전체를 훑지 않는다. 자세한 배경은 CameraRegistry.h 에 있다.
         */
        CameraRegistry& getCameraRegistry() { return _cameraRegistry; }
        /** @brief 카메라 컴포넌트의 레지스트리입니다. */
        const CameraRegistry& getCameraRegistry() const { return _cameraRegistry; }

        /**
         * @brief 틱에 참여하는 오브젝트의 레지스트리입니다(언리얼의 `FTickTaskManager` 에 해당). 자세한 배경은 TickRegistry.h 에 있습니다.
         * @details 틱 디스패치(`SceneTickScheduler`)가 소유한다. 컴포넌트가 틱을 켜거나 끄면 소유 오브젝트가 여기에 표시하고, 디스패치 전에
         *          표시된 오브젝트만 다시 훑는다.
         */
        TickRegistry& getTickRegistry() { return _tickScheduler.getTickRegistry(); }
        /** @brief 틱에 참여하는 오브젝트의 레지스트리입니다. */
        const TickRegistry& getTickRegistry() const { return _tickScheduler.getTickRegistry(); }

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. */
        Component* resolveComponent( ComponentHandle handle ) { return _store.resolveComponent( handle ); }

        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기 중이면 nullptr 입니다. 잠금을 잡지 않습니다(`findGameObjectByID`). */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return _store.resolveGameObject( handle ); }

        /**
         * @brief 이 씬의 강체 물리입니다(3D 와 2D 물리 씬, 고정 스텝, 물리 컴포넌트, 접촉 이벤트). 처음 쓸 때 물리 씬을 만듭니다.
         * @details 겹침 월드(`getOverlapWorld2D`)와는 따로 실행한다. 둘 다 `stepPhysics` 에서 겹침, 강체 순서로 한 번씩 진행한다.
         */
        ScenePhysics& getScenePhysics() { return _scenePhysics; }
        /** @brief 씬 오디오입니다. 리스너, 에미터, 가림, 리버브 존을 엔진에 전달합니다. `Scene::tick` 이 틱 뒤에 `update` 를 부릅니다. */
        SceneAudio& getSceneAudio() { return _sceneAudio; }
        /** @brief 이 씬의 강체 물리입니다. */
        const ScenePhysics& getScenePhysics() const { return _scenePhysics; }
        /**
         * @brief 이 씬의 내비게이션입니다. 에이전트 종류마다 내비메시와 군중을 관리하고, 베이크와 재베이크, 에이전트 갱신을 맡습니다.
         * @details 물리처럼 소유만 하고 `tick` 에서는 한 줄만 정한다. 물리 앞 틱 결과를 적용한 뒤, 애니메이션과 물리 앞에 실행한다.
         *          에이전트가 캐릭터 컨트롤러에 넘긴 속도가 이번 물리 프레임에 반영되게 하기 위해서다.
         */
        SceneNavigation& getSceneNavigation() { return *_pSceneNavigation; }
        /** @brief 이 씬의 내비게이션입니다. */
        const SceneNavigation& getSceneNavigation() const { return *_pSceneNavigation; }

        /** @brief 이 씬의 겹침 월드입니다. AABB 질의 월드, 2D 콜라이더 등록, 겹침 이벤트를 담당합니다. 질의는 `getOverlapWorld2D().getPhysicsWorld()` 로 합니다. */
        SceneOverlapWorld2D& getOverlapWorld2D() { return _overlapWorld2D; }
        /** @brief 이 씬의 겹침 월드입니다. */
        const SceneOverlapWorld2D& getOverlapWorld2D() const { return _overlapWorld2D; }

        /**
         * @brief GameObject 를 지연 삭제 큐에 넣습니다.
         * @param pObj 삭제할 게임 오브젝트
         * @param bDestroyChildren 자식 오브젝트도 함께 지울지 여부
         */
        void destroyObject( GameObject* pObj, bool bDestroyChildren = true ) { _store.destroyObject( pObj, bDestroyChildren ); }

        /** @brief Component 를 지연 삭제 큐에 넣습니다. 처리할 때 핸들로 다시 찾으므로, 그 사이에 다른 경로가 먼저 해제해도 안전합니다. */
        void destroyComponent( Component* pComp ) { _store.destroyComponent( pComp ); }

        /**
         * @brief 목록에서 이미 뺀 컴포넌트를 해체합니다. `onUnregister`(정확히 한 번), `onDestroy`, 소유자 연결 해제, 소멸, 풀 또는 힙 반납 순서로 진행합니다.
         * @details 컴포넌트 해체는 이 함수 하나를 거친다. 목록에서 빼는 일은 `GameObject::removeComponent` 와 `clearComponents` 가 한다.
         */
        void destroyComponentInstance( Component* pComp ) { _store.destroyComponentInstance( pComp ); }

        /** @brief 지연 삭제 큐에 있는 오브젝트와 컴포넌트를 실제로 해제합니다. */
        void processDeferredDestruction() { _store.processDeferredDestruction(); }

        /** @brief 등록 목록과 대기 목록을 모두 비웁니다. */
        void clear();

        /** @brief 이번 프레임에 추가된 GameObject 를 활성 목록에 합칩니다. */
        void mergePendingAdds() { _store.mergePendingAdds(); }

#if !defined( SW_SHIPPING )
        /**
         * @brief 모듈을 언로드하기 전에, 그 모듈이 정의한 컴포넌트 타입의 **살아 있는 인스턴스**를 모두 지웁니다.
         * @details 팩토리, 타입, 전역 변수는 등록 해제되지만 씬은 엔진이 소유하므로 모듈보다 오래 산다. 인스턴스가 남으면
         *          vtable 이 사라진 객체가 씬에 남아, 다음 틱이나 소멸에서 없는 코드로 점프한다. 지연 파괴 목록에 남은 것도
         *          소멸자가 모듈 코드이므로 먼저 지금 처리한다. 틱 밖에서만 부른다(구조 변경이 막혀 있으면 미뤄질 뿐이다).
         * @return 지운 컴포넌트 수입니다.
         */
        uint32 destroyComponentsOfModule( string_view moduleName ) { return _store.destroyComponentsOfModule( moduleName ); }
#endif

        /**
         * @brief 타입 이름(짧은 이름, FQN, 옛 이름 별칭이며 `이름#번호` 꼬리는 무시)으로 컴포넌트를 만들어 붙입니다. 에디터와 직렬화 전용이고, 게임은 addComponent<T> 를 씁니다.
         * @details 생성 함수는 리플렉션 테이블 한 곳에서 찾는다(`TypeRegistry::findType` → `TypeInfo::_addComponent`). 매니저가 따로 가진 팩토리 테이블은 없으므로
         *          모듈을 로드하거나 언로드하면 모든 매니저가 그 즉시 같은 결과를 낸다.
         */
        Component* addComponentByName( GameObject* pGameObject, hashed_string typeName, bool bLogWarning = true );

        /** @brief 모든 오브젝트의 틱 항목을 다음 틱 전에 다시 만들도록 표시합니다(타입 재바인딩, 씬 초기화). */
        void markTickStagesDirty() { _tickScheduler.getTickRegistry().markAllDirty(); }
        /**
         * @brief 틱 레지스트리가 오브젝트 항목을 다시 만든 틱의 수입니다. 진단과 회귀 테스트용입니다.
         * @details 틱에 참여하는 컴포넌트(`Component::hasTickWork`)가 생기거나 없어지거나 순서가 바뀔 때만 올라야 한다.
         *          틱하지 않는 MeshComponent 를 붙였다 떼는 것으로는 오르지 않는다. 다시 만들 때도 바뀐 오브젝트의 컴포넌트 몇 개만 훑는다.
         */
        uint32 getTickStageBuildCount() const { return _tickScheduler.getStageBuildCount(); }
        /** @brief 선행 조건 스테이지 경계에서 틱 중 트랜스폼 쓰기를 적용한 횟수(누적)입니다. 진단과 회귀 테스트용입니다. */
        uint32 getStageTransformApplyCount() const { return _tickScheduler.getStageTransformApplyCount(); }

        /** @brief 이름으로 만들 수 있는 컴포넌트 타입(리플렉션 테이블에서 `_addComponent` 가 있는 타입)의 짧은 이름 목록입니다. 에디터의 "Add Component" 가 씁니다. */
        static vector<hashed_string> getRegisteredComponentTypeNames();

        /** @brief 트랜스폼이 바뀌었음을 알려 세대를 올립니다(`getTransformHierarchy().notifyDirtied()`). */
        void notifyTransformDirtied() { _transformHierarchy.notifyDirtied(); }
        /** @brief 현재 트랜스폼 더티 세대 번호를 반환합니다. */
        uint64 getTransformGeneration() const { return _transformHierarchy.getGeneration(); }

    private:
        /** @brief 플레이 중에 붙어 대기열에 있는 컴포넌트의 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 실행 중에 대기열에 들어온 것은 다음 번에 실행합니다. */
        void dispatchPendingBeginPlay() { _store.dispatchPendingBeginPlay(); }
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 대기열에 넣습니다(`GameObject::attachCreatedComponent`). 핸들로 보관하므로 그 사이에 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle ) { _store.queueBeginPlay( handle ); }
        /** @brief 타입별 컴포넌트 풀을 얻거나 만듭니다(`GameObjectStore::getOrCreateComponentPool`). */
        PoolAllocator* getOrCreateComponentPool( const TypeInfo* pTypeInfo, size_t typeSize ) { return _store.getOrCreateComponentPool( pTypeInfo, typeSize ); }

        /**
         * @brief 겹침 월드와 강체 물리를 이 순서로 한 번씩 진행합니다. 틱과 트랜스폼 적용 뒤에 게임 스레드에서 실행합니다.
         * @details 유니티는 물리 갱신 뒤에 OnTrigger 를, 언리얼은 움직임이 끝난 뒤에 Begin/EndOverlap 을 부른다. 여기서는 그 프레임에 적용된 월드 위치로 측정한다.
         */
        void stepPhysics( float32 deltaTime );

        /** @brief 붙은 프레임 시스템 하나입니다. */
        struct FrameSystemEntry
        {
            hashed_string                 _key;
            unique_ptr<ISceneFrameSystem> _pSystem;
        };

        // 프레임 단계 본문이다. 프레임 단계 목록(`SceneFrameStepList.xxx`)의 항목마다 하나씩 있고, `tick` 이 항목 순서대로 부른다.
#define SW_SCENE_FRAME_STEP( Name ) void runFrameStep##Name( float32 deltaTime );
#include "Engine/Object/GameObject/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP

        // 단위들은 소유만 한다. 선언 순서가 생성 순서이며, 뒤 단위가 앞 단위를 참조로 받는다(틱 디스패치 → 계층, 레지스트리, 틱 중 규칙,
        // 저장소 → 틱 레지스트리, 틱 중 규칙). 오브젝트는 소멸자의 `clear` 가 먼저 지우므로 단위가 사라지는 순서에 기대지 않는다.

        /** @brief 틱 중 규칙입니다. 동결 플래그, 구조 변경 큐, 틱 뒤 큐, 비우는 순서를 담당합니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        StructuralChangeBuffer _structuralChangeBuffer;
        /** @brief 트랜스폼 계층입니다. */
        SceneTransformHierarchy _transformHierarchy;
        /** @brief 그릴 수 있는 컴포넌트의 레지스트리입니다. */
        PrimitiveRegistry _primitiveRegistry;
        /** @brief 타입별 컴포넌트 레지스트리입니다(빛 포함). */
        ComponentRegistry _componentRegistry;
        /** @brief 카메라 컴포넌트의 레지스트리입니다. */
        CameraRegistry _cameraRegistry;
        /** @brief 애니메이션 시스템입니다. */
        AnimationSystem _animationSystem;
        /** @brief 컴포넌트 틱 디스패치입니다. 틱 레지스트리, 그룹 포크-조인, 선행 조건 스테이지를 담당합니다. */
        SceneTickScheduler _tickScheduler;
        /** @brief 오브젝트 저장소입니다. 생성, 이름, id 테이블, 조회, 지연 파괴, 컴포넌트 풀, 시작 대기열을 담당합니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        GameObjectStore _store;
        /** @brief 겹침 월드입니다. */
        SceneOverlapWorld2D _overlapWorld2D;
        /** @brief 강체 물리입니다. 컴포넌트를 해제하면 바디를 반환합니다. */
        ScenePhysics _scenePhysics;
        /** @brief 오디오 컴포넌트 레지스트리와 엔진 연결을 담당합니다. */
        SceneAudio _sceneAudio;
        /** @brief 내비게이션입니다. 컴포넌트(에이전트, 장애물, 표면)를 해제하면 등록이 제거됩니다. 기능 쪽 타입이라 헤더에는 전방 선언만 두어 코어 헤더가 Navigation 을 퍼뜨리지 않게 합니다. */
        unique_ptr<SceneNavigation> _pSceneNavigation;
        /** @brief 단계마다 돌기 직전에 부르는 관찰자(`setFrameStepObserver`)입니다. 보통 비어 있습니다. */
        FrameStepObserver _frameStepObserver;
        /** @brief `FrameSystems` 단계가 붙인 순서대로 부르는 시스템입니다. 시스템이 컴포넌트를 가리킬 수 있으므로 오브젝트보다 먼저 비웁니다. */
        vector<FrameSystemEntry> _listFrameSystem;
        /** @brief 단계가 실행되는 동안 제거된 시스템입니다. 그 단계가 끝난 뒤 해제합니다. */
        vector<unique_ptr<ISceneFrameSystem>> _listRetiredFrameSystem;
        /** @brief `FrameSystems` 단계가 도는 중이면 true 입니다. */
        bool _bRunningFrameSystems;
    };
} // namespace sw

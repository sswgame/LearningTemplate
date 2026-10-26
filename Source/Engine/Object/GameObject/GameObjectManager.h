/**
 * @file GameObjectManager.h
 * @brief 씬 하나의 오브젝트 수명(생성 · 파괴 · 플레이 시작)을 관리합니다. 오브젝트 저장소 · 틱 디스패치 · 틱 중 규칙 · 레지스트리 · 물리를 소유하고, 게임이 부르는 API 를 전달합니다.
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
#include "Core/Delegate/DelegateSlot.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectStore.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Object/GameObject/Service/SceneEngineServices.h"
#include "Engine/Object/GameObject/Tick/ISceneFrameSystem.h"
#include "Engine/Object/GameObject/Tick/SceneFrameStep.h"
#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"
#include "Engine/Object/GameObject/Tick/StructuralChangeBuffer.h"

namespace sw
{
    class AssetManager;
    class Component;
    class GameObjectManager;
    class SceneComponent;

    /** @brief `GameObjectManager::makeTypedHandle` 의 결과입니다. */
    enum class TypedHandleResult : uint8
    {
        Success,       ///< 무효 핸들이거나 대상의 타입이 맞습니다
        TypeMismatch,  ///< 대상이 있지만 다른 타입입니다
        TargetMissing, ///< 대상이 없습니다(파괴됨, 삭제 대기). 타입을 확인할 수 없어 성공으로 치지 않습니다
    };

    /// @brief 씬 하나의 얼굴입니다 — 저장소(`GameObjectStore`) · 틱 디스패치(`SceneTickScheduler`) · 틱 중 규칙(`StructuralChangeBuffer`) · 레지스트리 · 물리를 소유하고
    ///        `tick` 의 단계를 정합니다. 게임이 부르는 API 는 같은 이름으로 각 단위에 전달합니다.
    class SW_API GameObjectManager
    {
        friend class GameObject;
        friend class StructuralChangeBuffer; ///< `drain` 이 틱 뒤 시작 줄(`dispatchPendingBeginPlay`)을 돌린다

    public:
        /**
         * @brief 엔진 서비스 묶음 @p services 를 받아 만듭니다(씬이 자기 묶음을 넘깁니다). 오브젝트는 없는 채로 시작합니다.
         * @details 묶음은 이 매니저가 한 번 들고, 씬 서비스(`ScenePhysics` · `SceneAudio` …)가 만들어질 때 `getSceneEngineServices` 로 자기 칸을 읽습니다.
         *          에셋 매니저는 씬 안의 코드가 `findAssetManager` 로 씁니다. 빈 칸은 그 서비스가 없다는 뜻입니다.
         */
        explicit GameObjectManager( const SceneEngineServices& services );
        /** @brief 바인딩된 서비스 표에서 묶음을 한 번 읽어 만듭니다(`SceneEngineServices::makeFromBoundEngine`). 테스트 · 프리팹 원형 같은 손수 만든 매니저용입니다. */
        GameObjectManager();
        /** @brief 등록된 오브젝트를 모두 파괴합니다. */
        ~GameObjectManager();

        /** @brief 새 GameObject 를 만들고 등록합니다. */
        GameObject* createGameObject( hashed_string name = hashed_string( GameObject::kDefaultName ) ) { return _store.createGameObject( name ); }

        /**
         * @brief 앞서 발급한 objectID 를 그대로 써서 오브젝트를 다시 만듭니다. 되돌리기 · 플레이 세션 복원 · 핫 리로드가 씁니다.
         * @details 핸들(`GameObjectHandle` · `ComponentHandle`)은 objectID 로 대상을 찾으므로, 복원한 오브젝트가 같은 id 를 받아야
         *          그 너머로도 핸들이 이어집니다. 그 id 로 등록된 오브젝트가 아직 있으면(삭제 대기 포함) 새 id 를 쓰고 경고를 남깁니다.
         *          옛 오브젝트의 지연 파괴가 나중에 id 로 정리하는 항목(슬롯 표 · 에디터 GUID 맵 등)이 새 오브젝트 몫까지 지우지
         *          않게 하기 위해서입니다. 발급 카운터는 그 id 뒤로 밀어 앞으로의 발급과 겹치지 않게 합니다.
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

        /** @brief 오브젝트 ID 로 GameObject 를 찾습니다. 락이 없습니다(칸이 그 id 를 들고 있으면). */
        GameObject* findGameObjectByID( uint64 objectID ) const { return _store.findGameObjectByID( objectID ); }

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

        /**
         * @brief 힙 할당 없이 씬에서 TComponent 와 그 파생 컴포넌트를 순회합니다. 오브젝트를 훑지 않고 타입별 버킷을 돕니다(`GameObjectStore::forEachComponentOfType`).
         * @details 비용은 씬의 정확한 타입 수와 맞는 컴포넌트 수의 합입니다. **방문 순서는 약속하지 않습니다** — 순서가 결과에 나오는 곳은 모은 뒤 정렬합니다.
         *          버킷에서 꺼낸 포인터를 `static_cast` 로 바꿉니다. REFLECT 타입은 가상이 아닌 단일 상속만 받으므로(파서 7-b) 보정이 맞습니다.
         * @note 공유 잠금 안에서 콜백을 부릅니다. 콜백 안에서 오브젝트를 만들거나 지우거나 컴포넌트를 붙이면 안 됩니다(`WalkScope`).
         */
        template <typename TComponent, typename Func>
        void forEachComponentOfType( Func&& func ) const
        {
            static_assert( std::is_base_of_v<Component, TComponent>, "TComponent must derive from sw::Component" );
            _store.forEachComponentOfType( findStaticType<TComponent>(), [&func]( Component* pComp )
            {
                func( static_cast<TComponent*>( pComp ) );
            } );
        }

        /** @brief 런타임 타입 @p pToType 이나 그 파생 컴포넌트를 순회합니다. 콜백은 `Component*` 를 받습니다. 타입을 템플릿으로 모르는 곳(인스펙터 선택기)이 씁니다. */
        template <typename Func>
        void forEachComponentOfType( const TypeInfo* pToType, Func&& func ) const
        {
            _store.forEachComponentOfType( pToType, std::forward<Func>( func ) );
        }

        /** @brief 비어 있지 않은 타입별 버킷 수입니다. 진단과 회귀 테스트(모듈 언로드 뒤 묘비)용입니다. */
        uint32 getTypedComponentBucketCount() const { return _store.getTypedComponentBucketCount(); }

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

        /**
         * @brief `update` 를 시작한 횟수입니다(게임 스레드의 씬 프레임 번호). 한 프레임의 틱 · 틱 뒤 큐 · 그 뒤 패킷 만들기가 같은 값을 봅니다.
         * @details "이번 프레임에 세워진 표시인가"를 비트 비우기 없이 번호 비교로 정하게 한다(`CameraComponent::markCut`).
         */
        uint64 getUpdateSerial() const { return _updateSerial; }

        /** @brief 플레이 중(`beginPlay` 뒤, `endPlay` 전)이면 true 입니다. 이때 붙는 컴포넌트는 다음 틱 단계에서 onBeginPlay 를 받습니다. */
        bool hasBegunPlay() const { return _store.hasBegunPlay(); }
        /**
         * @brief `Physics` 단계를 켜거나 끕니다(기본 켬). 끄면 물리 서비스가 진행하지 않습니다 — 틱만 도는 미리보기 씬이 씁니다(`ScenePreviewPolicy::_bPhysics`).
         */
        void setPhysicsStepEnabled( bool bEnabled ) { _bPhysicsStepEnabled = bEnabled; }
        /** @brief `Physics` 단계가 켜져 있으면 true 입니다. */
        bool isPhysicsStepEnabled() const { return _bPhysicsStepEnabled; }

        /**
         * @brief 씬 한 프레임을 진행합니다 — 표(`SceneFrameStepList.xxx`)의 단계를 줄 순서대로 돕니다.
         * @details 순서와 각 단계가 하는 일은 그 표 하나에 있습니다. 단계마다 `GT.Scene.tick.*` 프로파일 스코프가 있습니다.
         */
        void update( float32 deltaTime );

        using FrameStepObserver = Delegate<void( SceneFrameStep )>;
        /**
         * @brief 단계마다 돌기 **직전에** 부를 관찰자를 겁니다(게임 스레드). 칸 주인은 하나다(이미 걸려 있으면 단언) — 구독이 풀리면 빈다.
         * @details 진단 · 테스트용입니다 — 단계 사이의 상태를 보거나 순서를 기록합니다. 관찰자 안에서 구조를 바꾸지 마십시오.
         */
        [[nodiscard]] Subscription bindFrameStepObserver( FrameStepObserver observer ) { return _frameStepObserver.bind( std::move( observer ) ); }

        /** @brief 이름 붙은 프레임 시스템(`ISceneFrameSystem`)입니다. 없으면 nullptr 입니다. */
        ISceneFrameSystem* findFrameSystem( const hashed_string& key ) const;
        /**
         * @brief 프레임 시스템을 붙입니다(소유). 같은 이름이 있거나 nullptr 이면 거절하고 false 입니다. 붙인 순서가 `FrameSystems` 단계의 실행 순서입니다.
         * @details 게임 스레드, 틱 밖에서 부릅니다. 단계가 도는 중에 붙인 것은 다음 프레임부터 돕니다.
         *          접근 선언(`ISceneFrameSystem::getFrameAccess`)이 순서와 모순이어도 거절하고 오류를 남깁니다 — 이미 붙은 시스템이 이번 프레임의 값을 보아야 하는
         *          종류(`SceneFrameAccess::_reads`)를 새 시스템이 쓰면, 쓰는 쪽이 읽는 쪽 뒤에 서므로 붙이지 않습니다. 부르는 쪽은 결과를 확인합니다(`SW_ASSERT`).
         */
        [[nodiscard]] bool addFrameSystem( const hashed_string& key, unique_ptr<ISceneFrameSystem> pSystem );
        /** @brief 프레임 시스템을 떼어 놓습니다(없으면 할 일이 없다). 단계가 도는 중이면 그 단계가 끝난 뒤 놓습니다 — 도는 시스템이 자신을 떼어도 된다. */
        void removeFrameSystem( const hashed_string& key );

        /**
         * @brief 트랜스폼 계층입니다(루트 목록 · 더티 세대 · 플러시 알고리즘). 매니저는 소유하고 tick 의 단계만 정합니다.
         * @details `PrimitiveRegistry` 와 같은 자리입니다. 능력은 별도 타입이 갖고, 매니저는 순서만 정합니다. 병렬 시스템을
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

        /**
         * @brief 컴포넌트 @p self 에 대한 일을 executeOrDeferPostTick 으로 넘깁니다. 실행할 때 타입 핸들로 다시 찾고, 사라졌거나 삭제 대기면 부르지 않습니다.
         * @details 틱 중이면 틱 뒤로 미루고 아니면 바로 실행하는 의미는 executeOrDeferPostTick 과 같습니다.
         *          **핸들을 받지 않고 컴포넌트를 받습니다.** 타입(`TComponent`)은 @p self 에서 추론하고 핸들은 여기서 만들므로, 다른 컴포넌트의 핸들에
         *          이 타입의 일을 거는 실수(핸들과 템플릿 인자가 어긋나는 것)가 형식으로 없습니다. `const` 멤버 함수 안의 `*this` 도 받습니다.
         *          func 는 붙잡은 값이 없는 람다여야 하며(델리게이트 인라인 버퍼를 넘지 않게) `func( TComponent& )` 또는
         *          `func( TComponent&, GameObjectManager& )` 로 부릅니다.
         */
        template <typename TComponent, typename TFunc>
        void executeOrDeferPostTickOnComponent( const TComponent& self, TFunc func )
        {
            static_assert( std::is_empty_v<TFunc>, "post-tick component callable must not capture" );
            const TComponentHandle<TComponent> handle{ &self };
            GameObjectManager*                 pManager = this;
            executeOrDeferPostTick( [pManager, handle, func]()
            {
                TComponent* pComponent = pManager->resolveComponent( handle );
                // 삭제 대기 컴포넌트는 핸들로 아직 찾히지만 일을 받지 않는다(틱 · 이벤트와 같은 규칙).
                if ( pComponent == nullptr || pComponent->isPendingDestroy() )
                    return;
                if constexpr ( std::is_invocable_v<const TFunc&, TComponent&, GameObjectManager&> )
                    func( *pComponent, *pManager );
                else
                    func( *pComponent );
            } );
        }

        /** @brief SceneComponent 가 루트가 됐음을 트랜스폼 계층에 알립니다(더티면 플러시 목록에 오릅니다). */
        void registerRootSceneComponent( SceneComponent* pComp );

        /** @brief 부모가 생기거나 파괴된 SceneComponent 를 트랜스폼 계층의 플러시 목록에서 뺍니다. */
        void unregisterRootSceneComponent( SceneComponent* pComp );

        /**
         * @brief 그릴 수 있는 컴포넌트의 레지스트리입니다.
         * @details `PrimitiveRegistry` 와 같은 자리입니다. 능력은 별도 타입으로 두고 매니저는 그것을 소유만
         *          합니다. 컴포넌트는 등록 시점에 이것을 받아 들고 있으므로, 매니저 전체를 알 필요가 없습니다.
         */
        PrimitiveRegistry& getPrimitiveRegistry() { return _primitiveRegistry; }
        /** @brief 그릴 수 있는 컴포넌트의 레지스트리입니다. */
        const PrimitiveRegistry& getPrimitiveRegistry() const { return _primitiveRegistry; }

        /**
         * @brief 타입별 컴포넌트 레지스트리입니다(빛 · 2D 그림자 가림막 · 상호작용 대상 · 환경 질의 대상 …).
         * @details 프리미티브와 같은 이유로 있습니다. 매 프레임 씬을 뒤져 **찾지** 않고, 컴포넌트가 붙을 때 **등록받습니다** —
         *          `forEachComponentOfType` 은 타입별 버킷을 돌지만 공유 잠금을 잡고 순서를 약속하지 않으므로, 등록 순서 · 채널 · 잠금 없는 읽기가 필요한
         *          프레임 경로는 이 레지스트리를 씁니다. 자세한 사연은 ComponentRegistry.h 에 있습니다.
         */
        ComponentRegistry& getComponentRegistry() { return _componentRegistry; }
        /** @brief 타입별 컴포넌트 레지스트리입니다. */
        const ComponentRegistry& getComponentRegistry() const { return _componentRegistry; }

        /**
         * @brief 카메라 컴포넌트의 레지스트리입니다. 역할 · 우선순위로 고르는 규칙(`selectCamera`)도 여기 하나입니다.
         * @details 빛과 같은 이유로 있습니다 — 게임 · 에디터 카메라 선택이 씬 전체를 훑지 않습니다. 자세한 사연은 CameraRegistry.h 에 있습니다.
         */
        CameraRegistry& getCameraRegistry() { return _cameraRegistry; }
        /** @brief 카메라 컴포넌트의 레지스트리입니다. */
        const CameraRegistry& getCameraRegistry() const { return _cameraRegistry; }

        /**
         * @brief 틱에 참여하는 오브젝트의 레지스트리입니다(언리얼 `FTickTaskManager` 의 자리). 자세한 사연은 TickRegistry.h 에 있습니다.
         * @details 틱 디스패치(`SceneTickScheduler`)가 소유합니다. 컴포넌트가 틱을 켜고 끄면 소유 오브젝트가 여기에 표시하고, 디스패치 전에
         *          표시된 오브젝트만 다시 훑습니다.
         */
        TickRegistry& getTickRegistry() { return _tickScheduler.getTickRegistry(); }
        /** @brief 틱에 참여하는 오브젝트의 레지스트리입니다. */
        const TickRegistry& getTickRegistry() const { return _tickScheduler.getTickRegistry(); }

        /** @brief 핸들이 가리키는 컴포넌트를 찾습니다. 삭제 예정이면 nullptr 입니다. 타입을 아는 곳은 타입 핸들(`TComponentHandle<T>`)의 찾기를 씁니다. */
        Component* resolveComponent( ComponentHandle handle ) const { return _store.resolveComponent( handle ); }

        /**
         * @brief 타입 핸들이 가리키는 컴포넌트를 `T*` 로 찾습니다. 삭제 예정이면 nullptr 입니다.
         * @details 타입 없는 찾기 뒤에 `static_cast` 합니다(REFLECT 타입은 가상이 아닌 단일 상속뿐이라 보정이 맞습니다).
         *          **Shipping 이 아닌 모든 구성에서 대상의 타입을 확인합니다.** 핸들은 id 만 담으므로, id 를 다른 타입이 받으면(핫 리로드, 되돌리기의 id 복원)
         *          캐스트가 다른 타입의 칸을 읽습니다. 어긋나면 오류 로그와 Debug 단언을 남기고 nullptr 을 반환합니다. 그런 길이 없는 Shipping 만 확인을 뺍니다.
         */
        template <typename T>
        T* resolveComponent( TComponentHandle<T> handle ) const
        {
            static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
            static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
            Component* pComponent = _store.resolveComponent( handle.untyped() );
#if !defined( SW_SHIPPING )
            if ( pComponent != nullptr && isComponentOfType( *pComponent, findStaticType<T>() ) == false )
            {
                reportTypedResolveMismatch( handle.untyped(), *pComponent, findStaticType<T>() );
                return nullptr;
            }
#endif
            return static_cast<T*>( pComponent );
        }

        /**
         * @brief 타입 없는 핸들 @p handle 에서 타입 핸들을 만듭니다. 대상의 타입을 확인하는 유일한 길입니다(다운캐스트도 `untyped()` 로 여기를 지납니다).
         * @details | 입력 | 결과 | @p outHandle |
         *          | 무효 핸들 | `Success` | 무효 타입 핸들 |
         *          | 대상이 `T` 이거나 그 파생 | `Success` | 타입 핸들 |
         *          | 대상의 타입이 다름 | `TypeMismatch`, 오류 로그 | 바꾸지 않음 |
         *          | 대상이 없음(파괴됨, 삭제 대기) | `TargetMissing`, 오류 로그 | 바꾸지 않음 |
         *          실패를 빈 핸들로 바꾸지 않습니다. 빈 핸들은 "대상 없음"이라는 정상 값과 구분되지 않아, 타입이 틀린 핸들이 조용히 사라지기 때문입니다.
         *          대상 없음을 정상으로 보는 쪽은 그 결과를 직접 처리합니다.
         */
        template <typename T>
        [[nodiscard]] TypedHandleResult makeTypedHandle( ComponentHandle handle, TComponentHandle<T>& outHandle ) const
        {
            static_assert( std::is_base_of_v<Component, T>, "T must derive from sw::Component" );
            static_assert( HasOwnReflectBody_v<T> || HasReflectStaticType_v<T>, "T must declare its own REFLECT_BODY() (cannot slice to a base class StaticType)" );
            const TypedHandleResult result = resolveTypedHandleResult( handle, findStaticType<T>() );
            if ( result == TypedHandleResult::Success )
                outHandle = TComponentHandle<T>( ComponentHandleCastKey{}, handle );
            return result;
        }

        /** @brief 핸들이 가리키는 오브젝트를 찾습니다. 파괴됐거나 삭제 대기면 nullptr 입니다. 락을 잡지 않습니다(`findGameObjectByID`). */
        GameObject* resolveGameObject( GameObjectHandle handle ) const { return _store.resolveGameObject( handle ); }

        /** @brief 생성자로 받은 엔진 서비스 묶음입니다(빌린 포인터들). 씬 서비스가 만들어질 때 자기 칸을 읽습니다. */
        const SceneEngineServices& getSceneEngineServices() const { return _services; }
        /**
         * @brief 생성자로 받은 에셋 매니저입니다(빌림). 없으면 nullptr 입니다.
         * @details 씬 안의 엔진 코드(스프라이트 배치 · 애니메이션 알림)가 전역 조회 대신 씁니다. 게임 모듈은 `game::findSubsystem<AssetManager>()` 를 씁니다.
         */
        AssetManager* findAssetManager() const { return _services._pAssetManager; }
        /**
         * @brief 이 씬의 범위입니다 — 씬 서비스를 소유하고, 부모가 엔진 범위라 엔진 서비스도 타입으로 찾는다(`locateSubsystem<T>( &scope )`).
         * @details 언리얼 `UWorld` 의 서브시스템 컬렉션과 같은 자리입니다.
         */
        const SubsystemScope& getSceneScope() const { return _sceneScope; }
        /**
         * @brief 이 씬의 서비스 @p TService 입니다(`ISceneService.h`). 레지스트리의 서비스는 매니저마다 하나씩 늘 있습니다.
         * @details 언리얼 `UWorld::GetSubsystem<T>()` 와 같은 자리입니다. 코어는 서비스 타입을 모르고, 부르는 쪽이 그 타입의 헤더를 듭니다
         *          (`manager.getSubsystem<sw::ScenePhysics>()` · `getSubsystem<sw::SceneAudio>()` · `getSubsystem<SceneNavigation>()` · `getSubsystem<AnimationSystem>()`).
         */
        template <typename TService>
        TService& getSubsystem()
        {
            return _sceneScope.get<TService>();
        }
        /** @brief 이 씬의 서비스 @p TService 입니다. */
        template <typename TService>
        const TService& getSubsystem() const
        {
            return _sceneScope.get<TService>();
        }

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
         *          틱 밖에서 `deferPostTick` 으로 넣어 둔 일(구조 변경 · 틱 뒤 큐)도 모듈 코드의 람다일 수 있으므로, 모듈 코드가 아직 있을 때
         *          먼저 비웁니다(다음 프레임 끝에 돌 일을 앞당겨 돌린다 — 버리면 게임 쪽 일이 조용히 사라진다). 비우는 중에 다시 미룬 일도 빌 때까지 비웁니다(`drainUntilEmpty`).
         * @return 지운 컴포넌트 수입니다.
         */
        uint32 destroyComponentsOfModule( string_view moduleName )
        {
            if ( _structuralChangeBuffer.isFrozen() == false )
                _structuralChangeBuffer.drainUntilEmpty( *this );
            return _store.destroyComponentsOfModule( moduleName );
        }
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
         * @brief 틱 레지스트리가 오브젝트 항목을 다시 지은 틱의 수입니다. 진단 · 회귀 테스트용입니다.
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
        /** @brief @p component 의 타입이 @p pToType 이거나 그 파생이면 true 입니다. 타입 정보가 없으면(모듈이 내려감) false 입니다. */
        static bool isComponentOfType( const Component& component, const TypeInfo* pToType )
        {
            const TypeInfo* pTypeInfo = component.findCachedTypeInfo();
            return pTypeInfo != nullptr && pTypeInfo->isDerivedFrom( pToType );
        }
        /** @brief `makeTypedHandle` 의 확인입니다(템플릿 밖에서 찾고 로그를 남깁니다). */
        [[nodiscard]] TypedHandleResult resolveTypedHandleResult( ComponentHandle handle, const TypeInfo* pToType ) const;
        /** @brief `resolveComponent<T>` 가 다른 타입의 칸을 찾았을 때 오류 로그와 Debug 단언을 남깁니다. */
        void reportTypedResolveMismatch( ComponentHandle handle, const Component& found, const TypeInfo* pToType ) const;

        /** @brief 플레이 중에 붙어 줄을 선 컴포넌트의 onBeginPlay 를 부릅니다(게임 스레드, 틱 밖). 도는 중에 선 것은 다음 번에 돕니다. */
        void dispatchPendingBeginPlay() { _store.dispatchPendingBeginPlay(); }
        /** @brief 플레이 중에 붙은 컴포넌트를 시작 줄에 세웁니다(`GameObject::attachCreatedComponent`). 핸들로 들어 그새 해체돼도 안전합니다. */
        void queueBeginPlay( ComponentHandle handle ) { _store.queueBeginPlay( handle ); }
        /** @brief 타입별 컴포넌트 항목(풀과 버킷)을 얻거나 만듭니다(`GameObjectStore::getOrCreateComponentPoolEntry`). */
        ComponentPoolEntry* getOrCreateComponentPoolEntry( const TypeInfo* pTypeInfo, size_t typeSize ) { return _store.getOrCreateComponentPoolEntry( pTypeInfo, typeSize ); }
        /** @brief 막 붙인 컴포넌트를 타입별 버킷에 넣습니다(`GameObject::attachCreatedComponent`). */
        void registerTypedComponent( Component* pComp ) { _store.registerTypedComponent( pComp ); }

        /**
         * @brief `Physics` 단계의 서비스(`ScenePhysics` — 강체 물리와 2D 콜라이더)를 진행하고 그 쓰기를 펼칩니다. 틱 · 트랜스폼 적용 뒤, 게임 스레드에서.
         * @details 유니티는 물리 갱신 뒤 OnTrigger 를, 언리얼은 움직임이 끝난 뒤 Begin/EndOverlap 을 부른다. 여기서는 그 프레임에 적용된 월드 자리로 측정한다.
         */
        void stepPhysics( float32 deltaTime );

        /** @brief 등록 줄의 단계가 @p step 인 서비스를 씬 범위의 초기화 순서(의존 순서)로 갱신합니다. */
        void updateSceneServices( SceneFrameStep step, float32 deltaTime );

        /** @brief 이 매니저가 만든 씬 서비스 하나입니다(소유는 씬 범위 — 갱신 단계를 알려고 등록 줄과 짝지어 든다). */
        struct SceneServiceEntry
        {
            const SceneServiceRegistration* _pRegistration;
            ISceneService*                  _pService;
        };

        /** @brief 붙은 프레임 시스템 하나입니다. */
        struct FrameSystemEntry
        {
            hashed_string                 _key;
            unique_ptr<ISceneFrameSystem> _pSystem;
        };

        // 프레임 단계 본문 — 표(`SceneFrameStepList.xxx`)의 줄마다 하나. `tick` 이 줄 순서대로 부른다.
#define SW_SCENE_FRAME_STEP( Name ) void runFrameStep##Name( float32 deltaTime );
#include "Engine/Object/GameObject/Tick/SceneFrameStepList.xxx"
#undef SW_SCENE_FRAME_STEP

        // 단위들은 소유만 한다. 선언 순서가 생성 순서다 — 뒤 단위가 앞 단위를 참조로 받는다(틱 디스패치 → 계층 · 레지스트리 · 틱 중 규칙,
        // 저장소 → 틱 레지스트리 · 틱 중 규칙). 오브젝트는 소멸자의 `clear` 가 먼저 지우므로 단위가 사라지는 순서에 기대지 않는다.

        /** @brief 틱 중 규칙(동결 플래그 · 구조 변경 큐 · 틱 뒤 큐 · 비우는 순서)입니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        StructuralChangeBuffer _structuralChangeBuffer;
        /** @brief 트랜스폼 계층입니다. */
        SceneTransformHierarchy _transformHierarchy;
        // 레지스트리 셋은 값 멤버로 둔다. 헤더가 Core 만 include 해 `unique_ptr` 로 빼도 퍼지는 include 가 줄지 않고, 3 개월 동안 1 ~ 3 번 바뀌어
        // 다시 빌드를 아끼는 이득도 없다(2026-10-21 판정, 이 헤더는 같은 기간 112 번). 접근마다 간접 참조만 는다.
        /** @brief 그릴 수 있는 컴포넌트의 레지스트리입니다. */
        PrimitiveRegistry _primitiveRegistry;
        /** @brief 타입별 컴포넌트 레지스트리입니다(빛 포함). */
        ComponentRegistry _componentRegistry;
        /** @brief 카메라 컴포넌트의 레지스트리입니다. */
        CameraRegistry _cameraRegistry;
        /** @brief 컴포넌트 틱 디스패치(틱 레지스트리 · 그룹 포크-조인 · 선행 조건 스테이지)입니다. */
        SceneTickScheduler _tickScheduler;
        /** @brief 오브젝트 저장소(생성 · 이름 · id 표 · 조회 · 지연 파괴 · 컴포넌트 풀 · 시작 줄)입니다. 게임이 부르는 API 는 위의 전달 함수입니다. */
        GameObjectStore _store;
        /** @brief 씬 범위입니다 — 레지스트리(`SceneServiceRegistry`)에서 만든 기능 서비스(물리 · 오디오 · 내비게이션 …)를 소유한다. 부모는 엔진 범위. */
        SubsystemScope _sceneScope;
        /** @brief 씬 서비스를 범위의 초기화 순서로 든 목록입니다. 코어는 타입을 모르고 단계마다 `update` 만 부른다. */
        vector<SceneServiceEntry> _listSceneService;
        /** @brief 단계마다 돌기 직전에 부르는 관찰자(`bindFrameStepObserver`)입니다. 보통 비어 있습니다. */
        DelegateSlot<FrameStepObserver> _frameStepObserver;
        /** @brief `FrameSystems` 단계가 붙인 순서대로 부르는 시스템입니다. 오브젝트보다 먼저 비웁니다(시스템이 컴포넌트를 가리킬 수 있다). */
        vector<FrameSystemEntry> _listFrameSystem;
        /** @brief 단계가 도는 동안 뗀 시스템입니다 — 그 단계가 끝난 뒤 놓습니다. */
        vector<unique_ptr<ISceneFrameSystem>> _listRetiredFrameSystem;
        /** @brief 생성자로 받은 엔진 서비스 묶음(빌린 포인터들)입니다(`getSceneEngineServices` · `findAssetManager`). */
        SceneEngineServices _services;
        /** @brief `FrameSystems` 단계가 도는 중이면 true 입니다. */
        bool _bRunningFrameSystems;
        /** @brief `update` 를 시작한 횟수입니다(`getUpdateSerial`). */
        uint64 _updateSerial;
        /** @brief false 면 `Physics` 단계를 건너뜁니다(`setPhysicsStepEnabled`). */
        bool _bPhysicsStepEnabled;
    };
} // namespace sw

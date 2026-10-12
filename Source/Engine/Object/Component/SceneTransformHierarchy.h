/**
 * @file SceneTransformHierarchy.h
 * @brief 씬 컴포넌트 트랜스폼 계층입니다. 더티 루트 목록, 더티 세대, 틱 중 쓰기(대기 슬롯과 쓰기 큐)와 그 적용, 배치 쓰기, 월드 캐시 플러시를 맡습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/InlineAllocator.h"
#include "Core/Container/pair.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Object/Component/SceneTransformStorage.h"

namespace sw
{
    class GameObjectManager;
    class PrimitiveRegistry;
    class SceneComponent;

    /**
     * @struct SceneTransformWrite
     * @brief 배치 트랜스폼 쓰기 한 건입니다. 어느 컴포넌트에 어느 로컬 값을 쓸지 담습니다.
     * @details `GameObjectManager::applyTransformBatch` 가 받는다. 컴포넌트마다 세터를 부르는 대신 한 프레임의 쓰기를
     *          모아 워커에 나눈다. 유니티의 `TransformAccessArray` 와 `IJobParallelForTransform`, 언리얼 ISM 의
     *          `BatchUpdateInstancesTransforms` 에 해당한다. 세터 하나가 약 24 ns 이므로 큐브 8000 개에 세터 둘이면
     *          프레임당 380 us 가 들고, 이는 8000 개 규모에서 게임 스레드의 가장 큰 항목이다.
     */
    struct SceneTransformWrite
    {
        /** @brief 쓸 값의 비트 조합을 반환합니다(`SceneTransformPage::LocalValueBit`). */
        uint8 getValueMask() const
        {
            return static_cast<uint8>( ( _bSetPosition != SW_FALSE ? SceneTransformPage::kLocalPosition : 0u ) |
                                       ( _bSetRotation != SW_FALSE ? SceneTransformPage::kLocalRotation : 0u ) |
                                       ( _bSetScale != SW_FALSE ? SceneTransformPage::kLocalScale : 0u ) );
        }
        /** @brief 비트 하나가 가리키는 값을 반환합니다. */
        const float3& getValue( uint8 bit ) const
        {
            return ( bit == SceneTransformPage::kLocalPosition ) ? _localPosition : ( bit == SceneTransformPage::kLocalRotation ) ? _localRotation
                                                                                                                                  : _localScale;
        }
        /** @brief 비트 하나의 값을 넣고 쓸 값으로 표시합니다. */
        void setValue( uint8 bit, const float3& value )
        {
            if ( bit == SceneTransformPage::kLocalPosition )
            {
                _localPosition = value;
                _bSetPosition  = SW_TRUE;
            }
            else if ( bit == SceneTransformPage::kLocalRotation )
            {
                _localRotation = value;
                _bSetRotation  = SW_TRUE;
            }
            else
            {
                _localScale = value;
                _bSetScale  = SW_TRUE;
            }
        }

        ComponentHandle _handle;
        /**
         * @brief 틱 큐 전용입니다. 세터가 자기 자신을 기록합니다. 바깥에서 주는 배치는 비워 두고, 핸들로 대상을 찾습니다.
         * @details 큐에 쌓인 건은 같은 `tick()` 호출 안에서 적용되고 그 사이 컴포넌트는 해제되지 않는다(파괴는 틱 밖에서만 메모리를
         *          해제한다). 그래서 핸들로 다시 찾을 필요가 없고, 슬롯 테이블, 오브젝트, 컴포넌트 목록에서 생기는 캐시 미스 세 번이 사라진다.
         *          삭제 대기 여부는 적용하는 쪽이 확인한다.
         */
        SceneComponent* _pTarget{ nullptr };
        float3          _localPosition{};
        float3          _localRotation{};
        float3          _localScale{ 1.0f, 1.0f, 1.0f };
        uint8           _bSetPosition{ SW_FALSE };
        uint8           _bSetRotation{ SW_FALSE };
        uint8           _bSetScale{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class SceneTransformHierarchy
     * @brief 트랜스폼 계층의 상태와 알고리즘입니다. 더티 루트, 틱 중 쓰기와 그 적용, 배치 쓰기, 플러시를 담당하며, `GameObjectManager` 는 이것을 소유하고 단계 순서만 정합니다.
     * @details 언리얼의 `UpdateComponentToWorld` 계층, 유니티의 `TransformHierarchy` 에 해당한다. 값 자체는 전역
     *          `SceneTransformStorage` 의 슬롯에 있고, 이 타입은 한 씬에서 "무엇을 언제 다시 합성하나"를 맡는다. 매니저의 tick 은
     *          `beginTickWrites`, `applyTickWrites`, `flush` 를 차례로 부를 뿐 그 안의 알고리즘을 모른다. **"상태를 가진 시스템 타입
     *          하나, `engine::runParallel`, 매니저 tick 의 단계 한 줄"** 이 병렬 시스템 하나의 구조이고, 이 타입이 그 첫 예다.
     *
     *          **틱 중 쓰기는 두 경로가 있다.** 자기 오브젝트를 틱하는 스레드의 세터는 슬롯의 대기 위치에 바로 쓰고 슬롯 번호만 스레드 목록에
     *          올린다(`findPendingSlotList`). 다른 오브젝트의 컴포넌트에 쓰는 것은 두 스레드가 한 슬롯에 쓸 수 있으므로 쓰기 큐로 보낸다
     *          (`queueWriteParallel`). 틱이 끝나면 `applyTickWrites` 가 대기 슬롯, 쓰기 큐 순으로 스레드 슬롯 단위로 나눠 적용한다. 틱 중에
     *          다른 오브젝트가 읽는 값은 여전히 틱 전 값이다. 유니티 DOTS 가 게임플레이 잡 뒤에 `TransformSystemGroup` 을 두는 것과 같은
     *          구조다.
     *
     *          **로컬 값이 바뀐 슬롯의 뒤처리는 한 곳에서 한다**(`applyLocalChange`). 잎 루트(부모도 자식도 없는 루트)는 컴포넌트를 거치지 않고 슬롯에서
     *          바로 합성하고, 계층이 있는 것은 더티를 표시해 루트를 올린 뒤 플러시가 아래로 내려가며 합성한다. 틱 뒤 적용과 배치 쓰기가 같은 함수를 쓴다.
     *
     *          **플러시는 더티 루트만 순회한다.** 더러워진 노드는 자기 루트를 `_listDirtyRoot` 에 한 번만 올린다
     *          (`queueDirtyRoot` 이며, 루트의 `_bQueuedDirtyRoot` 가 중복을 막는다). **루트 전부**를 돌며 더티인지 물으면 루트마다
     *          캐시 미스가 한 번씩 생기므로, 8000 개 중 10 개만 움직여도 8000 개를 다 건드린다. 언리얼이
     *          `MarkRenderTransformDirty` 로 더티 컴포넌트를 목록에 올리고 그 목록만 보내는 것과 같은 구조다.
     *
     *          병렬 규칙: 루트 서브트리끼리는 겹치지 않으므로 루트 단위로 나눈다. 워커는 컨테이너를 만지지 않고
     *          포인터만 받는다. DFS 스택은 스레드 슬롯마다 하나씩 재사용한다(잡마다 만들면 프레임당 청크 수만큼
     *          할당이 생긴다). 적용과 배치 쓰기의 워커는 더티 루트를 슬롯별 스크래치에 모으고
     *          끝난 뒤 `mergeQueuedDirtyRoots` 가 한 목록으로 합친다.
     *
     *          **모든 루트의 목록은 보관하지 않는다.** 플러시는 더티 루트만 순회하므로 읽을 곳이 없고, 보관하면 스폰과 파괴마다 그 목록에 대한 배타 잠금을
     *          한 쌍씩 잡아야 한다.
     *
     *          스레드 계약: 한 인스턴스는 한 스레드만 만진다. 그 씬을 소유한 스레드(게임 스레드, 또는 아직 넘겨받지 않은 씬이면
     *          `SceneLoadAsync` 워커)다. 병렬 구간(플러시 잡, 적용 잡, 틱 중 세터)은 위의 규칙대로 자기 슬롯만 쓴다.
     */
    class SW_API SceneTransformHierarchy
    {
    public:
        /**
         * @brief 더티 루트가 이 수 이상이면 플러시를 루트 서브트리 단위로 잡에 나눕니다.
         * @details 잡 디스패치의 최소 비용(잠든 풀 기준 약 34 us)보다 작은 일은 직렬이 빠르다. 루트 2000 개는
         *          직렬 약 60 us 라 나눠도 같고, 8000 개는 직렬 200 us 가 병렬 110 us 가 된다(Release, 큐브 모두 이동).
         */
        static constexpr uint32 kParallelFlushRootCount = 2048;
        /**
         * @brief 쓰기가 이 건수 이상이면 워커에 나눕니다(틱 뒤 적용, 배치 쓰기).
         * @details 한 건이 핸들 해석, 필드 쓰기, 더티 표시로 약 60 ns 다. 잡 디스패치의 최소 비용(약 34 us)을 넘기려면 천 건은 돼야 한다.
         */
        static constexpr uint32 kParallelWriteCount = 1024;

        /** @brief 서브트리 DFS 스택입니다. 인라인 슬롯이 64개라 보통 깊이의 계층은 힙을 쓰지 않습니다. */
        using FlushStack = vector<pair<SceneComponent*, bool>, InlineAllocator<pair<SceneComponent*, bool>, 64>>;

        /** @brief 빈 계층을 만듭니다. */
        SceneTransformHierarchy();
        /** @brief 목록을 해제합니다. 컴포넌트의 수명은 GameObject 가 관리합니다. */
        ~SceneTransformHierarchy() = default;

        SceneTransformHierarchy( const SceneTransformHierarchy& )            = delete;
        SceneTransformHierarchy& operator=( const SceneTransformHierarchy& ) = delete;

        /** @brief 씬 컴포넌트가 루트가 되었을 때 부릅니다(등록, 부모에서 분리). 더티이면 플러시 목록에 올립니다. 컴포넌트는 더티 상태로 만들어집니다. */
        void registerRoot( SceneComponent* pComp );
        /** @brief 씬 컴포넌트가 더는 루트가 아닐 때 부릅니다(부모가 생기거나 파괴됨). 플러시 목록에 올라 있으면 제거합니다. 여러 번 불러도 안전합니다. */
        void unregisterRoot( SceneComponent* pComp );

        /**
         * @brief 더러워진 노드의 루트를 플러시 목록에 올립니다. **게임 스레드 전용**입니다. 이미 올라 있으면 아무 일도 하지 않습니다.
         * @details `markTransformDirty` 가 부모 체인을 올라가 루트에 닿았을 때 부른다. 체인 중간에서 이미
         *          "자손 더티"가 설정된 조상을 만나면 그 루트는 이미 올라 있으므로 올라가기를 멈춘다. 이 불변식이 목록의 근거다.
         */
        void queueDirtyRoot( SceneComponent* pRoot );
        /**
         * @brief 워커에서 부르는 버전입니다. 자기 스레드 슬롯의 스크래치에 올리고, 끝나면 `mergeQueuedDirtyRoots` 가 합칩니다.
         * @details 중복은 루트의 원자 플래그가 막으므로 같은 루트 아래의 두 자식을 다른 워커가 써도 한 번만 올라간다.
         */
        void queueDirtyRootParallel( SceneComponent* pRoot );
        /** @brief 워커 스크래치의 더티 루트를 본 목록으로 옮깁니다(적용과 배치 쓰기의 앞뒤, 게임 스레드). */
        void mergeQueuedDirtyRoots();

        /**
         * @brief 병렬 틱 동안 세터가 쓸 대기 슬롯 목록과 쓰기 큐를 스레드 슬롯 수만큼 준비합니다(틱 시작, 게임 스레드).
         * @details 슬롯 배열은 한 번 자라면 그대로 유지한다. 프레임마다 할당하는 것이 없다. 비우는 일은 `applyTickWrites` 가 한다.
         */
        void beginTickWrites();
        /**
         * @brief 틱 중의 세터 한 건을 **자기 스레드 슬롯**의 쓰기 큐에 올립니다(다른 오브젝트의 컴포넌트에 쓰는 경우). 워커에서 불립니다.
         * @details 힙 할당도 공유 뮤텍스도 없이 슬롯 목록에 값으로 넣는다(지연 델리게이트로 미루면 건마다 힙 람다, 뮤텍스 대기, 틱 뒤 직렬 재적용이 붙는다).
         *          같은 슬롯의 직전 건이 같은 컴포넌트면 합친다(위치와 스케일을 잇따라 부르는 흔한 경우).
         *          @p writerID 는 이 쓰기를 낸 틱의 주인 오브젝트이고(`GameObjectManager::queueTransformWrite` 가 그 스레드의 틱 주인에서 구한다) 순서 키가 된다.
         *          같은 컴포넌트에 여러 오브젝트의 틱이 썼으면 (쓴 오브젝트 id, 그 스레드의 순번) 순으로 적용해 마지막이 이긴다(유니티 `EntityCommandBuffer.ParallelWriter`
         *          의 sortKey). 키는 건(`SceneTransformWrite`)이 아니라 슬롯의 키 목록에 둔다. 건은 바깥 배치도 쓰는 64 바이트라, 키를 넣어
         *          80 바이트가 되면 배치 8000 건이 읽는 양이 25% 늘어난다.
         * @return 큐에 올렸으면 true 입니다. 슬롯이 준비되지 않았으면 false 이고, 부르는 쪽이 지연 델리게이트로 돌립니다.
         */
        bool queueWriteParallel( const SceneTransformWrite& write, uint64 writerID );
        /**
         * @brief **이 스레드 슬롯**의 대기 목록입니다. 틱 중에 슬롯에 바로 쓴 컴포넌트의 슬롯 번호가 여기에 오릅니다. 워커에서 불립니다.
         * @details 틱 디스패치가 청크마다 한 번 받아 `TickThreadState::_pPendingSlotList` 에 둔다. 세터(`SceneComponent::writeTickTransform`)는
         *          슬롯이 처음 대기에 들어갈 때 그 목록에 슬롯 번호만 올린다(4 바이트) — 세터마다 스레드 슬롯을 다시 묻지 않는다. 대기 값 자체는
         *          슬롯에 있다. 목록은 `beginTickWrites` 뒤 틱이 끝날 때까지 옮겨지지 않는다.
         * @return 스레드가 스크래치 슬롯을 받지 못했으면 nullptr 입니다. 그러면 세터는 쓰기 큐로 갑니다.
         */
        vector<uint32>* findPendingSlotList();
        /**
         * @brief 틱 중에 쓴 트랜스폼을 적용하고 목록을 비웁니다(틱 뒤, 게임 스레드). 먼저 대기 슬롯, 다음에 쓰기 큐 순서입니다.
         * @details 대기 슬롯은 모든 스레드 목록을 이어 붙인 번호 공간을 고르게 잘라 잡에 준다(슬롯 하나는 처음 대기에 들어간 스레드의 목록에만 있어
         *          겹치지 않는다 — 목록마다 잡 하나로 주면 가장 많이 틱한 스레드의 목록 하나가 합류를 붙잡는다). 쓰기 큐는
         *          **대상 단위로** 나눈다. 건을 (대상, 쓴 오브젝트, 순번)으로 정렬하고 대상 경계에서만 잘라 잡에 준다. 그러면 한 컴포넌트는 한
         *          워커만 쓰고(유니티 `TransformAccessArray` 가 트랜스폼 하나를 한 잡에만 주는 것과 같다), 이기는 값은 스레드 배정과 무관하게 정해진다.
         *          쓰기 큐를 스레드 슬롯 단위로 잡에 주면 두 오브젝트의 틱이 같은 컴포넌트를 쓸 때 두 워커가 그 슬롯을 동시에 쓰므로 안 된다.
         * @return 실제로 값이 바뀐 건수입니다.
         */
        uint32 applyTickWrites( GameObjectManager& manager, PrimitiveRegistry& registry );
        /**
         * @brief 바깥에서 준 쓰기 여럿을 적용합니다(`GameObjectManager::applyTransformBatch`). 건수가 임계값을 넘으면 워커에 나눕니다.
         * @details 틱 중이면 건마다 세터로 돌린다. 세터가 틱 중 쓰기 경로를 탄다. 배치의 병렬 적용은 구조 변경이 없는 틱 밖에서만 안전하다.
         *          핸들이 씬 컴포넌트가 아니거나 죽었으면 그 건은 건너뛴다. 세대는 끝에 한 번 올린다. 같은 핸들이 여러 번 있으면 배열에서
         *          뒤의 것이 이긴다. 병렬일 때도 그렇도록 건을 대상(componentID) 버킷으로 나눠, 한 대상은 한 워커가 배열 순서대로 쓴다.
         *          배열을 연속 구간으로 자르면 같은 핸들이 두 구간에 있을 때 두 워커가 한 슬롯을 동시에 쓴다.
         * @return 실제로 값이 바뀐 건수입니다(틱 중이면 세터로 돌린 건수).
         */
        uint32 applyBatch( GameObjectManager& manager, const SceneTransformWrite* pWrite, uint32 count );
        /** @brief 어느 스레드 슬롯에든 쌓인 쓰기나 대기 슬롯이 있으면 true 입니다. */
        bool hasQueuedWrites() const;

        /**
         * @brief 슬롯의 월드가 다시 합성된 직후의 알림입니다. 슬롯의 프리미티브에 렌더 더티를 기록하고, 알림 비트가 켜져 있으면 소유 컴포넌트의 훅을 부릅니다.
         * @details 메시 컴포넌트는 훅을 끄고 슬롯에 프리미티브 번호를 기록하므로 컴포넌트를 거치지 않는다. 컴포넌트의 합성
         *          (`SceneComponent::updateWorldTransformFromParent`)과 슬롯의 합성(`applyLocalChange`)이 같은 이 함수를 쓴다.
         * @param pRegistry 슬롯이 속한 씬의 프리미티브 레지스트리. 씬에 붙지 않은 컴포넌트면 nullptr 입니다.
         */
        static void notifyWorldUpdated( SceneTransformPage& page, uint32 pageIndex, PrimitiveRegistry* pRegistry );

        /** @brief 어떤 트랜스폼이 바뀌었음을 알려 세대를 올립니다. 워커에서 불러도 됩니다. */
        void notifyDirtied() { _dirtyGeneration.fetch_add( 1, std::memory_order_relaxed ); }
        /** @brief 현재 더티 세대입니다. 플러시가 이 값을 따라잡으면 할 일이 없습니다. */
        uint64 getGeneration() const { return _dirtyGeneration.load( std::memory_order_relaxed ); }
        /** @brief 플러시할 루트가 하나라도 있으면 true 입니다. 목록만 보고 판단하며 루트를 순회하지 않습니다. */
        bool hasDirty() const { return _listDirtyRoot.empty() == false; }
        /** @brief 지금 플러시를 기다리는 루트 수입니다. */
        size_t getDirtyRootCount() const { return _listDirtyRoot.size(); }

        /** @brief 더티 루트의 월드 캐시를 계층 순서로 갱신하고 목록을 비웁니다. 루트 수가 임계값을 넘으면 병렬로 처리합니다. */
        void flush();
        /** @brief 더티 목록을 비웁니다(매니저의 clear). */
        void clear();

        /**
         * @brief 한 루트 아래의 월드 트랜스폼을 갱신합니다(명시적 스택을 쓰는 DFS).
         * @details 스택 버퍼는 부르는 쪽이 준다. 서로 다른 루트의 서브트리는 겹치지 않으므로 잡 사이에 공유 쓰기가
         *          없다. 예외는 하나, 메시 컴포넌트가 렌더 더티를 기록하는 `PrimitiveRegistry` 인데 잠금 없는 원자 플래그다.
         */
        static void flushSubtree( SceneComponent* pRoot, bool bParentChanged, FlushStack& stack );

    private:
        /** @brief 틱 큐 건 하나의 순서 키입니다(슬롯의 건 목록과 같은 위치에 있습니다). */
        struct TickWriteKey
        {
            uint64 _writerID{ 0 }; ///< 이 쓰기를 낸 틱의 주인 오브젝트 id(틱 밖이면 0)
            uint32 _sequence{ 0 }; ///< 그 스레드가 올린 순번이다. 같은 오브젝트의 틱이 잇따라 쓴 건의 순서를 나타낸다
        };
        /** @brief 정렬할 틱 큐 건 하나입니다. 비교에 쓰는 값을 함께 보관해서 정렬이 건을 따라가지 않아도 됩니다. */
        struct OrderedTickWrite
        {
            const SceneTransformWrite* _pWrite{ nullptr };
            uint64                     _targetID{ 0 }; ///< 대상 componentID
            TickWriteKey               _key;
        };

        /** @brief 루트의 대기 플래그를 잡아 본 목록에 올립니다. 이미 잡혀 있으면 false 를 반환합니다. */
        [[nodiscard]] static bool tryMarkQueued( SceneComponent* pRoot );
        /** @brief 더티 루트 목록을 비우며 각 루트의 대기 플래그를 내립니다(플러시, clear). */
        void releaseDirtyRoots();
        /** @brief 모든 스레드 슬롯의 쓰기 큐와 대기 슬롯 목록을 비웁니다(적용 뒤). */
        void clearQueuedWrites();

        /**
         * @brief 로컬 값이 바뀐 슬롯의 월드를 맞춥니다. **워커에서 불립니다.**
         * @details 잎 루트(부모도 자식도 없는 루트)는 **컴포넌트를 거치지 않고** 슬롯에서 바로 합성하고 알린다. 순서를 기다릴 부모도
         *          내려갈 자식도 없고, 플러시 패스가 그 루트를 만질 일도 없다(큐브 8000 개가 모두 움직이는 프레임에서 사후 플러시
         *          114 us 가 통째로 사라진다). 계층이 있는 것은 소유 컴포넌트를 거쳐 더티를 표시하고 루트를 워커 스크래치에 올린다.
         * @return 잎 루트라 여기서 합성했으면 true 입니다.
         */
        [[nodiscard]] static bool applyLocalChange( SceneTransformPage& page, uint32 pageIndex, PrimitiveRegistry* pRegistry );
        /** @brief 슬롯 하나의 틱 대기 값을 로컬 값으로 옮기고 월드를 맞춥니다. 워커에서 불립니다. 값이 실제로 바뀌었으면 true 입니다. */
        [[nodiscard]] static bool applyPendingSlot( SceneTransformStorage& storage, uint32 transformSlot, PrimitiveRegistry& registry );
        /**
         * @brief 쓰기 한 건을 컴포넌트에 적용합니다. 워커에서 불립니다. 값이 같으면 아무것도 하지 않고 false 를 반환합니다.
         * @details 뒤처리는 `applyLocalChange` 가 한다. 잎 루트였으면 컴포넌트의 더티도 내린다. 이미 더티 목록에 있던 루트를 플러시가
         *          다시 합성하지 않게 하려는 것이다(컴포넌트를 이미 만진 경로라 비용이 싸다). 구조 변경(attach, detach)이 없는 구간에서만 부른다.
         */
        [[nodiscard]] static bool applyWrite( SceneComponent& target, const SceneTransformWrite& write );
        /**
         * @brief 쓰기 [start, end) 를 순서대로 적용합니다. 워커에서 불립니다. 죽었거나 씬 컴포넌트가 아닌 건은 건너뜁니다.
         * @param bUseCachedTarget 건이 가진 `_pTarget` 을 믿고 핸들을 다시 찾지 않을지 나타냅니다. 같은 `tick()` 안에서 쌓이고 적용되는 틱 큐만 true 입니다.
         */
        static uint32 applyWriteRange( GameObjectManager& manager, const SceneTransformWrite* pWrite, uint32 start, uint32 end, bool bUseCachedTarget );
        /** @brief 대기 슬롯 목록을 이어 붙인 번호 공간을 고르게 나눠 적용합니다(`applyTickWrites` 의 앞 절반). 바뀐 슬롯 수를 반환합니다. */
        uint32 applyPendingSlots( PrimitiveRegistry& registry );
        /** @brief 쓰기 큐를 대상 단위로 정렬하고 분할해 적용합니다(`applyTickWrites` 의 뒤 절반). 바뀐 건수를 반환합니다. */
        uint32 applyQueuedWriteSlots( GameObjectManager& manager );
        /** @brief 정렬한 틱 큐 건 [start, end) 를 순서대로 적용합니다. 워커에서 불립니다. 대상은 건이 가진 `_pTarget` 입니다. */
        static uint32 applyOrderedWrites( const OrderedTickWrite* pOrdered, uint32 start, uint32 end );

        /** @brief 이번 플러시가 순회할 루트입니다. 더러워진 노드가 자기 루트를 한 번씩 올립니다. */
        vector<SceneComponent*> _listDirtyRoot;
        /** @brief 워커가 올린 더티 루트입니다. 스레드 슬롯마다 하나이며 `engine::getParallelScratchSlotCount()` 크기입니다. */
        vector<vector<SceneComponent*>> _listDirtyRootScratch;
        /// @brief 워커가 자기 스크래치를 찾는 포인터입니다. 바깥 컨테이너를 워커가 인덱싱하면 레이스 탐지기가 쓰기로 계산합니다. `mergeQueuedDirtyRoots` 가 맞춥니다.
        vector<SceneComponent*>* _pDirtyRootScratch;
        uint32                   _dirtyRootScratchCount;
        /** @brief 병렬 틱 중 다른 오브젝트의 컴포넌트에 쓴 건의 큐입니다. 스레드 슬롯마다 하나이며, 틱이 끝나면 배치로 적용됩니다. */
        vector<vector<SceneTransformWrite>> _listWriteScratch;
        /// @brief 워커가 자기 스크래치를 찾는 포인터입니다. `_pDirtyRootScratch` 와 같은 이유입니다. `beginTickWrites` 가 맞춥니다.
        vector<SceneTransformWrite>* _pWriteScratch;
        /** @brief 쓰기 큐의 건마다 순서 키입니다. 스레드 슬롯마다 하나이며 건 목록과 같은 위치에 있습니다. 크기는 `_listWriteScratch` 와 같습니다. */
        vector<vector<TickWriteKey>> _listWriteKeyScratch;
        /// @brief 워커가 자기 스크래치를 찾는 포인터입니다. `_pWriteScratch` 와 같은 이유입니다. `beginTickWrites` 가 맞춥니다.
        vector<TickWriteKey>* _pWriteKeyScratch;
        /** @brief 병렬 틱 중 슬롯에 바로 쓴 컴포넌트의 슬롯 번호입니다. 스레드 슬롯마다 하나이며, 크기는 `_listWriteScratch` 와 같습니다. */
        vector<vector<uint32>> _listPendingSlotScratch;
        /// @brief 워커가 자기 스크래치를 찾는 포인터입니다. `_pWriteScratch` 와 같은 이유입니다. `beginTickWrites` 가 맞춥니다.
        vector<uint32>* _pPendingSlotScratch;
        /** @brief 쓰기 큐와 대기 슬롯 목록의 스레드 슬롯 수입니다. 둘은 같이 늘어납니다. */
        uint32 _writeScratchCount;
        /** @brief 이번 적용에서 비어 있지 않은 스레드 슬롯입니다. 적용 잡의 입력이고, 할당을 재사용합니다. */
        vector<uint32> _listActiveScratchSlot;
        /** @brief 위 슬롯 목록들을 이어 붙였을 때 각 목록의 시작 번호입니다(마지막 값은 전체 수). 할당을 재사용합니다. */
        vector<uint32> _listActiveScratchStart;
        /** @brief 틱 큐의 건을 (대상, 쓴 오브젝트, 순번)으로 정렬한 것입니다. 할당을 재사용합니다. */
        vector<OrderedTickWrite> _listOrderedWrite;
        /** @brief 정렬한 틱 큐를 잡으로 나눈 경계입니다. 대상 경계에서만 자릅니다. 마지막 값은 전체 건수입니다. */
        vector<uint32> _listWriteGroupStart;
        /** @brief 배치를 대상 버킷으로 나눈 건 번호입니다. 버킷마다 배열 순서를 유지합니다. 프레임마다 비우고 다시 채우며 할당은 재사용합니다. */
        vector<vector<uint32>> _listBatchBucket;
        /** @brief 스레드 슬롯마다 하나씩 재사용하는 DFS 스택입니다. `engine::getParallelScratchSlotCount()` 크기입니다. */
        vector<FlushStack> _listScratchStack;
        /**
         * @brief 트랜스폼이 바뀔 때마다 오르는 세대입니다. 바깥에서 "무엇이 바뀌었나"를 싸게 확인하는 변경 카운터입니다.
         * @details 플러시가 할 일이 있는지는 이 값이 아니라 더티 루트 목록으로 판단한다.
         */
        atomic<uint64> _dirtyGeneration;
    };
} // namespace sw

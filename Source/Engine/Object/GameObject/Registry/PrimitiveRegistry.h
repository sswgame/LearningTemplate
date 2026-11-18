/**
 * @file PrimitiveRegistry.h
 * @brief 그릴 수 있는 컴포넌트의 레지스트리입니다.
 *
 * [왜 별도 클래스인가]
 * 렌더러가 매 프레임 씬을 뒤져 그릴 것을 **찾는** 대신, 그릴 것이 붙을 때 자기를 **등록합니다**.
 * 그러면 프레임 루프에서 타입 검사(`castTo`)가 사라지고, 비용이 "전체 컴포넌트 수"가 아니라
 * "그릴 것의 수"에 비례합니다.
 *
 * 이것을 GameObjectManager 안에 두지 않은 이유는 `TickRegistry` 와 같습니다. 매니저는 이미 오브젝트
 * 저장소, 컴포넌트 풀, 팩토리, 틱 레지스트리를 가지고 있어서, 여기에 레지스트리까지 넣으면 컴포넌트가
 * 등록 하나 하려고 그 모두에 손이 닿습니다. 기능을 별도 타입으로 떼어 두면 컴포넌트는 자기가 쓰는
 * 것만 가지고 있으면 됩니다. 언리얼이 `UWorld` 안에 `FScene` 과 `FPhysScene` 을 따로 두는 것과 같은
 * 구성입니다.
 *
 * 레지스트리는 기반 타입(`ScenePrimitive`, `ScenePrimitiveBatch`)만 압니다. 메시 헤더를 include 하지 않으므로 코어가 기능 폴더를
 * 가리키지 않습니다. 들어오는 타입이 메시 계열뿐이라는 불변식은 기반의 private 생성자가 지킵니다(`ScenePrimitive.h`).
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/span.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    class Component;
    class ScenePrimitive;
    class ScenePrimitiveBatch;

    /**
     * @brief 인스턴스 배치의 항목 하나입니다. 레지스트리의 프리미티브 번호 공간에서 메시 칸 영역 뒤에 이어 붙습니다. 빈 칸(지운 배치의 자리)은 `_pBatch` 가 널입니다.
     * @details 번호는 메시 칸 영역의 크기(`getAll().size()` — 빈 칸 포함), 배치의 첫 항목 위치, 항목 인덱스를 더한 값이다. 메시 칸 영역은 두 배씩만 늘고
     *          그때만 이 번호가 밀린다 — 그 변경은 배치 세대(`getLayoutGeneration`)를 올려 빌더가 전체 수집으로 간다.
     */
    struct PrimitiveInstanceEntry
    {
        ScenePrimitiveBatch* _pBatch{ nullptr };
        uint32               _index{ 0 };
        /// @brief 이 칸에 들어온 차례입니다(`PrimitiveRegistry::getPrimitiveSerials` 와 같은 뜻 — 칸을 다시 쓸 때마다 오른다). 지운 뒤에도 남겨 다음 차례가 이어진다.
        uint32 _serial{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class PrimitiveRegistry
     * @brief 등록된 프리미티브 목록과 "무엇이 바뀌었나" 신호를 관리합니다.
     * @note 락 순서에 주의하십시오. 이 클래스의 락은 항상 **가장 안쪽**입니다. 매니저나 계층이 자기 락을 쥔 채
     *       등록과 해제를 부를 수 있으므로, 반대로 이 락을 쥔 채 매니저 락을 잡으면 교착이 됩니다. 그래서 등록과 해제는
     *       여기서만 끝냅니다(더티 표시는 락을 쓰지 않습니다).
     */
    class SW_API PrimitiveRegistry
    {
    public:
        /**
         * @brief 에셋 로드(또는 그 뒤의 등장 · 조립)를 기다리는 컴포넌트가 프레임마다 받는 함수입니다. 아직 기다리면 true, 다 끝났으면 false 를 반환합니다.
         * @details 씬 컴포넌트에 묶지 않는다 — 메시를 직접 그리지 않고 읽은 에셋으로 다른 컴포넌트를 조립하는 컴포넌트(외형)도 같은 목록에 걸어야
         *          자동화의 "에셋 일 0" 이 그 조립까지 센다(따로 세는 길을 두지 않는다).
         * @param deltaSeconds 이번 씬 프레임의 시간(초)입니다 — 등장 비율을 올리고 내리는 데 쓴다(`MeshComponent::beginAppearance`).
         */
        using AssetWaitFunction = bool ( * )( Component& component, float32 deltaSeconds );
        /** @brief 빈 레지스트리를 만듭니다. */
        PrimitiveRegistry() = default;
        /**
         * @brief 레지스트리를 비웁니다. 프리미티브의 수명은 GameObject 가 관리합니다.
         * @details 아직 등록된 인스턴스 배치가 있으면 그 배치의 레지스트리 포인터를 비웁니다. 배치가 나중에 사라져도 여기로 오지 않게 합니다.
         */
        ~PrimitiveRegistry();

        PrimitiveRegistry( const PrimitiveRegistry& )            = delete;
        PrimitiveRegistry& operator=( const PrimitiveRegistry& ) = delete;

        /** @brief 프리미티브를 등록합니다. 붙을 때 한 번 부릅니다. 이미 등록됐으면 무시합니다. */
        void add( ScenePrimitive* pPrimitive );
        /** @brief 프리미티브를 등록 해제합니다. 여러 번 불러도 안전합니다. */
        void remove( ScenePrimitive* pPrimitive );

        /**
         * @brief 이 프리미티브의 렌더 상태가 바뀌었다고 표시합니다. **잠금이 없습니다.** 워커 스레드에서 불러도 됩니다.
         * @details 트랜스폼 플러시가 루트 서브트리마다 병렬로 돌면서 이 함수를 부른다. 뮤텍스를 쥐고 목록에 push 하면 큐브 8000 개가
         *          모두 움직이는 프레임에 워커들이 그 잠금을 8000 번 다투므로, 슬롯마다 원자 비트 하나를 쓴다(워드 하나가 슬롯 64 개이며
         *          `_arrDirtyWord` 가 그 워드다). 0 에서 1 로 바꾼 쪽만 "하나라도" 플래그를 켠다. 컴포넌트 쪽에는 더티 표시가 없다(비트필드라 워커의 쓰기가
         *          이웃 비트와 같은 바이트를 고친다). 이 플래그가
         *          유일한 기준이고, 이미 켜진 슬롯은 읽기만 하므로 같은 프리미티브를 여러 번 표시해도 쓰기는 한 번이다.
         *          `_listPrimitive` 는 잠금 없이 읽는다. add 와 remove 는 게임 스레드가 병렬 구간 밖에서만 부른다
         *          (컴포넌트 추가와 제거는 틱 중에 미뤄진다). 그 전제가 깨지면 컨테이너 레이스 탐지기가 잡는다.
         */
        void markDirty( ScenePrimitive* pPrimitive );
        /**
         * @brief 프리미티브 번호 하나의 **월드 행렬만** 바뀌었다고 표시합니다. 잠금이 없습니다. 워커에서 불러도 됩니다.
         * @details 트랜스폼 슬롯에 적힌 번호로 표시한다(`SceneTransformPage::_arrPrimitiveIndex`). 틱 뒤 적용이 컴포넌트를 거치지 않고 부르는
         *          곳이라 `markDirty` 처럼 목록과 대조하지 않는다. 번호는 이 레지스트리가 준 것이고, add 와 remove 는 병렬 구간 밖이기 때문이다.
         *
         *          렌더 상태(`markDirty`)와 플래그가 **따로**다. 빌더는 이쪽만 켜진 프리미티브를 다시 모으지 않고 트랜스폼 저장소의 행렬만
         *          옮긴다(언리얼이 `UpdatePrimitiveTransform` 을 프록시 재생성과 구분하는 것과 같다). 메시, 머티리얼, 가시성은 움직였다고
         *          바뀌지 않기 때문이다.
         */
        void markTransformDirty( uint32 primitiveIndex );
        /**
         * @brief 프리미티브 **집합**이 바뀌었음을 표시합니다(등록 · 해제가 스스로 부르고, 정적 여부를 바꾼 메시도 부른다). 오브젝트 활성 토글은 여기가 아니라
         *        그 오브젝트의 메시가 자기 슬롯을 더티로 표시합니다(`Component::onOwnerActiveInHierarchyChanged`).
         * @details 집합 전체를 보는 소비자(정적 그림자 지문, 실시간 프로브의 복셀 격자)가 이 세대로 캐시를 버린다. 칸 번호는 바뀌지 않으므로 GPUScene 은
         *          이 세대로 전체 수집을 하지 않는다 — 바뀐 칸은 더티 비트로 받는다(`getLayoutGeneration`).
         */
        void markSetDirty() { _setGeneration.fetch_add( 1, std::memory_order_relaxed ); }

        /**
         * @brief 메시 칸 영역입니다. 렌더 스냅샷 수집의 입력입니다(인스턴스 배치 항목은 `getInstanceEntries`). 실제 타입은 `MeshComponent` 계열이고, **빈 칸은 널**입니다.
         * @details 칸은 고정이다 — 지우면 그 칸을 비워 프리리스트에 넣고 다음 등록이 다시 쓴다(언리얼 `FScene` 의 영속 프리미티브 칸 · 유니티 GPU Resident Drawer 의 인스턴스 칸).
         *          그래서 하나를 더하거나 지워도 다른 프리미티브의 번호가 그대로이고, GPUScene 은 바뀐 칸만 다시 모은다.
         */
        const vector<ScenePrimitive*>& getAll() const { return _listPrimitive; }
        /** @brief 집합의 세대입니다. 등록 · 해제 · 정적 여부 바꾸기에 오릅니다(`markSetDirty`). */
        uint64 getSetGeneration() const { return _setGeneration.load( std::memory_order_relaxed ); }
        /**
         * @brief 칸 배치의 세대입니다. 칸 번호가 밀릴 때(메시 칸 영역을 늘려 인스턴스 항목 번호가 밀림)만 오릅니다. 값이 달라졌으면 칸마다의 캐시를 버리고 전체를 다시 모읍니다.
         * @details 늘리기는 두 배씩이라 드물다. 그 밖의 등록 · 해제는 칸을 옮기지 않고 그 칸만 더티로 표시한다.
         */
        uint64 getLayoutGeneration() const { return _layoutGeneration.load( std::memory_order_relaxed ); }
        /**
         * @brief 집합 세대 @p sinceGeneration 뒤에 등록 · 해제된 **메시 칸** 번호를 @p outListSlot 에 담습니다(같은 칸이 두 번 올 수 있다). 기록이 그만큼 거슬러 가지 못하면 false 입니다.
         * @details 집합 전체를 보던 캐시(실시간 프로브의 복셀 격자)가 바뀐 칸만 다시 칠하려고 쓴다. 기록은 고리 `kMeshSetChangeLogCapacity` 개라 오래 안 본 소비자는 false 를 받고 다시 만든다.
         *          인스턴스 배치 · 정적 여부 바꾸기는 세대만 올리고 기록하지 않는다 — 메시 칸이 아니다(이 기록의 소비자는 메시 칸만 본다).
         */
        [[nodiscard]] bool collectMeshSetChanges( uint64 sinceGeneration, vector<uint32>& outListSlot ) const;
        /** @brief 렌더 상태가 바뀐 프리미티브가 하나라도 있으면 true 입니다. */
        bool hasDirty() const;
        /** @brief 더티 표시를 모두 지웁니다. 렌더 스냅샷이 반영을 마친 뒤 부릅니다. */
        void clearDirty();
        /**
         * @brief 더티 목록을 둘로 나눠 옮기고 표시를 지웁니다(`clearDirty` 와 목록 가져오기를 합친 동작입니다). 렌더 상태가 바뀐 번호는 `outListStateSlot` 에,
         *        **월드 행렬만** 바뀐 번호는 `outListTransformSlot` 에 담습니다. 둘 다 오름차순이고 겹치는 번호는 없습니다(둘 다 켜진 번호는 앞쪽에 담습니다).
         * @details 받는 쪽은 **바뀐 것만 다시 모으려고** 이 목록을 쓴다(8000 개 중 10 개만 움직이면 10 개만 모은다).
         */
        void consumeDirty( vector<uint32>& outListStateSlot, vector<uint32>& outListTransformSlot );
        /**
         * @brief 메시 컴포넌트마다 트랜스폼 저장소의 슬롯 번호입니다(`getAll()` 과 같은 순서와 길이). 빌더가 행렬을 컴포넌트를 거치지 않고 읽습니다.
         * @details 슬롯 번호는 컴포넌트 수명 동안 바뀌지 않으므로 등록할 때 한 번 기록하고, 제거할 때 위치 이동을 따라간다.
         */
        const vector<uint32>& getTransformSlots() const { return _listPrimitiveTransformSlot; }
        /**
         * @brief 메시 칸마다 그 칸에 들어온 차례입니다(`getAll()` 과 같은 길이). 등록이 칸을 받을 때마다 오른다(지운 칸을 다시 쓰면 다른 값).
         * @details 칸 번호에 묶인 GPU 상태(인스턴스 LOD 전환 · 칸 순 인스턴스 버퍼)가 칸을 다시 쓴 것을 알아채는 근거다 — 같은 칸 · 같은 내용이어도 차례가 다르면 다른 물체다.
         *          인스턴스 항목 칸의 차례는 `PrimitiveInstanceEntry::_serial` 이다.
         */
        const vector<uint32>& getPrimitiveSerials() const { return _listPrimitiveSerial; }

        /**
         * @brief 인스턴스 배치를 등록합니다. 항목마다 프리미티브 번호 하나를 받습니다(메시 컴포넌트 뒤에 이어서). 집합 세대가 오릅니다.
         * @details 배치는 소유하지 않는다. 배치가 먼저 사라지면 자기 소멸자에서 빠지고, 레지스트리가 먼저 사라지면 배치의 포인터를 비운다.
         */
        void addInstanceBatch( ScenePrimitiveBatch* pBatch );
        /** @brief 인스턴스 배치를 제거합니다. 항목 구간을 비워 프리리스트에 돌려줍니다(다른 배치는 자리가 그대로다). 집합 세대가 오릅니다. `removeInstanceBatches` 의 배치 하나짜리 호출입니다. */
        void removeInstanceBatch( ScenePrimitiveBatch* pBatch );
        /**
         * @brief 인스턴스 배치 여럿을 한 번에 제거합니다. 배치마다 항목 구간을 비우고 더티로 표시한 뒤 빈 구간 목록에 합칩니다(집합 세대는 한 번 오릅니다).
         * @details 남은 배치는 옮기지 않는다 — 옮기면 그 뒤 항목 전부의 칸 번호가 바뀌어 GPUScene 이 전체를 다시 모은다. 이 레지스트리에 들지 않은 배치 · 널 · 두 번 넘긴 배치는
         *          건너뜁니다. 지운 배치는 등록 전 상태가 됩니다.
         */
        void removeInstanceBatches( vector_reference<ScenePrimitiveBatch* const> listBatch );
        /** @brief 배치의 항목 하나를 더티로 표시합니다. 그 항목의 프리미티브 번호에 플래그를 켭니다. */
        void markInstanceDirty( ScenePrimitiveBatch* pBatch, uint32 index );
        /** @brief 인스턴스 배치의 항목 목록입니다. 메시 칸 영역 뒤에 이어지는 프리미티브들이고, 빈 항목(`_pBatch` 널)은 지운 배치의 자리입니다. */
        const vector<PrimitiveInstanceEntry>& getInstanceEntries() const { return _listInstanceEntry; }
        /** @brief 프리미티브 번호 공간의 크기입니다(메시 칸 영역 + 인스턴스 항목 목록, 빈 칸 포함). */
        uint32 getSlotCount() const { return static_cast<uint32>( _listPrimitive.size() + _listInstanceEntry.size() ); }
        /** @brief 살아 있는 칸 수입니다(등록된 메시 + 등록된 배치의 항목 — 빈 칸은 세지 않는다). 디버그 HUD 가 읽습니다. */
        uint32 getLiveSlotCount() const { return _liveSlotCount; }
        /**
         * @brief 에셋 로드(`AssetHandle`)를 기다리는 컴포넌트를 겁니다(게임 스레드). 이미 걸려 있으면 함수만 바꿉니다.
         * @details 기반에 가상 함수를 더하지 않으려고 함수 포인터로 받는다(파일 머리 `ScenePrimitive.h`). 프리미티브는 `remove` 가 같이 빼고,
         *          이 레지스트리에 인스턴스 배치를 거는 컴포넌트(폴리지 · VFX — 메시가 와야 배치를 짓는다)는 등록을 풀 때 `removeAssetWaiter` 로 뺀다 — 사라진 컴포넌트는 불리지 않는다.
         *          자동화의 "에셋 일 0" 판정(`AutomationCaptureUtil::countPendingAssetWork`)이 이 목록의 수를 센다 — 메시를 기다리는 컴포넌트는 여기에 걸어야 사진이 기다린다.
         */
        void addAssetWaiter( Component* pComponent, AssetWaitFunction pFunction );
        /** @brief 기다리는 컴포넌트를 뺍니다(등록을 풀 때). 걸려 있지 않으면 아무 일도 없습니다. */
        void removeAssetWaiter( const Component* pComponent );
        /**
         * @brief 기다리는 프리미티브의 함수를 한 번씩 부르고 다 받은 것을 뺍니다(씬 프레임 `AssetArrival` 단계, 게임 스레드, 병렬 구간 밖).
         * @details 함수 안에서 다시 `addAssetWaiter` 를 불러도 된다(다른 메시로 바꿔 다시 기다리는 경우) — 반환값이 남길지를 정한다.
         */
        void updateAssetWaiters( float32 deltaSeconds );
        /** @brief 에셋을 기다리는 컴포넌트 수입니다(테스트 · 진단 · 자동화의 캡처 대기). */
        size_t countAssetWaiters() const { return _listAssetWaiter.size(); }

        /**
         * @brief LOD 단계가 여럿인 메시를 그리는 프리미티브를 겁니다(게임 스레드). 이미 걸려 있으면 무시합니다. 메시 LOD 고르기(`MeshLODSystem`)가 이 목록만 돈다.
         * @details 씬의 모든 프리미티브를 프레임마다 훑지 않으려고 따로 둔다. 메시가 바뀔 때 컴포넌트가 걸고 떼며, `remove` 가 같이 뺀다.
         */
        void addLODPrimitive( ScenePrimitive* pPrimitive );
        /** @brief LOD 목록에서 뺍니다. 걸려 있지 않으면 아무 일도 없습니다. */
        void removeLODPrimitive( ScenePrimitive* pPrimitive );
        /** @brief LOD 단계가 여럿인 메시를 그리는 프리미티브들입니다(게임 스레드). */
        const unordered_set<ScenePrimitive*>& getLODPrimitives() const { return _setLODPrimitive; }

    private:
        /** @brief 에셋을 기다리는 컴포넌트 하나입니다. */
        struct AssetWaiter
        {
            Component*        _pComponent{ nullptr };
            AssetWaitFunction _pFunction{ nullptr };
        };

        /** @brief `_mutex` 를 이미 쥔 채로 기다리는 컴포넌트를 뺍니다. */
        void removeAssetWaiterLocked( const Component* pComponent ) SW_REQUIRES_LOCK( _mutex );

        /** @brief 메시 칸 영역의 처음 크기입니다(그 뒤로 두 배씩). 더티 비트 워드 하나(64 칸)와 같다. */
        static constexpr uint32 kInitialMeshSlotCount = 64;
        /** @brief 메시 칸 등록 · 해제 기록(`collectMeshSetChanges`)의 고리 크기입니다. 한 프레임 스폰 · 소멸 수백 개를 담고 남는다. */
        static constexpr uint32 kMeshSetChangeLogCapacity = 1024;

        /** @brief 메시 칸 등록 · 해제 기록 하나입니다(그 일로 오른 뒤의 집합 세대와 칸 번호). */
        struct MeshSetChange
        {
            uint64 _generation{ 0 };
            uint32 _slot{ 0 };
        };
        /** @brief 메시 칸 하나의 등록 · 해제를 기록하고 집합 세대를 올립니다(`_mutex` 를 쥔 채). */
        void recordMeshSetChange( uint32 slot ) SW_REQUIRES_LOCK( _mutex );

        /** @brief 비어 있는 인스턴스 항목 구간 하나입니다(`_listFreeEntrySpan`). */
        struct EntrySpan
        {
            uint32 _start{ 0 };
            uint32 _count{ 0 };
        };

        /**
         * @brief 메시 칸 영역을 두 배로 늘리고 새 칸을 프리리스트에 넣습니다(`_mutex` 를 쥔 채). 인스턴스 항목 번호가 밀리므로 배치 세대를 올리고 더티 비트를 버립니다.
         * @details 비트를 버려도 잃는 것이 없다 — 배치 세대가 오르면 빌더가 모든 칸을 다시 모은다.
         */
        void growMeshRegion() SW_REQUIRES_LOCK( _mutex );
        /** @brief 항목 @p count 개짜리 구간을 빈 구간에서 첫 맞춤으로 떼어 주고, 없으면 목록 끝에 붙입니다. 첫 자리를 반환합니다(`_mutex` 를 쥔 채). */
        [[nodiscard]] uint32 allocateEntrySpan( uint32 count ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 구간을 빈 구간 목록에 넣고 앞뒤 이웃과 합칩니다(`_mutex` 를 쥔 채). 목록은 자리 순입니다. */
        void freeEntrySpan( uint32 start, uint32 count ) SW_REQUIRES_LOCK( _mutex );

        /** @brief `_mutex` 를 이미 쥔 채로 더티 표시를 지웁니다. */
        void clearDirtyLocked() SW_REQUIRES_LOCK( _mutex );

        /** @brief 더티 플래그 배열을 최소 @p count 개의 슬롯으로 키웁니다(`_mutex` 를 쥔 채, 병렬 구간 밖). */
        void growDirtyFlags( uint32 count ) SW_REQUIRES_LOCK( _mutex );
        /** @brief 번호 하나에 렌더 상태 플래그를 켭니다. 메시 컴포넌트와 인스턴스 항목이 같은 경로를 씁니다. */
        void markSlotDirty( uint32 slot );
        /** @brief 플래그 배열 @p pArrWord 의 번호 하나를 켭니다. 이미 켜져 있으면 읽기만 합니다. 0 에서 1 로 바꿨으면 "하나라도" 플래그를 켭니다. */
        void markWordBit( atomic<uint64>* pArrWord, uint32 slot );
        /** @brief 플래그 배열 @p pArrWord 의 번호 하나를 내리고, 켜져 있었는지 반환합니다(`_mutex` 를 쥔 채). */
        static bool takeWordBit( atomic<uint64>* pArrWord, uint32 slot );
        /** @brief 플래그 배열 @p pArrWord 의 번호 하나를 @p bDirty 로 정합니다(`_mutex` 를 쥔 채). */
        static void storeWordBit( atomic<uint64>* pArrWord, uint32 slot, bool bDirty );

    private:
        /** @brief 등록된 메시 컴포넌트입니다. 소유하지 않습니다(수명은 GameObject 가 관리합니다). */
        vector<ScenePrimitive*> _listPrimitive;
        /** @brief `_listPrimitive` 와 같은 위치의 트랜스폼 슬롯 번호입니다(`getTransformSlots`). 빈 칸은 무효 번호입니다. */
        vector<uint32>                                  _listPrimitiveTransformSlot;
        vector<uint32>                                  _listPrimitiveSerial;                ///< 메시 칸마다 들어온 차례(`getPrimitiveSerials`)
        vector<uint32> _listFreePrimitiveSlot           SW_GUARDED_BY( _mutex );             ///< 메시 칸 프리리스트(스택 — 끝에서 꺼낸다). 늘릴 때 작은 번호가 끝에 오게 넣는다
        vector<EntrySpan> _listFreeEntrySpan            SW_GUARDED_BY( _mutex );             ///< 빈 인스턴스 항목 구간(자리 순, 이웃끼리 합쳐 둔다)
        vector<PrimitiveInstanceEntry>                  _listInstanceEntry;                  ///< 배치 순서대로 이어 붙은 항목들
        vector<ScenePrimitiveBatch*> _listInstanceBatch SW_GUARDED_BY( _mutex );             ///< 등록된 배치. 빼기와 자리 당기기에 씁니다
        vector<AssetWaiter>                             _listAssetWaiter;                    ///< 에셋 로드를 기다리는 컴포넌트(`addAssetWaiter`). 게임 스레드만 씁니다
        uint8 _bWalkingAssetWaiters                     SW_GUARDED_BY( _mutex ){ SW_FALSE }; ///< `updateAssetWaiters` 가 도는 중 — 빼기는 자리를 비우기만 한다
        unordered_set<ScenePrimitive*>                  _setLODPrimitive;                    ///< LOD 단계가 여럿인 메시의 프리미티브(`addLODPrimitive`). 밀집 배열 집합이라 걸기 · 떼기가 O(1) 이고 씬 로드가 O(N^2) 가 되지 않습니다. 게임 스레드만 씁니다
        /**
         * @brief 슬롯마다 "바뀌었다" 비트입니다. **워드 하나가 슬롯 64 개**입니다. 원자값이라 워커 여럿이 동시에 표시해도 됩니다.
         * @details `consumeDirty` 는 워드마다 exchange 한 번(8000 슬롯이면 125 번)이고 켜진 비트만 골라 순회한다. 슬롯마다 원자 바이트로 두면
         *          큐브 8000 개가 모두 움직이는 프레임에 잠긴 명령 8000 번이 게임 스레드에서 줄을 서고, 그 줄들은 방금 워커가 쓴 캐시 라인이다.
         *          표시하는 쪽은 그대로 "읽어 보고 없으면 fetch_or" 라 이미 켜진 슬롯은 쓰지 않는다.
         */
        unique_ptr<atomic<uint64>[]> _arrDirtyWord;
        /**
         * @brief 슬롯마다 "월드 행렬만 바뀌었다" 비트입니다(`markTransformDirty`). 구조는 `_arrDirtyWord` 와 같고 같이 늘어납니다.
         * @details 렌더 상태 비트와 따로 두는 이유는 빌더가 이쪽만 켜진 슬롯은 컴포넌트를 다시 읽지 않고 행렬만 옮기기 때문이다. 큐브 8000 개가 모두
         *          움직이면 전부가 이쪽이다.
         */
        unique_ptr<atomic<uint64>[]> _arrTransformDirtyWord;
        /// @brief 더티 비트가 덮는 슬롯 수입니다. 항상 64 의 배수이고 번호 공간(`getSlotCount`) 이상입니다.
        uint32 _dirtyFlagCapacity{ 0 };
        /**
         * @brief "켜진 플래그가 하나라도 있다"는 표시입니다. `hasDirty` 가 배열을 훑지 않고 답하는 근거입니다.
         * @details 개수가 아니라 플래그로 둔 이유는, 개수를 쓰면 워커 14개가 같은 캐시 라인을 8000 번 fetch_add 해서
         *          병렬 플러시가 직렬(200 us)보다 느려지기 때문이다(262 us). 플래그는 이미 켜져 있으면 읽기만 하므로(쓰기는
         *          프레임에 한 번) 라인이 공유 상태로 유지된다.
         */
        atomic<uint8> _bAnyDirty{ 0 };
        /// @brief add 와 remove(구조 변경)만 보호합니다. 더티 표시는 이 잠금을 쓰지 않습니다.
        mutable mutex                            _mutex;
        atomic<uint64>                           _setGeneration{ 1 };
        atomic<uint64>                           _layoutGeneration{ 1 };       ///< 칸 번호가 밀린 횟수(`getLayoutGeneration`)
        uint32                                   _liveSlotCount{ 0 };          ///< 살아 있는 칸 수(`getLiveSlotCount`). 등록 · 해제가 잠금 안에서 고친다
        vector<MeshSetChange> _listMeshSetChange SW_GUARDED_BY( _mutex );      ///< 메시 칸 등록 · 해제 고리(`kMeshSetChangeLogCapacity` 개까지 자란 뒤 덮어쓴다)
        uint64 _meshSetChangeTotal               SW_GUARDED_BY( _mutex ){ 0 }; ///< 지금까지 기록한 수(고리의 다음 자리 = 이 값 % 용량)
        uint64 _meshSetChangeLogStart            SW_GUARDED_BY( _mutex ){ 0 }; ///< 이 세대 뒤의 메시 칸 변경은 모두 고리에 있다(덮어쓴 기록의 세대)
    };
} // namespace sw

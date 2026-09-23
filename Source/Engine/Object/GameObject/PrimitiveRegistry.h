/**
 * @file PrimitiveRegistry.h
 * @brief 그릴 수 있는 컴포넌트의 등록부
 *
 * [왜 별도 클래스인가]
 * 렌더러가 매 프레임 씬을 뒤져 그릴 것을 **찾는** 대신, 그릴 것이 붙을 때 자기를 **등록한다**.
 * 그러면 프레임 루프에서 타입 검사(`castTo`)가 사라지고, 비용이 "전체 컴포넌트 수"가 아니라
 * "그릴 것의 수"에 비례한다.
 *
 * 이걸 GameObjectManager 안에 두지 않은 이유는 PhysicsWorld 와 같다. 매니저는 이미 오브젝트
 * 저장소 + 컴포넌트 풀 + 팩토리 + 틱 웨이브를 들고 있어서, 여기에 등록부까지 넣으면 컴포넌트가
 * 등록 하나 하려고 그 전부에 손이 닿는다. 능력을 별도 타입으로 떼어 두면 컴포넌트는 자기가 쓰는
 * 것만 들고 있으면 된다 — 언리얼이 `UWorld` 안에 `FScene`·`FPhysScene` 을 따로 두는 것과 같은
 * 구성이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    class MeshComponent;
    class MeshInstanceBatch;

    /**
     * @brief 인스턴스 배치의 항목 하나 — 등록부의 프리미티브 번호 공간에서 메시 컴포넌트 뒤에 이어 붙는다.
     * @details 번호 = 메시 컴포넌트 수 + 배치의 첫 항목 자리 + 항목 인덱스. 메시 컴포넌트가 늘거나 줄면 이 번호가 밀리는데,
     *          그 변경은 집합 세대를 올려 빌더가 전체 수집으로 가므로 더티 번호가 어긋나도 답이 틀리지 않는다.
     */
    struct PrimitiveInstanceEntry
    {
        MeshInstanceBatch* _pBatch{ nullptr };
        uint32             _index{ 0 };
    };

    /**
     * @class PrimitiveRegistry
     * @brief 등록된 프리미티브 목록과 "무엇이 바뀌었나" 신호를 관리합니다.
     * @note 락 순서 주의 — 이 클래스의 락은 항상 **가장 안쪽**이다. GameObjectManager 가
     *       `_mutex` 를 쥔 채(flushSceneTransforms) markDirty 를 부르므로, 반대로 이 락을 쥔 채
     *       매니저 락을 잡으면 교착이 된다. 그래서 등록/해제/더티는 여기서만 끝낸다.
     */
    class SW_API PrimitiveRegistry
    {
    public:
        /** @brief 빈 등록부를 만듭니다. */
        PrimitiveRegistry() = default;
        /**
         * @brief 등록부를 비웁니다. 프리미티브 수명은 GameObject 가 쥡니다.
         * @details 아직 등록된 인스턴스 배치가 있으면 그 배치의 등록부 포인터를 비운다 — 배치가 나중에 죽어도 여기로 오지 않게.
         */
        ~PrimitiveRegistry();

        PrimitiveRegistry( const PrimitiveRegistry& )            = delete;
        PrimitiveRegistry& operator=( const PrimitiveRegistry& ) = delete;

        /** @brief 프리미티브를 등록합니다. 붙을 때 1회. 이미 등록됐으면 무시합니다. */
        void add( MeshComponent* pComp );
        /** @brief 프리미티브를 등록 해제합니다. 멱등입니다. */
        void remove( MeshComponent* pComp );

        /**
         * @brief 이 프리미티브의 렌더 상태가 바뀌었다고 표시합니다. **락이 없다** — 워커 스레드에서 불러도 된다.
         * @details 트랜스폼 플러시가 루트 서브트리마다 병렬로 돌면서 여기를 부른다. 예전에는 뮤텍스를 쥐고
         *          목록에 push 했는데, 큐브 8000 개가 전부 움직이는 프레임에는 워커들이 그 락을 8000 번
         *          다투게 된다. 지금은 칸마다 원자 비트 하나다(워드 하나가 64 칸, `_arrDirtyWord`) — 0→1 로 바꾼 쪽만 "하나라도" 를 세운다. 컴포넌트
         *          쪽에는 더티 표시가 없다(비트필드라 워커의 쓰기가 이웃 비트와 같은 바이트를 고쳤다) — 이 플래그가
         *          유일한 정본이고, 같은 프리미티브를 몇 번 찍든 exchange 한 번씩이다.
         *          `_listPrimitive` 는 락 없이 읽는다: add/remove 는 게임 스레드가 병렬 구간 밖에서만 부른다
         *          (컴포넌트 추가·제거는 틱 중에 미뤄진다). 그 전제가 깨지면 컨테이너 레이스 탐지기가 잡는다.
         */
        void markDirty( MeshComponent* pComp );
        /**
         * @brief 프리미티브 **집합**이 바뀌었음을 표시합니다 (등록/해제/활성 토글).
         * @details 드물게 일어나므로 무엇이 바뀌었는지 따지지 않고 전부 다시 만들게 한다.
         */
        void markSetDirty() { _setGeneration.fetch_add( 1, std::memory_order_relaxed ); }

        /** @brief 등록된 프리미티브 목록입니다. 렌더 스냅샷 수집의 유일한 입력. */
        const vector<MeshComponent*>& getAll() const { return _listPrimitive; }
        /** @brief 집합의 세대입니다. 값이 달라졌으면 전체 재구축이 필요합니다. */
        uint64 getSetGeneration() const { return _setGeneration.load( std::memory_order_relaxed ); }
        /** @brief 렌더 상태가 바뀐 프리미티브가 하나라도 있으면 true. */
        bool hasDirty() const;
        /** @brief 더티 표시를 모두 지웁니다. 렌더 스냅샷이 반영을 마친 뒤 부릅니다. */
        void clearDirty();
        /**
         * @brief 더티 목록을 `outListSlot` 으로 옮기고 표시를 지웁니다 (`clearDirty` + 목록 가져오기).
         * @details 받는 쪽은 **바뀐 것만 다시 모으려고** 이 목록을 쓴다 — 예전에는 지우기만 하고
         *          목록을 버려서, 8000 개 중 10 개만 움직여도 8000 개를 전부 다시 모았다.
         */
        void consumeDirty( vector<uint32>& outListSlot );

        /**
         * @brief 인스턴스 배치를 등록합니다 — 항목마다 프리미티브 번호 하나(메시 컴포넌트 뒤에 이어서). 집합 세대가 오른다.
         * @details 배치는 소유하지 않는다. 배치가 먼저 죽으면 자기 소멸자에서 빠지고, 등록부가 먼저 죽으면 배치의 포인터를 비운다.
         */
        void addInstanceBatch( MeshInstanceBatch* pBatch );
        /** @brief 인스턴스 배치를 뺍니다 — 항목 구간을 지우고 뒤 배치의 자리를 당긴다. 집합 세대가 오른다. */
        void removeInstanceBatch( MeshInstanceBatch* pBatch );
        /** @brief 배치의 항목 하나를 더티로 표시합니다 — 그 항목의 프리미티브 번호에 깃발을 세운다. */
        void markInstanceDirty( MeshInstanceBatch* pBatch, uint32 index );
        /** @brief 인스턴스 배치의 항목들 — 메시 컴포넌트 목록 뒤에 이어지는 프리미티브들. */
        const vector<PrimitiveInstanceEntry>& getInstanceEntries() const { return _listInstanceEntry; }
        /** @brief 프리미티브 번호 공간의 크기 = 메시 컴포넌트 수 + 인스턴스 항목 수. */
        uint32 getSlotCount() const { return static_cast<uint32>( _listPrimitive.size() + _listInstanceEntry.size() ); }

    private:
        /** @brief `_mutex` 를 이미 쥔 채로 더티 표시를 지웁니다. */
        void clearDirtyLocked();

        /** @brief 더티 플래그 배열을 최소 @p count 칸으로 키웁니다 (`_mutex` 를 쥔 채, 병렬 구간 밖). */
        void growDirtyFlags( uint32 count );
        /** @brief 번호 하나에 깃발을 세운다 — 메시 컴포넌트와 인스턴스 항목이 같은 길을 쓴다. */
        void markSlotDirty( uint32 slot );
        /** @brief 번호 하나의 깃발을 내리고, 서 있었는지 돌려줍니다 (`_mutex` 를 쥔 채). */
        bool takeSlotDirty( uint32 slot );
        /** @brief 번호 하나의 깃발을 @p bDirty 로 둡니다 (`_mutex` 를 쥔 채). */
        void storeSlotDirty( uint32 slot, bool bDirty );

        /** @brief 소유하지 않습니다 — 수명은 GameObject 가 쥡니다. */
        vector<MeshComponent*>         _listPrimitive;
        vector<PrimitiveInstanceEntry> _listInstanceEntry; ///< 배치 순서대로 이어 붙은 항목들
        vector<MeshInstanceBatch*>     _listInstanceBatch; ///< 등록된 배치 — 빼기와 자리 당기기에 쓴다
        /**
         * @brief 칸마다 "바뀌었다" 비트 — **워드 하나가 칸 64 개**다. 원자라 워커 여럿이 동시에 찍어도 된다.
         * @details 예전엔 칸마다 원자 바이트였고 `consumeDirty` 가 선 칸마다 exchange 를 했다 — 큐브 8000 개가 전부 움직이는
         *          프레임에 잠긴 명령 8000 번이 게임 스레드에서 줄을 섰고, 그 줄들은 방금 워커가 쓴 캐시 라인이었다(2026-09-23
         *          프로파일에서 게임 스레드 바쁜 시간의 3.5 %). 이제 워드마다 exchange 한 번(125 번)이고 선 비트만 골라 돈다.
         *          찍는 쪽은 그대로 "읽어 보고 없으면 fetch_or" 라 이미 선 칸은 쓰지 않는다.
         */
        std::unique_ptr<atomic<uint64>[]> _arrDirtyWord;
        /// @brief 더티 비트가 덮는 칸 수 — 늘 64 의 배수이고 번호 공간(`getSlotCount`) 이상이다.
        uint32 _dirtyFlagCapacity{ 0 };
        /**
         * @brief "서 있는 플래그가 하나라도 있다". `hasDirty` 가 배열을 훑지 않고 답하는 근거.
         * @details 개수가 아니라 플래그인 이유: 개수는 워커 열넷이 같은 캐시 라인을 8000 번 fetch_add 하는 것이라
         *          병렬 플러시가 직렬(200 us)보다 느려졌다(262 us). 플래그는 이미 서 있으면 읽기만 하므로(쓰기는
         *          프레임에 한 번) 라인이 공유 상태로 머문다.
         */
        atomic<uint8> _bAnyDirty{ 0 };
        /// @brief add/remove(구조 변경)만 잡는다. 더티 표시는 잡지 않는다.
        mutable mutex  _mutex;
        atomic<uint64> _setGeneration{ 1 };
    };
} // namespace sw

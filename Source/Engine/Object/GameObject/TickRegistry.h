/**
 * @file TickRegistry.h
 * @brief 틱에 참여하는 오브젝트의 등록부 — 언리얼 `FTickTaskManager` 의 자리. 틱 멤버십이 바뀐 오브젝트만 다시 훑는다.
 *
 * [왜 별도 클래스인가]
 * 예전에는 매니저가 틱 웨이브를 **씬 전체를 훑어** 만들었다 — 컴포넌트 8000 개면 한 번에 0.5~1 ms 인데, 틱하는 컴포넌트가
 * 하나라도 생기거나 없어지면(총알처럼 스폰이 잦은 게임은 매 프레임) 그것을 통째로 다시 만들었다. 게다가 같은 오브젝트의
 * 항목을 서로 다른 서브웨이브로 갈라 서브웨이브마다 포크-조인 한 번(디스패치 바닥 ~50 us)이었다.
 *
 * 지금은 **오브젝트가 자기 틱 항목을 들고**(주 틱 · 서브틱, (그룹, 순서 키) 순), 등록부는 그룹마다 "틱할 것이 있는 오브젝트"
 * 목록을 든다. 멤버십이 바뀐 오브젝트만 표시되어 다음 틱 전에 자기 항목을 다시 짓고(컴포넌트 몇 개를 훑는 값), 디스패치는
 * 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눈다 — 한 오브젝트의 항목은 한 워커가 순서대로 돈다(같은 오브젝트의
 * 컴포넌트 둘이 동시에 돌지 않는다는 규칙은 그대로다).
 *
 * 선행 종속성(`addSubTickPrerequisite`)이 하나라도 등록되어 있으면 매니저가 DAG 웨이브 경로로 간다 — 오브젝트 단위로는
 * 계층을 넘는 순서를 표현할 수 없다. 그 웨이브도 **이 등록부가 자기 항목으로 짓는다**(`buildPrerequisiteWaves`) — 예전에는
 * 매니저가 씬 전체를 다시 훑어 같은 후보를 두 번째로 모았다. 그 캐시는 이 등록부의 세대로 무효화된다.
 *
 * `PhysicsWorld` · `PrimitiveRegistry` · `SceneTransformHierarchy` 와 같은 자리다: 능력은 별도 타입, 매니저는 순서.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;

    /**
     * @struct TickItem
     * @brief 오브젝트가 낼 틱 하나 — 컴포넌트의 주 틱(`_subTickId` 0) 또는 서브틱. 오브젝트가 (그룹, 순서 키) 순으로 든다.
     */
    struct TickItem
    {
        Component* _pComponent{ nullptr };
        uint32     _subTickId{ 0 };
        uint8      _group{ 0 };     ///< `TickGroup`
        uint8      _orderKey{ 64 }; ///< `TickPhase` + 우선순위(0..63)
    };

    /**
     * @brief 오브젝트 하나의 틱 항목. **인라인이 아니다 — 재서 기각했다.**
     * @details 인라인 두 칸(`InlineAllocator`)으로 두면 첫 틱의 등록부 구축이 절반(8000 개 힙 할당이 사라진다)이고 틱 디스패치도
     *          오브젝트당 라인 하나가 준다. 그런데 `GameObject` 가 192 → 232 바이트(3 → 4 라인)가 되어 오브젝트를 만지는 다른
     *          프레임 경로(씬 수집의 활성 검사 · 배치 쓰기의 핸들 해석)가 그만큼 느려졌다 — 틱 −32 us 에 수집·배치 +30~60 us
     *          (Release · 큐브 8000 · 번갈아 2회). 오브젝트가 세 라인에 남는 것이 더 값지다.
     */
    using TickItemList = vector<TickItem>;

    /** @brief 선행 종속성 경로의 웨이브 — 같은 오브젝트의 항목이 한 웨이브에 둘 이상 오지 않는다(웨이브 안은 병렬). */
    using TickWave = vector<TickItem>;

    /**
     * @class TickRegistry
     * @brief 그룹마다 "틱할 것이 있는 오브젝트" 목록 · 멤버십 더티 표시 · 오브젝트 항목 재구축 · 선행 종속성 웨이브.
     * @note 락 순서 — 이 클래스의 락(`_dirtyMutex`)은 가장 안쪽이다. `markObjectDirty` 는 워커에서 불려도 된다(원자 플래그 + 짧은 잠금).
     */
    class SW_API TickRegistry
    {
    public:
        /** @brief `TickGroup` 의 수. */
        static constexpr uint32 kGroupCount = 4;
        /** @brief 목록에 없음. */
        static constexpr uint32 kNotInList = 0xFFFFFFFFu;

        /** @brief 빈 등록부. 첫 `refresh` 가 씬 전체를 한 번 훑는다. */
        TickRegistry();
        /** @brief 목록을 놓습니다. 오브젝트 수명은 매니저가 쥡니다. */
        ~TickRegistry() = default;

        TickRegistry( const TickRegistry& )            = delete;
        TickRegistry& operator=( const TickRegistry& ) = delete;

        /**
         * @brief 오브젝트의 틱 멤버십이 바뀌었다 — 다음 `refresh` 가 그 항목을 다시 짓는다. 아무 스레드에서나 불러도 된다.
         * @details 이미 표시된 오브젝트는 다시 올리지 않는다(원자 플래그). 오브젝트 id 를 들므로 그 사이 죽어도 안전하다.
         */
        void markObjectDirty( GameObject* pObj );
        /** @brief 모든 오브젝트를 다시 훑게 합니다 (타입 재바인딩 · 씬 초기화). */
        void markAllDirty();

        /**
         * @brief 표시된 오브젝트의 항목을 다시 짓고 그룹 목록을 맞춥니다 — 게임 스레드, 틱 밖.
         * @return 하나라도 다시 지었으면 true (DAG 웨이브 캐시를 버리는 신호).
         */
        bool refresh( GameObjectManager& manager );

        /** @brief 오브젝트를 등록부에서 완전히 뺍니다 — 파괴 직전에. 멱등입니다. */
        void unregisterObject( GameObject* pObj );
        /** @brief 목록과 더티 표시를 비웁니다 (매니저 clear). */
        void clear();

        /** @brief 이 그룹에서 틱할 것이 있는 오브젝트들. 디스패치의 유일한 입력. */
        const vector<GameObject*>& getObjects( uint32 group ) const { return _arrListObject[group]; }
        /** @brief 선행 종속성이 등록된 서브틱이 하나라도 있으면 true — 매니저가 DAG 웨이브 경로로 간다. */
        bool hasPrerequisites() const { return _prerequisiteCount > 0; }
        /** @brief 항목이 바뀔 때마다 오르는 세대. DAG 웨이브 캐시가 이것으로 무효화된다. */
        uint64 getGeneration() const { return _generation; }

        /**
         * @brief 선행 종속성을 존중하는 웨이브 목록을 등록된 항목으로 짓습니다 — 그룹 순서대로, 웨이브 안은 병렬.
         * @details 그룹마다: 선행 종속성 DAG 를 Kahn 으로 레벨별 웨이브로 가르고(같은 레벨은 순서 키 · 등록 순서로 안정 정렬),
         *          레벨 하나를 다시 **오브젝트별 서브웨이브**로 가른다 — 같은 오브젝트의 항목이 한 웨이브에서 나란히 돌지 않게.
         *          순환은 남은 것을 순서 키 순으로 마지막 웨이브에 붙여 방어한다. `hasPrerequisites()` 일 때만 부를 값이 있다 —
         *          아니면 그룹 경로가 더 싸다. 세대가 바뀌지 않았으면 부르는 쪽이 캐시를 그대로 쓴다.
         */
        void buildPrerequisiteWaves( vector<TickWave>& outListWave ) const;

    private:
        /** @brief 오브젝트 하나의 항목을 컴포넌트에서 다시 짓고 그룹 멤버십을 맞춥니다. */
        void refreshObject( GameObject* pObj );
        /** @brief 그룹 목록에 넣거나 뺍니다 (O(1), 오브젝트가 자기 자리를 든다). */
        void setMembership( GameObject* pObj, uint32 group, bool bMember );

        vector<GameObject*> _arrListObject[kGroupCount]; ///< 그룹마다 틱할 것이 있는 오브젝트. 소유하지 않는다.
        vector<uint64>      _listDirtyObjectId;          ///< 다시 훑을 오브젝트 id — 죽었으면 해석이 비어 건너뛴다
        vector<uint64>      _listProcessingObjectId;     ///< `refresh` 가 위 목록과 바꿔 쓰는 버퍼(할당 재사용)
        mutex               _dirtyMutex;                 ///< `_listDirtyObjectId` 의 락 — 워커에서 표시할 수 있다
        atomic<uint8>       _bAllDirty;                  ///< 전부 다시 훑는다
        uint32              _prerequisiteCount;          ///< 등록된 서브틱 선행 종속성의 총수
        uint64              _generation;                 ///< 항목이 바뀔 때마다 오른다
    };
} // namespace sw

/**
 * @file TickRegistry.h
 * @brief 틱에 참여하는 오브젝트의 등록부입니다(언리얼 `FTickTaskManager` 의 자리). 틱 멤버십이 바뀐 오브젝트만 다시 훑습니다.
 *
 * [왜 별도 클래스인가]
 * 틱 스테이지를 **씬 전체를 훑어** 만들면 컴포넌트 8000 개에 한 번 0.5~1 ms 이고, 틱하는 컴포넌트가 하나라도 생기거나 없어질
 * 때마다(총알처럼 스폰이 잦은 게임은 매 프레임) 그것을 통째로 다시 만들게 됩니다.
 *
 * 그래서 **오브젝트가 자기 틱 항목을 들고**(주 틱 · 서브틱, (그룹, 순서 키) 순), 등록부는 그룹마다 "틱할 것이 있는 오브젝트"
 * 목록을 듭니다. 멤버십이 바뀐 오브젝트만 표시되어 다음 틱 전에 자기 항목을 다시 짓고(컴포넌트 몇 개를 훑는 값), 디스패치는
 * 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눕니다. 한 오브젝트의 항목은 한 워커가 순서대로 돕니다(같은 오브젝트의
 * 컴포넌트 둘이 동시에 돌지 않는다는 규칙은 그대로입니다).
 *
 * **그룹 목록의 칸은 틱에 필요한 것을 직접 듭니다**(`TickObjectEntry` — 오브젝트 · 첫 항목 · 나머지 항목의 자리). 언리얼
 * `FTickTaskManager` 가 그룹마다 틱 함수 포인터의 납작한 배열을 돌고, 유니티 `BehaviourManager` 가 콜백마다 동작 컴포넌트의 납작한
 * 목록을 도는 것과 같은 모양입니다. 두 엔진 모두 매 틱 액터 · 게임 오브젝트를 거치지 않고, 꺼진 것은 **목록에서 뺍니다.** 여기서도
 * 계층에서 꺼진 오브젝트는 목록에 없습니다(`GameObject::refreshActiveInHierarchy` 가 바뀔 때 표시합니다). 틱 중의 `setActive` 는
 * 틱 뒤로 미뤄지고 파괴는 컴포넌트마다 삭제 표시를 세우므로, 틱 도중에 목록이
 * 낡을 일은 컴포넌트의 표시로 다 가려집니다.
 *
 * **선행 조건(`addSubTickPrerequisite`)을 가진 항목만 스테이지로 갑니다.** 그런 항목은 오브젝트 항목 목록에서 그룹의 끝에 모이고 칸은 그 앞까지만
 * 들므로, 나머지 항목과 선행 조건이 없는 오브젝트는 모두 보통 길(오브젝트 칸 포크-조인)로 돕니다. 매니저는 그룹마다 보통 길을 먼저, 그 그룹의
 * 스테이지를 뒤에 돌립니다 — 그래서 선행 조건이 보통 길의 항목(다른 오브젝트의 주 틱 · 서브틱)이어도 순서가 맞습니다. 스테이지는 **이 등록부가
 * 짓고 듭니다**(`buildStages`). 선행 조건을 가진 오브젝트와 그것이 가리키는 오브젝트가 바뀔 때만 다시 짓습니다 — 사슬 밖 오브젝트의 스폰 ·
 * 파괴는 스테이지를 건드리지 않습니다. 언리얼이 선행 조건 없는 틱 함수를 그룹 안에서 바로 내고 선행 조건이 있는 것만 기다리게 하는 것과 같습니다.
 *
 * `PhysicsWorld` · `PrimitiveRegistry` · `SceneTransformHierarchy` 와 같은 자리입니다. 능력은 별도 타입이 갖고, 매니저는 순서만 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"

namespace sw
{
    class Component;
    class GameObject;
    class GameObjectManager;

    /**
     * @struct TickItem
     * @brief 오브젝트가 낼 틱 하나입니다. 컴포넌트의 주 틱(`_subTickId` 0) 또는 서브틱입니다. 오브젝트가 (그룹, 순서 키) 순으로 듭니다.
     */
    struct TickItem
    {
        Component* _pComponent{ nullptr };
        uint32     _subTickId{ 0 };
        uint8      _group{ 0 };                   ///< `TickGroup`
        uint8      _orderKey{ 64 };               ///< `TickPhase` + 우선순위(0..63)
        uint8      _bHasPrerequisite{ SW_FALSE }; ///< 선행 조건을 가진 서브틱 — 보통 길이 아니라 스테이지가 돈다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 오브젝트 하나의 틱 항목입니다. **인라인이 아닙니다. 재 보고 기각했습니다.**
     * @details 인라인 두 칸(`InlineAllocator`)으로 두면 첫 틱의 등록부 구축이 절반(8000 개 힙 할당이 사라집니다)이고 틱 디스패치도
     *          오브젝트당 캐시 라인 하나가 줍니다. 그런데 `GameObject` 가 192 → 232 바이트(3 → 4 라인)가 되어 오브젝트를 만지는 다른
     *          프레임 경로(씬 수집의 활성 검사 · 배치 쓰기의 핸들 해석)가 그만큼 느려집니다 — 틱 −32 us 에 수집 · 배치 +30~60 us
     *          (Release · 큐브 8000 · 번갈아 2회). 오브젝트가 세 라인에 남는 것이 더 값집니다.
     */
    using TickItemList = vector<TickItem>;

    /**
     * @struct TickStage
     * @brief 선행 조건 경로의 스테이지 하나입니다. 같은 오브젝트의 항목이 한 스테이지에 둘 이상 오지 않습니다(스테이지 안은 병렬).
     * @details `_bApplyBefore` 가 서 있으면 매니저가 이 스테이지를 돌기 **전에** 그때까지 쌓인 틱 중 트랜스폼 쓰기를 적용합니다
     *          (`GameObjectManager::applyStageTransforms`) — 앞에서 돈 선행 조건이 옮긴 자리를 이 스테이지가 같은 프레임에 읽습니다.
     *          기다리는 항목이 없는 스테이지(레벨 0, 같은 레벨을 오브젝트별로 가른 둘째 이후)에는 서지 않습니다.
     */
    struct TickStage
    {
        vector<TickItem> _listItem;
        uint8            _group{ 0 };               ///< 이 스테이지 항목의 그룹(한 스테이지 = 한 그룹)
        uint8            _bApplyBefore{ SW_FALSE }; ///< 돌기 전에 틱 중 트랜스폼 쓰기를 적용할지
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TickObjectEntry
     * @brief 그룹 목록의 칸 하나 — 오브젝트 하나의 그 그룹 항목입니다. 디스패치는 이 칸과 컴포넌트만 읽습니다.
     * @details 첫 항목은 칸 안에 복사해 둡니다. 틱하는 오브젝트는 대개 그룹마다 항목이 하나라 항목 버퍼를 건너지 않습니다. 둘째부터는
     *          오브젝트의 항목 버퍼(`_pItem`, 첫 항목 포함)에서 읽습니다. 그 버퍼는 등록부만 다시 짓고, 다시 지을 때 이 칸도 함께 고칩니다.
     */
    struct TickObjectEntry
    {
        GameObject*     _pObject{ nullptr }; ///< 이 칸의 오브젝트. 틱 중 "지금 이 스레드가 틱하는 오브젝트" 로 적힙니다
        TickItem        _firstItem{};        ///< 이 그룹의 첫 항목(복사본)
        const TickItem* _pItem{ nullptr };   ///< 이 그룹 항목들의 시작(오브젝트의 항목 버퍼 안)
        uint32          _itemCount{ 0 };     ///< 이 그룹 항목 수(1 이상)
    };
} // namespace sw

namespace sw
{
    /**
     * @class TickRegistry
     * @brief 그룹마다의 "틱할 것이 있는 오브젝트" 목록, 멤버십 더티 표시, 오브젝트 항목 재구축, 선행 조건 스테이지를 맡습니다.
     * @note 락 순서: 이 클래스의 락(`_dirtyMutex`)은 가장 안쪽입니다. `markObjectDirty` 는 워커에서 불려도 됩니다(원자 플래그 + 짧은 잠금).
     */
    class SW_API TickRegistry
    {
    public:
        /** @brief `TickGroup` 의 수입니다. */
        static constexpr uint32 kGroupCount = 4;
        /** @brief 목록에 없다는 표시입니다. */
        static constexpr uint32 kNotInList = 0xFFFFFFFFu;

        /** @brief 빈 등록부를 만듭니다. 첫 `refresh` 가 씬 전체를 한 번 훑습니다. */
        TickRegistry();
        /** @brief 목록을 놓습니다. 오브젝트 수명은 매니저가 쥡니다. */
        ~TickRegistry() = default;

        TickRegistry( const TickRegistry& )            = delete;
        TickRegistry& operator=( const TickRegistry& ) = delete;

        /**
         * @brief 오브젝트의 틱 멤버십이 바뀌었다고 표시합니다. 다음 `refresh` 가 그 항목을 다시 짓습니다. 아무 스레드에서나 불러도 됩니다.
         * @details 이미 표시된 오브젝트는 다시 올리지 않습니다(원자 플래그). 오브젝트 id 를 들므로 그 사이 사라져도 안전합니다.
         */
        void markObjectDirty( GameObject* pObj );
        /** @brief 모든 오브젝트를 다시 훑게 합니다(타입 재바인딩 · 씬 초기화). */
        void markAllDirty();

        /**
         * @brief 표시된 오브젝트의 항목을 다시 짓고 그룹 목록을 맞춥니다. 게임 스레드에서 틱 밖에 부릅니다.
         * @details 선행 조건에 걸린 오브젝트가 바뀌었으면 이어서 스테이지를 다시 짓습니다(`buildStages`).
         * @return 하나라도 다시 지었으면 true 입니다.
         */
        bool refresh( GameObjectManager& manager );

        /** @brief 오브젝트를 등록부에서 완전히 뺍니다. 파괴 직전에 부릅니다. 멱등입니다. */
        void unregisterObject( GameObject* pObj );
        /** @brief 목록과 더티 표시를 비웁니다(매니저 clear). */
        void clear();

        /** @brief 이 그룹에서 틱할 것이 있고 계층에서 켜진 오브젝트의 칸들입니다. 디스패치의 유일한 입력입니다. */
        const vector<TickObjectEntry>& getEntries( uint32 group ) const { return _arrListEntry[group]; }
        /** @brief 선행 조건이 등록된 서브틱이 하나라도 있으면 true 입니다. */
        bool hasPrerequisites() const { return _prerequisiteCount > 0; }
        /** @brief 선행 조건을 가진 항목의 스테이지입니다(그룹 순). 매니저가 그룹마다 보통 길 뒤에 그 그룹의 것을 돕니다. */
        const vector<TickStage>& getStages() const { return _listStage; }
        /** @brief 스테이지가 도는 항목 수입니다(선행 조건을 가진 켜진 항목). 나머지는 모두 보통 길입니다. */
        uint32 getStageItemCount() const { return _stageItemCount; }
        /** @brief 스테이지를 다시 지은 횟수(누적)입니다. 사슬 밖 오브젝트의 스폰 · 파괴로는 오르지 않습니다. 진단 · 회귀 테스트용입니다. */
        uint32 getStageBuildCount() const { return _stageBuildCount; }

    private:
        /**
         * @brief 선행 조건을 가진 항목으로 스테이지 목록을 짓습니다. 그룹 순서대로 짓고, 스테이지 안은 병렬입니다.
         * @details 먼저 선행 조건이 뒤 그룹에 있는 항목을 그 그룹으로 옮깁니다(사슬을 따라 — 언리얼 `ActualStartTickGroup`). 보통 길에서 도는
         *          선행 조건은 핸들의 오브젝트 id 로 그 오브젝트의 항목만 찾아 그룹을 봅니다. 그다음 그룹마다: 후보 사이의 선행 조건 DAG 를 Kahn 으로
         *          레벨별 스테이지로 가르고(같은 레벨은 순서 키 · 등록 순서로 안정 정렬), 레벨 하나를 다시 **오브젝트별 스테이지**로 가릅니다 — 같은
         *          오브젝트의 항목이 한 스테이지에서 나란히 돌지 않습니다. 기다리는 레벨의 첫 스테이지에 `TickStage::_bApplyBefore` 를 세웁니다.
         *          순환은 남은 것을 순서 키 순으로 마지막 스테이지에 붙여 방어합니다.
         */
        void buildStages( GameObjectManager& manager );
        /** @brief 오브젝트 하나의 항목을 컴포넌트에서 다시 짓고 그룹 멤버십을 맞춥니다. */
        void refreshObject( GameObject* pObj );
        /** @brief 그룹 목록에 넣거나 뺍니다(O(1), 오브젝트가 자기 자리를 듭니다). 넣을 때(이미 있으면 그 자리에) 칸 내용을 @p entry 로 씁니다. */
        void setMembership( GameObject* pObj, uint32 group, bool bMember, const TickObjectEntry& entry );

        vector<TickObjectEntry> _arrListEntry[kGroupCount]; ///< 그룹마다 틱할 것이 있고 켜진 오브젝트의 칸. 오브젝트를 소유하지 않습니다.
        vector<uint64>          _listDirtyObjectId;         ///< 다시 훑을 오브젝트 id. 사라졌으면 해석이 비어 건너뜁니다
        vector<uint64>          _listProcessingObjectId;    ///< `refresh` 가 위 목록과 바꿔 쓰는 버퍼(할당 재사용)
        mutex                   _dirtyMutex;                ///< `_listDirtyObjectId` 의 락. 워커에서 표시할 수 있습니다
        atomic<uint8>           _bAllDirty;                 ///< 전부 다시 훑을지 여부
        uint32                  _prerequisiteCount;         ///< 등록된 서브틱 선행 조건의 총수
        uint32                  _stageItemCount;            ///< 스테이지가 도는 항목 수
        uint32                  _stageBuildCount;           ///< 스테이지를 다시 지은 횟수(진단)
        uint64                  _stageGeneration;           ///< 선행 조건에 걸린 오브젝트가 바뀔 때마다 오릅니다
        uint64                  _builtStageGeneration;      ///< `_listStage` 를 지은 세대
        unordered_set<uint64>   _uniqueDependentObjectId;   ///< 선행 조건을 가진 항목이 있는 오브젝트
        unordered_set<uint64>   _uniqueReferencedObjectId;  ///< 지난 스테이지 빌드에서 선행 조건이 가리킨 오브젝트 — 바뀌면 다시 짓는다
        vector<TickStage>       _listStage;                 ///< 선행 조건을 가진 항목의 스테이지(그룹 순)
    };
} // namespace sw

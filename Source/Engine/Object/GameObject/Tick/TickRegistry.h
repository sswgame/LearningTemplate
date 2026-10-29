/**
 * @file TickRegistry.h
 * @brief 틱에 참여하는 오브젝트의 레지스트리입니다(언리얼의 `FTickTaskManager` 에 해당). 틱 멤버십이 바뀐 오브젝트만 다시 훑습니다.
 *
 * [왜 별도 클래스인가]
 * 틱 스테이지를 **씬 전체를 훑어** 만들면 컴포넌트 8000 개에 한 번 0.5~1 ms 가 듭니다. 게다가 틱하는 컴포넌트가 하나라도 생기거나 없어질
 * 때마다(총알처럼 스폰이 잦은 게임은 매 프레임) 그것을 통째로 다시 만들어야 합니다.
 *
 * 그래서 **오브젝트가 자기 틱 항목을 보관하고**(주 틱과 서브틱을 (그룹, 순서 키) 순으로), 레지스트리는 그룹마다 "틱할 것이 있는 오브젝트"
 * 목록을 가집니다. 멤버십이 바뀐 오브젝트만 표시되어 다음 틱 전에 자기 항목을 다시 만들고(컴포넌트 몇 개를 훑는 비용입니다), 디스패치는
 * 그룹마다 오브젝트 목록을 한 번의 포크-조인으로 나눕니다. 한 오브젝트의 항목은 한 워커가 순서대로 처리합니다(같은 오브젝트의
 * 컴포넌트 둘이 동시에 돌지 않는다는 규칙은 그대로입니다).
 *
 * **그룹 목록의 항목은 틱에 필요한 것을 직접 가집니다**(`TickObjectEntry`: 오브젝트, 첫 항목, 나머지 항목의 위치). 언리얼의
 * `FTickTaskManager` 가 그룹마다 틱 함수 포인터의 납작한 배열을 돌고, 유니티의 `BehaviourManager` 가 콜백마다 동작 컴포넌트의 납작한
 * 목록을 도는 것과 같은 구조입니다. 두 엔진 모두 매 틱 액터나 게임 오브젝트를 거치지 않고, 꺼진 것은 **목록에서 뺍니다.** 여기서도
 * 계층에서 꺼진 오브젝트는 목록에 없습니다(`GameObject::refreshActiveInHierarchy` 가 바뀔 때 표시합니다). 틱 중의 `setActive` 는
 * 틱 뒤로 미뤄지고 파괴는 컴포넌트마다 삭제 표시를 세우므로, 틱 도중에 목록이
 * 낡을 일은 컴포넌트의 표시로 다 가려집니다.
 *
 * **선행 조건(`addSubTickPrerequisite`)을 가진 항목만 스테이지로 갑니다.** 그런 항목은 오브젝트 항목 목록에서 그룹의 끝에 모이고 목록 항목은 그 앞까지만
 * 가리키므로, 나머지 항목과 선행 조건이 없는 오브젝트는 모두 기본 경로(오브젝트 단위 포크-조인)로 처리합니다. 매니저는 그룹마다 기본 경로를 먼저, 그 그룹의
 * 스테이지를 뒤에 실행합니다. 그래서 선행 조건이 기본 경로의 항목(다른 오브젝트의 주 틱이나 서브틱)이어도 순서가 맞습니다. 스테이지는 **이 레지스트리가
 * 만들고 보관합니다**(`rebuildStages`). 선행 조건을 가진 오브젝트와 그것이 가리키는 오브젝트가 바뀔 때만 다시 만듭니다. 체인 밖 오브젝트의 스폰과
 * 파괴는 스테이지를 건드리지 않습니다. 언리얼이 선행 조건 없는 틱 함수를 그룹 안에서 바로 실행하고 선행 조건이 있는 것만 기다리게 하는 것과 같습니다.
 *
 * **일괄 틱 타입(`TickBatchRegistry`)은 오브젝트 칸 대신 타입별 목록으로 갑니다.** 그룹에서 항목이 등록된 타입의 주 틱 하나뿐인 오브젝트는 칸 목록이 아니라
 * 그 타입의 (컴포넌트, 오브젝트) 연속 목록(`TickBatchList`)에 들고, 디스패치가 구간을 그 타입의 함수 하나에 넘깁니다(가상 호출 없음). 오브젝트는 그룹마다
 * 두 목록 중 한 곳에만 있고 `_arrTickIndex` 가 그 안의 위치입니다. 오브젝트 안의 순서와 "한 오브젝트는 한 스레드" 규칙은 그대로입니다.
 *
 * `PrimitiveRegistry`, `SceneTransformHierarchy` 와 같은 구조입니다. 기능은 별도 타입이 가지고, 매니저는 순서만 정합니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Defines.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    struct TickThreadState;

    class Component;
    class GameObject;
    class GameObjectManager;
    class TickBatchRegistry;

    /**
     * @struct TickBatchElement
     * @brief 일괄 틱 목록의 원소 하나입니다 — 틱할 컴포넌트와 그 오브젝트(틱 주인)입니다(`TickBatchRegistry`).
     */
    struct TickBatchElement
    {
        Component*  _pComponent{ nullptr };
        GameObject* _pObject{ nullptr };
    };

    /** @brief 같은 타입의 원소 [0, @p count) 를 차례로 틱하는 함수입니다. 원소마다 @p threadState 의 틱 주인을 그 오브젝트로 둡니다(`TickBatchOf`). */
    using TickBatchFunction = void ( * )( TickThreadState& threadState, const TickBatchElement* pElement, uint32 count, float32 deltaTime );
} // namespace sw

namespace sw
{
    /**
     * @struct TickItem
     * @brief 오브젝트가 낼 틱 하나입니다. 컴포넌트의 주 틱(`_subTickID` 0) 또는 서브틱이며, 오브젝트가 (그룹, 순서 키) 순으로 보관합니다.
     */
    struct TickItem
    {
        Component* _pComponent{ nullptr };
        uint32     _subTickID{ 0 };
        uint8      _group{ 0 };                   ///< `TickGroup`
        uint8      _orderKey{ 64 };               ///< `TickPhase` + 우선순위(0..63)
        uint8      _bHasPrerequisite{ SW_FALSE }; ///< 선행 조건을 가진 서브틱이다. 기본 경로가 아니라 스테이지에서 처리한다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 오브젝트 하나의 틱 항목입니다. **인라인 저장을 쓰지 않습니다.** 측정 결과 손해였습니다.
     * @details 인라인 슬롯 두 개(`InlineAllocator`)로 두면 첫 틱의 레지스트리 구축이 절반으로 줄고(8000 개의 힙 할당이 사라집니다) 틱 디스패치도
     *          오브젝트당 캐시 라인 하나가 줄어듭니다. 그런데 `GameObject` 가 192 바이트에서 232 바이트(3 라인에서 4 라인)가 되어 오브젝트를 만지는 다른
     *          프레임 경로(씬 수집의 활성 검사, 배치 쓰기의 핸들 해석)가 그만큼 느려집니다. 틱은 32 us 줄지만 수집과 배치는 30~60 us 늘어납니다
     *          (Release, 큐브 8000 개, 번갈아 2회 측정). 오브젝트가 세 라인에 머무는 쪽이 더 이득입니다.
     */
    using TickItemList = vector<TickItem>;

    /**
     * @struct TickStage
     * @brief 선행 조건 경로의 스테이지 하나입니다. 같은 오브젝트의 항목이 한 스테이지에 둘 이상 오지 않으며, 스테이지 안은 병렬로 처리합니다.
     * @details `_bApplyBefore` 가 켜져 있으면 매니저가 이 스테이지를 실행하기 **전에** 그때까지 쌓인 틱 중 트랜스폼 쓰기를 적용한다
     *          (`SceneTickScheduler`). 앞에서 실행된 선행 조건이 옮긴 위치를 이 스테이지가 같은 프레임에 읽게 하려는 것이다.
     *          기다리는 항목이 없는 스테이지(레벨 0, 같은 레벨을 오브젝트별로 나눈 둘째 이후)에는 켜지 않는다.
     */
    struct TickStage
    {
        vector<TickItem> _listItem;
        vector<uint64>   _listItemObjectID;         ///< `_listItem` 과 같은 순서로, 항목마다 그 오브젝트의 id 다(증분 갱신이 오브젝트의 항목을 찾는다)
        uint32           _level{ 0 };               ///< 이 스테이지가 속한 선행 조건 레벨이다(0 = 후보 사이에 기다릴 것이 없는 레벨, 순환의 남은 항목은 최댓값)
        uint8            _group{ 0 };               ///< 이 스테이지 항목의 그룹이다(한 스테이지는 한 그룹)
        uint8            _bApplyBefore{ SW_FALSE }; ///< 실행하기 전에 틱 중 트랜스폼 쓰기를 적용할지 나타낸다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TickObjectEntry
     * @brief 그룹 목록의 항목 하나입니다. 오브젝트 하나의 그 그룹 항목을 담으며, 디스패치는 이 항목과 컴포넌트만 읽습니다.
     * @details 첫 항목은 이 구조체 안에 복사해 둔다. 틱하는 오브젝트는 대개 그룹마다 항목이 하나라 항목 버퍼를 따라가지 않는다. 둘째부터는
     *          오브젝트의 항목 버퍼(`_pItem`, 첫 항목 포함)에서 읽는다. 그 버퍼는 레지스트리만 다시 만들고, 다시 만들 때 이 항목도 함께 고친다.
     */
    struct TickObjectEntry
    {
        GameObject*     _pObject{ nullptr }; ///< 이 항목의 오브젝트다. 틱 중에는 "지금 이 스레드가 틱하는 오브젝트"로 기록된다
        TickItem        _firstItem{};        ///< 이 그룹의 첫 항목(복사본)
        const TickItem* _pItem{ nullptr };   ///< 이 그룹 항목들의 시작(오브젝트의 항목 버퍼 안)
        uint32          _itemCount{ 0 };     ///< 이 그룹 항목 수(1 이상)
    };
} // namespace sw

namespace sw
{
    /**
     * @struct TickBatchList
     * @brief 한 그룹에서 같은 타입의 일괄 틱 원소 목록입니다(`TickBatchRegistry`). 디스패치는 구간 하나를 그 타입의 함수에 넘깁니다.
     */
    struct TickBatchList
    {
        TickBatchFunction        _pFunction{ nullptr }; ///< 이 목록 타입의 일괄 틱 함수(목록의 키)
        vector<TickBatchElement> _listElement;          ///< 그 타입의 주 틱 하나만 가진, 계층에서 켜진 오브젝트
    };
} // namespace sw

namespace sw
{
    /**
     * @class TickRegistry
     * @brief 그룹마다의 "틱할 것이 있는 오브젝트" 목록, 멤버십 더티 표시, 오브젝트 항목 재구축, 선행 조건 스테이지를 맡습니다.
     * @note 락 순서: 이 클래스의 락(`_dirtyMutex`)은 가장 안쪽입니다. `markObjectDirty` 는 워커에서 불려도 됩니다(원자 플래그와 짧은 잠금을 씁니다).
     */
    class SW_API TickRegistry
    {
    public:
        /** @brief `TickGroup` 의 수입니다. */
        static constexpr uint32 kGroupCount = 4;
        /** @brief 목록에 없다는 표시입니다. */
        static constexpr uint32 kNotInList = invalid_index::kUint32;

        /** @brief 빈 레지스트리를 만듭니다. 첫 `refresh` 가 씬 전체를 한 번 훑습니다. */
        TickRegistry();
        /** @brief 목록을 해제합니다. 오브젝트의 수명은 매니저가 관리합니다. */
        ~TickRegistry() = default;

        TickRegistry( const TickRegistry& )            = delete;
        TickRegistry& operator=( const TickRegistry& ) = delete;

        /**
         * @brief 오브젝트의 틱 멤버십이 바뀌었다고 표시합니다. 다음 `refresh` 가 그 항목을 다시 만듭니다. 아무 스레드에서나 불러도 됩니다.
         * @details 이미 표시된 오브젝트는 다시 올리지 않는다(원자 플래그). 오브젝트 id 를 보관하므로 그 사이에 사라져도 안전하다.
         */
        void markObjectDirty( GameObject* pObj );
        /** @brief 모든 오브젝트를 다시 훑게 합니다(타입 재바인딩, 씬 초기화). */
        void markAllDirty();

        /**
         * @brief 표시된 오브젝트의 항목을 다시 만들고 그룹 목록을 맞춥니다. 게임 스레드에서 틱 밖에 부릅니다.
         * @details 선행 조건에 걸린 오브젝트가 바뀌었으면 이어서 스테이지를 다시 만든다(`rebuildStages`).
         * @return 하나라도 다시 만들었으면 true 입니다.
         */
        bool refresh( GameObjectManager& manager );

        /** @brief 오브젝트를 레지스트리에서 완전히 제거합니다. 파괴 직전에 부릅니다. 여러 번 불러도 안전합니다. */
        void unregisterObject( GameObject* pObj );
        /** @brief 목록과 더티 표시를 비웁니다(매니저의 clear). */
        void clear();

        /** @brief 이 그룹에서 틱할 것이 있고 계층에서 켜진 오브젝트의 항목 목록입니다(일괄 틱으로 간 오브젝트는 빼고). 디스패치의 입력입니다. */
        const vector<TickObjectEntry>& getEntries( uint32 group ) const { return _arrListEntry[group]; }
        /**
         * @brief 이 그룹의 타입별 일괄 틱 목록입니다. 디스패치의 다른 입력입니다(오브젝트 칸과 한 포크-조인으로 돈다).
         * @details 그 그룹에서 틱 항목이 등록된 타입(`TickBatchRegistry`) 컴포넌트의 주 틱 하나뿐인 오브젝트가 여기에 온다. 빈 목록은 `refresh` 가 걷는다.
         */
        const vector<TickBatchList>& getBatchLists( uint32 group ) const { return _arrListBatch[group]; }
        /** @brief 선행 조건이 등록된 서브틱이 하나라도 있으면 true 입니다. */
        bool hasPrerequisites() const { return _prerequisiteCount > 0; }
        /**
         * @brief 선행 조건을 가진 항목의 스테이지 목록입니다(그룹 순). 매니저가 그룹마다 기본 경로 뒤에 그 그룹의 스테이지를 실행합니다.
         * @note `refresh` 가 끝난 뒤에만 맞습니다. `unregisterObject` 와 `refresh` 사이에는 파괴된 오브젝트의 항목이 남아 있을 수 있어(`getStageItemCount` 도 같다)
         *       틱 디스패치처럼 `refresh` 직후에만 읽습니다.
         */
        const vector<TickStage>& getStages() const { return _listStage; }
        /** @brief 스테이지가 처리하는 항목 수입니다(선행 조건을 가진 켜진 항목). 나머지는 모두 기본 경로입니다. */
        uint32 getStageItemCount() const { return _stageItemCount; }
        /** @brief 스테이지를 다시 만든 횟수(누적)입니다. 체인 밖 오브젝트의 스폰과 파괴로는 오르지 않습니다. 진단과 회귀 테스트용입니다. */
        uint32 getStageBuildCount() const { return _stageBuildCount; }
        /** @brief 스테이지를 다시 만들지 않고 말단 오브젝트의 항목만 끼우거나 뺀 횟수(누적)입니다. 진단과 회귀 테스트용입니다. */
        uint32 getStageDeltaCount() const { return _stageDeltaCount; }
        /** @brief 스테이지를 훑어 파괴 · 변경된 오브젝트의 항목을 걷어 낸 횟수(누적)입니다. 한 `refresh` 에 오브젝트가 몇 개 빠져도 한 번만 오른다. 진단과 회귀 테스트용입니다. */
        uint32 getStageCompactCount() const { return _stageCompactCount; }

    private:
        /** @brief 스테이지에서 한 오브젝트의 항목을 빼 달라는 대기 요청입니다. */
        struct StageRemoval
        {
            uint64 _objectID{ 0 };
            uint64 _order{ 0 }; ///< `_stageEditOrder` 순번
        };
        /** @brief 레벨 0 스테이지에 끼울 대기 항목입니다. 어느 스테이지인지는 (그룹, 오브젝트 안의 순번)으로 적용할 때 정합니다. */
        struct StageInsertion
        {
            TickItem _item{};
            uint64   _objectID{ 0 };
            uint64   _order{ 0 };                   ///< `_stageEditOrder` 순번
            uint32   _slot{ 0 };                    ///< 같은 오브젝트의 같은 그룹 안에서 몇 번째 항목인가(레벨 0 의 몇 번째 스테이지인가)
            uint8    _bMarkApplyBefore{ SW_FALSE }; ///< 이 그룹의 오브젝트 항목이 끝났고 기다리는 항목이 있었다 — 그룹 레벨 0 의 첫 스테이지에 적용 먼저를 켠다
        };

        /**
         * @brief 선행 조건을 가진 항목으로 스테이지 목록을 만듭니다. 그룹 순서대로 만들고, 스테이지 안은 병렬로 처리합니다.
         * @details 먼저 선행 조건이 뒤쪽 그룹에 있는 항목을 그 그룹으로 옮긴다(체인을 따라가며, 언리얼의 `ActualStartTickGroup` 에 해당한다). 기본 경로에서 도는
         *          선행 조건은 핸들의 오브젝트 id 로 그 오브젝트의 항목만 찾아 그룹을 본다. 그다음 그룹마다 후보 사이의 선행 조건 DAG 를 Kahn 알고리즘으로
         *          레벨별 스테이지로 나누고(같은 레벨은 순서 키와 등록 순서로 안정 정렬한다), 레벨 하나를 다시 **오브젝트별 스테이지**로 나눈다. 같은
         *          오브젝트의 항목이 한 스테이지에서 나란히 실행되지 않게 하려는 것이다. 기다리는 레벨의 첫 스테이지에 `TickStage::_bApplyBefore` 를 켠다.
         *          순환은 남은 항목을 순서 키 순으로 마지막 스테이지에 붙여 방어한다.
         */
        void rebuildStages( GameObjectManager& manager );
        /** @brief 오브젝트 하나의 항목을 컴포넌트에서 다시 만들고 그룹 멤버십을 맞춥니다. */
        void refreshObject( GameObjectManager& manager, GameObject* pObj );
        /**
         * @brief 아무도 기다리지 않는 오브젝트의 선행 조건 항목만 스테이지에 다시 끼웁니다(스폰 · 파괴가 잦은 팔로워가 매번 전체 재구축을 부르지 않게).
         * @details 말단의 조건은 새 항목의 선행 조건이 모두 의존 오브젝트 밖(보통 길)을 가리키는 것이다. 그러면 후보 사이의 간선이 없어 레벨 0 에 들고,
         *          뒤에 오는 항목의 순서는 달라지지 않는다. 오브젝트 id 로 이전 항목을 빼고(아무도 기다리지 않으니 늘 안전하다) 새 항목을 그룹의 레벨 0 스테이지에
         *          순서 키 자리로 넣는다. 말단이 아니면 false 를 돌려, 부른 쪽이 전체 재구축을 세운다.
         *          스테이지는 바로 고치지 않고 제거와 끼움을 요청으로 쌓는다(`flushStageEdits`가 `refresh` 끝에서 한 번에 적용한다).
         */
        [[nodiscard]] bool applyStageDelta( GameObjectManager& manager, GameObject* pObj );
        /**
         * @brief 스테이지에서 이 오브젝트의 항목을 모두 빼 달라고 기록만 합니다(O(1)). 실제로 빼는 일은 `flushStageEdits` 가 모아서 한 번에 합니다.
         * @details 오브젝트마다 스테이지를 훑어 지우면 K 개를 지우는 데 O(K · S)(S 는 스테이지의 항목 수)가 든다. 요청을 순번과 함께 쌓아 두고 스테이지를 한 번만 훑는다.
         */
        void queueStageRemoval( uint64 objectID );
        /**
         * @brief 쌓인 스테이지 편집(제거 요청, 끼움 요청)을 요청 순서대로 한 번에 적용합니다. 스테이지 안 항목의 순서는 하나씩 적용했을 때와 같습니다.
         * @details 제거는 스테이지마다 한 번 훑으며 요청된 오브젝트의 항목을 걷어 내고(남은 항목의 순서는 그대로), 그다음 끼움 요청을 쌓인 순서대로
         *          `insertIntoStage` 로 넣는다. 끼움 요청 뒤에 같은 오브젝트의 제거 요청이 또 있었으면(낡은 요청) 건너뛴다. `refresh` 가 끝에서 부르고
         *          `rebuildStages` 는 쌓인 편집을 버린다.
         */
        void flushStageEdits();
        /**
         * @brief 그룹 목록(오브젝트 칸 또는 타입별 일괄 목록)에 넣거나 뺍니다. 오브젝트가 자기 위치(`_arrTickIndex`)를 가지고 있어 O(1) 입니다(일괄 목록은 타입 수만큼 본다).
         * @details @p pBatchFunction 이 있으면 그 타입의 일괄 목록에, 없으면 오브젝트 칸 목록에 @p entry 로 넣는다. 이미 같은 목록에 있으면 그 자리에 쓰고,
         *          다른 목록에 있으면 거기서 빼고 옮긴다. @p bMember 가 false 면 어느 목록에서든 뺀다.
         */
        void setMembership( GameObject* pObj, uint32 group, bool bMember, const TickObjectEntry& entry, TickBatchFunction pBatchFunction );
        /** @brief 오브젝트가 이 그룹의 어느 일괄 목록에 있으면 그 목록입니다. 없으면 nullptr 입니다. */
        TickBatchList* findBatchMembership( const GameObject* pObj, uint32 group );
        /** @brief 이 그룹에서 @p pFunction 의 일괄 목록입니다. 없으면 만듭니다. */
        TickBatchList& getOrCreateBatchList( uint32 group, TickBatchFunction pFunction );
        /** @brief 빈 일괄 목록을 걷습니다(모듈이 내려간 타입의 함수를 들고 있지 않게). */
        void removeEmptyBatchLists();
        /** @brief 오브젝트의 이 그룹 항목이 일괄 틱 대상이면 그 함수입니다(정확한 타입의 주 틱 하나). 아니면 nullptr 입니다. */
        static TickBatchFunction findBatchFunction( TickBatchRegistry* pBatchRegistry, const TickItem* pItem, uint32 itemCount );

        vector<TickObjectEntry>           _arrListEntry[kGroupCount];   ///< 그룹마다 틱할 것이 있고 켜진 오브젝트의 항목이다. 오브젝트를 소유하지 않는다.
        vector<TickBatchList>             _arrListBatch[kGroupCount];   ///< 그룹마다 타입별 일괄 틱 목록이다. 오브젝트는 칸 목록과 이 목록 중 한 곳에만 있다
        uint32                            _batchGeneration;             ///< 마지막으로 본 `TickBatchRegistry::getGeneration`(표가 없으면 0) — 바뀌면 모두 다시 훑는다
        vector<uint64> _listDirtyObjectID SW_GUARDED_BY( _dirtyMutex ); ///< 다시 훑을 오브젝트 id 다. 사라졌으면 해석 결과가 비어 건너뛴다
        vector<uint64>                    _listProcessingObjectID;      ///< `refresh` 가 위 목록과 바꿔 쓰는 버퍼(할당 재사용)
        mutex                             _dirtyMutex;                  ///< `_listDirtyObjectID` 의 잠금이다. 워커에서 표시할 수 있다
        atomic<uint8>                     _bAllDirty;                   ///< 전부 다시 훑을지 여부
        uint32                            _prerequisiteCount;           ///< 등록된 서브틱 선행 조건의 총수
        uint32                            _stageItemCount;              ///< 스테이지가 도는 항목 수
        uint32                            _stageBuildCount;             ///< 스테이지를 다시 만든 횟수(진단)
        uint32                            _stageDeltaCount;             ///< 말단 오브젝트의 항목만 끼우거나 뺀 횟수(진단)
        uint32                            _stageCompactCount;           ///< 스테이지를 훑어 항목을 걷어 낸 횟수(진단) — 제거 요청 수가 아니라 `flushStageEdits` 횟수다
        uint64                            _stageEditOrder;              ///< 스테이지 편집 요청마다 오르는 순번이다(제거와 끼움의 선후를 가린다)
        vector<StageRemoval>              _listStageRemoval;            ///< 스테이지에서 빼 달라는 대기 요청이다. `flushStageEdits` 가 적용한다
        vector<StageInsertion>            _listStageInsertion;          ///< 레벨 0 스테이지에 끼울 대기 항목이다(요청 순서)
        uint64                            _stageGeneration;             ///< 선행 조건에 걸린 오브젝트가 바뀔 때마다 오른다
        uint64                            _builtStageGeneration;        ///< `_listStage` 를 만든 세대
        vector<uint64>                    _listDependentObjectID;       ///< 선행 조건을 가진 항목이 있는 오브젝트 id(정렬됨 — 스테이지가 늘 같은 순서로 지어진다)
        vector<uint64>                    _listReferencedObjectID;      ///< 지난 스테이지 빌드에서 선행 조건이 가리킨 오브젝트 id(정렬됨, 중복 없음)다. 바뀌면 다시 만든다
        vector<TickStage>                 _listStage;                   ///< 선행 조건을 가진 항목의 스테이지(그룹 순)
    };
} // namespace sw

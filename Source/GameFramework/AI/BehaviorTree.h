/**
 * @file BehaviorTree.h
 * @brief 행동 트리 — 정의(공유)와 실행기(행위자마다)를 나눈 언리얼식 트리입니다. 셀렉터 · 시퀀스 · 데코레이터(반전 · 성공 강제 · 반복 · 쿨다운 ·
 *        시간 제한 · 블랙보드 조건과 관찰 중단) · 작업 · 조건 · 대기.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Blackboard;

    /** @brief 노드가 돌려주는 상태입니다. */
    enum class BehaviorStatus : uint8
    {
        Success = 0,
        Failure,
        Running
    };

    /** @brief 노드 종류입니다. */
    enum class BehaviorNodeType : uint8
    {
        Selector = 0,        ///< 자식을 차례로 — 하나가 성공하면 성공
        Sequence,            ///< 자식을 차례로 — 하나가 실패하면 실패
        Inverter,            ///< 자식의 성공 · 실패를 뒤집는다
        ForceSuccess,        ///< 끝나면 늘 성공
        Repeat,              ///< 자식을 `_count` 번(0 이면 실패할 때까지) — 한 틱에 한 번
        Cooldown,            ///< 끝난 뒤 `_seconds` 동안은 실패
        TimeLimit,           ///< `_seconds` 안에 안 끝나면 중단하고 실패
        BlackboardCondition, ///< 블랙보드 값이 조건에 맞을 때만 자식을 돌린다
        Action,              ///< 작업 함수
        Condition,           ///< 조건 함수(성공 · 실패)
        Wait                 ///< `_seconds` 기다린다
    };

    /** @brief 블랙보드 조건의 비교입니다. */
    enum class BlackboardCompare : uint8
    {
        IsSet = 0,
        IsNotSet,
        Equal,
        NotEqual,
        Less,
        LessOrEqual,
        Greater,
        GreaterOrEqual
    };

    /**
     * @brief 블랙보드 조건이 결과가 바뀔 때 무엇을 멈추는가입니다(언리얼 Observer Aborts).
     * @details `Self` — 이 가지가 도는 중에 조건이 거짓이 되면 가지를 멈춘다. `LowerPriority` — 이 가지보다 뒤(우선순위가 낮은) 가지가 도는 중에 조건이
     *          참이 되면 그 가지를 멈추고 처음부터 다시 고른다. 노드는 우선순위 순서(깊이 우선)로 더한다 — `add*` 를 부르는 순서가 곧 그 순서다.
     */
    enum class BehaviorAbortMode : uint8
    {
        None = 0,
        Self,
        LowerPriority,
        Both
    };

    struct BehaviorContext;

    /** @brief 작업 · 조건 함수입니다. 작업은 끝날 때까지 `Running` 을 돌려주고, 중단되면 `_bAborted` 로 한 번 더 불려 뒷정리합니다. */
    using BehaviorTaskFunction = BehaviorStatus ( * )( BehaviorContext& context );

    /** @brief 작업 함수가 받는 것입니다. */
    struct BehaviorContext
    {
        Blackboard* _pBlackboard{ nullptr };
        void*       _pOwner{ nullptr }; ///< 행위자(게임이 넘긴 것)
        float32     _deltaTime{ 0.0f };
        int32       _nodeIndex{ -1 };
        uint8       _bJustStarted{ SW_FALSE }; ///< 이 작업이 이번 틱에 시작했다(목적지 정하기 등 처음 한 번)
        uint8       _bAborted{ SW_FALSE };     ///< 중단 — 돌려준 값은 쓰지 않는다
    };

    /** @brief 트리의 노드 하나(정의 — 상태 없음)입니다. */
    struct BehaviorNode
    {
        string               _name{};
        hashed_string        _key{}; ///< 블랙보드 조건의 이름
        BehaviorTaskFunction _function{ nullptr };
        float32              _seconds{ 0.0f };
        float32              _compareValue{ 0.0f };
        int32                _parent{ -1 };
        int32                _firstChild{ -1 };
        int32                _nextSibling{ -1 };
        int32                _count{ 0 };
        BehaviorNodeType     _type{ BehaviorNodeType::Sequence };
        BlackboardCompare    _compare{ BlackboardCompare::IsSet };
        BehaviorAbortMode    _abortMode{ BehaviorAbortMode::None };
        uint8                _bReactive{ SW_FALSE }; ///< 셀렉터 — 매 틱 앞 자식부터 다시 본다
    };

    /**
     * @class BehaviorTree
     * @brief 트리 정의입니다. 여러 행위자가 같은 트리를 나눠 쓰고 상태는 `BehaviorTreeRunner` 가 행위자마다 듭니다(언리얼 BT 에셋 · 인스턴스와 같다).
     * @details 부모 −1 로 더한 첫 노드가 뿌리입니다. 함수 포인터는 게임 모듈의 것이니 핫 리로드 뒤에는 트리를 다시 만듭니다.
     * @code
     *     const int32 root  = tree.addSelector( -1, "Root" );
     *     const int32 fight = tree.addBlackboardCondition( root, "Target", BlackboardCompare::IsSet, 0.0f, BehaviorAbortMode::Both );
     *     tree.addAction( fight, "Attack", &attackTarget );
     *     tree.addAction( root, "Wander", &wander );
     * @endcode
     */
    class SW_GF_API BehaviorTree
    {
    public:
        BehaviorTree();

        int32 addSelector( int32 parent, const utf8* pName = "Selector", bool bReactive = false );
        int32 addSequence( int32 parent, const utf8* pName = "Sequence" );
        int32 addInverter( int32 parent );
        int32 addForceSuccess( int32 parent );
        /** @brief @p count 번 되풀이(0 = 자식이 실패할 때까지)입니다. */
        int32 addRepeat( int32 parent, int32 count );
        int32 addCooldown( int32 parent, float32 seconds );
        int32 addTimeLimit( int32 parent, float32 seconds );
        int32 addBlackboardCondition( int32 parent, const hashed_string& key, BlackboardCompare compare, float32 value = 0.0f,
                                      BehaviorAbortMode abortMode = BehaviorAbortMode::None );
        int32 addAction( int32 parent, const utf8* pName, BehaviorTaskFunction function );
        int32 addCondition( int32 parent, const utf8* pName, BehaviorTaskFunction function );
        int32 addWait( int32 parent, float32 seconds );

        /** @brief 뿌리가 있고, 데코레이터는 자식이 꼭 하나, 컴포짓은 하나 이상, 작업 · 조건은 함수가 있으면 true 입니다. */
        bool                isValid() const;
        const BehaviorNode& getNode( int32 index ) const { return _listNode[static_cast<size_t>( index )]; }
        int32               getNodeCount() const { return static_cast<int32>( _listNode.size() ); }
        bool                isDecorator( int32 index ) const;
        /** @brief 블랙보드 조건이 참인지 봅니다. */
        static bool evaluateCondition( const BehaviorNode& node, const Blackboard& blackboard );

    private:
        int32 addNode( int32 parent, BehaviorNodeType type, const utf8* pName );

        vector<BehaviorNode> _listNode;
    };

    /**
     * @class BehaviorTreeRunner
     * @brief 행위자 하나의 트리 실행 상태입니다. 매 틱 뿌리부터 내려가되 도는 중인(Running) 가지는 그 자리에서 이어갑니다.
     * @details 뿌리가 끝나면(성공 · 실패) 다음 틱에 처음부터 다시 돕니다. `getActiveLeafName` 은 디버그 표시(언리얼 BT 디버거의 깜빡이는 노드)에 씁니다.
     */
    class SW_GF_API BehaviorTreeRunner
    {
    public:
        BehaviorTreeRunner();

        void initialize( const BehaviorTree* pTree );
        /** @brief 한 틱 돌립니다. 뿌리의 상태입니다. */
        BehaviorStatus tick( Blackboard& blackboard, void* pOwner, float32 deltaTime );
        /** @brief 도는 작업을 모두 중단하고 처음 상태로 돌립니다. */
        void reset();

        int32       getActiveLeaf() const { return _activeLeaf; }
        const utf8* getActiveLeafName() const;
        bool        isNodeActive( int32 index ) const { return index >= 0 && index < static_cast<int32>( _listActive.size() ) && _listActive[static_cast<size_t>( index )] != SW_FALSE; }
        float32     getElapsedTime() const { return _elapsedTime; }

    private:
        BehaviorStatus tickNode( int32 index );
        /** @brief 노드를 끝난 상태로 돌립니다(커서 · 타이머 · 횟수, 쿨다운은 남긴다). */
        void finishNode( int32 index );
        /** @brief 도는 노드와 그 아래를 중단합니다 — 작업은 `_bAborted` 로 한 번 불린다. */
        void abortNode( int32 index );
        /** @brief 우선순위가 높은 가지의 조건이 거짓 → 참이 됐으면 true 입니다(관찰 중단 LowerPriority). */
        bool hasHigherPriorityTrigger();

        const BehaviorTree* _pTree;
        BehaviorContext     _context;
        vector<int32>       _listCursor;
        vector<float32>     _listTimer;
        vector<float32>     _listCooldownUntil;
        vector<int32>       _listCounter;
        vector<uint8>       _listActive;
        vector<uint8>       _listLastCondition; ///< 관찰 중단 조건의 지난 틱 결과
        float32             _elapsedTime;
        int32               _activeLeaf;
    };
} // namespace sw

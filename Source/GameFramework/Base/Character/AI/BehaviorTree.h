/**
 * @file BehaviorTree.h
 * @brief 행동 트리를 정의합니다.
 * @details 정의(여러 행위자가 공유)와 실행기(행위자마다)를 나눈 언리얼식 트리이다.
 *          노드는 셀렉터, 시퀀스, 데코레이터, 작업, 조건, 대기, 이동이 있다. 이동은 격자와 내비메시가 같이 쓰는 `INavMover` 로 한다.
 *          데코레이터는 반전, 성공 강제, 반복, 쿨다운, 시간 제한, 블랙보드 조건(관찰 중단 포함)을 지원한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Blackboard;

    /** @brief 노드가 반환하는 상태입니다. */
    enum class BehaviorStatus : uint8
    {
        Success = 0,
        Failure,
        Running
    };

    /** @brief 노드 종류입니다. */
    enum class BehaviorNodeType : uint8
    {
        Selector = 0,        ///< 자식을 차례로 돌린다. 하나가 성공하면 성공
        Sequence,            ///< 자식을 차례로 돌린다. 하나가 실패하면 실패
        Inverter,            ///< 자식의 성공과 실패를 뒤집는다
        ForceSuccess,        ///< 끝나면 늘 성공
        Repeat,              ///< 자식을 `_count` 번 돌린다(0 이면 실패할 때까지). 한 틱에 한 번만 돌린다
        Cooldown,            ///< 끝난 뒤 `_seconds` 동안은 실패
        TimeLimit,           ///< `_seconds` 안에 안 끝나면 중단하고 실패
        BlackboardCondition, ///< 블랙보드 값이 조건에 맞을 때만 자식을 돌린다
        Action,              ///< 작업 함수
        Condition,           ///< 조건 함수(성공 또는 실패)
        Wait,                ///< `_seconds` 기다린다
        MoveTo               ///< 블랙보드 벡터 `_key` 로 러너의 `INavMover` 를 보낸다. 닿으면 성공, 경로가 없으면 실패
    };

    /** @brief 블랙보드 조건의 비교 방식입니다. */
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
     * @brief 블랙보드 조건의 결과가 바뀔 때 무엇을 멈출지 정합니다. 언리얼의 Observer Aborts 에 해당합니다.
     * @details `Self` 는 이 가지가 도는 중에 조건이 거짓이 되면 가지를 멈춘다.
     *          `LowerPriority` 는 이 가지보다 뒤(우선순위가 낮은) 가지가 도는 중에 조건이 참이 되면 그 가지를 멈추고 처음부터 다시 고른다.
     *          노드는 우선순위 순서(깊이 우선)로 더하므로, `add*` 를 부르는 순서가 곧 우선순위이다.
     */
    enum class BehaviorAbortMode : uint8
    {
        None = 0,
        Self,
        LowerPriority,
        Both
    };

    struct BehaviorContext;

    class INavMover;

    /** @brief 작업과 조건 함수입니다. 작업은 끝날 때까지 `Running` 을 반환하고, 중단되면 `_bAborted` 로 한 번 더 불려 뒷정리합니다. */
    using BehaviorTaskFunction = BehaviorStatus ( * )( BehaviorContext& context );

    /** @brief 작업 함수가 받는 인자입니다. */
    struct BehaviorContext
    {
        Blackboard* _pBlackboard{ nullptr };
        void*       _pOwner{ nullptr }; ///< 게임이 넘긴 행위자
        INavMover*  _pMover{ nullptr }; ///< 행위자의 이동 인터페이스(`BehaviorTreeRunner::setMover`). 없으면 이동 노드가 실패한다
        float32     _deltaTime{ 0.0f };
        int32       _nodeIndex{ -1 };
        uint8       _bJustStarted{ SW_FALSE }; ///< 이 작업이 이번 틱에 시작했다. 목적지 정하기처럼 처음 한 번 할 일에 쓴다
        uint8       _bAborted{ SW_FALSE };     ///< 중단되어 불렸다. 돌려준 값은 쓰지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 트리의 노드 하나입니다. 정의일 뿐이며 상태는 없습니다. */
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
        uint8                _bReactive{ SW_FALSE }; ///< 셀렉터가 매 틱 앞 자식부터 다시 본다
    };
} // namespace sw

namespace sw
{
    /**
     * @class BehaviorTree
     * @brief 트리 정의입니다. 여러 행위자가 같은 트리를 나눠 쓰고, 상태는 `BehaviorTreeRunner` 가 행위자마다 보관합니다.
     * @details 언리얼의 BT 에셋과 인스턴스의 관계와 같다.
     *          부모를 -1 로 더한 첫 노드가 루트이다.
     *          함수 포인터는 게임 모듈의 것이므로 핫 리로드 뒤에는 트리를 다시 만들어야 한다.
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
        /** @brief 자식을 @p count 번 되풀이합니다. 0 이면 자식이 실패할 때까지 되풀이합니다. */
        int32 addRepeat( int32 parent, int32 count );
        int32 addCooldown( int32 parent, float32 seconds );
        int32 addTimeLimit( int32 parent, float32 seconds );
        int32 addBlackboardCondition( int32 parent, const hashed_string& key, BlackboardCompare compare, float32 value = 0.0f,
                                      BehaviorAbortMode abortMode = BehaviorAbortMode::None );
        int32 addAction( int32 parent, const utf8* pName, BehaviorTaskFunction function );
        int32 addCondition( int32 parent, const utf8* pName, BehaviorTaskFunction function );
        int32 addWait( int32 parent, float32 seconds );
        /**
         * @brief 블랙보드 벡터 @p destinationKey 로 가는 이동 노드를 더합니다. 언리얼 `BTTask_MoveTo`(블랙보드 키 + 관찰)에 해당합니다.
         * @details 러너의 `INavMover`(내비메시 에이전트든 격자 행위자든)에 목적지를 요청한다. 닿으면 성공, 경로가 없거나 멈추면 실패이고, 가는 동안은 Running 이다.
         *          도는 동안 블랙보드 값이 @p repathDistance 미터보다 멀리 바뀌면 새 목적지를 다시 요청한다(쫓기). 중단되면 이동을 멈춘다.
         *          키가 비었거나 러너에 이동 인터페이스가 없으면 실패한다.
         */
        int32 addMoveTo( int32 parent, const hashed_string& destinationKey, float32 repathDistance = 0.5f );

        /** @brief 트리가 올바르면 true 입니다. 루트가 있고, 데코레이터는 자식이 꼭 하나, 컴포짓은 하나 이상, 작업과 조건은 함수가 있어야 합니다. */
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
} // namespace sw

namespace sw
{
    /**
     * @class BehaviorTreeRunner
     * @brief 행위자 하나의 트리 실행 상태입니다. 매 틱 루트부터 내려가되, 도는 중인(Running) 가지는 멈춘 곳에서 이어갑니다.
     * @details 루트가 끝나면(성공이든 실패든) 다음 틱에 처음부터 다시 돈다.
     *          `getActiveLeafName` 은 디버그 표시와 자동화 감시 값에 쓴다. 언리얼 BT 디버거에서 깜빡이는 노드와 같다.
     */
    class SW_GF_API BehaviorTreeRunner
    {
    public:
        BehaviorTreeRunner();

        void initialize( const BehaviorTree* pTree );
        /** @brief 이동 노드(`addMoveTo`)가 쓸 행위자의 이동 인터페이스입니다. 행위자보다 먼저 사라지면 nullptr 로 바꿉니다. */
        void setMover( INavMover* pMover ) { _context._pMover = pMover; }
        /** @brief 한 틱 돌리고 루트의 상태를 반환합니다. */
        BehaviorStatus tick( Blackboard& blackboard, void* pOwner, float32 deltaTime );
        /** @brief 도는 작업을 모두 중단하고 처음 상태로 돌립니다. */
        void reset();

        /** @brief 도는 중(Running)인 잎 노드입니다. 없으면 -1 입니다. */
        int32 getActiveLeaf() const { return _activeLeaf; }
        /**
         * @brief 지난 틱이 머문 잎 노드의 이름입니다. 도는 잎이 있으면 그것, 없으면 그 틱에 마지막으로 성공한 작업입니다. 둘 다 없으면 빈 문자열입니다.
         * @details 한 틱에 끝나는 작업만 있는 트리(경제 AI 처럼 작업마다 성공 강제)는 도는 잎이 늘 없다. 그때 "이번 틱에 실제로 한 일" 을 보여 준다.
         */
        const utf8* getActiveLeafName() const;
        bool        isNodeActive( int32 index ) const { return index >= 0 && index < static_cast<int32>( _listActive.size() ) && _listActive[static_cast<size_t>( index )] != SW_FALSE; }
        float32     getElapsedTime() const { return _elapsedTime; }

    private:
        /** @brief 노드 하나를 한 틱 돌립니다. 노드 종류마다 짧은 분기이고, 자식을 여럿 도는 합성 노드는 아래 함수로 나눴습니다. */
        BehaviorStatus tickNode( int32 index );
        /** @brief 시퀀스 한 틱입니다. @p inoutCursor 번째 자식부터 성공하는 동안 다음 자식으로 갑니다. */
        BehaviorStatus tickSequence( const BehaviorNode& node, int32& inoutCursor );
        /**
         * @brief 셀렉터 한 틱입니다. 실패하지 않은 첫 자식의 상태를 돌려줍니다.
         * @details 반응형이면 매 틱 앞 자식부터 봅니다. 앞 자식이 실패하지 않으면 돌던 뒤 자식을 중단하고 그쪽으로 커서를 옮깁니다.
         */
        BehaviorStatus tickSelector( const BehaviorNode& node, int32& inoutCursor );
        /** @brief @p node 의 @p order 번째 자식 노드 번호입니다. 자식이 모자라면 -1 입니다. */
        int32 findChild( const BehaviorNode& node, int32 order ) const;
        /** @brief 이동 노드 한 틱입니다(목적지를 요청하거나 다시 요청하고, 이동 인터페이스의 상태를 노드 상태로 옮긴다). */
        BehaviorStatus tickMoveTo( int32 index, bool bFirst );
        /** @brief 노드를 끝난 상태로 돌립니다. 커서, 타이머, 횟수를 비우되 쿨다운은 남깁니다. */
        void finishNode( int32 index );
        /** @brief 도는 노드와 그 아래를 중단합니다. 작업은 `_bAborted` 로 한 번 불립니다. */
        void abortNode( int32 index );
        /** @brief 우선순위가 높은 가지의 조건이 거짓에서 참이 됐으면 true 입니다. 관찰 중단 `LowerPriority` 가 씁니다. */
        bool pollHigherPriorityTrigger();

        const BehaviorTree* _pTree;
        BehaviorContext     _context;
        vector<int32>       _listCursor;
        vector<float32>     _listTimer;
        vector<float32>     _listCooldownUntil;
        vector<int32>       _listCounter;
        vector<uint8>       _listActive;
        vector<uint8>       _listLastCondition; ///< 관찰 중단 조건의 지난 틱 결과
        vector<float3>      _listMoveTarget;    ///< 이동 노드가 마지막으로 요청한 목적지
        float32             _elapsedTime;
        int32               _activeLeaf;
        int32               _lastSucceededLeaf; ///< 이번 틱에 마지막으로 성공한 작업. 도는 잎이 없을 때 `getActiveLeafName` 이 쓴다
    };
} // namespace sw

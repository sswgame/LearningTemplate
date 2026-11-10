#include "pch.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTree.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Navigation/INavMover.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeNodeParams.h"
#include "GameFramework/Runtime/Character/AI/Blackboard.h"
#include "GameFramework/Runtime/Gameplay/Condition/GameConditions.h"

#include <limits>

namespace sw
{
    namespace
    {
        struct BehaviorTreeInternal
        {
            /** @brief 값이 없으면 NaN 이라 어떤 비교도 참이 아니다 — 없는 키를 0 으로 보지 않는다(있는지는 `hasBlackboard` 로 묻는다). */
            static float64 readBlackboard( const GameplayContext& context, const ConditionNameArg& args )
            {
                const Blackboard*       pBlackboard = context.find<BlackboardSlot>();
                const optional<float32> value       = pBlackboard != nullptr ? pBlackboard->getFloat( args._name ) : nullopt;
                return value.has_value() ? static_cast<float64>( *value ) : std::numeric_limits<float64>::quiet_NaN();
            }

            static bool hasBlackboard( const GameplayContext& context, const ConditionNameArg& args )
            {
                const Blackboard* pBlackboard = context.find<BlackboardSlot>();
                return pBlackboard != nullptr && pBlackboard->isSet( args._name );
            }

            static ActionResult runSetBlackboard( ActionContext& context, const BlackboardSetParams& params )
            {
                Blackboard* pBlackboard = context._world.find<BlackboardWriteSlot>();
                if ( pBlackboard == nullptr )
                    return ActionResult::Failed;
                pBlackboard->setFloat( params._key, params._value );
                return ActionResult::Succeeded;
            }

            static ActionResult runClearBlackboard( ActionContext& context, const BlackboardClearParams& params )
            {
                Blackboard* pBlackboard = context._world.find<BlackboardWriteSlot>();
                if ( pBlackboard == nullptr )
                    return ActionResult::Failed;
                pBlackboard->clearValue( params._key );
                return ActionResult::Succeeded;
            }

            static ActionResult runAddBlackboard( ActionContext& context, const BlackboardAddParams& params )
            {
                Blackboard*             pBlackboard = context._world.find<BlackboardWriteSlot>();
                const optional<float32> current     = pBlackboard != nullptr ? pBlackboard->getFloat( params._key ) : nullopt;
                if ( current.has_value() == false )
                    return ActionResult::Failed;
                pBlackboard->setFloat( params._key, *current + params._value );
                return ActionResult::Succeeded;
            }

            static ActionResult runCopyBlackboard( ActionContext& context, const BlackboardCopyParams& params )
            {
                Blackboard* pBlackboard = context._world.find<BlackboardWriteSlot>();
                return pBlackboard != nullptr && pBlackboard->copyValue( params._from, params._to ) ? ActionResult::Succeeded : ActionResult::Failed;
            }

            static bool abortsSelf( BehaviorAbortMode mode ) { return mode == BehaviorAbortMode::Self || mode == BehaviorAbortMode::Both; }
            static bool abortsLowerPriority( BehaviorAbortMode mode ) { return mode == BehaviorAbortMode::LowerPriority || mode == BehaviorAbortMode::Both; }
        };
    } // namespace

    SW_CONDITION( blackboard, ConditionNameArg, &BehaviorTreeInternal::readBlackboard );
    SW_CONDITION( hasBlackboard, ConditionNameArg, &BehaviorTreeInternal::hasBlackboard );
    SW_GAME_ACTION( BlackboardSet, "blackboard.set", BlackboardSetParams, &BehaviorTreeInternal::runSetBlackboard );
    SW_GAME_ACTION( BlackboardClear, "blackboard.clear", BlackboardClearParams, &BehaviorTreeInternal::runClearBlackboard );
    SW_GAME_ACTION( BlackboardAdd, "blackboard.add", BlackboardAddParams, &BehaviorTreeInternal::runAddBlackboard );
    SW_GAME_ACTION( BlackboardCopy, "blackboard.copy", BlackboardCopyParams, &BehaviorTreeInternal::runCopyBlackboard );
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // BehaviorTree
    // ------------------------------------------------------------------------------
    BehaviorTree::BehaviorTree()
        : _listNode{}
    {
    }

    expected<int32> BehaviorTree::addNode( optional<int32> parent, BehaviorNodeKind type, const utf8* pName )
    {
        if ( parent.has_value() == false && _listNode.empty() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "a behavior tree has one root" } );
        if ( parent.has_value() && ( *parent < 0 || *parent >= static_cast<int32>( _listNode.size() ) ) )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the parent node does not exist" } );
        const int32  index = static_cast<int32>( _listNode.size() );
        BehaviorNode node;
        node._type   = type;
        node._name   = pName != nullptr ? pName : "";
        node._parent = parent;
        _listNode.push_back( node );
        if ( parent.has_value() )
        {
            // 형제 목록 끝에 붙인다 — 더한 순서가 우선순위다.
            BehaviorNode& parentNode = _listNode[static_cast<size_t>( *parent )];
            if ( parentNode._firstChild.has_value() == false )
                parentNode._firstChild = index;
            else
            {
                int32 sibling = *parentNode._firstChild;
                while ( _listNode[static_cast<size_t>( sibling )]._nextSibling.has_value() )
                {
                    sibling = *_listNode[static_cast<size_t>( sibling )]._nextSibling;
                }
                _listNode[static_cast<size_t>( sibling )]._nextSibling = index;
            }
        }
        return index;
    }

    expected<int32> BehaviorTree::addSelector( optional<int32> parent, const utf8* pName, bool bReactive )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Selector, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._bReactive = bReactive ? SW_TRUE : SW_FALSE;
        return index;
    }

    expected<int32> BehaviorTree::addSequence( optional<int32> parent, const utf8* pName )
    {
        return addNode( parent, BehaviorNodeKind::Sequence, pName );
    }

    expected<int32> BehaviorTree::addInverter( optional<int32> parent )
    {
        return addNode( parent, BehaviorNodeKind::Inverter, "Inverter" );
    }

    expected<int32> BehaviorTree::addForceSuccess( optional<int32> parent )
    {
        return addNode( parent, BehaviorNodeKind::ForceSuccess, "ForceSuccess" );
    }

    expected<int32> BehaviorTree::addRepeat( optional<int32> parent, int32 count )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Repeat, "Repeat" );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._count = MathUtil::max( 0, count );
        return index;
    }

    expected<int32> BehaviorTree::addCooldown( optional<int32> parent, float32 seconds )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Cooldown, "Cooldown" );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    expected<int32> BehaviorTree::addTimeLimit( optional<int32> parent, float32 seconds )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::TimeLimit, "TimeLimit" );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    expected<int32> BehaviorTree::addBlackboardCondition( optional<int32> parent, const hashed_string& key, BlackboardCompare compare, float32 value, BehaviorAbortMode abortMode )
    {
        // 코드로 짓는 트리도 같은 식이다. 있는지 묻기와 값 비교는 다른 조건이다(없는 키는 어떤 비교도 참이 아니다).
        const bool bPresence = compare == BlackboardCompare::IsSet || compare == BlackboardCompare::IsNotSet;
        string     text      = bPresence ? "hasBlackboard(" : "blackboard(";
        text += key.c_str();
        text += ")";
        if ( compare == BlackboardCompare::IsNotSet )
            text = "!" + text;
        ConditionExpr condition;
        if ( ConditionExpr::compile( text, "BehaviorTree", condition ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the blackboard condition does not compile" } );
        switch ( compare )
        {
            case BlackboardCompare::IsSet:
            case BlackboardCompare::IsNotSet:
                break;
            case BlackboardCompare::Equal:
                condition.appendCompare( ConditionOpCode::Equal, value );
                break;
            case BlackboardCompare::NotEqual:
                condition.appendCompare( ConditionOpCode::NotEqual, value );
                break;
            case BlackboardCompare::Less:
                condition.appendCompare( ConditionOpCode::Less, value );
                break;
            case BlackboardCompare::LessOrEqual:
                condition.appendCompare( ConditionOpCode::LessEqual, value );
                break;
            case BlackboardCompare::Greater:
                condition.appendCompare( ConditionOpCode::Greater, value );
                break;
            case BlackboardCompare::GreaterOrEqual:
                condition.appendCompare( ConditionOpCode::GreaterEqual, value );
                break;
        }
        return addConditionDecorator( parent, key.c_str(), condition, abortMode );
    }

    expected<int32> BehaviorTree::addConditionDecorator( optional<int32> parent, const utf8* pName, const ConditionExpr& condition, BehaviorAbortMode abortMode )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::BlackboardCondition, pName );
        if ( index.has_value() == false )
            return index;
        BehaviorNode& node = _listNode[static_cast<size_t>( *index )];
        node._condition    = condition;
        node._abortMode    = abortMode;
        return index;
    }

    expected<int32> BehaviorTree::addAction( optional<int32> parent, const utf8* pName, const GameActionCall& action )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Action, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._action = action;
        return index;
    }

    expected<int32> BehaviorTree::addCondition( optional<int32> parent, const utf8* pName, const ConditionExpr& condition )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Condition, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._condition = condition;
        return index;
    }

    expected<int32> BehaviorTree::addSubTreeSlot( optional<int32> parent, const utf8* pName, const hashed_string& slot )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::SubTree, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._key = slot;
        return index;
    }

    expected<int32> BehaviorTree::addAction( optional<int32> parent, const utf8* pName, BehaviorTaskFunction function )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Action, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._function = function;
        return index;
    }

    expected<int32> BehaviorTree::addCondition( optional<int32> parent, const utf8* pName, BehaviorTaskFunction function )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Condition, pName );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._function = function;
        return index;
    }

    expected<int32> BehaviorTree::addWait( optional<int32> parent, float32 seconds )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::Wait, "Wait" );
        if ( index.has_value() )
            _listNode[static_cast<size_t>( *index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    expected<int32> BehaviorTree::addMoveTo( optional<int32> parent, const hashed_string& destinationKey, float32 repathDistance )
    {
        const expected<int32> index = addNode( parent, BehaviorNodeKind::MoveTo, destinationKey.c_str() );
        if ( index.has_value() == false )
            return index;
        BehaviorNode& node   = _listNode[static_cast<size_t>( *index )];
        node._key            = destinationKey;
        node._repathDistance = MathUtil::max( 0.0f, repathDistance );
        return index;
    }

    bool BehaviorTree::isDecorator( int32 index ) const
    {
        switch ( getNode( index )._type )
        {
            case BehaviorNodeKind::Inverter:
            case BehaviorNodeKind::ForceSuccess:
            case BehaviorNodeKind::Repeat:
            case BehaviorNodeKind::Cooldown:
            case BehaviorNodeKind::TimeLimit:
            case BehaviorNodeKind::BlackboardCondition:
                return true;
            case BehaviorNodeKind::Selector:
            case BehaviorNodeKind::Sequence:
            case BehaviorNodeKind::Action:
            case BehaviorNodeKind::Condition:
            case BehaviorNodeKind::Wait:
            case BehaviorNodeKind::MoveTo:
            case BehaviorNodeKind::SubTree:
                return false;
        }
        return false;
    }

    bool BehaviorTree::isValid() const
    {
        if ( _listNode.empty() )
            return false;
        for ( int32 index = 0; index < getNodeCount(); ++index )
        {
            const BehaviorNode& node       = getNode( index );
            int32               childCount = 0;
            for ( optional<int32> child = node._firstChild; child.has_value(); child = getNode( *child )._nextSibling )
            {
                ++childCount;
            }
            const bool bComposite = node._type == BehaviorNodeKind::Selector || node._type == BehaviorNodeKind::Sequence;
            const bool bLeaf      = node._type == BehaviorNodeKind::Action || node._type == BehaviorNodeKind::Condition || node._type == BehaviorNodeKind::Wait ||
                               node._type == BehaviorNodeKind::MoveTo || node._type == BehaviorNodeKind::SubTree;
            // 코드로 지은 잎은 함수를, 데이터로 지은 잎은 동작 · 식을 든다.
            const bool bMissingAction    = node._type == BehaviorNodeKind::Action && node._function == nullptr && node._action.isValid() == false;
            const bool bMissingCondition = node._type == BehaviorNodeKind::Condition && node._function == nullptr && node._condition.isEmpty();
            const bool bNeedsKey         = node._type == BehaviorNodeKind::MoveTo || node._type == BehaviorNodeKind::SubTree;
            if ( bComposite && childCount == 0 )
                return false;
            if ( isDecorator( index ) && childCount != 1 )
                return false;
            if ( bLeaf && ( childCount != 0 || bMissingAction || bMissingCondition ) )
                return false;
            if ( bNeedsKey && node._key.empty() )
                return false;
        }
        return true;
    }

    bool BehaviorTree::evaluateCondition( const BehaviorNode& node, const GameplayContext& context )
    {
        return node._condition.evaluate( context );
    }

    // ------------------------------------------------------------------------------
    // BehaviorTreeRunner
    // ------------------------------------------------------------------------------
    BehaviorTreeRunner::BehaviorTreeRunner()
        : _pTree{ nullptr }
        , _context{}
        , _actionContext{}
        , _world{}
        , _listSlot{}
        , _listCursor{}
        , _listTimer{}
        , _listCooldownUntil{}
        , _listCounter{}
        , _listActive{}
        , _listLastCondition{}
        , _listMoveTarget{}
        , _elapsedTime{ 0.0f }
        , _tickCount{ 0 }
        , _activeLeaf{}
        , _lastSucceededLeaf{}
    {
    }

    void BehaviorTreeRunner::initialize( const BehaviorTree* pTree )
    {
        _pTree                 = pTree;
        const size_t nodeCount = pTree != nullptr ? static_cast<size_t>( pTree->getNodeCount() ) : 0;
        _listCursor.assign( nodeCount, 0 );
        _listTimer.assign( nodeCount, 0.0f );
        _listCooldownUntil.assign( nodeCount, -1.0e30f );
        _listCounter.assign( nodeCount, 0 );
        _listActive.assign( nodeCount, SW_FALSE );
        _listLastCondition.assign( nodeCount, SW_FALSE );
        _listMoveTarget.assign( nodeCount, float3{} );
        _elapsedTime = 0.0f;
        _activeLeaf.reset();
        _lastSucceededLeaf.reset();
    }

    void BehaviorTreeRunner::reset()
    {
        if ( _pTree != nullptr && _pTree->getNodeCount() > 0 )
            abortNode( 0 );
        _activeLeaf.reset();
        _lastSucceededLeaf.reset();
    }

    const utf8* BehaviorTreeRunner::getActiveLeafName() const
    {
        const optional<int32> leaf = _activeLeaf.has_value() ? _activeLeaf : _lastSucceededLeaf;
        return _pTree != nullptr && leaf.has_value() ? _pTree->getNode( *leaf )._name.c_str() : "";
    }

    BehaviorStatus BehaviorTreeRunner::tick( Blackboard& blackboard, void* pOwner, float32 deltaTime )
    {
        const bool bInvalidTreeState = _pTree == nullptr || _pTree->getNodeCount() == 0 || static_cast<int32>( _listActive.size() ) != _pTree->getNodeCount();
        if ( bInvalidTreeState )
            return BehaviorStatus::Failure;
        _elapsedTime += deltaTime;
        _context._pBlackboard = &blackboard;
        _context._pOwner      = pOwner;
        _context._deltaTime   = deltaTime;
        GameplayContext world = _world;
        world.set<BlackboardSlot>( &blackboard );
        world.set<BlackboardWriteSlot>( &blackboard );
        world.setSeed( GameConditions::makeSeed( _tickCount++, 0 ) );
        _actionContext._world     = world;
        _actionContext._pOwner    = pOwner;
        _actionContext._deltaTime = deltaTime;
        if ( pollHigherPriorityTrigger() )
            abortNode( 0 ); // 우선순위 높은 가지가 깨어났다 — 처음부터 다시 고른다
        _activeLeaf.reset();
        _lastSucceededLeaf.reset();
        return tickNode( 0 );
    }

    bool BehaviorTreeRunner::pollHigherPriorityTrigger()
    {
        bool bTriggered = false;
        for ( int32 index = 0; index < _pTree->getNodeCount(); ++index )
        {
            const BehaviorNode& node = _pTree->getNode( index );
            if ( node._type != BehaviorNodeKind::BlackboardCondition || BehaviorTreeInternal::abortsLowerPriority( node._abortMode ) == false )
                continue;
            const uint8 bNow                                 = BehaviorTree::evaluateCondition( node, _actionContext._world ) ? SW_TRUE : SW_FALSE;
            const bool  bRisen                               = bNow != SW_FALSE && _listLastCondition[static_cast<size_t>( index )] == SW_FALSE;
            _listLastCondition[static_cast<size_t>( index )] = bNow;
            // 노드 번호가 앞 = 깊이 우선 순서가 앞 = 우선순위가 높다. 도는 가지 안의 조건(자기 조상)은 Self 쪽 일이다.
            const bool bHigherPriorityRisen = bRisen && _activeLeaf.has_value() && index < *_activeLeaf && _listActive[static_cast<size_t>( index )] == SW_FALSE;
            if ( bHigherPriorityRisen )
                bTriggered = true;
        }
        return bTriggered;
    }

    void BehaviorTreeRunner::finishNode( int32 index )
    {
        _listActive[static_cast<size_t>( index )]  = SW_FALSE;
        _listCursor[static_cast<size_t>( index )]  = 0;
        _listTimer[static_cast<size_t>( index )]   = 0.0f;
        _listCounter[static_cast<size_t>( index )] = 0;
    }

    void BehaviorTreeRunner::abortNode( int32 index )
    {
        if ( _listActive[static_cast<size_t>( index )] == SW_FALSE )
            return;
        const BehaviorNode& node = _pTree->getNode( index );
        for ( optional<int32> child = node._firstChild; child.has_value(); child = _pTree->getNode( *child )._nextSibling )
        {
            abortNode( *child );
        }
        if ( node._type == BehaviorNodeKind::Action && node._function != nullptr )
        {
            _context._nodeIndex    = index;
            _context._bJustStarted = SW_FALSE;
            _context._bAborted     = SW_TRUE;
            (void)node._function( _context ); // 뒷정리만 — 결과는 쓰지 않는다
            _context._bAborted = SW_FALSE;
        }
        if ( node._type == BehaviorNodeKind::Action && node._action.isValid() )
        {
            _actionContext._bJustStarted = SW_FALSE;
            _actionContext._bAborted     = SW_TRUE;
            (void)node._action.invoke( _actionContext ); // 뒷정리만 — 결과는 쓰지 않는다
            _actionContext._bAborted = SW_FALSE;
        }
        if ( node._type == BehaviorNodeKind::SubTree )
        {
            BehaviorTreeRunner* pSlotRunner = findSlotRunner( node._key );
            if ( pSlotRunner != nullptr )
                pSlotRunner->reset();
        }
        if ( node._type == BehaviorNodeKind::MoveTo && _context._pMover != nullptr )
            _context._pMover->stopMoving();
        finishNode( index );
    }

    optional<int32> BehaviorTreeRunner::findChild( const BehaviorNode& node, int32 order ) const
    {
        optional<int32> child = node._firstChild;
        for ( int32 skip = 0; skip < order && child.has_value(); ++skip )
        {
            child = _pTree->getNode( *child )._nextSibling;
        }
        return child;
    }

    BehaviorStatus BehaviorTreeRunner::tickSequence( const BehaviorNode& node, int32& inoutCursor )
    {
        for ( optional<int32> child = findChild( node, inoutCursor ); child.has_value(); child = _pTree->getNode( *child )._nextSibling )
        {
            const BehaviorStatus childStatus = tickNode( *child );
            if ( childStatus != BehaviorStatus::Success )
                return childStatus;
            ++inoutCursor;
        }
        return BehaviorStatus::Success;
    }

    BehaviorStatus BehaviorTreeRunner::tickSelector( const BehaviorNode& node, int32& inoutCursor )
    {
        const int32     start = ( node._bReactive != SW_FALSE ) ? 0 : inoutCursor;
        optional<int32> child = findChild( node, start );
        for ( int32 order = start; child.has_value(); ++order, child = _pTree->getNode( *child )._nextSibling )
        {
            const BehaviorStatus childStatus = tickNode( *child );
            if ( childStatus == BehaviorStatus::Failure )
                continue;
            if ( order != inoutCursor )
            {
                const optional<int32> running = findChild( node, inoutCursor );
                if ( running.has_value() && running != *child )
                    abortNode( *running );
                inoutCursor = order;
            }
            return childStatus;
        }
        return BehaviorStatus::Failure;
    }

    // 노드 종류마다 짧은 분기인 switch 디스패처라 더 나누지 않는다. 여러 자식을 도는 합성 노드는 tickSequence 와 tickSelector 로 나눴다.
    BehaviorStatus BehaviorTreeRunner::tickNode( int32 index )
    {
        const BehaviorNode& node                  = _pTree->getNode( index );
        const bool          bFirst                = _listActive[static_cast<size_t>( index )] == SW_FALSE;
        _listActive[static_cast<size_t>( index )] = SW_TRUE;
        int32&         cursor                     = _listCursor[static_cast<size_t>( index )];
        float32&       timer                      = _listTimer[static_cast<size_t>( index )];
        BehaviorStatus status                     = BehaviorStatus::Failure;

        switch ( node._type )
        {
            case BehaviorNodeKind::Sequence:
            {
                status = tickSequence( node, cursor );
                break;
            }
            case BehaviorNodeKind::Selector:
            {
                status = tickSelector( node, cursor );
                break;
            }
            case BehaviorNodeKind::Inverter:
            {
                const BehaviorStatus childStatus = tickNode( node._firstChild.value() );
                status                           = childStatus == BehaviorStatus::Running ? BehaviorStatus::Running
                                                                                          : ( childStatus == BehaviorStatus::Success ? BehaviorStatus::Failure : BehaviorStatus::Success );
                break;
            }
            case BehaviorNodeKind::ForceSuccess:
            {
                status = tickNode( node._firstChild.value() ) == BehaviorStatus::Running ? BehaviorStatus::Running : BehaviorStatus::Success;
                break;
            }
            case BehaviorNodeKind::Repeat:
            {
                const BehaviorStatus childStatus = tickNode( node._firstChild.value() );
                if ( childStatus != BehaviorStatus::Success )
                {
                    status = childStatus;
                    break;
                }
                int32& counter = _listCounter[static_cast<size_t>( index )];
                ++counter;
                // 한 틱에 한 번만 — 바로 끝나는 자식이 한 틱 안에서 끝없이 돌지 않게.
                status = ( node._count > 0 && counter >= node._count ) ? BehaviorStatus::Success : BehaviorStatus::Running;
                break;
            }
            case BehaviorNodeKind::Cooldown:
            {
                float32& cooldownUntil = _listCooldownUntil[static_cast<size_t>( index )];
                if ( bFirst && _elapsedTime < cooldownUntil )
                {
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild.value() );
                if ( status != BehaviorStatus::Running )
                    cooldownUntil = _elapsedTime + node._seconds;
                break;
            }
            case BehaviorNodeKind::TimeLimit:
            {
                timer += _context._deltaTime;
                if ( timer > node._seconds )
                {
                    abortNode( node._firstChild.value() );
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild.value() );
                break;
            }
            case BehaviorNodeKind::BlackboardCondition:
            {
                const bool bCheck = bFirst || BehaviorTreeInternal::abortsSelf( node._abortMode );
                if ( bCheck && BehaviorTree::evaluateCondition( node, _actionContext._world ) == false )
                {
                    if ( bFirst == false )
                        abortNode( node._firstChild.value() ); // 관찰 중단 Self — 조건이 깨졌다
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild.value() );
                break;
            }
            case BehaviorNodeKind::Condition:
            {
                if ( node._function != nullptr )
                {
                    _context._nodeIndex    = index;
                    _context._bJustStarted = SW_TRUE;
                    status                 = node._function( _context ) == BehaviorStatus::Success ? BehaviorStatus::Success : BehaviorStatus::Failure;
                }
                else
                {
                    status = node._condition.evaluate( _actionContext._world ) ? BehaviorStatus::Success : BehaviorStatus::Failure;
                }
                break;
            }
            case BehaviorNodeKind::Action:
            {
                status = runAction( node, index, bFirst );
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                else if ( status == BehaviorStatus::Success )
                    _lastSucceededLeaf = index;
                break;
            }
            case BehaviorNodeKind::Wait:
            {
                timer += _context._deltaTime;
                status = timer >= node._seconds ? BehaviorStatus::Success : BehaviorStatus::Running;
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                break;
            }
            case BehaviorNodeKind::MoveTo:
            {
                status = tickMoveTo( index, bFirst );
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                break;
            }
            case BehaviorNodeKind::SubTree:
            {
                BehaviorTreeRunner* pSlotRunner = findSlotRunner( node._key );
                if ( pSlotRunner == nullptr )
                {
                    // 빈 슬롯은 쓰는 쪽의 빠뜨림이다. 가지를 도는 내내 찍지 않게 한 번만 알린다(`_listLastCondition` 은 조건 노드만 쓴다).
                    if ( _listLastCondition[static_cast<size_t>( index )] == SW_FALSE )
                        SW_LOG_WARNING_ID( "BehaviorTree.SlotEmpty", "Behavior tree slot '%#' has no tree (BehaviorTreeRunner::setSlotTree)", node._key.c_str() );
                    _listLastCondition[static_cast<size_t>( index )] = SW_TRUE;
                    status                                           = BehaviorStatus::Failure;
                    break;
                }
                pSlotRunner->_world           = _world;
                pSlotRunner->_context._pMover = _context._pMover;
                status                        = pSlotRunner->tick( *_context._pBlackboard, _context._pOwner, _context._deltaTime );
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                break;
            }
        }

        if ( status != BehaviorStatus::Running )
            finishNode( index );
        return status;
    }

    BehaviorStatus BehaviorTreeRunner::runAction( const BehaviorNode& node, int32 index, bool bJustStarted )
    {
        if ( node._function != nullptr )
        {
            _context._nodeIndex    = index;
            _context._bJustStarted = bJustStarted ? SW_TRUE : SW_FALSE;
            return node._function( _context );
        }
        _actionContext._bJustStarted = bJustStarted ? SW_TRUE : SW_FALSE;
        switch ( node._action.invoke( _actionContext ) )
        {
            case ActionResult::Succeeded:
                return BehaviorStatus::Success;
            case ActionResult::Failed:
                return BehaviorStatus::Failure;
            case ActionResult::Running:
                return BehaviorStatus::Running;
        }
        return BehaviorStatus::Failure;
    }

    BehaviorTreeRunner* BehaviorTreeRunner::findSlotRunner( const hashed_string& slot )
    {
        for ( const SlotBinding& binding : _listSlot )
        {
            if ( binding._slot == slot )
                return binding._pRunner.get();
        }
        return nullptr;
    }

    void BehaviorTreeRunner::setSlotTree( const hashed_string& slot, const BehaviorTree* pTree )
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._slot != slot )
                continue;
            _listSlot[index]._pRunner->reset(); // 도는 가지의 뒷정리
            if ( pTree != nullptr )
                _listSlot[index]._pRunner->initialize( pTree );
            else
                _listSlot.erase( _listSlot.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
        if ( pTree == nullptr )
            return;
        SlotBinding binding;
        binding._slot    = slot;
        binding._pRunner = sw::make_shared<BehaviorTreeRunner>();
        binding._pRunner->initialize( pTree );
        _listSlot.push_back( std::move( binding ) );
    }

    BehaviorStatus BehaviorTreeRunner::tickMoveTo( int32 index, bool bFirst )
    {
        const BehaviorNode&    node   = _pTree->getNode( index );
        INavMover*             pMover = _context._pMover;
        const optional<float3> target = _context._pBlackboard->getVector( node._key );
        if ( pMover == nullptr )
            return BehaviorStatus::Failure;
        if ( target.has_value() == false )
        {
            // 도는 중에 목적지가 사라졌다. 트리만 실패로 넘어가면 몸은 옛 목적지로 계속 간다.
            if ( bFirst == false )
                pMover->stopMoving();
            return BehaviorStatus::Failure;
        }
        const float3 destination = *target;
        float3&      issued      = _listMoveTarget[static_cast<size_t>( index )];
        const bool   bRepath     = bFirst || ( destination - issued ).getLength() > node._repathDistance;
        if ( bRepath )
        {
            issued = destination;
            if ( pMover->moveTo( destination ) == false )
                return BehaviorStatus::Failure;
            // 요청이 다음 갱신에 풀리는 구현(내비메시 에이전트)이 있다 — 이번 틱의 상태는 옛 요청의 것이라 읽지 않는다.
            return BehaviorStatus::Running;
        }
        switch ( pMover->getMoveStatus() )
        {
            case NavMoveStatus::Moving:
                return BehaviorStatus::Running;
            case NavMoveStatus::Arrived:
                return BehaviorStatus::Success;
            case NavMoveStatus::Idle:   // 다른 것이 멈췄다
            case NavMoveStatus::Failed: // 길이 없다
                return BehaviorStatus::Failure;
        }
        return BehaviorStatus::Failure;
    }
} // namespace sw

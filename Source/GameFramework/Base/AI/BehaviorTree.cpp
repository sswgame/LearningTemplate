#include "pch.h"

#include "GameFramework/Base/AI/BehaviorTree.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/AI/Blackboard.h"

namespace sw
{
    namespace
    {
        struct BehaviorTreeInternal
        {
            static bool abortsSelf( BehaviorAbortMode mode ) { return mode == BehaviorAbortMode::Self || mode == BehaviorAbortMode::Both; }
            static bool abortsLowerPriority( BehaviorAbortMode mode ) { return mode == BehaviorAbortMode::LowerPriority || mode == BehaviorAbortMode::Both; }
        };
    } // namespace
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

    int32 BehaviorTree::addNode( int32 parent, BehaviorNodeType type, const utf8* pName )
    {
        if ( parent < 0 && _listNode.empty() == false )
            return -1; // 뿌리는 하나
        if ( parent >= static_cast<int32>( _listNode.size() ) )
            return -1;
        const int32  index = static_cast<int32>( _listNode.size() );
        BehaviorNode node;
        node._type   = type;
        node._name   = pName != nullptr ? pName : "";
        node._parent = parent;
        _listNode.push_back( node );
        if ( parent >= 0 )
        {
            // 형제 목록 끝에 붙인다 — 더한 순서가 우선순위다.
            BehaviorNode& parentNode = _listNode[static_cast<size_t>( parent )];
            if ( parentNode._firstChild < 0 )
                parentNode._firstChild = index;
            else
            {
                int32 sibling = parentNode._firstChild;
                while ( _listNode[static_cast<size_t>( sibling )]._nextSibling >= 0 )
                {
                    sibling = _listNode[static_cast<size_t>( sibling )]._nextSibling;
                }
                _listNode[static_cast<size_t>( sibling )]._nextSibling = index;
            }
        }
        return index;
    }

    int32 BehaviorTree::addSelector( int32 parent, const utf8* pName, bool bReactive )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Selector, pName );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._bReactive = bReactive ? SW_TRUE : SW_FALSE;
        return index;
    }

    int32 BehaviorTree::addSequence( int32 parent, const utf8* pName )
    {
        return addNode( parent, BehaviorNodeType::Sequence, pName );
    }

    int32 BehaviorTree::addInverter( int32 parent )
    {
        return addNode( parent, BehaviorNodeType::Inverter, "Inverter" );
    }

    int32 BehaviorTree::addForceSuccess( int32 parent )
    {
        return addNode( parent, BehaviorNodeType::ForceSuccess, "ForceSuccess" );
    }

    int32 BehaviorTree::addRepeat( int32 parent, int32 count )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Repeat, "Repeat" );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._count = MathUtil::max( 0, count );
        return index;
    }

    int32 BehaviorTree::addCooldown( int32 parent, float32 seconds )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Cooldown, "Cooldown" );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    int32 BehaviorTree::addTimeLimit( int32 parent, float32 seconds )
    {
        const int32 index = addNode( parent, BehaviorNodeType::TimeLimit, "TimeLimit" );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    int32 BehaviorTree::addBlackboardCondition( int32 parent, const hashed_string& key, BlackboardCompare compare, float32 value, BehaviorAbortMode abortMode )
    {
        const int32 index = addNode( parent, BehaviorNodeType::BlackboardCondition, key.c_str() );
        if ( index < 0 )
            return index;
        BehaviorNode& node = _listNode[static_cast<size_t>( index )];
        node._key          = key;
        node._compare      = compare;
        node._compareValue = value;
        node._abortMode    = abortMode;
        return index;
    }

    int32 BehaviorTree::addAction( int32 parent, const utf8* pName, BehaviorTaskFunction function )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Action, pName );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._function = function;
        return index;
    }

    int32 BehaviorTree::addCondition( int32 parent, const utf8* pName, BehaviorTaskFunction function )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Condition, pName );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._function = function;
        return index;
    }

    int32 BehaviorTree::addWait( int32 parent, float32 seconds )
    {
        const int32 index = addNode( parent, BehaviorNodeType::Wait, "Wait" );
        if ( index >= 0 )
            _listNode[static_cast<size_t>( index )]._seconds = MathUtil::max( 0.0f, seconds );
        return index;
    }

    bool BehaviorTree::isDecorator( int32 index ) const
    {
        switch ( getNode( index )._type )
        {
            case BehaviorNodeType::Inverter:
            case BehaviorNodeType::ForceSuccess:
            case BehaviorNodeType::Repeat:
            case BehaviorNodeType::Cooldown:
            case BehaviorNodeType::TimeLimit:
            case BehaviorNodeType::BlackboardCondition:
                return true;
            case BehaviorNodeType::Selector:
            case BehaviorNodeType::Sequence:
            case BehaviorNodeType::Action:
            case BehaviorNodeType::Condition:
            case BehaviorNodeType::Wait:
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
            for ( int32 child = node._firstChild; child >= 0; child = getNode( child )._nextSibling )
            {
                ++childCount;
            }
            const bool bComposite = node._type == BehaviorNodeType::Selector || node._type == BehaviorNodeType::Sequence;
            const bool bLeaf      = node._type == BehaviorNodeType::Action || node._type == BehaviorNodeType::Condition || node._type == BehaviorNodeType::Wait;
            if ( bComposite && childCount == 0 )
                return false;
            if ( isDecorator( index ) && childCount != 1 )
                return false;
            if ( bLeaf && ( childCount != 0 || ( node._type != BehaviorNodeType::Wait && node._function == nullptr ) ) )
                return false;
        }
        return true;
    }

    bool BehaviorTree::evaluateCondition( const BehaviorNode& node, const Blackboard& blackboard )
    {
        const float32 value = blackboard.getFloat( node._key, 0.0f );
        switch ( node._compare )
        {
            case BlackboardCompare::IsSet:
                return blackboard.isSet( node._key );
            case BlackboardCompare::IsNotSet:
                return blackboard.isSet( node._key ) == false;
            case BlackboardCompare::Equal:
                return MathUtil::abs( value - node._compareValue ) < 1.0e-5f;
            case BlackboardCompare::NotEqual:
                return MathUtil::abs( value - node._compareValue ) >= 1.0e-5f;
            case BlackboardCompare::Less:
                return value < node._compareValue;
            case BlackboardCompare::LessOrEqual:
                return value <= node._compareValue;
            case BlackboardCompare::Greater:
                return value > node._compareValue;
            case BlackboardCompare::GreaterOrEqual:
                return value >= node._compareValue;
        }
        return false;
    }

    // ------------------------------------------------------------------------------
    // BehaviorTreeRunner
    // ------------------------------------------------------------------------------
    BehaviorTreeRunner::BehaviorTreeRunner()
        : _pTree{ nullptr }
        , _context{}
        , _listCursor{}
        , _listTimer{}
        , _listCooldownUntil{}
        , _listCounter{}
        , _listActive{}
        , _listLastCondition{}
        , _elapsedTime{ 0.0f }
        , _activeLeaf{ -1 }
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
        _elapsedTime = 0.0f;
        _activeLeaf  = -1;
    }

    void BehaviorTreeRunner::reset()
    {
        if ( _pTree != nullptr && _pTree->getNodeCount() > 0 )
            abortNode( 0 );
        _activeLeaf = -1;
    }

    const utf8* BehaviorTreeRunner::getActiveLeafName() const
    {
        return _pTree != nullptr && _activeLeaf >= 0 ? _pTree->getNode( _activeLeaf )._name.c_str() : "";
    }

    BehaviorStatus BehaviorTreeRunner::tick( Blackboard& blackboard, void* pOwner, float32 deltaTime )
    {
        if ( _pTree == nullptr || _pTree->getNodeCount() == 0 || static_cast<int32>( _listActive.size() ) != _pTree->getNodeCount() )
            return BehaviorStatus::Failure;
        _elapsedTime += deltaTime;
        _context._pBlackboard = &blackboard;
        _context._pOwner      = pOwner;
        _context._deltaTime   = deltaTime;
        if ( hasHigherPriorityTrigger() )
            abortNode( 0 ); // 우선순위 높은 가지가 깨어났다 — 처음부터 다시 고른다
        _activeLeaf = -1;
        return tickNode( 0 );
    }

    bool BehaviorTreeRunner::hasHigherPriorityTrigger()
    {
        bool bTriggered = false;
        for ( int32 index = 0; index < _pTree->getNodeCount(); ++index )
        {
            const BehaviorNode& node = _pTree->getNode( index );
            if ( node._type != BehaviorNodeType::BlackboardCondition || BehaviorTreeInternal::abortsLowerPriority( node._abortMode ) == false )
                continue;
            const uint8 bNow                                 = BehaviorTree::evaluateCondition( node, *_context._pBlackboard ) ? SW_TRUE : SW_FALSE;
            const bool  bRisen                               = bNow != SW_FALSE && _listLastCondition[static_cast<size_t>( index )] == SW_FALSE;
            _listLastCondition[static_cast<size_t>( index )] = bNow;
            // 노드 번호가 앞 = 깊이 우선 순서가 앞 = 우선순위가 높다. 도는 가지 안의 조건(자기 조상)은 Self 쪽 일이다.
            if ( bRisen && _activeLeaf >= 0 && index < _activeLeaf && _listActive[static_cast<size_t>( index )] == SW_FALSE )
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
        for ( int32 child = node._firstChild; child >= 0; child = _pTree->getNode( child )._nextSibling )
        {
            abortNode( child );
        }
        if ( node._type == BehaviorNodeType::Action && node._function != nullptr )
        {
            _context._nodeIndex    = index;
            _context._bJustStarted = SW_FALSE;
            _context._bAborted     = SW_TRUE;
            (void)node._function( _context ); // 뒷정리만 — 결과는 쓰지 않는다
            _context._bAborted = SW_FALSE;
        }
        finishNode( index );
    }

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
            case BehaviorNodeType::Sequence:
            {
                int32 child = node._firstChild;
                for ( int32 skip = 0; skip < cursor && child >= 0; ++skip )
                {
                    child = _pTree->getNode( child )._nextSibling;
                }
                status = BehaviorStatus::Success;
                while ( child >= 0 )
                {
                    const BehaviorStatus childStatus = tickNode( child );
                    if ( childStatus != BehaviorStatus::Success )
                    {
                        status = childStatus;
                        break;
                    }
                    ++cursor;
                    child = _pTree->getNode( child )._nextSibling;
                }
                break;
            }
            case BehaviorNodeType::Selector:
            {
                // 반응형이면 매 틱 앞 자식부터 — 앞 자식이 실패하지 않으면 도는 뒤 자식을 멈추고 그쪽으로 간다.
                const int32 start = ( node._bReactive != SW_FALSE ) ? 0 : cursor;
                int32       child = node._firstChild;
                for ( int32 skip = 0; skip < start && child >= 0; ++skip )
                {
                    child = _pTree->getNode( child )._nextSibling;
                }
                status = BehaviorStatus::Failure;
                for ( int32 order = start; child >= 0; ++order, child = _pTree->getNode( child )._nextSibling )
                {
                    const BehaviorStatus childStatus = tickNode( child );
                    if ( childStatus == BehaviorStatus::Failure )
                        continue;
                    if ( order != cursor )
                    {
                        int32 running = node._firstChild;
                        for ( int32 skip = 0; skip < cursor && running >= 0; ++skip )
                        {
                            running = _pTree->getNode( running )._nextSibling;
                        }
                        if ( running >= 0 && running != child )
                            abortNode( running );
                        cursor = order;
                    }
                    status = childStatus;
                    break;
                }
                break;
            }
            case BehaviorNodeType::Inverter:
            {
                const BehaviorStatus childStatus = tickNode( node._firstChild );
                status                           = childStatus == BehaviorStatus::Running ? BehaviorStatus::Running
                                                                                          : ( childStatus == BehaviorStatus::Success ? BehaviorStatus::Failure : BehaviorStatus::Success );
                break;
            }
            case BehaviorNodeType::ForceSuccess:
            {
                status = tickNode( node._firstChild ) == BehaviorStatus::Running ? BehaviorStatus::Running : BehaviorStatus::Success;
                break;
            }
            case BehaviorNodeType::Repeat:
            {
                const BehaviorStatus childStatus = tickNode( node._firstChild );
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
            case BehaviorNodeType::Cooldown:
            {
                float32& cooldownUntil = _listCooldownUntil[static_cast<size_t>( index )];
                if ( bFirst && _elapsedTime < cooldownUntil )
                {
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild );
                if ( status != BehaviorStatus::Running )
                    cooldownUntil = _elapsedTime + node._seconds;
                break;
            }
            case BehaviorNodeType::TimeLimit:
            {
                timer += _context._deltaTime;
                if ( timer > node._seconds )
                {
                    abortNode( node._firstChild );
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild );
                break;
            }
            case BehaviorNodeType::BlackboardCondition:
            {
                const bool bCheck = bFirst || BehaviorTreeInternal::abortsSelf( node._abortMode );
                if ( bCheck && BehaviorTree::evaluateCondition( node, *_context._pBlackboard ) == false )
                {
                    if ( bFirst == false )
                        abortNode( node._firstChild ); // 관찰 중단 Self — 조건이 깨졌다
                    status = BehaviorStatus::Failure;
                    break;
                }
                status = tickNode( node._firstChild );
                break;
            }
            case BehaviorNodeType::Condition:
            {
                _context._nodeIndex    = index;
                _context._bJustStarted = SW_TRUE;
                status                 = node._function( _context ) == BehaviorStatus::Success ? BehaviorStatus::Success : BehaviorStatus::Failure;
                break;
            }
            case BehaviorNodeType::Action:
            {
                _context._nodeIndex    = index;
                _context._bJustStarted = bFirst ? SW_TRUE : SW_FALSE;
                status                 = node._function( _context );
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                break;
            }
            case BehaviorNodeType::Wait:
            {
                timer += _context._deltaTime;
                status = timer >= node._seconds ? BehaviorStatus::Success : BehaviorStatus::Running;
                if ( status == BehaviorStatus::Running )
                    _activeLeaf = index;
                break;
            }
        }

        if ( status != BehaviorStatus::Running )
            finishNode( index );
        return status;
    }
} // namespace sw

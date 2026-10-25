#include "pch.h"

#include "GameFramework/PlugIn/Editor/Base/AI/BehaviorTreeEditModel.h"

#include "Core/Container/VectorUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Base/Actor/AI/BehaviorTaskRegistry.h"
#include "GameFramework/Base/Actor/AI/BehaviorTreeAsset.h"

#include <optional>

namespace sw::editor
{
    namespace
    {
        struct BehaviorTreeEditModelInternal
        {
            /** @brief 새 노드의 기본 값입니다. 시간 노드가 0 초로 시작하면 곧바로 끝나 버려서 눈에 띄지 않으므로 1 초로 둔다. */
            static constexpr float32 kDefaultSeconds = 1.0f;

            static bool hasSeconds( BehaviorNodeType type )
            {
                return type == BehaviorNodeType::Cooldown || type == BehaviorNodeType::TimeLimit || type == BehaviorNodeType::Wait;
            }

            static bool hasTask( BehaviorNodeType type )
            {
                return type == BehaviorNodeType::Action || type == BehaviorNodeType::Condition;
            }

            static bool hasKey( BehaviorNodeType type )
            {
                return type == BehaviorNodeType::BlackboardCondition || type == BehaviorNodeType::MoveTo;
            }

            static bool isComposite( BehaviorNodeType type )
            {
                return type == BehaviorNodeType::Selector || type == BehaviorNodeType::Sequence;
            }

            static string makeNodeError( const XMLNode& node, string_view reason )
            {
                string text = "<";
                text += node.getName();
                text += "> ";
                text.append( reason.data(), reason.size() );
                return text;
            }

            /** @brief 노드 원소의 속성을 @p inoutValue(`_type` 은 채워져 있다)로 읽습니다. 없거나 깨진 속성이 하나라도 있으면 false 입니다(오류는 로그에 남는다). */
            [[nodiscard]] static bool readAttributes( const XMLNode& node, BehaviorTreeEditNode& inoutValue )
            {
                std::optional<string>  text;
                std::optional<float32> number;
                std::optional<bool>    flag;
                bool                   bRead = node.readOptionalAttribute<string>( "name", text );
                if ( text.has_value() )
                    inoutValue._name = *text;
                switch ( inoutValue._type )
                {
                    case BehaviorNodeType::Selector:
                    {
                        bRead                 = node.readOptionalAttribute<bool>( "reactive", flag ) && bRead;
                        inoutValue._bReactive = flag.value_or( false );
                        break;
                    }
                    case BehaviorNodeType::Repeat:
                    {
                        const std::optional<int32> count = node.readRequiredAttribute<int32>( "count" );
                        bRead                            = count.has_value() && bRead;
                        inoutValue._count                = count.value_or( 0 );
                        if ( inoutValue._count < 0 )
                            bRead = false;
                        break;
                    }
                    case BehaviorNodeType::Cooldown:
                    case BehaviorNodeType::TimeLimit:
                    case BehaviorNodeType::Wait:
                    {
                        const std::optional<float32> seconds = node.readRequiredAttribute<float32>( "seconds" );
                        bRead                                = seconds.has_value() && bRead;
                        inoutValue._seconds                  = seconds.value_or( 0.0f );
                        if ( inoutValue._seconds < 0.0f )
                            bRead = false;
                        break;
                    }
                    case BehaviorNodeType::BlackboardCondition:
                    {
                        const std::optional<string> key = node.readRequiredAttribute<string>( "key" );
                        bRead                           = key.has_value() && bRead;
                        inoutValue._key                 = key.value_or( string{} );
                        std::optional<string> compareText;
                        std::optional<string> abortText;
                        bRead             = node.readOptionalAttribute<string>( "compare", compareText ) && bRead;
                        bRead             = node.readOptionalAttribute<string>( "abort", abortText ) && bRead;
                        bRead             = node.readOptionalAttribute<float32>( "value", number ) && bRead;
                        inoutValue._value = number.value_or( 0.0f );
                        if ( compareText.has_value() && BehaviorTreeNames::parse( *compareText, inoutValue._compare ) == false )
                            bRead = false;
                        if ( abortText.has_value() && BehaviorTreeNames::parse( *abortText, inoutValue._abort ) == false )
                            bRead = false;
                        break;
                    }
                    case BehaviorNodeType::Action:
                    case BehaviorNodeType::Condition:
                    {
                        const std::optional<string> task = node.readRequiredAttribute<string>( "task" );
                        bRead                            = task.has_value() && bRead;
                        inoutValue._task                 = task.value_or( string{} );
                        break;
                    }
                    case BehaviorNodeType::MoveTo:
                    {
                        const std::optional<string> key = node.readRequiredAttribute<string>( "key" );
                        bRead                           = key.has_value() && bRead;
                        inoutValue._key                 = key.value_or( string{} );
                        bRead                           = node.readOptionalAttribute<float32>( "repathDistance", number ) && bRead;
                        inoutValue._repathDistance      = number.value_or( 0.5f );
                        if ( inoutValue._repathDistance < 0.0f )
                            bRead = false;
                        break;
                    }
                    case BehaviorNodeType::Sequence:
                    case BehaviorNodeType::Inverter:
                    case BehaviorNodeType::ForceSuccess:
                    {
                        break;
                    }
                }
                return bRead;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void BehaviorTreeEditModel::clear()
    {
        _listNode.clear();
        _rootID = -1;
        _nextID = 1;
    }

    const BehaviorTreeEditNode* BehaviorTreeEditModel::find( int32 id ) const
    {
        for ( const BehaviorTreeEditNode& node : _listNode )
        {
            if ( node._id == id )
                return &node;
        }
        return nullptr;
    }

    BehaviorTreeEditNode* BehaviorTreeEditModel::find( int32 id )
    {
        for ( BehaviorTreeEditNode& node : _listNode )
        {
            if ( node._id == id )
                return &node;
        }
        return nullptr;
    }

    bool BehaviorTreeEditModel::canHoldChild( const BehaviorTreeEditNode& parent ) const
    {
        if ( BehaviorTreeNames::isLeaf( parent._type ) )
            return false;
        const bool bDecoratorFull = BehaviorTreeNames::isDecorator( parent._type ) && parent._listChild.empty() == false;
        return bDecoratorFull == false;
    }

    uint32 BehaviorTreeEditModel::getDepth( int32 id ) const
    {
        uint32                      depth = 0;
        const BehaviorTreeEditNode* pNode = find( id );
        while ( pNode != nullptr && pNode->_parent >= 0 )
        {
            ++depth;
            pNode = find( pNode->_parent );
        }
        return depth;
    }

    uint32 BehaviorTreeEditModel::getSubtreeHeight( int32 id ) const
    {
        const BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return 0;
        uint32 height = 0;
        for ( const int32 childID : pNode->_listChild )
        {
            const uint32 childHeight = getSubtreeHeight( childID ) + 1;
            if ( childHeight > height )
                height = childHeight;
        }
        return height;
    }

    int32 BehaviorTreeEditModel::addNode( int32 parentID, BehaviorNodeType type )
    {
        using Internal = BehaviorTreeEditModelInternal;
        if ( parentID < 0 )
        {
            if ( _rootID >= 0 )
                return -1;
        }
        else
        {
            const BehaviorTreeEditNode* pParent = find( parentID );
            if ( pParent == nullptr || canHoldChild( *pParent ) == false )
                return -1;
            if ( getDepth( parentID ) + 1 > BehaviorTreeNames::kMaxDepth )
                return -1;
        }

        BehaviorTreeEditNode node{};
        node._id     = _nextID++;
        node._parent = parentID;
        node._type   = type;
        if ( Internal::hasSeconds( type ) )
            node._seconds = Internal::kDefaultSeconds;
        _listNode.push_back( std::move( node ) );

        const int32 newID = _listNode.back()._id;
        if ( parentID < 0 )
            _rootID = newID;
        else
            find( parentID )->_listChild.push_back( newID ); // 부모는 위에서 찾았다 - 다시 찾는 까닭은 push_back 이 포인터를 무효로 하기 때문이다
        return newID;
    }

    void BehaviorTreeEditModel::collectSubtree( int32 id, vector<int32>& outListID ) const
    {
        outListID.push_back( id );
        const BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return;
        for ( const int32 childID : pNode->_listChild )
        {
            collectSubtree( childID, outListID );
        }
    }

    void BehaviorTreeEditModel::detachFromParent( BehaviorTreeEditNode& node )
    {
        BehaviorTreeEditNode* pParent = find( node._parent );
        if ( pParent != nullptr )
        {
            const int32 id = node._id;
            VectorUtil::removeIf( pParent->_listChild, [id]( int32 childID )
            { return childID == id; } );
        }
        node._parent = -1;
    }

    bool BehaviorTreeEditModel::removeNode( int32 id )
    {
        BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return false;
        vector<int32> listRemoved;
        collectSubtree( id, listRemoved );
        detachFromParent( *pNode );
        if ( id == _rootID )
            _rootID = -1;
        VectorUtil::removeIf( _listNode, [&listRemoved]( const BehaviorTreeEditNode& node )
        {
            for ( const int32 removedID : listRemoved )
            {
                if ( removedID == node._id )
                    return true;
            }
            return false;
        } );
        return true;
    }

    bool BehaviorTreeEditModel::moveChild( int32 id, int32 delta )
    {
        const BehaviorTreeEditNode* pNode   = find( id );
        BehaviorTreeEditNode*       pParent = pNode != nullptr ? find( pNode->_parent ) : nullptr;
        if ( pParent == nullptr )
            return false;
        vector<int32>& listChild = pParent->_listChild;
        int32          index     = 0;
        while ( index < static_cast<int32>( listChild.size() ) && listChild[static_cast<size_t>( index )] != id )
        {
            ++index;
        }
        int32 target = index + delta;
        if ( target < 0 )
            target = 0;
        const int32 lastIndex = static_cast<int32>( listChild.size() ) - 1;
        if ( target > lastIndex )
            target = lastIndex;
        if ( target == index )
            return false;
        listChild.erase( listChild.begin() + index );
        listChild.insert( listChild.begin() + target, id );
        return true;
    }

    bool BehaviorTreeEditModel::reparent( int32 id, int32 newParentID )
    {
        BehaviorTreeEditNode*       pNode      = find( id );
        const BehaviorTreeEditNode* pNewParent = find( newParentID );
        if ( pNode == nullptr || pNewParent == nullptr )
            return false;
        const bool bIsRoot     = pNode->_parent < 0;
        const bool bSameParent = pNode->_parent == newParentID;
        if ( bIsRoot || bSameParent )
            return false;
        vector<int32> listSubtree;
        collectSubtree( id, listSubtree );
        for ( const int32 subtreeID : listSubtree )
        {
            if ( subtreeID == newParentID )
                return false; // 제 자손 아래로는 못 간다
        }
        if ( canHoldChild( *pNewParent ) == false )
            return false;
        if ( getDepth( newParentID ) + 1 + getSubtreeHeight( id ) > BehaviorTreeNames::kMaxDepth )
            return false;
        detachFromParent( *pNode );
        pNode->_parent = newParentID;
        find( newParentID )->_listChild.push_back( id );
        return true;
    }

    void BehaviorTreeEditModel::collectPreorder( vector<int32>& outListID ) const
    {
        outListID.clear();
        if ( _rootID >= 0 )
            collectSubtree( _rootID, outListID );
    }

    string BehaviorTreeEditModel::makeDisplayName( int32 id ) const
    {
        const BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return {};
        if ( pNode->_name.empty() == false )
            return pNode->_name;
        if ( pNode->_task.empty() == false )
            return pNode->_task;
        return string{ BehaviorTreeNames::toString( pNode->_type ) };
    }

    void BehaviorTreeEditModel::collectIssues( vector<BehaviorTreeEditIssue>& outListIssue ) const
    {
        using Internal = BehaviorTreeEditModelInternal;
        outListIssue.clear();
        if ( _rootID < 0 )
        {
            outListIssue.push_back( BehaviorTreeEditIssue{ -1, "The tree has no root node - right-click the canvas to add one" } );
            return;
        }
        for ( const BehaviorTreeEditNode& node : _listNode )
        {
            const bool bComposite = Internal::isComposite( node._type );
            if ( bComposite && node._listChild.empty() )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "A composite needs at least one child" } );
            const bool bDecorator = BehaviorTreeNames::isDecorator( node._type );
            if ( bDecorator && node._listChild.size() != 1 )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "A decorator needs exactly one child" } );
            if ( Internal::hasTask( node._type ) )
            {
                if ( node._task.empty() )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Pick a task" } );
                else if ( BehaviorTaskRegistry::get().findTask( node._task ) == nullptr )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, string{ "No task named '" } + node._task + "' is registered - the game would not load this tree" } );
            }
            if ( Internal::hasKey( node._type ) && node._key.empty() )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Set a blackboard key" } );
            const bool bTimeNode = Internal::hasSeconds( node._type );
            if ( bTimeNode && node._seconds < 0.0f )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Seconds must not be negative" } );
            if ( node._type == BehaviorNodeType::MoveTo && node._repathDistance < 0.0f )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "The repath distance must not be negative" } );
            if ( node._type == BehaviorNodeType::Repeat && node._count < 0 )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "The repeat count must not be negative" } );
        }
    }
} // namespace sw::editor

namespace sw::editor
{
    int32 BehaviorTreeEditModel::parseNode( const XMLNode& node, int32 parentID, uint32 depth, string& outError )
    {
        using Internal = BehaviorTreeEditModelInternal;
        BehaviorNodeType type{};
        if ( BehaviorTreeNames::parse( node.getName(), type ) == false )
        {
            outError = Internal::makeNodeError( node, "is not a behavior tree node (known: " + BehaviorTreeNames::joinNodeTypeNames() + ")" );
            return -1;
        }
        const BehaviorTreeNames::AttributeList known = BehaviorTreeNames::getAttributeNames( type );
        vector<const utf8*>                    listUnknown;
        if ( XMLNameCheck::collectUnknownAttributes( node, known._ppName, known._count, listUnknown ) > 0 )
        {
            outError = Internal::makeNodeError( node, string{ "has the unknown attribute '" } + listUnknown[0] + "'" );
            return -1;
        }
        if ( depth > BehaviorTreeNames::kMaxDepth )
        {
            outError = Internal::makeNodeError( node, "is nested too deep" );
            return -1;
        }
        const bool bLeaf = BehaviorTreeNames::isLeaf( type );
        if ( bLeaf && node.findChild().isValid() )
        {
            outError = Internal::makeNodeError( node, "is a leaf and cannot have child nodes" );
            return -1;
        }

        BehaviorTreeEditNode value{};
        value._type = type;
        if ( Internal::readAttributes( node, value ) == false )
        {
            outError = Internal::makeNodeError( node, "has a missing or invalid attribute - see the log" );
            return -1;
        }

        const int32 id = addNode( parentID, type );
        if ( id < 0 )
        {
            outError = Internal::makeNodeError( node, "cannot be placed here (a leaf or a decorator that already has a child is its parent)" );
            return -1;
        }
        {
            // 읽은 값을 새 노드에 옮긴다. 번호와 부모, 자식 목록은 addNode 가 정한 대로 둔다.
            BehaviorTreeEditNode* pNew = find( id );
            value._id                  = pNew->_id;
            value._parent              = pNew->_parent;
            *pNew                      = std::move( value );
        }
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( parseNode( child, id, depth + 1, outError ) < 0 )
                return -1;
        }
        return id;
    }

    bool BehaviorTreeEditModel::parse( string_view xmlText, string& outError )
    {
        XMLDocument document;
        if ( document.parse( xmlText, "BehaviorTreeEditModel" ) == false )
        {
            outError = document.getLastError();
            return false;
        }
        const XMLNode root = document.getRoot( "BehaviorTree" );
        if ( root.isValid() == false )
        {
            outError = "The file has no <BehaviorTree> root element";
            return false;
        }
        vector<const utf8*> listUnknown;
        if ( XMLNameCheck::collectUnknownAttributes( root, nullptr, 0, listUnknown ) > 0 )
        {
            outError = string{ "<BehaviorTree> has the unknown attribute '" } + listUnknown[0] + "'";
            return false;
        }

        BehaviorTreeEditModel next;
        const XMLNode         first = root.findChild();
        if ( first.isValid() )
        {
            if ( first.findNextSibling().isValid() )
            {
                outError = "The tree has more than one root node - wrap them in a <Selector> or <Sequence>";
                return false;
            }
            if ( next.parseNode( first, -1, 0, outError ) < 0 )
                return false;
        }
        *this = std::move( next );
        outError.clear();
        return true;
    }

    void BehaviorTreeEditModel::writeNode( int32 id, const XMLNode& parentXML ) const
    {
        const BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return;
        const XMLNode xml = parentXML.appendChild( BehaviorTreeNames::toString( pNode->_type ) );
        if ( pNode->_name.empty() == false && BehaviorTreeNames::getAttributeNames( pNode->_type )._count > 0 )
        {
            // 이름 속성이 있는 종류만 쓴다(`name` 은 Selector · Sequence · Action · Condition)
            const BehaviorTreeNames::AttributeList known = BehaviorTreeNames::getAttributeNames( pNode->_type );
            for ( uint32 index = 0; index < known._count; ++index )
            {
                if ( string_view{ known._ppName[index] } == "name" )
                    xml.appendAttribute( "name", string_view{ pNode->_name } );
            }
        }
        switch ( pNode->_type )
        {
            case BehaviorNodeType::Selector:
            {
                if ( pNode->_bReactive )
                    xml.appendAttribute( "reactive", true );
                break;
            }
            case BehaviorNodeType::Repeat:
            {
                xml.appendAttribute( "count", pNode->_count );
                break;
            }
            case BehaviorNodeType::Cooldown:
            case BehaviorNodeType::TimeLimit:
            case BehaviorNodeType::Wait:
            {
                xml.appendAttribute( "seconds", pNode->_seconds );
                break;
            }
            case BehaviorNodeType::BlackboardCondition:
            {
                xml.appendAttribute( "key", string_view{ pNode->_key } );
                if ( pNode->_compare != BlackboardCompare::IsSet )
                    xml.appendAttribute( "compare", BehaviorTreeNames::toString( pNode->_compare ) );
                if ( pNode->_value != 0.0f )
                    xml.appendAttribute( "value", pNode->_value );
                if ( pNode->_abort != BehaviorAbortMode::None )
                    xml.appendAttribute( "abort", BehaviorTreeNames::toString( pNode->_abort ) );
                break;
            }
            case BehaviorNodeType::Action:
            case BehaviorNodeType::Condition:
            {
                xml.appendAttribute( "task", string_view{ pNode->_task } );
                break;
            }
            case BehaviorNodeType::MoveTo:
            {
                xml.appendAttribute( "key", string_view{ pNode->_key } );
                if ( pNode->_repathDistance != 0.5f )
                    xml.appendAttribute( "repathDistance", pNode->_repathDistance );
                break;
            }
            case BehaviorNodeType::Sequence:
            case BehaviorNodeType::Inverter:
            case BehaviorNodeType::ForceSuccess:
            {
                break;
            }
        }
        for ( const int32 childID : pNode->_listChild )
        {
            writeNode( childID, xml );
        }
    }

    string BehaviorTreeEditModel::write() const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "BehaviorTree" );
        if ( _rootID >= 0 )
            writeNode( _rootID, root );
        return document.saveToString();
    }
} // namespace sw::editor

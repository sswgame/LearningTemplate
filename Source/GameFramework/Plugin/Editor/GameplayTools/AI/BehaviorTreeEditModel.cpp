#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/AI/BehaviorTreeEditModel.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Behavior/ActionRegistry.h"
#include "Engine/Behavior/BehaviorTreeSchema.h"
#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLNameCheck.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"

namespace sw::editor
{
    namespace
    {
        struct BehaviorTreeEditModelInternal
        {
            static bool isComposite( BehaviorNodeKind type )
            {
                return type == BehaviorNodeKind::Selector || type == BehaviorNodeKind::Sequence;
            }

            /** @brief 노드 하나를 읽지 못한 이유입니다. 대상은 노드의 요소 이름입니다. */
            static Error makeNodeError( const XMLNode& node, const utf8* pReason )
            {
                return Error{ ErrorKind::InvalidArgument, pReason, node.getName() };
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
        _baseID.clear();
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

    expected<int32> BehaviorTreeEditModel::addNode( int32 parentID, BehaviorNodeKind type )
    {
        if ( parentID < 0 )
        {
            if ( _rootID >= 0 )
                return unexpected( Error{ ErrorKind::AlreadyExists, "the tree already has a root" } );
        }
        else
        {
            const BehaviorTreeEditNode* pParent = find( parentID );
            if ( pParent == nullptr || canHoldChild( *pParent ) == false )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the parent does not exist or cannot take a child" } );
            if ( getDepth( parentID ) + 1 > BehaviorTreeNames::kMaxDepth )
                return unexpected( Error{ ErrorKind::InvalidArgument, "the node would be nested too deep" } );
        }

        BehaviorTreeEditNode node{};
        node._id     = _nextID++;
        node._parent = parentID;
        BehaviorTreeXML::resetNodeFields( type, node );
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
        const BehaviorActionParams* pAction = pNode->_params.find<BehaviorActionParams>();
        if ( pAction != nullptr && pAction->_task.empty() == false )
            return pAction->_task;
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
            const auto addConditionIssue = [&]( const string& text )
            {
                ConditionExpr expr;
                if ( text.empty() )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Write a condition" } );
                else if ( const expected<void> compiled = ConditionExpr::tryCompile( text, expr ); compiled.has_value() == false )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, string( compiled.error().detail().data(), compiled.error().detail().size() ) } );
            };
            if ( const BehaviorActionParams* pAction = node._params.find<BehaviorActionParams>(); pAction != nullptr )
            {
                if ( pAction->_task.empty() )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Pick a task" } );
                else if ( ActionRegistry::find( pAction->_task ) == nullptr )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, string{ "No action named '" } + pAction->_task + "' is registered - the game would not load this tree" } );
            }
            if ( const BehaviorConditionParams* pCondition = node._params.find<BehaviorConditionParams>(); pCondition != nullptr )
                addConditionIssue( pCondition->_test );
            if ( const BehaviorConditionDecoratorParams* pDecorator = node._params.find<BehaviorConditionDecoratorParams>(); pDecorator != nullptr )
                addConditionIssue( pDecorator->_test );
            if ( const BehaviorMoveToParams* pMove = node._params.find<BehaviorMoveToParams>(); pMove != nullptr )
            {
                if ( pMove->_key.empty() )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Set a blackboard key" } );
                if ( pMove->_repathDistance < 0.0f )
                    outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "The repath distance must not be negative" } );
            }
            if ( const BehaviorTimedParams* pTimed = node._params.find<BehaviorTimedParams>(); pTimed != nullptr && pTimed->_seconds < 0.0f )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "Seconds must not be negative" } );
            if ( const BehaviorRepeatParams* pRepeat = node._params.find<BehaviorRepeatParams>(); pRepeat != nullptr && pRepeat->_count < 0 )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "The repeat count must not be negative" } );
            if ( const BehaviorSubTreeParams* pSubTree = node._params.find<BehaviorSubTreeParams>(); pSubTree != nullptr && pSubTree->_tree.empty() == pSubTree->_slot.empty() )
                outListIssue.push_back( BehaviorTreeEditIssue{ node._id, "A SubTree needs exactly one of tree / slot" } );
        }
    }
} // namespace sw::editor

namespace sw::editor
{
    expected<int32> BehaviorTreeEditModel::loadNodeFromXML( const XMLNode& node, int32 parentID, uint32 depth )
    {
        using Internal = BehaviorTreeEditModelInternal;
        BehaviorNodeKind type{};
        if ( BehaviorTreeNames::parse( node.getName(), type ) == false )
        {
            Error error = Internal::makeNodeError( node, "Not a behavior tree node" );
            error.appendDetail( " (known: " ).appendDetail( BehaviorTreeNames::joinNodeTypeNames() ).appendDetail( ")" );
            return unexpected( error );
        }
        if ( depth > BehaviorTreeNames::kMaxDepth )
            return unexpected( Internal::makeNodeError( node, "The node is nested too deep" ) );
        const bool bLeaf = BehaviorTreeNames::isLeaf( type );
        if ( bLeaf && node.findChild().isValid() )
            return unexpected( Internal::makeNodeError( node, "A leaf node cannot have child nodes" ) );

        BehaviorTreeEditNode value{};
        if ( BehaviorTreeXML::readNodeFields( node, type, "BehaviorTreeEditModel", value ) == false )
            return unexpected( Internal::makeNodeError( node, "The node has a missing, unknown or invalid attribute - see the log" ) );

        const expected<int32> added = addNode( parentID, type );
        if ( added.has_value() == false )
            return unexpected( Internal::makeNodeError( node, "The node cannot be placed here (a leaf or a decorator that already has a child is its parent)" ) );
        const int32 id = *added;
        {
            // 읽은 값을 새 노드에 옮긴다. 번호와 부모, 자식 목록은 addNode 가 정한 대로 둔다.
            BehaviorTreeEditNode* pNew = find( id );
            value._id                  = pNew->_id;
            value._parent              = pNew->_parent;
            *pNew                      = std::move( value );
        }
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( const expected<int32> loaded = loadNodeFromXML( child, id, depth + 1 ); loaded.has_value() == false )
                return loaded;
        }
        return id;
    }

    expected<void> BehaviorTreeEditModel::parse( string_view xmlText )
    {
        // `_base` 를 펼쳐 연다. 부모 id 는 들고 있다가 저장이 부모와의 차이로 쓴다(`makeSaveText`).
        string flatText;
        bool   bComposed = false; // 펼치지 않으면 flatText 는 빈 채로 남는다
        if ( ResourceComposer::compose( xmlText, "BehaviorTreeEditModel", ResourceContext::forSchema( &BehaviorTreeSchema::make ), flatText, bComposed ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Its _base chain could not be flattened - see the log" } );
        XMLDocument document;
        if ( document.parse( bComposed ? string_view{ flatText } : xmlText, "BehaviorTreeEditModel" ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The XML could not be read", document.getLastError() } );
        const XMLNode root = document.getRoot( "BehaviorTree" );
        if ( root.isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The file has no <BehaviorTree> root element" } );
        vector<const utf8*> listUnknown;
        if ( XMLNameCheck::collectUnknownAttributes( root, nullptr, 0, listUnknown ) > 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "<BehaviorTree> has an unknown attribute", listUnknown[0] } );

        BehaviorTreeEditModel next;
        const XMLNode         first = root.findChild();
        if ( first.isValid() )
        {
            if ( first.findNextSibling().isValid() )
                return unexpected( Error{ ErrorKind::InvalidArgument, "The tree has more than one root node - wrap them in a <Selector> or <Sequence>" } );
            if ( const expected<int32> loaded = next.loadNodeFromXML( first, -1, 0 ); loaded.has_value() == false )
                return unexpected( loaded.error() );
        }
        next._baseID = DataInheritance::findBasePath( xmlText );
        *this        = std::move( next );
        return {};
    }

    expected<string> BehaviorTreeEditModel::makeSaveText() const
    {
        const string fullText = write();
        if ( _baseID.empty() )
            return fullText;
        string                parentText;
        BehaviorTreeEditModel parent;
        if ( AssetIO::readTextAsset( AssetID{ _baseID }, parentText ) == false || parent.parse( parentText ).has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "The base tree could not be read - not saved", _baseID } );
        string               deltaText;
        const expected<void> delta = ResourceComposer::makeCheckedDelta( fullText, parent.write(), BehaviorTreeSchema::make(), _baseID, []( const string& text )
        {
            BehaviorTreeEditModel reread;
            return reread.parse( text ).has_value() ? reread.write() : string{};
        }, deltaText );
        if ( delta.has_value() == false )
            return unexpected( Error{ ErrorKind::Unsupported, "Not saved: the edit cannot be written as a difference from the base (or a node without an id changed)", delta.error().detail() } );
        return deltaText;
    }

    void BehaviorTreeEditModel::writeNode( int32 id, const XMLNode& parentXML ) const
    {
        const BehaviorTreeEditNode* pNode = find( id );
        if ( pNode == nullptr )
            return;
        const XMLNode xml = parentXML.appendChild( BehaviorTreeNames::toString( pNode->_type ) );
        BehaviorTreeXML::writeNodeFields( xml, *pNode );
        for ( const int32 childID : pNode->_listChild )
        {
            writeNode( childID, xml );
        }
    }

    string BehaviorTreeEditModel::writeSubtree( int32 id ) const
    {
        XMLDocument   document;
        const XMLNode root = document.appendRoot( "BehaviorTree" );
        if ( find( id ) != nullptr )
            writeNode( id, root );
        return document.saveToString();
    }

    int32 BehaviorTreeEditModel::graftSubtree( const BehaviorTreeEditModel& source, int32 sourceID, int32 parentID )
    {
        const BehaviorTreeEditNode* pSource = source.find( sourceID );
        BehaviorTreeEditNode        copy    = *pSource;
        copy._id                            = _nextID++;
        copy._parent                        = parentID;
        copy._listChild.clear();
        const int32 newID = copy._id;
        _listNode.push_back( std::move( copy ) );
        for ( const int32 childID : pSource->_listChild )
        {
            const int32 newChildID = graftSubtree( source, childID, newID );
            find( newID )->_listChild.push_back( newChildID ); // push_back 이 포인터를 무효로 하므로 매번 다시 찾는다
        }
        return newID;
    }

    expected<int32> BehaviorTreeEditModel::pasteSubtree( int32 parentID, string_view xmlText, int32 insertIndex )
    {
        BehaviorTreeEditModel pasted;
        if ( const expected<void> parsed = pasted.parse( xmlText ); parsed.has_value() == false )
            return unexpected( parsed.error() );
        if ( pasted._rootID < 0 )
            return unexpected( Error{ ErrorKind::InvalidArgument, "There is no node to paste" } );
        if ( parentID < 0 )
        {
            if ( _rootID >= 0 )
                return unexpected( Error{ ErrorKind::AlreadyExists, "The tree already has a root - select the parent node to paste under" } );
            _rootID = graftSubtree( pasted, pasted._rootID, -1 );
            return _rootID;
        }
        const BehaviorTreeEditNode* pParent = find( parentID );
        if ( pParent == nullptr || canHoldChild( *pParent ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "That node cannot take a child here (a leaf, or a decorator that already has one)" } );
        if ( getDepth( parentID ) + 1 + pasted.getSubtreeHeight( pasted._rootID ) > BehaviorTreeNames::kMaxDepth )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Pasting there would pass the depth limit" } );
        const int32    newID     = graftSubtree( pasted, pasted._rootID, parentID );
        vector<int32>& listChild = find( parentID )->_listChild;
        const bool     bAppend   = insertIndex < 0 || static_cast<size_t>( insertIndex ) >= listChild.size();
        listChild.insert( bAppend ? listChild.end() : listChild.begin() + insertIndex, newID );
        return newID;
    }

    expected<int32> BehaviorTreeEditModel::duplicateSubtree( int32 id )
    {
        const BehaviorTreeEditNode* pNode   = find( id );
        const BehaviorTreeEditNode* pParent = pNode != nullptr ? find( pNode->_parent ) : nullptr;
        if ( pParent == nullptr )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The root cannot be duplicated - a tree has one root" } );
        const int32 parentID = pParent->_id;
        int32       index    = 0;
        while ( pParent->_listChild[static_cast<size_t>( index )] != id )
        {
            ++index;
        }
        return pasteSubtree( parentID, writeSubtree( id ), index + 1 );
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

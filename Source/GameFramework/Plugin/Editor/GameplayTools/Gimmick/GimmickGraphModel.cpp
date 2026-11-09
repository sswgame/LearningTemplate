#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Gimmick/GimmickGraphModel.h"

#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Serialization/Format/XMLSerializer.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuit.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuitDef.h"

namespace sw::editor
{
    namespace
    {
        struct GimmickGraphModelInternal
        {
            static constexpr float32 kColumnWidth = 260.0f; ///< 층 사이 가로 간격(96 DPI 기준 캔버스 좌표)
            static constexpr float32 kRowHeight   = 150.0f;

            static bool isSamePort( string_view lhs, string_view rhs ) { return StringUtil::equals( lhs, rhs, true ); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    GimmickGraphModel::GimmickGraphModel( const GimmickNodeRegistry& registry )
        : _pRegistry{ &registry }
        , _listNode{}
        , _listWire{}
        , _listOrphanWire{}
        , _listComment{}
        , _nextNodeGraphID{ 1 }
        , _nextWireGraphID{ 1 }
    {
    }

    void GimmickGraphModel::clear()
    {
        _listNode.clear();
        _listWire.clear();
        _listOrphanWire.clear();
        _listComment.clear();
        _nextNodeGraphID = 1;
        _nextWireGraphID = 1;
    }

    void GimmickGraphModel::populate( const vector<GimmickNodeDesc>& listNode, const vector<GameObjectHandle>& listTarget, const vector<GimmickWireDesc>& listWire,
                                      const vector<GraphCommentBox>& listComment )
    {
        clear();
        _listComment = listComment;
        bool bPlaced = false;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            GimmickGraphNode node;
            node._id       = listNode[index]._id;
            node._kind     = listNode[index]._kind;
            node._params   = listNode[index]._params;
            node._target   = index < listTarget.size() ? listTarget[index] : GameObjectHandle{};
            node._position = listNode[index]._editorPosition;
            node._graphID  = _nextNodeGraphID++;
            bPlaced        = bPlaced || node._position._x != 0.0f || node._position._y != 0.0f;
            _listNode.push_back( std::move( node ) );
        }
        for ( const GimmickWireDesc& desc : listWire )
        {
            hashed_string          fromNode;
            hashed_string          fromPort;
            hashed_string          toNode;
            hashed_string          toPort;
            const bool             bParsed   = GimmickCircuitDef::splitPortReference( desc._from, fromNode, fromPort ) && GimmickCircuitDef::splitPortReference( desc._to, toNode, toPort );
            const optional<uint32> fromIndex = bParsed ? findNodeIndexByID( fromNode ) : nullopt;
            const optional<uint32> toIndex   = bParsed ? findNodeIndexByID( toNode ) : nullopt;
            if ( fromIndex.has_value() == false || toIndex.has_value() == false )
            {
                _listOrphanWire.push_back( desc ); // 끝을 못 찾은 배선은 버리지 않고 고아로 들고 있다가 그대로 내보낸다
                continue;
            }
            GimmickGraphWire wire;
            wire._fromNode = _listNode[*fromIndex]._graphID;
            wire._fromPort = fromPort.c_str();
            wire._toNode   = _listNode[*toIndex]._graphID;
            wire._toPort   = toPort.c_str();
            wire._bInvert  = desc._bInvert;
            wire._graphID  = _nextWireGraphID++;
            _listWire.push_back( std::move( wire ) );
        }
        if ( bPlaced == false )
            autoLayout();
    }

    string GimmickGraphModel::makePortReference( const GimmickGraphNode& node, string_view port ) const
    {
        string text = node._id.c_str();
        text += ".";
        text.append( port.data(), port.size() );
        return text;
    }

    void GimmickGraphModel::exportTo( vector<GimmickNodeDesc>& outListNode, vector<GameObjectHandle>& outListTarget, vector<GimmickWireDesc>& outListWire,
                                      vector<GraphCommentBox>& outListComment ) const
    {
        outListComment = _listComment;
        outListNode.clear();
        outListTarget.clear();
        outListWire.clear();
        for ( const GimmickGraphNode& node : _listNode )
        {
            GimmickNodeDesc desc;
            desc._id             = node._id;
            desc._kind           = node._kind;
            desc._params         = node._params;
            desc._editorPosition = node._position;
            outListNode.push_back( std::move( desc ) );
            outListTarget.push_back( node._target );
        }
        for ( const GimmickGraphWire& wire : _listWire )
        {
            // 노드를 지울 때 배선도 지우므로 두 끝은 늘 있다.
            const uint32    fromIndex = findNodeIndex( wire._fromNode ).value();
            const uint32    toIndex   = findNodeIndex( wire._toNode ).value();
            GimmickWireDesc desc;
            desc._from    = makePortReference( _listNode[fromIndex], wire._fromPort );
            desc._to      = makePortReference( _listNode[toIndex], wire._toPort );
            desc._bInvert = wire._bInvert;
            outListWire.push_back( std::move( desc ) );
        }
        for ( const GimmickWireDesc& orphan : _listOrphanWire )
        {
            outListWire.push_back( orphan );
        }
    }

    uint64 GimmickGraphModel::computeSignature() const
    {
        vector<GimmickNodeDesc>  listNode;
        vector<GameObjectHandle> listTarget;
        vector<GimmickWireDesc>  listWire;
        vector<GraphCommentBox>  listComment;
        exportTo( listNode, listTarget, listWire, listComment );
        // 항목은 리플렉션 직렬화로 글이 된다 — PROPERTY 를 더하면 서명이 저절로 따라간다(손으로 늘어놓은 칸은 빠뜨리면 되돌리기를 놓친다).
        StringBuilder<constant::buffer::kMax1024> text;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            text.append( string_view{ XMLSerializer::serialize( &listNode[index], *GimmickNodeDesc::StaticType() ) } ).append( listTarget[index].objectID() );
        }
        for ( const GimmickWireDesc& wire : listWire )
        {
            text.append( string_view{ XMLSerializer::serialize( &wire, *GimmickWireDesc::StaticType() ) } );
        }
        for ( const GraphCommentBox& comment : listComment )
        {
            text.append( string_view{ XMLSerializer::serialize( &comment, *GraphCommentBox::StaticType() ) } );
        }
        return HashUtil::computeStringHash64( text.c_str(), text.size(), false );
    }

    optional<uint32> GimmickGraphModel::findNodeIndex( int32 graphID ) const
    {
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._graphID == graphID )
                return static_cast<uint32>( index );
        }
        return nullopt;
    }

    optional<uint32> GimmickGraphModel::findNodeIndexByID( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._id == id )
                return static_cast<uint32>( index );
        }
        return nullopt;
    }

    optional<uint32> GimmickGraphModel::findWireIndex( int32 wireGraphID ) const
    {
        for ( size_t index = 0; index < _listWire.size(); ++index )
        {
            if ( _listWire[index]._graphID == wireGraphID )
                return static_cast<uint32>( index );
        }
        return nullopt;
    }

    const GimmickNodeKind* GimmickGraphModel::findKind( const GimmickGraphNode& node ) const
    {
        return _pRegistry->findKind( node._kind );
    }

    string GimmickGraphModel::makeUniqueID( const hashed_string& kind ) const
    {
        string base = kind.c_str();
        if ( base.empty() == false )
            base[0] = static_cast<utf8>( StringUtil::toLowerChar( base[0] ) );
        for ( int32 serial = 1;; ++serial )
        {
            StringBuilder<constant::buffer::kMax128> candidate;
            candidate.appendFormat( "%#_%#", base, serial );
            if ( findNodeIndexByID( hashed_string( candidate.view() ) ).has_value() == false )
                return string( candidate.view() );
        }
    }

    int32 GimmickGraphModel::addNode( const hashed_string& kind )
    {
        if ( _pRegistry->findKind( kind ) == nullptr )
            return kNoGraphID;
        GimmickGraphNode node;
        node._id      = hashed_string( makeUniqueID( kind ) );
        node._kind    = kind;
        node._graphID = _nextNodeGraphID++;
        _listNode.push_back( std::move( node ) );
        return _listNode.back()._graphID;
    }

    vector<int32> GimmickGraphModel::duplicateNodes( const vector<int32>& listGraphID, const float2& offset )
    {
        vector<int32> listNew;
        vector<int32> listSource;
        for ( const int32 graphID : listGraphID )
        {
            const optional<uint32> index = findNodeIndex( graphID );
            if ( index.has_value() == false )
                continue;                                      // 고른 뒤 지운 노드
            const GimmickGraphNode source = _listNode[*index]; // addNode 가 목록을 늘리기 전에 값으로 둔다
            const int32            newID  = addNode( source._kind );
            if ( newID == kNoGraphID )
                continue;
            GimmickGraphNode& copy = _listNode.back();
            copy._params           = source._params;
            copy._target           = source._target;
            copy._position         = source._position + offset;
            copy._bPlaced          = false;
            listSource.push_back( graphID );
            listNew.push_back( newID );
        }
        const auto mapID = [&listSource, &listNew]( int32 graphID )
        {
            for ( size_t index = 0; index < listSource.size(); ++index )
            {
                if ( listSource[index] == graphID )
                    return listNew[index];
            }
            return kNoGraphID;
        };
        const vector<GimmickGraphWire> listWire = _listWire; // addWire 가 목록을 늘린다
        for ( const GimmickGraphWire& wire : listWire )
        {
            const int32 fromNode = mapID( wire._fromNode );
            const int32 toNode   = mapID( wire._toNode );
            if ( fromNode == kNoGraphID || toNode == kNoGraphID )
                continue;
            const expected<int32> wireID = addWire( fromNode, wire._fromPort, toNode, wire._toPort ); // 원본에 있던 배선이라 실패는 없다 — 있으면 그 배선만 빠진다
            if ( wireID.has_value() && wire._bInvert )
                (void)setWireInvert( *wireID, true ); // 방금 만든 배선이라 늘 있다
        }
        return listNew;
    }

    bool GimmickGraphModel::removeNode( int32 graphID )
    {
        const optional<uint32> index = findNodeIndex( graphID );
        if ( index.has_value() == false )
            return false;
        _listNode.erase( _listNode.begin() + static_cast<ptrdiff_t>( *index ) );
        for ( size_t wireIndex = _listWire.size(); wireIndex > 0; --wireIndex )
        {
            const GimmickGraphWire& wire = _listWire[wireIndex - 1];
            if ( wire._fromNode == graphID || wire._toNode == graphID )
                _listWire.erase( _listWire.begin() + static_cast<ptrdiff_t>( wireIndex - 1 ) );
        }
        return true;
    }

    expected<void> GimmickGraphModel::renameNode( int32 graphID, const hashed_string& newID )
    {
        const optional<uint32> index = findNodeIndex( graphID );
        if ( index.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "No such node" } );
        if ( newID.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Node id is empty" } );
        const optional<uint32> other = findNodeIndexByID( newID );
        if ( other.has_value() && *other != *index )
            return unexpected( Error{ ErrorKind::AlreadyExists, "Node id is already used by another node", newID.view() } );
        _listNode[*index]._id = newID;
        return {};
    }

    bool GimmickGraphModel::setNodeParam( int32 graphID, string_view name, string_view value )
    {
        const optional<uint32> index = findNodeIndex( graphID );
        if ( index.has_value() == false || name.empty() )
            return false;
        GimmickGraphNode& node = _listNode[*index];
        vector<string>    listName;
        vector<string>    listValue;
        parseParams( node._params, listName, listValue );
        int32 found = -1;
        for ( size_t paramIndex = 0; paramIndex < listName.size(); ++paramIndex )
        {
            if ( StringUtil::equals( listName[paramIndex], name, true ) )
            {
                found = static_cast<int32>( paramIndex );
                break;
            }
        }
        if ( value.empty() )
        {
            if ( found < 0 )
                return true;
            listName.erase( listName.begin() + found );
            listValue.erase( listValue.begin() + found );
        }
        else if ( found >= 0 )
        {
            listValue[static_cast<size_t>( found )] = string( value );
        }
        else
        {
            listName.push_back( string( name ) );
            listValue.push_back( string( value ) );
        }
        node._params = formatParams( listName, listValue );
        return true;
    }

    bool GimmickGraphModel::setNodeTarget( int32 graphID, GameObjectHandle target )
    {
        const optional<uint32> index = findNodeIndex( graphID );
        if ( index.has_value() == false )
            return false;
        _listNode[*index]._target = target;
        return true;
    }

    expected<int32> GimmickGraphModel::addWire( int32 fromNode, string_view fromPort, int32 toNode, string_view toPort )
    {
        const optional<uint32> foundFrom = findNodeIndex( fromNode );
        const optional<uint32> foundTo   = findNodeIndex( toNode );
        if ( foundFrom.has_value() == false || foundTo.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "No such node" } );
        const uint32           fromIndex = *foundFrom;
        const uint32           toIndex   = *foundTo;
        const GimmickNodeKind* pFromKind = findKind( _listNode[fromIndex] );
        const GimmickNodeKind* pToKind   = findKind( _listNode[toIndex] );
        if ( pFromKind == nullptr || pToKind == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "The node kind is unknown" } );
        if ( pFromKind->findOutput( hashed_string( fromPort ) ).has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The first end is not an output port of its node", fromPort } );
        if ( pToKind->findInput( hashed_string( toPort ) ).has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The second end is not an input port of its node", toPort } );
        for ( const GimmickGraphWire& wire : _listWire )
        {
            const bool bWireExists = wire._fromNode == fromNode && wire._toNode == toNode && GimmickGraphModelInternal::isSamePort( wire._fromPort, fromPort ) &&
                                     GimmickGraphModelInternal::isSamePort( wire._toPort, toPort );
            if ( bWireExists )
                return unexpected( Error{ ErrorKind::AlreadyExists, "The wire already exists" } );
        }
        GimmickGraphWire wire;
        wire._fromNode      = fromNode;
        wire._fromPort      = string( fromPort );
        wire._toNode        = toNode;
        wire._toPort        = string( toPort );
        wire._graphID       = _nextWireGraphID++;
        const int32 graphID = wire._graphID;
        _listWire.push_back( std::move( wire ) );
        return graphID;
    }

    bool GimmickGraphModel::removeWire( int32 wireGraphID )
    {
        const optional<uint32> index = findWireIndex( wireGraphID );
        if ( index.has_value() == false )
            return false;
        _listWire.erase( _listWire.begin() + static_cast<ptrdiff_t>( *index ) );
        return true;
    }

    bool GimmickGraphModel::setWireInvert( int32 wireGraphID, bool bInvert )
    {
        const optional<uint32> index = findWireIndex( wireGraphID );
        if ( index.has_value() == false )
            return false;
        _listWire[*index]._bInvert = bInvert;
        return true;
    }

    int32 GimmickGraphModel::removeOrphanWires()
    {
        const int32 count = static_cast<int32>( _listOrphanWire.size() );
        _listOrphanWire.clear();
        return count;
    }

    void GimmickGraphModel::autoLayout()
    {
        const size_t  nodeCount = _listNode.size();
        vector<int32> listLevel( nodeCount, 0 );
        // 층 = 출력에서 입력으로 가는 가장 긴 길이. 순환은 노드 수만큼만 돌아서 끊는다
        for ( size_t pass = 0; pass < nodeCount; ++pass )
        {
            bool bChanged = false;
            for ( const GimmickGraphWire& wire : _listWire )
            {
                // 노드를 지울 때 배선도 지우므로 두 끝은 늘 있다.
                const uint32 fromIndex = findNodeIndex( wire._fromNode ).value();
                const uint32 toIndex   = findNodeIndex( wire._toNode ).value();
                if ( fromIndex == toIndex )
                    continue;
                const int32 wanted = listLevel[fromIndex] + 1;
                if ( listLevel[toIndex] < wanted && wanted < static_cast<int32>( nodeCount ) )
                {
                    listLevel[toIndex] = wanted;
                    bChanged           = true;
                }
            }
            if ( bChanged == false )
                break;
        }
        vector<int32> listRowOfLevel;
        for ( size_t index = 0; index < nodeCount; ++index )
        {
            const size_t level = static_cast<size_t>( listLevel[index] );
            if ( listRowOfLevel.size() <= level )
                listRowOfLevel.resize( level + 1, 0 );
            GimmickGraphNode& node = _listNode[index];
            node._position         = float2{ static_cast<float32>( level ) * GimmickGraphModelInternal::kColumnWidth,
                                     static_cast<float32>( listRowOfLevel[level] ) * GimmickGraphModelInternal::kRowHeight };
            node._bPlaced          = false;
            ++listRowOfLevel[level];
        }
    }

    string GimmickGraphModel::readParam( string_view params, string_view name )
    {
        vector<string> listName;
        vector<string> listValue;
        parseParams( params, listName, listValue );
        for ( size_t index = 0; index < listName.size(); ++index )
        {
            if ( StringUtil::equals( listName[index], name, true ) )
                return listValue[index];
        }
        return string();
    }

    void GimmickGraphModel::parseParams( string_view params, vector<string>& outListName, vector<string>& outListValue )
    {
        outListName.clear();
        outListValue.clear();
        size_t start = 0;
        while ( start < params.size() )
        {
            size_t end = params.find( ';', start );
            if ( end == string_view::npos )
                end = params.size();
            const string_view pair = StringUtil::trim( params.substr( start, end - start ) );
            start                  = end + 1;
            if ( pair.empty() )
                continue;
            string_view name;
            string_view value;
            if ( StringUtil::trySplitKeyValue( pair, name, value ) == false )
                continue; // 이름 없는 조각은 검증이 알린다(`collectIssues`)
            outListName.push_back( string( name ) );
            outListValue.push_back( string( value ) );
        }
    }

    string GimmickGraphModel::formatParams( const vector<string>& listName, const vector<string>& listValue )
    {
        string result;
        for ( size_t index = 0; index < listName.size() && index < listValue.size(); ++index )
        {
            if ( index > 0 )
                result += "; ";
            result += listName[index];
            result += "=";
            result += listValue[index];
        }
        return result;
    }

    void GimmickGraphModel::exportDef( GimmickCircuitDef& outDef, vector<string>& outListError ) const
    {
        outDef.clear();
        outDef.setSourceName( "GimmickGraph" );
        for ( const GimmickGraphNode& node : _listNode )
        {
            GimmickNodeDef& nodeDef = outDef.addNode( node._id, node._kind );
            // 컴포넌트의 `makeDef` 와 같은 규칙: "name=value; name=value", 이름 없는 조각은 오류
            size_t start = 0;
            while ( start < node._params.size() )
            {
                size_t end = node._params.find( ';', start );
                if ( end == string::npos )
                    end = node._params.size();
                const string_view pair = StringUtil::trim( string_view( node._params ).substr( start, end - start ) );
                start                  = end + 1;
                if ( pair.empty() )
                    continue;
                string_view name;
                string_view value;
                if ( StringUtil::trySplitKeyValue( pair, name, value ) == false )
                {
                    outListError.push_back( string( "node '" ) + node._id.c_str() + "' has a parameter without name=value: '" + string( pair ) + "'" );
                    continue;
                }
                nodeDef.setParam( hashed_string( name ), value );
            }
        }
        for ( const GimmickGraphWire& wire : _listWire )
        {
            // 노드를 지울 때 배선도 지우므로 두 끝은 늘 있다.
            const uint32 fromIndex = findNodeIndex( wire._fromNode ).value();
            const uint32 toIndex   = findNodeIndex( wire._toNode ).value();
            (void)outDef.addWire( makePortReference( _listNode[fromIndex], wire._fromPort ), makePortReference( _listNode[toIndex], wire._toPort ),
                                  wire._bInvert ); // 둘 다 "node.port" 모양이라 실패하지 않는다
        }
    }

    void GimmickGraphModel::collectIssues( vector<GimmickGraphIssue>& outListIssue ) const
    {
        outListIssue.clear();
        GimmickCircuitDef def;
        vector<string>    listError;
        exportDef( def, listError );
        (void)GimmickCircuit::validate( def, *_pRegistry, listError ); // 문제는 listError 에 모인다
        for ( const string& error : listError )
        {
            GimmickGraphIssue issue;
            issue._message = error;
            for ( const GimmickGraphNode& node : _listNode )
            {
                const string quoted = string( "'" ) + node._id.c_str() + "'";
                if ( error.find( quoted ) != string::npos )
                {
                    issue._nodeGraphID = node._graphID;
                    break;
                }
            }
            outListIssue.push_back( std::move( issue ) );
        }
        for ( const GimmickWireDesc& orphan : _listOrphanWire )
        {
            GimmickGraphIssue issue;
            issue._message = string( "wire '" ) + orphan._from + "' -> '" + orphan._to + "' points at a node that does not exist";
            outListIssue.push_back( std::move( issue ) );
        }
    }

    float2 GimmickGraphModel::computeNewNodePosition() const
    {
        constexpr float32 kRowHeight = 150.0f;
        float32           lowestY    = -kRowHeight;
        for ( const GimmickGraphNode& node : _listNode )
        {
            lowestY = node._position._y > lowestY ? node._position._y : lowestY;
        }
        return float2{ 0.0f, lowestY + kRowHeight };
    }
} // namespace sw::editor

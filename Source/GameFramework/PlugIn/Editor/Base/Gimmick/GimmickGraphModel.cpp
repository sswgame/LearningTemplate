#include "pch.h"

#include "GameFramework/PlugIn/Editor/Base/Gimmick/GimmickGraphModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuit.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitDef.h"

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
        , _nextNodeGraphID{ 1 }
        , _nextWireGraphID{ 1 }
    {
    }

    void GimmickGraphModel::clear()
    {
        _listNode.clear();
        _listWire.clear();
        _listOrphanWire.clear();
        _nextNodeGraphID = 1;
        _nextWireGraphID = 1;
    }

    void GimmickGraphModel::populate( const vector<GimmickNodeDesc>& listNode, const vector<GameObjectHandle>& listTarget, const vector<GimmickWireDesc>& listWire )
    {
        clear();
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            GimmickGraphNode node;
            node._id      = listNode[index]._id;
            node._kind    = listNode[index]._kind;
            node._params  = listNode[index]._params;
            node._target  = index < listTarget.size() ? listTarget[index] : GameObjectHandle{};
            node._graphID = _nextNodeGraphID++;
            _listNode.push_back( std::move( node ) );
        }
        for ( const GimmickWireDesc& desc : listWire )
        {
            hashed_string fromNode;
            hashed_string fromPort;
            hashed_string toNode;
            hashed_string toPort;
            const bool    bParsed   = GimmickCircuitDef::splitPortReference( desc._from, fromNode, fromPort ) && GimmickCircuitDef::splitPortReference( desc._to, toNode, toPort );
            const int32   fromIndex = bParsed ? findNodeIndexByID( fromNode ) : -1;
            const int32   toIndex   = bParsed ? findNodeIndexByID( toNode ) : -1;
            if ( fromIndex < 0 || toIndex < 0 )
            {
                _listOrphanWire.push_back( desc );
                continue;
            }
            GimmickGraphWire wire;
            wire._fromNode = _listNode[static_cast<size_t>( fromIndex )]._graphID;
            wire._fromPort = fromPort.c_str();
            wire._toNode   = _listNode[static_cast<size_t>( toIndex )]._graphID;
            wire._toPort   = toPort.c_str();
            wire._bInvert  = desc._bInvert;
            wire._graphID  = _nextWireGraphID++;
            _listWire.push_back( std::move( wire ) );
        }
        autoLayout();
    }

    string GimmickGraphModel::makePortReference( const GimmickGraphNode& node, string_view port ) const
    {
        string text = node._id.c_str();
        text += ".";
        text.append( port.data(), port.size() );
        return text;
    }

    void GimmickGraphModel::exportTo( vector<GimmickNodeDesc>& outListNode, vector<GameObjectHandle>& outListTarget, vector<GimmickWireDesc>& outListWire ) const
    {
        outListNode.clear();
        outListTarget.clear();
        outListWire.clear();
        for ( const GimmickGraphNode& node : _listNode )
        {
            GimmickNodeDesc desc;
            desc._id     = node._id;
            desc._kind   = node._kind;
            desc._params = node._params;
            outListNode.push_back( std::move( desc ) );
            outListTarget.push_back( node._target );
        }
        for ( const GimmickGraphWire& wire : _listWire )
        {
            const int32 fromIndex = findNodeIndex( wire._fromNode );
            const int32 toIndex   = findNodeIndex( wire._toNode );
            if ( fromIndex < 0 || toIndex < 0 )
                continue; // 노드를 지울 때 배선도 지우므로 닿지 않는 경우다
            GimmickWireDesc desc;
            desc._from    = makePortReference( _listNode[static_cast<size_t>( fromIndex )], wire._fromPort );
            desc._to      = makePortReference( _listNode[static_cast<size_t>( toIndex )], wire._toPort );
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
        exportTo( listNode, listTarget, listWire );
        string text;
        for ( size_t index = 0; index < listNode.size(); ++index )
        {
            text += listNode[index]._id.c_str();
            text += '|';
            text += listNode[index]._kind.c_str();
            text += '|';
            text += listNode[index]._params;
            text += '|';
            text += std::to_string( listTarget[index].objectID() ).c_str();
            text += '\n';
        }
        for ( const GimmickWireDesc& wire : listWire )
        {
            text += wire._from;
            text += '>';
            text += wire._to;
            text += wire._bInvert ? "!" : "";
            text += '\n';
        }
        return StringUtil::computeHash64( text.c_str(), text.size(), false );
    }

    int32 GimmickGraphModel::findNodeIndex( int32 graphID ) const
    {
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._graphID == graphID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 GimmickGraphModel::findNodeIndexByID( const hashed_string& id ) const
    {
        for ( size_t index = 0; index < _listNode.size(); ++index )
        {
            if ( _listNode[index]._id == id )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 GimmickGraphModel::findWireIndex( int32 wireGraphID ) const
    {
        for ( size_t index = 0; index < _listWire.size(); ++index )
        {
            if ( _listWire[index]._graphID == wireGraphID )
                return static_cast<int32>( index );
        }
        return -1;
    }

    const GimmickNodeKind* GimmickGraphModel::findKind( const GimmickGraphNode& node ) const { return _pRegistry->findKind( node._kind ); }

    string GimmickGraphModel::makeUniqueID( const hashed_string& kind ) const
    {
        string base = kind.c_str();
        if ( base.empty() == false )
            base[0] = static_cast<utf8>( StringUtil::toLowerChar( base[0] ) );
        for ( int32 serial = 1;; ++serial )
        {
            StringBuilder<constant::kMaxBuffer128> candidate;
            candidate.appendFormat( "%#_%#", base, serial );
            if ( findNodeIndexByID( hashed_string( candidate.view() ) ) < 0 )
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

    bool GimmickGraphModel::removeNode( int32 graphID )
    {
        const int32 index = findNodeIndex( graphID );
        if ( index < 0 )
            return false;
        _listNode.erase( _listNode.begin() + index );
        for ( size_t wireIndex = _listWire.size(); wireIndex > 0; --wireIndex )
        {
            const GimmickGraphWire& wire = _listWire[wireIndex - 1];
            if ( wire._fromNode == graphID || wire._toNode == graphID )
                _listWire.erase( _listWire.begin() + static_cast<ptrdiff_t>( wireIndex - 1 ) );
        }
        return true;
    }

    bool GimmickGraphModel::renameNode( int32 graphID, const hashed_string& newID, string& outError )
    {
        outError.clear();
        const int32 index = findNodeIndex( graphID );
        if ( index < 0 )
        {
            outError = "no such node";
            return false;
        }
        if ( newID.empty() )
        {
            outError = "node id is empty";
            return false;
        }
        const int32 other = findNodeIndexByID( newID );
        if ( other >= 0 && other != index )
        {
            outError = "node id is already used by another node";
            return false;
        }
        _listNode[static_cast<size_t>( index )]._id = newID;
        return true;
    }

    bool GimmickGraphModel::setNodeParam( int32 graphID, string_view name, string_view value )
    {
        const int32 index = findNodeIndex( graphID );
        if ( index < 0 || name.empty() )
            return false;
        GimmickGraphNode& node = _listNode[static_cast<size_t>( index )];
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
        const int32 index = findNodeIndex( graphID );
        if ( index < 0 )
            return false;
        _listNode[static_cast<size_t>( index )]._target = target;
        return true;
    }

    bool GimmickGraphModel::addWire( int32 fromNode, string_view fromPort, int32 toNode, string_view toPort, int32& outWireGraphID, string& outError )
    {
        outError.clear();
        outWireGraphID        = kNoGraphID;
        const int32 fromIndex = findNodeIndex( fromNode );
        const int32 toIndex   = findNodeIndex( toNode );
        if ( fromIndex < 0 || toIndex < 0 )
        {
            outError = "no such node";
            return false;
        }
        const GimmickNodeKind* pFromKind = findKind( _listNode[static_cast<size_t>( fromIndex )] );
        const GimmickNodeKind* pToKind   = findKind( _listNode[static_cast<size_t>( toIndex )] );
        if ( pFromKind == nullptr || pToKind == nullptr )
        {
            outError = "the node kind is unknown";
            return false;
        }
        if ( pFromKind->findOutput( hashed_string( fromPort ) ) < 0 )
        {
            outError = "the first end is not an output port of its node";
            return false;
        }
        if ( pToKind->findInput( hashed_string( toPort ) ) < 0 )
        {
            outError = "the second end is not an input port of its node";
            return false;
        }
        for ( const GimmickGraphWire& wire : _listWire )
        {
            if ( wire._fromNode == fromNode && wire._toNode == toNode && GimmickGraphModelInternal::isSamePort( wire._fromPort, fromPort ) &&
                 GimmickGraphModelInternal::isSamePort( wire._toPort, toPort ) )
            {
                outError = "the wire already exists";
                return false;
            }
        }
        GimmickGraphWire wire;
        wire._fromNode = fromNode;
        wire._fromPort = string( fromPort );
        wire._toNode   = toNode;
        wire._toPort   = string( toPort );
        wire._graphID  = _nextWireGraphID++;
        outWireGraphID = wire._graphID;
        _listWire.push_back( std::move( wire ) );
        return true;
    }

    bool GimmickGraphModel::removeWire( int32 wireGraphID )
    {
        const int32 index = findWireIndex( wireGraphID );
        if ( index < 0 )
            return false;
        _listWire.erase( _listWire.begin() + index );
        return true;
    }

    bool GimmickGraphModel::setWireInvert( int32 wireGraphID, bool bInvert )
    {
        const int32 index = findWireIndex( wireGraphID );
        if ( index < 0 )
            return false;
        _listWire[static_cast<size_t>( index )]._bInvert = bInvert;
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
                const int32 fromIndex = findNodeIndex( wire._fromNode );
                const int32 toIndex   = findNodeIndex( wire._toNode );
                if ( fromIndex < 0 || toIndex < 0 || fromIndex == toIndex )
                    continue;
                const int32 wanted = listLevel[static_cast<size_t>( fromIndex )] + 1;
                if ( listLevel[static_cast<size_t>( toIndex )] < wanted && wanted < static_cast<int32>( nodeCount ) )
                {
                    listLevel[static_cast<size_t>( toIndex )] = wanted;
                    bChanged                                  = true;
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
            const size_t equal = pair.find( '=' );
            if ( equal == string_view::npos || equal == 0 )
                continue; // 이름 없는 조각은 검증이 알린다(`collectIssues`)
            outListName.push_back( string( StringUtil::trim( pair.substr( 0, equal ) ) ) );
            outListValue.push_back( string( StringUtil::trim( pair.substr( equal + 1 ) ) ) );
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
                const size_t equal = pair.find( '=' );
                if ( equal == string_view::npos || equal == 0 )
                {
                    outListError.push_back( string( "node '" ) + node._id.c_str() + "' has a parameter without name=value: '" + string( pair ) + "'" );
                    continue;
                }
                nodeDef.setParam( hashed_string( StringUtil::trim( pair.substr( 0, equal ) ) ), StringUtil::trim( pair.substr( equal + 1 ) ) );
            }
        }
        for ( const GimmickGraphWire& wire : _listWire )
        {
            const int32 fromIndex = findNodeIndex( wire._fromNode );
            const int32 toIndex   = findNodeIndex( wire._toNode );
            if ( fromIndex < 0 || toIndex < 0 )
                continue;
            (void)outDef.addWire( makePortReference( _listNode[static_cast<size_t>( fromIndex )], wire._fromPort ), makePortReference( _listNode[static_cast<size_t>( toIndex )], wire._toPort ),
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
} // namespace sw::editor

#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Gimmick/GimmickCircuit.h"

#include "Core/Container/DependencyGraph.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Serialization/BitStream.h"
#include "Core/String/StringUtil.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct GimmickCircuitInternal
        {
            static string joinNames( const vector<hashed_string>& listName )
            {
                string joined;
                for ( const hashed_string& name : listName )
                {
                    if ( joined.empty() == false )
                        joined += ", ";
                    joined += name.c_str();
                }
                return joined.empty() ? string( "none" ) : joined;
            }

            /** @brief 매개변수 문자열을 종류의 형식대로 해석합니다. 숫자나 벡터를 하나도 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool parseParam( const GimmickParamSpec& spec, const string& text, GimmickParamValue& outValue )
            {
                outValue._text = text;
                if ( spec._type == GimmickParamType::Text )
                    return true;
                vector<float32> listValue;
                if ( XMLReadUtil::parseFloatList( text, 4, listValue ) == false || ( spec._type == GimmickParamType::Number && listValue.size() != 1 ) )
                    return false;
                listValue.resize( 4, 0.0f ); // 벡터는 모자란 성분이 0 이다
                outValue._vector = float4{ listValue[0], listValue[1], listValue[2], listValue[3] };
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    GimmickCircuit::GimmickCircuit()
        : _listNode{}
        , _listInputWire{}
        , _listParam{}
        , _listFloatState{}
        , _listIntState{}
        , _listOrder{}
        , _initialState{}
        , _timer{}
        , _stepIndex{ 0 }
        , _stepTime{ constant::time::kDefaultFixedStepSeconds }
        , _arrTransition{}
        , _transitionCount{ 0 }
    {
    }

    void GimmickCircuit::clear()
    {
        _listNode.clear();
        _listInputWire.clear();
        _listParam.clear();
        _listFloatState.clear();
        _listIntState.clear();
        _listOrder.clear();
        _initialState = GimmickCircuitState{};
        _timer.reset();
        _stepIndex       = 0;
        _transitionCount = 0;
    }

    bool GimmickCircuit::validate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError )
    {
        GimmickCircuit circuit;
        return circuit.populate( def, registry, outListError );
    }

    bool GimmickCircuit::populate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError )
    {
        const size_t errorBase = outListError.size();
        clear();
        _stepTime = def.getStepTime() > 0.0f ? def.getStepTime() : constant::time::kDefaultFixedStepSeconds;
        _timer    = FixedStepTimer( _stepTime, constant::time::kDefaultMaxFrameSeconds );

        // 상태가 담을 수 있는 노드 수를 넘으면 다른 검증 전에 멈춘다(노드마다 할당하지 않는다).
        const vector<GimmickNodeDef>& listNodeDef = def.getNodes();
        if ( listNodeDef.size() > GimmickCircuitState::kMaxNodeCount )
        {
            utf8 arrBuffer[constant::buffer::kMax256]{};
            formatstring( arrBuffer, constant::buffer::kMax256, "%#: %# nodes, more than the %# a circuit state holds", def.describeSource().c_str(),
                          static_cast<uint64>( listNodeDef.size() ), GimmickCircuitState::kMaxNodeCount );
            outListError.push_back( string( arrBuffer ) );
            return false;
        }
        populateNodes( def, registry, outListError );
        populateWires( def, outListError );
        initializeNodeStates( def, outListError );
        computeEvaluationOrder( def, outListError );
        if ( outListError.size() != errorBase )
        {
            clear();
            return false;
        }
        saveState( _initialState );
        return true;
    }

    void GimmickCircuit::populateNodes( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError )
    {
        using Internal                            = GimmickCircuitInternal;
        const vector<GimmickNodeDef>& listNodeDef = def.getNodes();
        _listNode.resize( listNodeDef.size() );
        for ( size_t nodeIndex = 0; nodeIndex < listNodeDef.size(); ++nodeIndex )
        {
            const GimmickNodeDef& nodeDef = listNodeDef[nodeIndex];
            NodeRuntime&          node    = _listNode[nodeIndex];
            node._id                      = nodeDef._id;
            if ( nodeDef._id.empty() )
            {
                outListError.push_back( def.describeSource() + ": node #" + std::to_string( nodeIndex ).c_str() + " has no id" );
                continue;
            }
            for ( size_t otherIndex = 0; otherIndex < nodeIndex; ++otherIndex )
            {
                if ( listNodeDef[otherIndex]._id == nodeDef._id )
                    outListError.push_back( def.describeNode( nodeDef ) + ": duplicate node id" );
            }
            node._pKind = registry.findKind( nodeDef._kind );
            if ( node._pKind == nullptr )
            {
                outListError.push_back( def.describeNode( nodeDef ) + ": unknown node kind" );
                continue;
            }
            const GimmickNodeKind& kind = *node._pKind;
            for ( const GimmickParamDef& param : nodeDef._listParam )
            {
                if ( kind.findParam( param._name ).has_value() == false )
                {
                    vector<hashed_string> listParamName;
                    for ( const GimmickParamSpec& spec : kind._listParam )
                    {
                        listParamName.push_back( spec._name );
                    }
                    outListError.push_back( def.describeNode( nodeDef ) + ": unknown parameter '" + param._name.c_str() + "' (parameters: " +
                                            Internal::joinNames( listParamName ) + ")" );
                }
            }
            node._paramStart = static_cast<uint32>( _listParam.size() );
            for ( const GimmickParamSpec& spec : kind._listParam )
            {
                const string*     pText = nodeDef.findParam( spec._name );
                GimmickParamValue value;
                if ( Internal::parseParam( spec, pText != nullptr ? *pText : spec._defaultText, value ) == false )
                    outListError.push_back( def.describeNode( nodeDef ) + ": parameter '" + spec._name.c_str() + "' is not a number: '" +
                                            ( pText != nullptr ? *pText : spec._defaultText ) + "'" );
                _listParam.push_back( value );
            }
            node._floatStart = static_cast<uint32>( _listFloatState.size() );
            node._intStart   = static_cast<uint32>( _listIntState.size() );
            _listFloatState.resize( _listFloatState.size() + kind._floatStateCount, 0.0f );
            _listIntState.resize( _listIntState.size() + kind._intStateCount, 0 );
        }
    }

    void GimmickCircuit::populateWires( const GimmickCircuitDef& def, vector<string>& outListError )
    {
        using Internal = GimmickCircuitInternal;
        vector<vector<InputWire>> listWirePerNode( _listNode.size() );
        for ( const GimmickWireDef& wire : def.getWires() )
        {
            const string where = def.describeSource() + ": wire " + wire._fromNode.c_str() + "." + wire._fromPort.c_str() + " -> " + wire._toNode.c_str() +
                                 "." + wire._toPort.c_str();
            const optional<int32> fromNode = findNode( wire._fromNode );
            const optional<int32> toNode   = findNode( wire._toNode );
            if ( fromNode.has_value() == false )
                outListError.push_back( where + ": unknown source node '" + wire._fromNode.c_str() + "'" );
            if ( toNode.has_value() == false )
                outListError.push_back( where + ": unknown target node '" + wire._toNode.c_str() + "'" );
            const GimmickNodeKind* pFromKind = fromNode.has_value() ? _listNode[static_cast<size_t>( *fromNode )]._pKind : nullptr;
            const GimmickNodeKind* pToKind   = toNode.has_value() ? _listNode[static_cast<size_t>( *toNode )]._pKind : nullptr;
            const optional<uint32> fromPort  = pFromKind != nullptr ? pFromKind->findOutput( wire._fromPort ) : nullopt;
            const optional<uint32> toPort    = pToKind != nullptr ? pToKind->findInput( wire._toPort ) : nullopt;
            if ( pFromKind != nullptr && fromPort.has_value() == false )
                outListError.push_back( where + ": unknown output '" + wire._fromPort.c_str() + "' (outputs: " + Internal::joinNames( pFromKind->_listOutput ) + ")" );
            if ( pToKind != nullptr && toPort.has_value() == false )
                outListError.push_back( where + ": unknown input '" + wire._toPort.c_str() + "' (inputs: " + Internal::joinNames( pToKind->_listInput ) + ")" );
            if ( fromPort.has_value() == false || toPort.has_value() == false )
                continue;
            InputWire input;
            input._sourceNode = static_cast<uint32>( *fromNode );
            input._sourcePort = static_cast<uint8>( *fromPort );
            input._targetPort = static_cast<uint8>( *toPort );
            input._bInvert    = wire._bInvert;
            listWirePerNode[static_cast<size_t>( *toNode )].push_back( input );
            _listNode[static_cast<size_t>( *toNode )]._connectedInputBits |= 1u << *toPort;
        }
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            node._wireStart   = static_cast<uint32>( _listInputWire.size() );
            node._wireCount   = static_cast<uint32>( listWirePerNode[nodeIndex].size() );
            _listInputWire.insert( _listInputWire.end(), listWirePerNode[nodeIndex].begin(), listWirePerNode[nodeIndex].end() );
        }
    }

    void GimmickCircuit::initializeNodeStates( const GimmickCircuitDef& def, vector<string>& outListError )
    {
        const vector<GimmickNodeDef>& listNodeDef = def.getNodes();
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind == nullptr || node._pKind->_pInitialize == nullptr )
                continue;
            string             error;
            GimmickNodeContext context = makeContext( node );
            context._pError            = &error;
            if ( node._pKind->_pInitialize( context ) == false )
                outListError.push_back( def.describeNode( listNodeDef[nodeIndex] ) + ": " + ( error.empty() ? string( "invalid parameters" ) : error ) );
            node._outputBits = context._outputBits;
        }
    }

    void GimmickCircuit::computeEvaluationOrder( const GimmickCircuitDef& def, vector<string>& outListError )
    {
        // 고리를 끊는 노드(Delay)는 앞 스텝의 입력을 쓰므로 맨 앞에 두고 간선에서 뺀다. 나머지는 배선의 원천이 먼저인 위상 순서다(준비된 것은 데이터 차례).
        const auto isEvaluated = []( const NodeRuntime& node )
        { return node._pKind != nullptr && node._pKind->canBreakCycle() == false; };
        DependencyGraph graph{ static_cast<uint32>( _listNode.size() ) };
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            const NodeRuntime& node = _listNode[nodeIndex];
            if ( isEvaluated( node ) == false )
            {
                if ( node._pKind != nullptr )
                    _listOrder.push_back( static_cast<uint32>( nodeIndex ) );
                continue;
            }
            for ( uint32 wireIndex = 0; wireIndex < node._wireCount; ++wireIndex )
            {
                const uint32 sourceIndex = _listInputWire[node._wireStart + wireIndex]._sourceNode;
                if ( isEvaluated( _listNode[sourceIndex] ) )
                    graph.addDependency( static_cast<uint32>( nodeIndex ), sourceIndex );
            }
        }
        const DependencyOrder order = graph.sortInDataOrder();
        if ( order.isSorted() == false )
        {
            string cycleNodes;
            for ( const uint32 nodeIndex : order._listCycle )
            {
                if ( cycleNodes.empty() == false )
                    cycleNodes += " -> ";
                cycleNodes += _listNode[nodeIndex]._id.c_str();
            }
            outListError.push_back( def.describeSource() + ": signal loop without a Delay through nodes " + cycleNodes );
            return;
        }
        for ( const uint32 nodeIndex : order._listOrder )
        {
            if ( isEvaluated( _listNode[nodeIndex] ) )
                _listOrder.push_back( nodeIndex );
        }
    }

    GimmickNodeContext GimmickCircuit::makeContext( NodeRuntime& node )
    {
        GimmickNodeContext context;
        context._pParam             = _listParam.data() + node._paramStart;
        context._pFloatState        = _listFloatState.data() + node._floatStart;
        context._pIntState          = _listIntState.data() + node._intStart;
        context._sensorValue        = node._sensorValue;
        context._sensorImpulse      = node._sensorImpulse;
        context._pathLength         = node._pathLength;
        context._stepTime           = _stepTime;
        context._stepIndex          = _stepIndex;
        context._previousInputBits  = node._previousInputBits;
        context._connectedInputBits = node._connectedInputBits;
        context._outputBits         = node._outputBits;
        return context;
    }

    uint32 GimmickCircuit::gatherInputs( const NodeRuntime& node ) const
    {
        uint32 bits = 0;
        for ( uint32 wireIndex = 0; wireIndex < node._wireCount; ++wireIndex )
        {
            const InputWire& wire    = _listInputWire[node._wireStart + wireIndex];
            const bool       bSource = ( _listNode[wire._sourceNode]._outputBits & ( 1u << wire._sourcePort ) ) != 0;
            if ( bSource != ( wire._bInvert == SW_TRUE ) )
                bits |= 1u << wire._targetPort;
        }
        return bits;
    }

    void GimmickCircuit::step()
    {
        for ( const uint32 nodeIndex : _listOrder )
        {
            NodeRuntime&       node     = _listNode[nodeIndex];
            const bool         bBreaker = node._pKind->canBreakCycle();
            GimmickNodeContext context  = makeContext( node );
            context._inputBits          = bBreaker ? node._previousInputBits : gatherInputs( node );
            node._pKind->_pStep( context );
            if ( context._outputBits != node._outputBits )
            {
                // 진단 기록 — 고정 용량 고리(상태 · 해시에 들지 않는다).
                _arrTransition[_transitionCount % kTransitionCapacity] = GimmickOutputTransition{ _stepIndex, static_cast<int32>( nodeIndex ), node._outputBits, context._outputBits };
                ++_transitionCount;
            }
            node._outputBits = context._outputBits;
            if ( bBreaker == false )
                node._previousInputBits = context._inputBits;
        }
        // 고리를 끊는 노드는 모든 노드가 이번 스텝을 실행한 뒤의 입력을 받아 둔다.
        for ( const uint32 nodeIndex : _listOrder )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind->canBreakCycle() == false )
                continue;
            GimmickNodeContext context = makeContext( node );
            context._inputBits         = gatherInputs( node );
            node._pKind->_pCommit( context );
            node._previousInputBits = context._inputBits;
        }
        for ( NodeRuntime& node : _listNode )
        {
            node._sensorImpulse = 0.0f;
        }
        ++_stepIndex;
    }

    void GimmickCircuit::writeDebugLines( DebugHUDSectionWriter& writer, uint32 maxLines ) const
    {
        // 켜진 비트를 포트 이름으로 — 비트 번호는 사람이 읽지 못한다.
        const auto makePortNames = []( const vector<hashed_string>& listPort, uint32 bits ) -> string
        {
            string text;
            for ( size_t port = 0; port < listPort.size() && port < 32; ++port )
            {
                if ( ( bits & ( 1u << static_cast<uint32>( port ) ) ) == 0 )
                    continue;
                if ( text.empty() == false )
                    text += ",";
                text += listPort[port].c_str();
            }
            return text.empty() ? string( "-" ) : text;
        };
        uint32 lineCount = 0;
        writer.addLine( "step", to_string( _stepIndex ) );
        for ( int32 node = 0; node < getNodeCount(); ++node )
        {
            if ( ++lineCount > maxLines )
                break;
            const GimmickNodeKind& kind  = getKind( node );
            string                 value = "out " + makePortNames( kind._listOutput, getOutputBits( node ) ) + "  in " + makePortNames( kind._listInput, getPreviousInputBits( node ) );
            if ( kind._category == GimmickNodeCategory::Sensor )
                value += "  sensor " + to_string( getSensorValue( node ) );
            if ( kind._floatStateCount > 0 )
                value += "  value " + to_string( getActuatorValue( node ) );
            writer.addLine( string( getNodeID( node ).c_str() ) + " (" + kind._name.c_str() + ")", value );
        }
        vector<GimmickOutputTransition> listTransition;
        collectRecentTransitions( listTransition );
        for ( auto it = listTransition.rbegin(); it != listTransition.rend() && ++lineCount <= maxLines; ++it )
        {
            const GimmickNodeKind& kind = getKind( it->_node );
            writer.addLine( "step " + to_string( it->_stepIndex ), string( getNodeID( it->_node ).c_str() ) + " " + makePortNames( kind._listOutput, it->_beforeBits ) + " -> " +
                                                                       makePortNames( kind._listOutput, it->_afterBits ) );
        }
    }

    void GimmickCircuit::collectRecentTransitions( vector<GimmickOutputTransition>& outListTransition ) const
    {
        outListTransition.clear();
        const uint32 count = _transitionCount < kTransitionCapacity ? _transitionCount : kTransitionCapacity;
        for ( uint32 index = _transitionCount - count; index < _transitionCount; ++index )
        {
            outListTransition.push_back( _arrTransition[index % kTransitionCapacity] );
        }
    }

    int32 GimmickCircuit::update( float32 deltaTime )
    {
        if ( isBuilt() == false )
            return 0;
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            step();
        }
        return stepCount;
    }

    int32 GimmickCircuit::consumeTime( float32 deltaTime )
    {
        return isBuilt() ? _timer.consume( deltaTime ) : 0;
    }

    void GimmickCircuit::resetToInitial()
    {
        if ( isBuilt() )
            copyState( _initialState ); // 같은 회로가 만든 상태라 노드 순서가 같다
    }

    void GimmickCircuit::setSensorValue( int32 node, float32 value )
    {
        if ( 0 <= node && node < getNodeCount() )
            _listNode[static_cast<size_t>( node )]._sensorValue = value;
    }

    void GimmickCircuit::addSensorImpulse( int32 node, float32 amount )
    {
        if ( 0 <= node && node < getNodeCount() )
            _listNode[static_cast<size_t>( node )]._sensorImpulse += amount;
    }

    void GimmickCircuit::setPathLength( int32 node, float32 length )
    {
        if ( 0 <= node && node < getNodeCount() )
            _listNode[static_cast<size_t>( node )]._pathLength = length;
    }

    optional<int32> GimmickCircuit::findNode( const hashed_string& id ) const
    {
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._id == id )
                return static_cast<int32>( nodeIndex );
        }
        return nullopt;
    }

    float32 GimmickCircuit::getActuatorValue( int32 node ) const
    {
        return getFloatState( node, 0 );
    }

    float32 GimmickCircuit::getFloatState( int32 node, int32 slot ) const
    {
        const NodeRuntime& runtime = _listNode[static_cast<size_t>( node )];
        if ( slot < 0 || slot >= static_cast<int32>( runtime._pKind->_floatStateCount ) )
            return 0.0f;
        return _listFloatState[runtime._floatStart + static_cast<uint32>( slot )];
    }

    int32 GimmickCircuit::getIntState( int32 node, int32 slot ) const
    {
        const NodeRuntime& runtime = _listNode[static_cast<size_t>( node )];
        if ( slot < 0 || slot >= static_cast<int32>( runtime._pKind->_intStateCount ) )
            return 0;
        return _listIntState[runtime._intStart + static_cast<uint32>( slot )];
    }

    const GimmickParamValue* GimmickCircuit::findParam( int32 node, const hashed_string& name ) const
    {
        const NodeRuntime&     runtime    = _listNode[static_cast<size_t>( node )];
        const optional<uint32> paramIndex = runtime._pKind->findParam( name );
        return paramIndex.has_value() ? &_listParam[runtime._paramStart + *paramIndex] : nullptr;
    }

    string GimmickCircuitDef::describeSource() const
    {
        return getSourceName().empty() ? string( "<gimmick>" ) : getSourceName();
    }

    string GimmickCircuitDef::describeNode( const GimmickNodeDef& node ) const
    {
        return describeSource() + ": node '" + node._id.c_str() + "' (" + node._kind.c_str() + ")";
    }

    float3 GimmickCircuit::readParamVector( int32 node, const utf8* pName ) const
    {
        const GimmickParamValue* pValue = findParam( node, hashed_string( pName ) );
        return pValue != nullptr ? float3{ pValue->_vector._x, pValue->_vector._y, pValue->_vector._z } : float3{};
    }

    optional<float32> GimmickCircuit::readParamNumber( int32 node, const utf8* pName ) const
    {
        const GimmickParamValue* pValue = findParam( node, hashed_string( pName ) );
        return pValue != nullptr ? optional<float32>{ pValue->_vector._x } : nullopt; // 없을 때의 값은 호출부가 정한다
    }

    const string* GimmickCircuit::readParamText( int32 node, const utf8* pName ) const
    {
        const GimmickParamValue* pValue = findParam( node, hashed_string( pName ) );
        return pValue != nullptr ? &pValue->_text : nullptr;
    }

    void GimmickCircuit::saveState( GimmickCircuitState& outState ) const
    {
        outState._stepIndex   = _stepIndex;
        outState._accumulator = _timer._accumulator;
        outState._listNode.resize( _listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            const NodeRuntime&     node  = _listNode[nodeIndex];
            const GimmickNodeKind& kind  = *node._pKind;
            GimmickNodeState&      state = outState._listNode[nodeIndex];
            state._id                    = node._id.getHash();
            state._kind                  = kind._name.getHash();
            state._outputBits            = node._outputBits;
            state._previousInputBits     = node._previousInputBits;
            state._sensorValue           = node._sensorValue;
            state._sensorImpulse         = node._sensorImpulse;
            state._stateVersion          = kind._stateVersion;
            state._listFloatState.assign( _listFloatState.begin() + node._floatStart, _listFloatState.begin() + node._floatStart + kind._floatStateCount );
            state._listIntState.assign( _listIntState.begin() + node._intStart, _listIntState.begin() + node._intStart + kind._intStateCount );
        }
    }

    void GimmickCircuit::writeNode( size_t nodeIndex, const GimmickNodeState& state )
    {
        NodeRuntime& node       = _listNode[nodeIndex];
        node._outputBits        = state._outputBits;
        node._previousInputBits = state._previousInputBits;
        node._sensorValue       = state._sensorValue;
        node._sensorImpulse     = state._sensorImpulse;
        std::copy( state._listFloatState.begin(), state._listFloatState.end(), _listFloatState.begin() + node._floatStart );
        std::copy( state._listIntState.begin(), state._listIntState.end(), _listIntState.begin() + node._intStart );
    }

    void GimmickCircuit::copyState( const GimmickCircuitState& state )
    {
        SW_ASSERT( state._listNode.size() == _listNode.size() );
        _stepIndex          = state._stepIndex;
        _timer._accumulator = state._accumulator;
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            writeNode( nodeIndex, state._listNode[nodeIndex] );
        }
    }

    GimmickStateLoad GimmickCircuit::loadState( const GimmickCircuitState& state, GimmickStateMatch weakestAccepted )
    {
        // 1) 짝짓기: 저장 노드마다 같은 id 의 회로 노드를 찾는다. 노드 순서가 그대로인 흔한 경우는 같은 번호를 먼저 본다.
        GimmickStateLoad         result;
        vector<optional<uint32>> listSavedIndex( _listNode.size() ); // 회로 노드마다 짝인 저장 노드 번호(짝이 없으면 비어 있다)
        uint32                   droppedCount = 0;
        for ( size_t savedIndex = 0; savedIndex < state._listNode.size(); ++savedIndex )
        {
            const GimmickNodeState& saved = state._listNode[savedIndex];
            optional<int32>         nodeIndex;
            const bool              bSameSlot = savedIndex < _listNode.size() && _listNode[savedIndex]._id.getHash() == saved._id;
            if ( bSameSlot )
            {
                nodeIndex = static_cast<int32>( savedIndex );
            }
            else
            {
                for ( size_t candidate = 0; candidate < _listNode.size(); ++candidate )
                {
                    if ( _listNode[candidate]._id.getHash() == saved._id )
                    {
                        nodeIndex = static_cast<int32>( candidate );
                        break;
                    }
                }
            }
            if ( nodeIndex.has_value() == false )
            {
                ++droppedCount; // 회로에서 지운 노드
                continue;
            }
            const size_t           node     = static_cast<size_t>( *nodeIndex );
            const GimmickNodeKind& kind     = *_listNode[node]._pKind;
            const bool             bChanged = saved._kind != kind._name.getHash() || saved._stateVersion != kind._stateVersion ||
                                  saved._listFloatState.size() != kind._floatStateCount || saved._listIntState.size() != kind._intStateCount;
            if ( bChanged )
            {
                result._changedNode = nodeIndex;
                return result; // Mismatch
            }
            if ( listSavedIndex[node].has_value() )
                return result; // 같은 id 가 둘인 저장 상태 — Mismatch, 바뀐 노드 없음
            listSavedIndex[node] = static_cast<uint32>( savedIndex );
        }

        // 2) 맞는 정도와 받아들이는 한계
        bool bEveryNodeSaved = droppedCount == 0;
        for ( const optional<uint32>& savedIndex : listSavedIndex )
        {
            bEveryNodeSaved = bEveryNodeSaved && savedIndex.has_value();
        }
        result._match = bEveryNodeSaved ? GimmickStateMatch::Exact : GimmickStateMatch::NodeSetChanged;
        if ( static_cast<uint8>( result._match ) > static_cast<uint8>( weakestAccepted ) )
            return result;

        // 3) 옮기기: 짝이 없는 회로 노드(새로 더한 노드)는 처음 상태다.
        if ( result._match != GimmickStateMatch::Exact )
            copyState( _initialState );
        _stepIndex          = state._stepIndex;
        _timer._accumulator = state._accumulator;
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( listSavedIndex[nodeIndex].has_value() )
                writeNode( nodeIndex, state._listNode[*listSavedIndex[nodeIndex]] );
        }
        result._bApplied = true;
        return result;
    }

    uint64 GimmickCircuit::computeStateHash() const
    {
        GimmickCircuitState state;
        saveState( state );
        BitWriter  writer;
        const bool bWritten = FixedLayout::write( state, writer );
        if ( bWritten == false )
        {
            // 개수는 populate 와 registerKind 가 막았고 누적 시간은 음수가 될 수 없다. 남는 것은 실행의 결함이다.
            SW_LOG_ERROR_ID( "GimmickCircuit.GimmickCircuitStateValue", "Gimmick circuit state has a value outside its declared range at step %#", _stepIndex );
            SW_ASSERT( bWritten );
        }
        return HashUtil::appendBytes64( HashUtil::kFNVOffset64, writer.getBytes().data(), static_cast<size_t>( writer.getByteCount() ) );
    }
} // namespace sw

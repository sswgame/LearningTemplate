#include "pch.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuit.h"

#include "Core/Common/FourCcUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Foundation/Data/GameDataXML.h"

namespace sw
{
    namespace
    {
        struct GimmickCircuitInternal
        {
            static constexpr uint32 kStateTag     = FourCcUtil::make( "GMKC" );
            static constexpr uint32 kStateVersion = 1u;

            static uint64 hashBytes( uint64 hash, const void* pData, size_t size )
            {
                const uint8* pByte = static_cast<const uint8*>( pData );
                for ( size_t index = 0; index < size; ++index )
                {
                    hash = ( hash ^ pByte[index] ) * HashUtil::kFnvPrime64;
                }
                return hash;
            }

            template <typename T>
            static void writeValue( vector<uint8>& outBytes, const T& value )
            {
                const size_t offset = outBytes.size();
                outBytes.resize( offset + sizeof( T ) );
                std::memcpy( outBytes.data() + offset, &value, sizeof( T ) );
            }

            template <typename T>
            [[nodiscard]] static bool readValue( const vector<uint8>& bytes, size_t& inoutOffset, T& outValue )
            {
                if ( inoutOffset + sizeof( T ) > bytes.size() )
                    return false;
                std::memcpy( &outValue, bytes.data() + inoutOffset, sizeof( T ) );
                inoutOffset += sizeof( T );
                return true;
            }

            static string describeSource( const GimmickCircuitDef& def ) { return def.getSourceName().empty() ? string( "<gimmick>" ) : def.getSourceName(); }

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

            static string describeNode( const GimmickCircuitDef& def, const GimmickNodeDef& node )
            {
                return describeSource( def ) + ": node '" + node._id.c_str() + "' (" + node._kind.c_str() + ")";
            }

            /** @brief 매개변수 글을 종류의 모양대로 해석합니다. 숫자 · 벡터를 하나도 읽지 못하면 false 입니다. */
            [[nodiscard]] static bool parseParam( const GimmickParamSpec& spec, const string& text, GimmickParamValue& outValue )
            {
                outValue._text = text;
                if ( spec._type == GimmickParamType::Text )
                    return true;
                float32      arrValue[4] = {};
                const uint32 count       = GameDataXML::parseFloats( text, arrValue, 4 );
                if ( count == 0 || ( spec._type == GimmickParamType::Number && count != 1 ) )
                    return false;
                outValue._vector = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
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
        , _initialStateBytes{}
        , _timer{}
        , _layoutHash{ 0 }
        , _stepIndex{ 0 }
        , _stepTime{ GimmickCircuitDef::kDefaultStepTime }
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
        _initialStateBytes.clear();
        _timer.reset();
        _layoutHash = 0;
        _stepIndex  = 0;
    }

    bool GimmickCircuit::validate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError )
    {
        GimmickCircuit circuit;
        return circuit.populate( def, registry, outListError );
    }

    bool GimmickCircuit::populate( const GimmickCircuitDef& def, const GimmickNodeRegistry& registry, vector<string>& outListError )
    {
        using Internal         = GimmickCircuitInternal;
        const size_t errorBase = outListError.size();
        clear();
        _stepTime = def.getStepTime() > 0.0f ? def.getStepTime() : GimmickCircuitDef::kDefaultStepTime;
        _timer    = FixedStepTimer( _stepTime, 0.25f );

        // 1) 노드 — id · 종류 · 매개변수
        const vector<GimmickNodeDef>& listNodeDef = def.getNodes();
        _listNode.resize( listNodeDef.size() );
        for ( size_t nodeIndex = 0; nodeIndex < listNodeDef.size(); ++nodeIndex )
        {
            const GimmickNodeDef& nodeDef = listNodeDef[nodeIndex];
            NodeRuntime&          node    = _listNode[nodeIndex];
            node._id                      = nodeDef._id;
            if ( nodeDef._id.empty() )
            {
                outListError.push_back( Internal::describeSource( def ) + ": node #" + std::to_string( nodeIndex ).c_str() + " has no id" );
                continue;
            }
            for ( size_t otherIndex = 0; otherIndex < nodeIndex; ++otherIndex )
            {
                if ( listNodeDef[otherIndex]._id == nodeDef._id )
                    outListError.push_back( Internal::describeNode( def, nodeDef ) + ": duplicate node id" );
            }
            node._pKind = registry.findKind( nodeDef._kind );
            if ( node._pKind == nullptr )
            {
                outListError.push_back( Internal::describeNode( def, nodeDef ) + ": unknown node kind" );
                continue;
            }
            const GimmickNodeKind& kind = *node._pKind;
            for ( const GimmickParamDef& param : nodeDef._listParam )
            {
                if ( kind.findParam( param._name ) < 0 )
                {
                    vector<hashed_string> listParamName;
                    for ( const GimmickParamSpec& spec : kind._listParam )
                    {
                        listParamName.push_back( spec._name );
                    }
                    outListError.push_back( Internal::describeNode( def, nodeDef ) + ": unknown parameter '" + param._name.c_str() + "' (parameters: " +
                                            Internal::joinNames( listParamName ) + ")" );
                }
            }
            node._paramStart = static_cast<uint32>( _listParam.size() );
            for ( const GimmickParamSpec& spec : kind._listParam )
            {
                const string*     pText = nodeDef.findParam( spec._name );
                GimmickParamValue value;
                if ( Internal::parseParam( spec, pText != nullptr ? *pText : spec._defaultText, value ) == false )
                    outListError.push_back( Internal::describeNode( def, nodeDef ) + ": parameter '" + spec._name.c_str() + "' is not a number: '" +
                                            ( pText != nullptr ? *pText : spec._defaultText ) + "'" );
                _listParam.push_back( value );
            }
            node._floatStart = static_cast<uint32>( _listFloatState.size() );
            node._intStart   = static_cast<uint32>( _listIntState.size() );
            _listFloatState.resize( _listFloatState.size() + kind._floatStateCount, 0.0f );
            _listIntState.resize( _listIntState.size() + kind._intStateCount, 0 );
        }

        // 2) 배선 — 출력 포트에서 입력 포트로, 받는 노드 순서로 모은다(같은 입력의 여러 배선은 OR).
        vector<vector<InputWire>> listWirePerNode( _listNode.size() );
        for ( const GimmickWireDef& wire : def.getWires() )
        {
            const string where = Internal::describeSource( def ) + ": wire " + wire._fromNode.c_str() + "." + wire._fromPort.c_str() + " -> " + wire._toNode.c_str() +
                                 "." + wire._toPort.c_str();
            const int32 fromNode = findNode( wire._fromNode );
            const int32 toNode   = findNode( wire._toNode );
            if ( fromNode < 0 )
                outListError.push_back( where + ": unknown source node '" + wire._fromNode.c_str() + "'" );
            if ( toNode < 0 )
                outListError.push_back( where + ": unknown target node '" + wire._toNode.c_str() + "'" );
            const GimmickNodeKind* pFromKind = fromNode >= 0 ? _listNode[static_cast<size_t>( fromNode )]._pKind : nullptr;
            const GimmickNodeKind* pToKind   = toNode >= 0 ? _listNode[static_cast<size_t>( toNode )]._pKind : nullptr;
            const int32            fromPort  = pFromKind != nullptr ? pFromKind->findOutput( wire._fromPort ) : -1;
            const int32            toPort    = pToKind != nullptr ? pToKind->findInput( wire._toPort ) : -1;
            if ( pFromKind != nullptr && fromPort < 0 )
                outListError.push_back( where + ": unknown output '" + wire._fromPort.c_str() + "' (outputs: " + Internal::joinNames( pFromKind->_listOutput ) + ")" );
            if ( pToKind != nullptr && toPort < 0 )
                outListError.push_back( where + ": unknown input '" + wire._toPort.c_str() + "' (inputs: " + Internal::joinNames( pToKind->_listInput ) + ")" );
            if ( fromPort < 0 || toPort < 0 )
                continue;
            InputWire input;
            input._sourceNode = static_cast<uint32>( fromNode );
            input._sourcePort = static_cast<uint8>( fromPort );
            input._targetPort = static_cast<uint8>( toPort );
            input._bInvert    = wire._bInvert;
            listWirePerNode[static_cast<size_t>( toNode )].push_back( input );
            _listNode[static_cast<size_t>( toNode )]._connectedInputBits |= 1u << static_cast<uint32>( toPort );
        }
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            node._wireStart   = static_cast<uint32>( _listInputWire.size() );
            node._wireCount   = static_cast<uint32>( listWirePerNode[nodeIndex].size() );
            _listInputWire.insert( _listInputWire.end(), listWirePerNode[nodeIndex].begin(), listWirePerNode[nodeIndex].end() );
        }

        // 3) 처음 상태 — 종류의 초기화가 매개변수를 해석해 채운다(틀린 이름 · 모양은 여기서 거절).
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind == nullptr || node._pKind->_pInitialize == nullptr )
                continue;
            string             error;
            GimmickNodeContext context = makeContext( node );
            context._pError            = &error;
            if ( node._pKind->_pInitialize( context ) == false )
                outListError.push_back( Internal::describeNode( def, listNodeDef[nodeIndex] ) + ": " + ( error.empty() ? string( "invalid parameters" ) : error ) );
            node._outputBits = context._outputBits;
        }

        // 4) 평가 순서 — 고리를 끊는 노드(지연)가 먼저, 나머지는 위상 순서(같으면 정의 순서). 남는 노드가 있으면 지연 없는 고리다.
        vector<uint32> listIndegree( _listNode.size(), 0 );
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            const NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind == nullptr || node._pKind->breaksCycle() )
                continue;
            for ( uint32 wireIndex = 0; wireIndex < node._wireCount; ++wireIndex )
            {
                const NodeRuntime& source = _listNode[_listInputWire[node._wireStart + wireIndex]._sourceNode];
                if ( source._pKind != nullptr && source._pKind->breaksCycle() == false )
                    ++listIndegree[nodeIndex];
            }
        }
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._pKind != nullptr && _listNode[nodeIndex]._pKind->breaksCycle() )
                _listOrder.push_back( static_cast<uint32>( nodeIndex ) );
        }
        vector<uint8> listPlaced( _listNode.size(), SW_FALSE );
        bool          bProgress = true;
        while ( bProgress )
        {
            bProgress = false;
            for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
            {
                const NodeRuntime& node     = _listNode[nodeIndex];
                const bool         bSkipped = node._pKind == nullptr || node._pKind->breaksCycle() || listPlaced[nodeIndex] == SW_TRUE;
                if ( bSkipped || listIndegree[nodeIndex] != 0 )
                    continue;
                listPlaced[nodeIndex] = SW_TRUE;
                _listOrder.push_back( static_cast<uint32>( nodeIndex ) );
                bProgress = true;
                // 이 노드를 원천으로 하는 배선의 받는 쪽 차수를 내린다.
                for ( size_t targetIndex = 0; targetIndex < _listNode.size(); ++targetIndex )
                {
                    const NodeRuntime& target = _listNode[targetIndex];
                    if ( target._pKind == nullptr || target._pKind->breaksCycle() )
                        continue;
                    for ( uint32 wireIndex = 0; wireIndex < target._wireCount; ++wireIndex )
                    {
                        if ( _listInputWire[target._wireStart + wireIndex]._sourceNode == nodeIndex )
                            --listIndegree[targetIndex];
                    }
                }
            }
        }
        string cycleNodes;
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            const NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind == nullptr || node._pKind->breaksCycle() || listPlaced[nodeIndex] == SW_TRUE )
                continue;
            if ( cycleNodes.empty() == false )
                cycleNodes += ", ";
            cycleNodes += node._id.c_str();
        }
        if ( cycleNodes.empty() == false )
            outListError.push_back( Internal::describeSource( def ) + ": signal loop without a Delay through nodes " + cycleNodes );

        if ( outListError.size() != errorBase )
        {
            clear();
            return false;
        }
        computeLayoutHash();
        saveState( _initialStateBytes );
        return true;
    }

    void GimmickCircuit::computeLayoutHash()
    {
        using Internal = GimmickCircuitInternal;
        uint64 hash    = HashUtil::kFnvOffset64;
        for ( const NodeRuntime& node : _listNode )
        {
            const uint64 kindHash = static_cast<uint64>( node._pKind->_name.getHash() );
            const uint64 idHash   = static_cast<uint64>( node._id.getHash() );
            hash                  = Internal::hashBytes( hash, &kindHash, sizeof( kindHash ) );
            hash                  = Internal::hashBytes( hash, &idHash, sizeof( idHash ) );
        }
        const uint64 arrCount[] = { _listNode.size(), _listFloatState.size(), _listIntState.size() };
        _layoutHash             = Internal::hashBytes( hash, arrCount, sizeof( arrCount ) );
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
            const bool         bBreaker = node._pKind->breaksCycle();
            GimmickNodeContext context  = makeContext( node );
            context._inputBits          = bBreaker ? node._previousInputBits : gatherInputs( node );
            node._pKind->_pStep( context );
            node._outputBits = context._outputBits;
            if ( bBreaker == false )
                node._previousInputBits = context._inputBits;
        }
        // 고리를 끊는 노드는 모든 노드가 이번 걸음을 낸 뒤의 입력을 받아 둔다.
        for ( const uint32 nodeIndex : _listOrder )
        {
            NodeRuntime& node = _listNode[nodeIndex];
            if ( node._pKind->breaksCycle() == false )
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

    int32 GimmickCircuit::consumeTime( float32 deltaTime ) { return isBuilt() ? _timer.consume( deltaTime ) : 0; }

    void GimmickCircuit::resetToInitial()
    {
        if ( _initialStateBytes.empty() == false )
            (void)loadState( _initialStateBytes ); // 같은 회로가 저장한 바이트라 모양이 늘 맞는다
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

    int32 GimmickCircuit::findNode( const hashed_string& id ) const
    {
        for ( size_t nodeIndex = 0; nodeIndex < _listNode.size(); ++nodeIndex )
        {
            if ( _listNode[nodeIndex]._id == id )
                return static_cast<int32>( nodeIndex );
        }
        return -1;
    }

    float32 GimmickCircuit::getActuatorValue( int32 node ) const { return getFloatState( node, 0 ); }

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
        const NodeRuntime& runtime    = _listNode[static_cast<size_t>( node )];
        const int32        paramIndex = runtime._pKind->findParam( name );
        return paramIndex >= 0 ? &_listParam[runtime._paramStart + static_cast<uint32>( paramIndex )] : nullptr;
    }

    void GimmickCircuit::saveState( vector<uint8>& outBytes ) const
    {
        using Internal = GimmickCircuitInternal;
        outBytes.clear();
        outBytes.reserve( 32 + _listNode.size() * 16 + _listFloatState.size() * 4 + _listIntState.size() * 4 );
        Internal::writeValue( outBytes, Internal::kStateTag );
        Internal::writeValue( outBytes, Internal::kStateVersion );
        Internal::writeValue( outBytes, _layoutHash );
        Internal::writeValue( outBytes, _stepIndex );
        Internal::writeValue( outBytes, _timer._accumulator );
        for ( const NodeRuntime& node : _listNode )
        {
            Internal::writeValue( outBytes, node._outputBits );
            Internal::writeValue( outBytes, node._previousInputBits );
            Internal::writeValue( outBytes, node._sensorValue );
            Internal::writeValue( outBytes, node._sensorImpulse );
        }
        for ( const float32 value : _listFloatState )
        {
            Internal::writeValue( outBytes, value );
        }
        for ( const int32 value : _listIntState )
        {
            Internal::writeValue( outBytes, value );
        }
    }

    bool GimmickCircuit::loadState( const vector<uint8>& bytes )
    {
        using Internal = GimmickCircuitInternal;
        const size_t expectedSize =
            sizeof( uint32 ) * 3 + sizeof( uint64 ) + sizeof( float32 ) + _listNode.size() * 16 + _listFloatState.size() * 4 + _listIntState.size() * 4;
        size_t     offset{ 0 };
        uint32     magic{ 0 };
        uint32     version{ 0 };
        uint64     layoutHash{ 0 };
        uint32     stepIndex{ 0 };
        float32    accumulator{ 0.0f };
        const bool bHeader = bytes.size() == expectedSize && Internal::readValue( bytes, offset, magic ) && Internal::readValue( bytes, offset, version ) &&
                             Internal::readValue( bytes, offset, layoutHash ) && Internal::readValue( bytes, offset, stepIndex ) &&
                             Internal::readValue( bytes, offset, accumulator );
        if ( bHeader == false || magic != Internal::kStateTag || version != Internal::kStateVersion || layoutHash != _layoutHash )
            return false;
        _stepIndex          = stepIndex;
        _timer._accumulator = accumulator;
        for ( NodeRuntime& node : _listNode )
        {
            (void)Internal::readValue( bytes, offset, node._outputBits );        // 크기는 위에서 expectedSize 로 확인했다
            (void)Internal::readValue( bytes, offset, node._previousInputBits ); // 크기는 위에서 expectedSize 로 확인했다
            (void)Internal::readValue( bytes, offset, node._sensorValue );       // 크기는 위에서 expectedSize 로 확인했다
            (void)Internal::readValue( bytes, offset, node._sensorImpulse );     // 크기는 위에서 expectedSize 로 확인했다
        }
        for ( float32& value : _listFloatState )
        {
            (void)Internal::readValue( bytes, offset, value ); // 크기는 위에서 expectedSize 로 확인했다
        }
        for ( int32& value : _listIntState )
        {
            (void)Internal::readValue( bytes, offset, value ); // 크기는 위에서 expectedSize 로 확인했다
        }
        return true;
    }

    uint64 GimmickCircuit::computeStateHash() const
    {
        vector<uint8> bytes;
        saveState( bytes );
        return GimmickCircuitInternal::hashBytes( HashUtil::kFnvOffset64, bytes.data(), bytes.size() );
    }
} // namespace sw

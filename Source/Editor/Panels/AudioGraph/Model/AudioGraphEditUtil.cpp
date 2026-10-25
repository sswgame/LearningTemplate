#include "pch.h"

#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Audio/Graph/AudioGraph.h"

namespace sw::editor
{
    bool AudioGraphEditUtil::tryDecodeNodeID( const AudioGraphDef& def, int32 nodeID, uint32& outNodeIndex )
    {
        const bool bInRange = 1 <= nodeID && nodeID <= static_cast<int32>( def._listNode.size() );
        if ( bInRange == false )
            return false;
        outNodeIndex = static_cast<uint32>( nodeID - 1 );
        return true;
    }

    bool AudioGraphEditUtil::tryDecodePinID( const AudioGraphDef& def, int32 pinID, uint32& outNodeIndex, bool& outbInput )
    {
        const int32 slot       = pinID % kPinScale;
        const bool  bKnownSlot = pinID > 0 && slot <= 1;
        if ( bKnownSlot == false || tryDecodeNodeID( def, pinID / kPinScale, outNodeIndex ) == false )
            return false;
        outbInput = slot == 1;
        // 소스(오실레이터 · 잡음 · 그래뉼러)는 들어오는 핀을 그리지 않는다.
        return outbInput == false || AudioGraphRegistry::getNodeType( def._listNode[outNodeIndex]._type )._bSource == false;
    }

    bool AudioGraphEditUtil::tryDecodeLinkID( const AudioGraphDef& def, int32 linkID, uint32& outReaderIndex, uint32& outInputSlot )
    {
        if ( linkID <= 0 || tryDecodeNodeID( def, linkID / kLinkScale, outReaderIndex ) == false )
            return false;
        outInputSlot = static_cast<uint32>( linkID % kLinkScale );
        return outInputSlot < def._listNode[outReaderIndex]._listInput.size();
    }

    bool AudioGraphEditUtil::connect( AudioGraphDef& inoutDef, uint32 fromIndex, uint32 toIndex )
    {
        const size_t nodeCount  = inoutDef._listNode.size();
        const bool   bValidEnds = fromIndex < nodeCount && toIndex < nodeCount;
        if ( bValidEnds == false || fromIndex == toIndex )
            return false;
        AudioGraphNodeDesc& reader = inoutDef._listNode[toIndex];
        if ( AudioGraphRegistry::getNodeType( reader._type )._bSource )
            return false;
        const hashed_string& name = inoutDef._listNode[fromIndex]._name;
        for ( const hashed_string& input : reader._listInput )
        {
            if ( input == name )
                return false;
        }
        reader._listInput.push_back( name );
        vector<uint32> listOrder;
        if ( inoutDef.makeProcessingOrder( listOrder ) )
            return true;
        reader._listInput.pop_back(); // 고리 — 잇기 전으로 되돌린다
        return false;
    }

    bool AudioGraphEditUtil::disconnect( AudioGraphDef& inoutDef, uint32 readerIndex, uint32 inputSlot )
    {
        if ( readerIndex >= inoutDef._listNode.size() )
            return false;
        vector<hashed_string>& listInput = inoutDef._listNode[readerIndex]._listInput;
        if ( inputSlot >= listInput.size() )
            return false;
        listInput.erase( listInput.begin() + static_cast<ptrdiff_t>( inputSlot ) );
        return true;
    }

    bool AudioGraphEditUtil::addNode( AudioGraphDef& inoutDef, AudioGraphNodeType type, const float2& canvasPosition, uint32& outNodeIndex )
    {
        if ( inoutDef._listNode.size() >= AudioGraphDef::kMaxNodeCount )
            return false;
        const utf8* pTypeName = AudioGraphRegistry::getNodeType( type )._pName;
        uint32      number    = 1;
        string      name      = pTypeName;
        while ( inoutDef.findNodeIndex( hashed_string( string_view( name ) ) ) >= 0 )
        {
            ++number;
            StringBuilder<constant::kMaxBuffer64> numbered;
            numbered.append( pTypeName ).append( number );
            name = string( numbered.view() );
        }
        AudioGraphNodeDesc node;
        node._name           = hashed_string( string_view( name ) );
        node._type           = type;
        node._editorPosition = canvasPosition;
        outNodeIndex         = static_cast<uint32>( inoutDef._listNode.size() );
        inoutDef._listNode.push_back( std::move( node ) );
        return true;
    }

    bool AudioGraphEditUtil::removeNode( AudioGraphDef& inoutDef, uint32 nodeIndex )
    {
        if ( nodeIndex >= inoutDef._listNode.size() )
            return false;
        const hashed_string name = inoutDef._listNode[nodeIndex]._name;
        inoutDef._listNode.erase( inoutDef._listNode.begin() + static_cast<ptrdiff_t>( nodeIndex ) );
        for ( AudioGraphNodeDesc& node : inoutDef._listNode )
        {
            for ( size_t inputIndex = node._listInput.size(); inputIndex > 0; --inputIndex )
            {
                if ( node._listInput[inputIndex - 1] == name )
                    node._listInput.erase( node._listInput.begin() + static_cast<ptrdiff_t>( inputIndex - 1 ) );
            }
        }
        if ( inoutDef._output == name )
            inoutDef._output = hashed_string{};
        return true;
    }

    void AudioGraphEditUtil::replaceNodeReferences( AudioGraphDef& inoutDef, const hashed_string& oldName, const hashed_string& newName )
    {
        for ( AudioGraphNodeDesc& node : inoutDef._listNode )
        {
            for ( hashed_string& input : node._listInput )
            {
                if ( input.isEqual( oldName, NameCase::CaseSensitive ) )
                    input = newName;
            }
        }
        if ( inoutDef._output.isEqual( oldName, NameCase::CaseSensitive ) )
            inoutDef._output = newName;
    }

    bool AudioGraphEditUtil::layoutIfUnplaced( AudioGraphDef& inoutDef )
    {
        for ( const AudioGraphNodeDesc& node : inoutDef._listNode )
        {
            const bool bPlaced = node._editorPosition._x != 0.0f || node._editorPosition._y != 0.0f;
            if ( bPlaced )
                return false;
        }
        vector<uint32> listOrder;
        if ( inoutDef._listNode.empty() || inoutDef.makeProcessingOrder( listOrder ) == false )
            return false;
        // 깊이 = 가장 깊은 입력 + 1(처리 순서대로 구하므로 입력이 먼저 정해진다).
        vector<uint32> listDepth( inoutDef._listNode.size(), 0 );
        vector<uint32> listRowCount( inoutDef._listNode.size() + 1, 0 );
        for ( const uint32 nodeIndex : listOrder )
        {
            AudioGraphNodeDesc& node  = inoutDef._listNode[nodeIndex];
            uint32              depth = 0;
            for ( const hashed_string& input : node._listInput )
            {
                depth = MathUtil::max( depth, listDepth[static_cast<size_t>( inoutDef.findNodeIndex( input ) )] + 1u );
            }
            listDepth[nodeIndex] = depth;
            const uint32 row     = listRowCount[depth]++;
            node._editorPosition = float2{ static_cast<float32>( depth ) * kLayoutColumnWidth, static_cast<float32>( row ) * kLayoutRowHeight };
        }
        // 모두 원점이면 다음에 다시 놓이므로 첫 노드를 한 칸 띄운다(원점 하나가 "놓지 않음" 으로 읽히지 않게).
        inoutDef._listNode[listOrder.front()]._editorPosition._y += 1.0f;
        return true;
    }
} // namespace sw::editor

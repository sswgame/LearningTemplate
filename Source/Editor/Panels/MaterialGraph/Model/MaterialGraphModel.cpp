#include "pch.h"

#include "Editor/Panels/MaterialGraph/Model/MaterialGraphModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphGeneratedPaths.h"
#include "Engine/Graphics/Material/Dev/MaterialGraphNodeRegistry.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw::editor
{
    MaterialGraphModel::MaterialGraphModel()
        : _graph{}
        , _result{}
        , _preview{}
        , _listNodePin{}
        , _basePath{}
        , _previewPath{}
        , _pendingEdit{}
        , _previewFramesLeft{ 0 }
        , _selectedNode{ -1 }
        , _pendingPlacedNode{ -1 }
        , _placingNode{ -1 }
        , _bCompileDirty{ SW_TRUE }
        , _bPreviewDirty{ SW_TRUE }
        , _bSubgraph{ SW_FALSE }
    {
    }

    bool MaterialGraphModel::load( string_view path )
    {
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        _placingNode       = -1;
        _pendingEdit       = MaterialGraphEdit{};
        _bCompileDirty     = SW_TRUE;
        _bPreviewDirty     = SW_TRUE;
        _previewPath.clear();
        string text;
        if ( AssetIO::readTextAsset( AssetID{ path }, text ) == false || _graph.loadFromXMLText( text, path ) == false )
        {
            _graph = MaterialGraphDef{};
            rebuildPins();
            return false;
        }
        _basePath = DataInheritance::findBasePath( text );
        rebuildPins();
        return true;
    }

    void MaterialGraphModel::restore( string_view xmlText )
    {
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        _placingNode       = -1;
        if ( _graph.loadFromXMLText( xmlText, "<undo>" ) == false )
        {
            SW_LOG_ERROR_ID( "MaterialGraphPanel.UndoUnreadable", "Material graph undo snapshot could not be read - showing an empty graph" );
            _graph = MaterialGraphDef{};
        }
        _bCompileDirty = SW_TRUE;
        _bPreviewDirty = SW_TRUE;
        rebuildPins();
    }

    bool MaterialGraphModel::saveDocument( const AbsolutePath& diskPath )
    {
        if ( MaterialGraphCompileResult::isSubgraph( _graph ) == false )
        {
            MaterialGraphCompileResult result;
            if ( MaterialGraphCompileResult::compile( _graph, "<save>", result ) == false )
            {
                for ( const MaterialGraphIssue& issue : result._listIssue )
                {
                    SW_LOG_WARNING_ID( "MaterialGraphPanel.SaveBlocked", "Material graph not saved: node '%#': [%#] %#", issue._nodeName, issue._id, issue._message );
                }
                return false;
            }
        }
        return _graph.saveToFile( diskPath, AssetID{ _basePath } );
    }

    bool MaterialGraphModel::takeEdit( MaterialGraphEdit& outEdit )
    {
        if ( _pendingEdit._pLabel == nullptr )
            return false;
        outEdit      = _pendingEdit;
        _pendingEdit = MaterialGraphEdit{};
        return true;
    }

    void MaterialGraphModel::recordEdit( const utf8* pLabel, bool bChangesShader )
    {
        _pendingEdit = MaterialGraphEdit{ pLabel };
        if ( bChangesShader == false )
            return;
        _bCompileDirty = SW_TRUE;
        _bPreviewDirty = SW_TRUE;
    }

    void MaterialGraphModel::rebuildPins()
    {
        _listNodePin.clear();
        _listNodePin.resize( _graph._listNode.size() );
        for ( size_t nodeIndex = 0; nodeIndex < _graph._listNode.size(); ++nodeIndex )
        {
            // 모르는 종류 · 읽지 못한 하위 그래프는 핀 없이 그린다 — 컴파일 문제가 노드에 이유를 붙인다.
            (void)MaterialGraphCompileResult::collectNodePins( _graph._listNode[nodeIndex], _listNodePin[nodeIndex]._listInput, _listNodePin[nodeIndex]._listOutput );
        }
    }

    bool MaterialGraphModel::tryDecodePin( int32 pinID, uint32& outNodeIndex, uint32& outSlot, bool& outbInput ) const
    {
        const int32 nodeID = pinID / kPinScale;
        const int32 slot   = pinID % kPinScale;
        if ( nodeID < 1 || nodeID > static_cast<int32>( _listNodePin.size() ) )
            return false;
        outNodeIndex          = static_cast<uint32>( nodeID - 1 );
        outbInput             = slot < kOutputPinBase;
        outSlot               = static_cast<uint32>( outbInput ? slot : slot - kOutputPinBase );
        const size_t pinCount = outbInput ? _listNodePin[outNodeIndex]._listInput.size() : _listNodePin[outNodeIndex]._listOutput.size();
        return outSlot < pinCount;
    }

    bool MaterialGraphModel::usesNode( uint32 fromIndex, uint32 targetIndex, vector<uint8>& inoutListSeen ) const
    {
        if ( fromIndex == targetIndex )
            return true;
        if ( inoutListSeen[fromIndex] != 0 )
            return false;
        inoutListSeen[fromIndex] = 1;
        for ( const MaterialGraphLink& link : _graph._listNode[fromIndex]._listLink )
        {
            const optional<uint32> upstream = _graph.findNodeIndex( link._fromNode );
            if ( upstream.has_value() && usesNode( *upstream, targetIndex, inoutListSeen ) )
                return true;
        }
        return false;
    }

    bool MaterialGraphModel::connectPins( int32 fromPin, int32 toPin )
    {
        uint32 fromNode = 0;
        uint32 fromSlot = 0;
        uint32 toNode   = 0;
        uint32 toSlot   = 0;
        bool   bFromIn  = false;
        bool   bToIn    = false;
        if ( tryDecodePin( fromPin, fromNode, fromSlot, bFromIn ) == false || tryDecodePin( toPin, toNode, toSlot, bToIn ) == false || bFromIn || bToIn == false )
            return false;
        vector<uint8> listSeen;
        listSeen.resize( _graph._listNode.size() );
        if ( usesNode( fromNode, toNode, listSeen ) )
            return false;

        MaterialGraphLink newLink;
        newLink._pin                         = _listNodePin[toNode]._listInput[toSlot]._name;
        newLink._fromNode                    = _graph._listNode[fromNode]._name;
        newLink._fromPin                     = _listNodePin[fromNode]._listOutput[fromSlot]._name;
        vector<MaterialGraphLink>& listLink  = _graph._listNode[toNode]._listLink;
        bool                       bReplaced = false;
        for ( MaterialGraphLink& link : listLink )
        {
            if ( link._pin != newLink._pin )
                continue;
            link      = newLink;
            bReplaced = true;
        }
        if ( bReplaced == false )
            listLink.push_back( std::move( newLink ) );
        recordEdit( "Connect Material Graph Pins", true );
        return true;
    }

    bool MaterialGraphModel::disconnectInputPin( int32 inputPin )
    {
        uint32 nodeIndex = 0;
        uint32 slot      = 0;
        bool   bInput    = false;
        if ( tryDecodePin( inputPin, nodeIndex, slot, bInput ) == false || bInput == false )
            return false;
        const string& pinName = _listNodePin[nodeIndex]._listInput[slot]._name;
        VectorUtil::removeIf( _graph._listNode[nodeIndex]._listLink, [&pinName]( const MaterialGraphLink& link )
        { return link._pin == pinName; } );
        recordEdit( "Disconnect Material Graph Pin", true );
        return true;
    }

    bool MaterialGraphModel::addNode( string_view kindName, const float2& canvasPosition )
    {
        if ( MaterialGraphNodeRegistry::find( kindName ) == nullptr )
            return false;
        MaterialGraphNodeDef node;
        node._kind     = string{ kindName };
        node._position = canvasPosition;
        for ( uint32 number = 1; node._name.empty(); ++number )
        {
            fixed_string<constant::buffer::kMax64> name;
            formatstring( name.data(), name.capacity(), "%#%#", kindName, number );
            if ( _graph.findNodeIndex( name.c_str() ).has_value() == false )
                node._name = name.c_str();
        }
        _graph._listNode.push_back( std::move( node ) );
        const int32 nodeIndex = static_cast<int32>( _graph._listNode.size() ) - 1;
        _selectedNode         = nodeIndex;
        _pendingPlacedNode    = nodeIndex;
        rebuildPins();
        recordEdit( "Add Material Graph Node", true );
        return true;
    }

    void MaterialGraphModel::removeNodes( const vector<int32>& listNodeID )
    {
        if ( listNodeID.empty() )
            return;
        vector<string> listRemoved;
        for ( const int32 nodeID : listNodeID )
        {
            const bool bInRange = 1 <= nodeID && nodeID <= static_cast<int32>( _graph._listNode.size() );
            if ( bInRange )
                listRemoved.push_back( _graph._listNode[static_cast<size_t>( nodeID - 1 )]._name );
        }
        VectorUtil::removeIf( _graph._listNode, [&listRemoved]( const MaterialGraphNodeDef& node )
        { return VectorUtil::contains( listRemoved, node._name ); } );
        for ( MaterialGraphNodeDef& node : _graph._listNode )
        {
            VectorUtil::removeIf( node._listLink, [&listRemoved]( const MaterialGraphLink& link )
            { return VectorUtil::contains( listRemoved, link._fromNode ); } );
        }
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        _placingNode       = -1;
        rebuildPins();
        recordEdit( "Delete Material Graph Node", true );
    }

    void MaterialGraphModel::beginCanvasFrame()
    {
        _placingNode       = _pendingPlacedNode;
        _pendingPlacedNode = -1;
    }

    void MaterialGraphModel::recordNodePosition( uint32 nodeIndex, const float2& position )
    {
        if ( isPlacingNode( nodeIndex ) )
            return;
        float2&    stored = _graph._listNode[nodeIndex]._position;
        const bool bMoved = MathUtil::abs( stored._x - position._x ) > kMoveEpsilon || MathUtil::abs( stored._y - position._y ) > kMoveEpsilon;
        if ( bMoved == false )
            return;
        stored = position;
        recordEdit( "Move Material Graph Node", false );
    }

    void MaterialGraphModel::selectNodeByID( int32 nodeID )
    {
        const bool bInRange = 1 <= nodeID && nodeID <= static_cast<int32>( _graph._listNode.size() );
        _selectedNode       = bInRange ? nodeID - 1 : -1;
    }

    string MaterialGraphModel::readValue( uint32 nodeIndex, string_view name, string_view defaultText ) const
    {
        const MaterialGraphValue* pValue = _graph._listNode[nodeIndex].findValue( name );
        return pValue != nullptr ? pValue->_value : string{ defaultText };
    }

    void MaterialGraphModel::setValue( uint32 nodeIndex, string_view name, string_view text )
    {
        MaterialGraphNodeDef& node = _graph._listNode[nodeIndex];
        bool                  bSet = false;
        for ( MaterialGraphValue& value : node._listValue )
        {
            if ( value._name != name )
                continue;
            value._value = string{ text };
            bSet         = true;
        }
        if ( bSet == false )
        {
            MaterialGraphValue value;
            value._name  = string{ name };
            value._value = string{ text };
            node._listValue.push_back( std::move( value ) );
        }
        rebuildPins(); // 하위 그래프 입력 노드의 `Type` · `Name` 이 핀을 바꾼다
        recordEdit( "Edit Material Graph Value", true );
    }

    void MaterialGraphModel::setAssetPath( uint32 nodeIndex, string_view path )
    {
        _graph._listNode[nodeIndex]._assetPath = string{ path };
        rebuildPins();
        recordEdit( "Edit Material Graph Asset", true );
    }

    void MaterialGraphModel::refreshCompile( string_view documentPath )
    {
        if ( _bCompileDirty == SW_FALSE )
            return;
        _bCompileDirty = SW_FALSE;
        _bSubgraph     = MaterialGraphCompileResult::isSubgraph( _graph ) ? SW_TRUE : SW_FALSE;
        _result        = MaterialGraphCompileResult{};
        if ( _bSubgraph == SW_FALSE )
            (void)MaterialGraphCompileResult::compile( _graph, documentPath, _result ); // 문제는 결과에 남아 노드에 붙는다
    }

    void MaterialGraphModel::populatePreview( Scene& scene )
    {
        _preview.populateScene( scene );
        _previewPath.clear();
        _bPreviewDirty = SW_TRUE;
    }

    void MaterialGraphModel::refreshPreview( Scene* pScene, string_view documentPath )
    {
        if ( _previewFramesLeft > 0 )
            --_previewFramesLeft;
        const bool bWaiting = pScene == nullptr || _bPreviewDirty == SW_FALSE || _previewFramesLeft > 0 || documentPath.empty();
        if ( bWaiting )
            return;
        _previewFramesLeft = kPreviewIntervalFrames;
        _bPreviewDirty     = SW_FALSE;
        if ( _bSubgraph != SW_FALSE || _result._listIssue.empty() == false || _result._hlsl.empty() )
            return;
        MaterialGraphGeneratedPaths paths;
        if ( MaterialGraphGeneratedPaths::makeGeneratedPaths( documentPath, paths ) == false )
            return;
        const MaterialGraphGeneratedPaths previewPaths = MaterialGraphGeneratedPaths::makePreviewPaths( paths );
        const AssetPaths&                 assetPaths   = editor::getSubsystem<AssetPaths>();
        uint32                            writtenCount = 0;
        if ( MaterialGraphGeneratedPaths::writeGenerated( assetPaths, documentPath, previewPaths, _result, &writtenCount ) == false )
            return;
        if ( _previewPath == previewPaths._materialPath )
        {
            if ( writtenCount > 0 )
                editor::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().reload( AssetID{ _previewPath } );
            return;
        }
        _previewPath = previewPaths._materialPath;
        _preview.syncPreviewSubject( pScene, _previewPath );
    }
} // namespace sw::editor

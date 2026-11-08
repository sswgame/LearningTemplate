#include "pch.h"

#include "Editor/Panels/AudioGraph/Model/AudioGraphModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Path/AbsolutePath.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/IAudioService.h"

namespace sw::editor
{
    AudioGraphModel::AudioGraphModel()
        : _editGraph{}
        , _listOrder{}
        , _previewID{ 0 }
        , _pendingEdit{}
        , _selectedNode{ -1 }
        , _pendingPlacedNode{ -1 }
        , _placingNode{ -1 }
    {
    }

    void AudioGraphModel::clearNodeRefs()
    {
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        _placingNode       = -1;
    }

    bool AudioGraphModel::load( string_view path )
    {
        clearNodeRefs();
        _pendingEdit = AudioGraphEdit{};
        if ( _editGraph.loadFromAsset( AssetID{ path } ) == false )
        {
            _editGraph = AudioGraphDef{};
            return false;
        }
        // 손으로 쓴 파일은 위치가 없다 — 처리 깊이로 한 번 놓는다. 놓기는 고침이 아니다(저장할 것이 생기지 않는다).
        (void)AudioGraphEditUtil::layoutIfUnplaced( _editGraph ); // 이미 놓인 파일이면 아무것도 하지 않는다
        return true;
    }

    void AudioGraphModel::restore( string_view xmlText )
    {
        clearNodeRefs();
        AudioGraphDef restored;
        if ( restored.loadFromXMLText( xmlText, "<audio graph undo>" ) == false )
            SW_LOG_ERROR_ID( "AudioGraphModel.AudioGraphUndoSnapshot", "Audio graph undo snapshot could not be read - the graph is cleared" );
        restored._baseID = _editGraph._baseID; // 되돌리기 글은 펼친 한 벌이다 — 문서의 부모는 그대로 둬야 저장이 차이로 쓴다
        _editGraph       = std::move( restored );
    }

    bool AudioGraphModel::saveDocument( const AbsolutePath& diskPath ) const
    {
        return _editGraph.saveToAsset( diskPath );
    }

    bool AudioGraphModel::takeEdit( AudioGraphEdit& outEdit )
    {
        if ( _pendingEdit._pLabel == nullptr )
            return false;
        outEdit      = _pendingEdit;
        _pendingEdit = AudioGraphEdit{};
        return true;
    }

    bool AudioGraphModel::isPreviewAudioReady()
    {
        return editor::getSubsystem<IAudioService>().isInitialized(); // 에디터는 오디오를 짓는 구성이라 서비스는 늘 있다(전용 서버 구성만 오디오를 짓지 않는다)
    }

    void AudioGraphModel::playPreview( const hashed_string& path )
    {
        if ( isPreviewAudioReady() == false )
            return;
        AudioEngine& engine = editor::getSubsystem<IAudioService>().getEngine();
        if ( _previewID != 0 )
            engine.stop( _previewID, 0.0f );
        _previewID = engine.playClip( path, hashed_string( AudioBusNames::kUI ), AudioClipPlayParams{} );
    }

    void AudioGraphModel::stopPreview()
    {
        if ( _previewID == 0 || isPreviewAudioReady() == false )
            return;
        editor::getSubsystem<IAudioService>().getEngine().stop( _previewID, 0.05f );
        _previewID = 0;
    }

    void AudioGraphModel::refreshPreview()
    {
        if ( _previewID != 0 && isPreviewAudioReady() && editor::getSubsystem<IAudioService>().getEngine().isPlaying( _previewID ) == false )
            _previewID = 0;
    }

    void AudioGraphModel::beginCanvasFrame()
    {
        _placingNode       = _pendingPlacedNode;
        _pendingPlacedNode = -1;
    }

    bool AudioGraphModel::tryDecodePin( int32 pinID, bool& outbInput ) const
    {
        uint32 nodeIndex = 0;
        return AudioGraphEditUtil::tryDecodePinID( _editGraph, pinID, nodeIndex, outbInput );
    }

    bool AudioGraphModel::connectPins( int32 fromPin, int32 toPin )
    {
        uint32     fromIndex = 0;
        uint32     toIndex   = 0;
        bool       bInput    = false;
        const bool bDecoded  = AudioGraphEditUtil::tryDecodePinID( _editGraph, fromPin, fromIndex, bInput ) && AudioGraphEditUtil::tryDecodePinID( _editGraph, toPin, toIndex, bInput );
        if ( bDecoded == false || AudioGraphEditUtil::connect( _editGraph, fromIndex, toIndex ) == false )
            return false;
        recordEdit( "Connect Audio Graph Nodes" );
        return true;
    }

    bool AudioGraphModel::addNode( AudioGraphNodeKind type, const float2& canvasPosition )
    {
        uint32 newIndex{ 0 };
        if ( AudioGraphEditUtil::addNode( _editGraph, type, canvasPosition, newIndex ) == false )
            return false;
        _pendingPlacedNode = static_cast<int32>( newIndex );
        _selectedNode      = static_cast<int32>( newIndex );
        recordEdit( "Add Audio Graph Node" );
        return true;
    }

    uint32 AudioGraphModel::duplicateNodes( const vector<hashed_string>& listName, const float2& offset )
    {
        const vector<hashed_string> listNew = AudioGraphEditUtil::duplicateNodes( _editGraph, listName, offset );
        if ( listNew.empty() )
            return 0;
        _selectedNode      = static_cast<int32>( _editGraph.findNodeIndex( listNew.back() ).value() );  // 방금 더한 사본이다
        _pendingPlacedNode = static_cast<int32>( _editGraph.findNodeIndex( listNew.front() ).value() ); // 사본은 뒤에 붙는다 — 다음 캔버스 프레임이 첫 사본부터 데이터 자리에 놓는다
        recordEdit( "Duplicate Audio Graph Nodes" );
        return static_cast<uint32>( listNew.size() );
    }

    void AudioGraphModel::collectNodeNames( const vector<int32>& listNodeID, vector<hashed_string>& outListName ) const
    {
        outListName.clear();
        for ( const int32 nodeID : listNodeID )
        {
            uint32 nodeIndex = 0;
            if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) )
                outListName.push_back( _editGraph._listNode[nodeIndex]._name );
        }
    }

    void AudioGraphModel::applyDeletions( vector<int32>& inoutListLinkID, vector<int32>& inoutListNodeID )
    {
        // 번호가 밀리지 않게 큰 id 부터 지운다(같은 노드의 입력 자리도 뒤 자리가 큰 id 다). 링크를 먼저 지운 뒤 노드를 지운다.
        VectorUtil::sort( inoutListLinkID, std::greater<int32>{} );
        for ( const int32 linkID : inoutListLinkID )
        {
            uint32 readerIndex = 0;
            uint32 inputSlot   = 0;
            if ( AudioGraphEditUtil::tryDecodeLinkID( _editGraph, linkID, readerIndex, inputSlot ) && AudioGraphEditUtil::disconnect( _editGraph, readerIndex, inputSlot ) )
                recordEdit( "Delete Audio Graph Link" );
        }
        VectorUtil::sort( inoutListNodeID, std::greater<int32>{} );
        for ( const int32 nodeID : inoutListNodeID )
        {
            uint32 nodeIndex = 0;
            if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) && AudioGraphEditUtil::removeNode( _editGraph, nodeIndex ) )
            {
                // 노드를 지우면 뒤 번호가 당겨진다 — 번호로 든 고름 · 놓을 노드가 다른 노드를 가리키지 않게 푼다.
                recordEdit( "Delete Audio Graph Node" );
                clearNodeRefs();
            }
        }
    }

    void AudioGraphModel::recordNodePosition( uint32 nodeIndex, const float2& position )
    {
        if ( nodeIndex >= _editGraph._listNode.size() || isPlacingNode( nodeIndex ) )
            return;
        float2&    stored = _editGraph._listNode[nodeIndex]._editorPosition;
        const bool bMoved = MathUtil::abs( position._x - stored._x ) > kMoveEpsilon || MathUtil::abs( position._y - stored._y ) > kMoveEpsilon;
        if ( bMoved == false )
            return;
        stored = position;
        recordEdit( "Move Audio Graph Nodes", "audiograph.move" );
    }

    void AudioGraphModel::selectNodeByID( int32 nodeID )
    {
        uint32 nodeIndex = 0;
        if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) )
            _selectedNode = static_cast<int32>( nodeIndex );
    }

    AudioGraphNodeDef* AudioGraphModel::editSelectedNode()
    {
        const bool bSelected = 0 <= _selectedNode && _selectedNode < static_cast<int32>( _editGraph._listNode.size() );
        return bSelected ? &_editGraph._listNode[static_cast<size_t>( _selectedNode )] : nullptr;
    }

    void AudioGraphModel::setSelectedAsOutput()
    {
        const AudioGraphNodeDef* pNode = editSelectedNode();
        if ( pNode == nullptr )
            return;
        _editGraph._output = pNode->_name;
        recordEdit( "Set Audio Graph Output" );
    }

    void AudioGraphModel::followSelectedRename( const hashed_string& previousName )
    {
        const AudioGraphNodeDef* pNode = editSelectedNode();
        if ( pNode == nullptr || pNode->_name.isEqual( previousName, NameCase::CaseSensitive ) )
            return;
        AudioGraphEditUtil::replaceNodeReferences( _editGraph, previousName, pNode->_name );
    }
} // namespace sw::editor

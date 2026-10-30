#include "pch.h"

#include "Editor/Panels/AudioGraph/Model/AudioGraphModel.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/AudioGraph/Model/AudioGraphEditUtil.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    AudioGraphModel::AudioGraphModel()
        : _editGraph{}
        , _listOrder{}
        , _editPath{}
        , _previewID{ 0 }
        , _selectedNode{ -1 }
        , _pendingPlacedNode{ -1 }
        , _placingNode{ -1 }
        , _bEditDirty{ false }
    {
    }

    bool AudioGraphModel::load( string_view path )
    {
        _selectedNode      = -1;
        _pendingPlacedNode = -1;
        _placingNode       = -1;
        _bEditDirty        = false;
        if ( _editGraph.loadFromAsset( AssetID{ path } ) == false )
        {
            _editGraph = AudioGraphDef{};
            _editPath  = hashed_string{};
            return false;
        }
        _editPath = hashed_string( path );
        // 손으로 쓴 파일은 위치가 없다 — 처리 깊이로 한 번 놓는다. 놓기는 고침이 아니다(저장할 것이 생기지 않는다).
        (void)AudioGraphEditUtil::layoutIfUnplaced( _editGraph ); // 이미 놓인 파일이면 아무것도 하지 않는다
        return true;
    }

    void AudioGraphModel::save()
    {
        // 검사를 통과하지 못하면 쓰지 않고 오류를 남긴다(사본은 남아 계속 고칠 수 있다).
        const std::optional<AbsolutePath> graphFile = editor::getSubsystem<AssetPaths>().toDiskPath( _editPath.view() );
        if ( graphFile.has_value() == false || _editGraph.saveToAsset( *graphFile ) )
            _bEditDirty = false;
    }

    void AudioGraphModel::playPreview( AudioEngine& engine )
    {
        if ( _previewID != 0 )
            engine.stop( _previewID, 0.0f );
        _previewID = engine.playClip( _editPath, hashed_string( AudioBusNames::kUI ), AudioClipPlayParams{} );
    }

    void AudioGraphModel::stopPreview( AudioEngine& engine )
    {
        if ( _previewID == 0 )
            return;
        engine.stop( _previewID, 0.05f );
        _previewID = 0;
    }

    void AudioGraphModel::refreshPreview( const AudioEngine& engine )
    {
        if ( _previewID != 0 && engine.isPlaying( _previewID ) == false )
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
        _bEditDirty = true;
        return true;
    }

    bool AudioGraphModel::addNode( AudioGraphNodeType type, const float2& canvasPosition )
    {
        uint32 newIndex{ 0 };
        if ( AudioGraphEditUtil::addNode( _editGraph, type, canvasPosition, newIndex ) == false )
            return false;
        _pendingPlacedNode = static_cast<int32>( newIndex );
        _selectedNode      = static_cast<int32>( newIndex );
        _bEditDirty        = true;
        return true;
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
                _bEditDirty = true;
        }
        VectorUtil::sort( inoutListNodeID, std::greater<int32>{} );
        for ( const int32 nodeID : inoutListNodeID )
        {
            uint32 nodeIndex = 0;
            if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) && AudioGraphEditUtil::removeNode( _editGraph, nodeIndex ) )
            {
                // 노드를 지우면 뒤 번호가 당겨진다 — 번호로 든 고름 · 놓을 노드가 다른 노드를 가리키지 않게 푼다.
                _bEditDirty        = true;
                _selectedNode      = -1;
                _pendingPlacedNode = -1;
                _placingNode       = -1;
            }
        }
    }

    void AudioGraphModel::noteNodePosition( uint32 nodeIndex, const float2& position )
    {
        if ( nodeIndex >= _editGraph._listNode.size() || isPlacingNode( nodeIndex ) )
            return;
        float2&    stored = _editGraph._listNode[nodeIndex]._editorPosition;
        const bool bMoved = MathUtil::abs( position._x - stored._x ) > kMoveEpsilon || MathUtil::abs( position._y - stored._y ) > kMoveEpsilon;
        if ( bMoved == false )
            return;
        stored      = position;
        _bEditDirty = true;
    }

    void AudioGraphModel::selectNodeByID( int32 nodeID )
    {
        uint32 nodeIndex = 0;
        if ( AudioGraphEditUtil::tryDecodeNodeID( _editGraph, nodeID, nodeIndex ) )
            _selectedNode = static_cast<int32>( nodeIndex );
    }

    AudioGraphNodeDesc* AudioGraphModel::editSelectedNode()
    {
        const bool bSelected = 0 <= _selectedNode && _selectedNode < static_cast<int32>( _editGraph._listNode.size() );
        return bSelected ? &_editGraph._listNode[static_cast<size_t>( _selectedNode )] : nullptr;
    }

    void AudioGraphModel::setSelectedAsOutput()
    {
        const AudioGraphNodeDesc* pNode = editSelectedNode();
        if ( pNode == nullptr )
            return;
        _editGraph._output = pNode->_name;
        _bEditDirty        = true;
    }

    void AudioGraphModel::followSelectedRename( const hashed_string& previousName )
    {
        const AudioGraphNodeDesc* pNode = editSelectedNode();
        if ( pNode == nullptr || pNode->_name.isEqual( previousName, NameCase::CaseSensitive ) )
            return;
        AudioGraphEditUtil::replaceNodeReferences( _editGraph, previousName, pNode->_name );
    }
} // namespace sw::editor

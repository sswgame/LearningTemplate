#include "pch.h"

#include "Editor/Panels/Animation/Model/AnimationGraphPreviewModel.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"

namespace sw::editor
{
    AnimationGraphPreviewModel::AnimationGraphPreviewModel()
        : _previewGraph{}
        , _previewPlayer{}
        , _previewStateName{}
        , _holdSeconds{ 0.0f }
        , _flowLinkID{ 0 }
        , _bPlaying{ false }
    {
    }

    void AnimationGraphPreviewModel::resetForDocument()
    {
        _previewPlayer.stop();
        _previewStateName.clear();
        _holdSeconds = 0.0f;
        _flowLinkID  = 0;
        _bPlaying    = false;
    }

    void AnimationGraphPreviewModel::play( const AnimationGraphAsset& graph, string_view graphPath )
    {
        syncGraph( graph );
        (void)_previewPlayer.play( hashed_string{}, false, 0.0f ); // 클립이 없어 재생은 비고 상태 이름만 옮긴다
        _bPlaying    = true;
        _holdSeconds = 0.0f;
        EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), graphPath );
        noteTransition();
    }

    void AnimationGraphPreviewModel::advance( const AnimationGraphAsset& graph, string_view graphPath )
    {
        syncGraph( graph );
        step( graphPath );
    }

    void AnimationGraphPreviewModel::stop()
    {
        _previewPlayer.stop();
        _bPlaying = false;
    }

    void AnimationGraphPreviewModel::update( float32 deltaSeconds, string_view graphPath )
    {
        if ( _bPlaying == false )
            return;
        _holdSeconds += deltaSeconds;
        if ( _holdSeconds < kHoldSeconds )
            return;
        _holdSeconds = 0.0f;
        step( graphPath );
    }

    bool AnimationGraphPreviewModel::isCurrentState( string_view nodeName ) const
    {
        const hashed_string& current = _previewPlayer.getCurrentStateName();
        return current.empty() == false && current.view() == nodeName;
    }

    int32 AnimationGraphPreviewModel::takeFlowLinkID()
    {
        const int32 linkID = _flowLinkID;
        _flowLinkID        = 0;
        return linkID;
    }

    void AnimationGraphPreviewModel::syncGraph( const AnimationGraphAsset& graph )
    {
        _previewGraph = graph;
        _previewPlayer.setGraph( &_previewGraph );
    }

    void AnimationGraphPreviewModel::step( string_view graphPath )
    {
        if ( _previewPlayer.advance() == false )
            _bPlaying = false;
        else
            EditorViewportPreview::applyAnimationNode( _previewPlayer.getCurrentStateName().c_str(), graphPath );
        noteTransition();
    }

    void AnimationGraphPreviewModel::noteTransition()
    {
        const string current{ _previewPlayer.getCurrentStateName().c_str() };
        if ( current != _previewStateName && _previewStateName.empty() == false )
        {
            // 노드 id 와 링크를 같은 사본에서 찾는다 — 예전에는 사본의 노드 id 로 문서의 링크를 찾아, 사본을 넣은 뒤 고친 문서에서는 다른 전이를 비출 수 있었다.
            const AnimationGraphNode* pFrom = _previewGraph.findNodeByName( _previewStateName );
            const AnimationGraphNode* pTo   = _previewGraph.findNodeByName( current );
            for ( const AnimationGraphLink& link : _previewGraph._listLink )
            {
                const bool bLinkBetweenNodes = pFrom != nullptr && pTo != nullptr && link._fromNode == pFrom->_id && link._toNode == pTo->_id;
                if ( bLinkBetweenNodes )
                    _flowLinkID = link._id;
            }
        }
        _previewStateName = current;
    }
} // namespace sw::editor

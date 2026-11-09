#include "pch.h"

#include "Editor/Panels/Dialogue/Model/DialogueGraphPreviewModel.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw::editor
{
    DialogueGraphPreviewModel::DialogueGraphPreviewModel()
        : _graphPath{}
        , _holdSeconds{ 0.0f }
        , _nodeID{ 0 }
        , _bPlaying{ SW_FALSE }
    {
    }

    void DialogueGraphPreviewModel::play( const DialogueGraphAsset& graph )
    {
        const DialogueAssetNode* pStart = graph.findStartNode();
        _bPlaying                       = SW_TRUE;
        enterNode( graph, pStart != nullptr ? optional<int32>( pStart->_id ) : nullopt );
    }

    void DialogueGraphPreviewModel::step( const DialogueGraphAsset& graph, const DialogueStepInput& input )
    {
        if ( _nodeID <= 0 )
        {
            const DialogueAssetNode* pStart = graph.findStartNode();
            enterNode( graph, pStart != nullptr ? optional<int32>( pStart->_id ) : nullopt );
            return;
        }
        const DialogueAssetNode* pNode = graph.findNode( _nodeID );
        enterNode( graph, pNode != nullptr ? DialogueCursor::step( graph, *pNode, input ) : nullopt );
    }

    void DialogueGraphPreviewModel::stop()
    {
        _bPlaying    = SW_FALSE;
        _nodeID      = 0;
        _holdSeconds = 0.0f;
    }

    void DialogueGraphPreviewModel::update( const DialogueGraphAsset& graph, float32 deltaSeconds )
    {
        if ( _bPlaying == false || _nodeID <= 0 )
            return;
        const DialogueAssetNode* pNode = graph.findNode( _nodeID );
        const DialogueNodeInfo*  pInfo = pNode != nullptr ? DialogueGraphAsset::findNodeInfo( pNode->_type ) : nullptr;
        if ( pInfo == nullptr )
        {
            _bPlaying = SW_FALSE;
            return;
        }

        switch ( pInfo->_flow )
        {
            case DialogueNodeFlow::PassThrough:
            {
                step( graph, DialogueStepInput{} );
                return;
            }
            case DialogueNodeFlow::WaitAdvance:
            {
                _holdSeconds += deltaSeconds;
                if ( _holdSeconds < kHoldSeconds )
                    return;
                step( graph, DialogueStepInput{} );
                return;
            }
            case DialogueNodeFlow::Finish:
            {
                _bPlaying = SW_FALSE;
                return;
            }
            case DialogueNodeFlow::WaitChoice: // 사람이 툴바 버튼으로 선택한다
            case DialogueNodeFlow::Condition:
            {
                return;
            }
        }
    }

    void DialogueGraphPreviewModel::enterNode( const DialogueGraphAsset& graph, optional<int32> nextNodeID )
    {
        _holdSeconds                   = 0.0f;
        const DialogueAssetNode* pNode = nextNodeID.has_value() ? graph.findNode( *nextNodeID ) : nullptr;
        const DialogueNodeInfo*  pInfo = pNode != nullptr ? DialogueGraphAsset::findNodeInfo( pNode->_type ) : nullptr;
        if ( pInfo == nullptr )
        {
            _bPlaying = SW_FALSE;
            return;
        }

        _nodeID = *nextNodeID;
        // 러너가 알리는 노드(대사, 선택지)만 뷰포트에 보인다.
        if ( pInfo->_flow == DialogueNodeFlow::WaitAdvance || pInfo->_flow == DialogueNodeFlow::WaitChoice )
            EditorViewportPreview::applyDialogueLine( _graphPath, pNode->_speaker, pNode->_text );
        if ( pInfo->_flow == DialogueNodeFlow::Finish )
            _bPlaying = SW_FALSE;
    }
} // namespace sw::editor

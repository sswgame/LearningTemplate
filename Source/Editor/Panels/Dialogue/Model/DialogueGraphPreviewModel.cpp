#include "pch.h"

#include "Editor/Panels/Dialogue/Model/DialogueGraphPreviewModel.h"

#include "Editor/Common/Commands/Scene/EditorViewportPreview.h"

#include "Engine/Dialogue/DialogueCursor.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"

namespace sw::editor
{
    void DialogueGraphPreviewModel::play( const DialogueGraphAsset& graph )
    {
        const DialogueAssetNode* pStart = graph.findStartNode();
        _bPlaying                       = true;
        enterNode( graph, pStart != nullptr ? pStart->_id : 0 );
    }

    void DialogueGraphPreviewModel::step( const DialogueGraphAsset& graph, const DialogueStepInput& input )
    {
        if ( _nodeID <= 0 )
        {
            const DialogueAssetNode* pStart = graph.findStartNode();
            enterNode( graph, pStart != nullptr ? pStart->_id : 0 );
            return;
        }
        const DialogueAssetNode* pNode = graph.findNode( _nodeID );
        enterNode( graph, pNode != nullptr ? DialogueCursor::step( graph, *pNode, input ) : 0 );
    }

    void DialogueGraphPreviewModel::stop()
    {
        _bPlaying    = false;
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
            _bPlaying = false;
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
                _bPlaying = false;
                return;
            }
            case DialogueNodeFlow::WaitChoice: // 사람이 툴바 버튼으로 선택한다
            case DialogueNodeFlow::Condition:
            {
                return;
            }
        }
    }

    void DialogueGraphPreviewModel::enterNode( const DialogueGraphAsset& graph, int32 nodeID )
    {
        _holdSeconds                   = 0.0f;
        const DialogueAssetNode* pNode = graph.findNode( nodeID );
        const DialogueNodeInfo*  pInfo = pNode != nullptr ? DialogueGraphAsset::findNodeInfo( pNode->_type ) : nullptr;
        if ( pInfo == nullptr )
        {
            _bPlaying = false;
            return;
        }

        _nodeID = nodeID;
        // 러너가 알리는 노드(대사, 선택지)만 뷰포트에 보인다.
        if ( pInfo->_flow == DialogueNodeFlow::WaitAdvance || pInfo->_flow == DialogueNodeFlow::WaitChoice )
            EditorViewportPreview::applyDialogueLine( pNode->_speaker, pNode->_text );
        if ( pInfo->_flow == DialogueNodeFlow::Finish )
            _bPlaying = false;
    }
} // namespace sw::editor

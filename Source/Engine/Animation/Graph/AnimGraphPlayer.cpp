#include "pch.h"

#include "Engine/Animation/Graph/AnimGraphPlayer.h"

#include "Engine/Animation/Graph/AnimGraphAsset.h"

namespace sw
{
    AnimGraphPlayer::AnimGraphPlayer()
        : _player{}
        , _currentStateName{}
        , _pGraph{ nullptr }
        , _pSource{ nullptr }
        , _currentNodeId{ 0 }
        , _defaultBlendSeconds{ 0.15f }
    {
    }

    void AnimGraphPlayer::setGraph( const AnimGraphAsset* pGraph )
    {
        stop();
        _pGraph = pGraph;
    }

    bool AnimGraphPlayer::play( const hashed_string& stateName, bool bLoop, float32 blendSeconds )
    {
        if ( _pGraph != nullptr )
        {
            // 이름이 비었으면 진입 노드, 그래프에 없는 이름이면 그래프 밖에서 그 이름을 바로 재생한다(전이 없음).
            const AnimGraphNode* pNode = stateName.empty() ? _pGraph->findEntryNode() : _pGraph->findNodeByName( stateName.c_str() );
            if ( pNode != nullptr )
                return enterNode( *pNode, bLoop ? kLoopYes : kLoopNo, blendSeconds );
        }
        _currentNodeId    = 0;
        _currentStateName = stateName;
        return startPlayable( stateName, bLoop, blendSeconds );
    }

    void AnimGraphPlayer::playPlayable( const hashed_string& stateName, const IAnimPlayable* pPlayable, bool bLoop )
    {
        const AnimGraphNode* pNode = ( _pGraph != nullptr && stateName.empty() == false ) ? _pGraph->findNodeByName( stateName.c_str() ) : nullptr;
        _currentNodeId             = pNode != nullptr ? pNode->_id : 0;
        _currentStateName          = stateName;
        _player.play( pPlayable, bLoop );
    }

    void AnimGraphPlayer::stop()
    {
        _player.stop();
        _currentNodeId    = 0;
        _currentStateName = hashed_string{};
    }

    void AnimGraphPlayer::update( float32 deltaSeconds, AnimParameterSet* pParameter, vector<AnimFiredNotify>* pOutListFired )
    {
        if ( _pGraph != nullptr && _currentNodeId > 0 && pParameter != nullptr )
        {
            for ( const AnimGraphLink& link : _pGraph->_listLink )
            {
                if ( link._fromNode != _currentNodeId || link._op == AnimConditionOp::None )
                    continue;
                if ( link.isConditionMet( pParameter->getFloat( link._parameter ) ) == false )
                    continue;
                if ( link._op == AnimConditionOp::Trigger )
                    pParameter->consumeTrigger( link._parameter );
                const AnimGraphNode* pTarget = _pGraph->findNode( link._toNode );
                if ( pTarget != nullptr )
                    (void)enterNode( *pTarget, kLoopFromPlayable, link._blendSeconds );
                break;
            }
        }

        _player.update( deltaSeconds, pOutListFired );

        if ( _pGraph == nullptr || _currentNodeId <= 0 || _player.hasFinished() == false )
            return;
        // 재생할 것이 없는 상태는 길이 0 이라 곧 "끝났다" — 그래도 다음으로 바로 넘어가지 않고 머문다(다음 advance · play 까지).
        if ( _player.getCurrentPlayable() == nullptr )
            return;
        const AnimGraphLink* pFinish = _pGraph->findFinishLink( _currentNodeId );
        const AnimGraphNode* pNext   = pFinish != nullptr ? _pGraph->findNode( pFinish->_toNode ) : nullptr;
        if ( pNext != nullptr )
            (void)enterNode( *pNext, kLoopNo, pFinish->_blendSeconds );
    }

    bool AnimGraphPlayer::advance()
    {
        if ( _pGraph == nullptr )
            return false;
        if ( _currentNodeId <= 0 )
            return play( hashed_string{}, false, 0.0f );
        const AnimGraphLink* pFinish = _pGraph->findFinishLink( _currentNodeId );
        const AnimGraphNode* pNext   = pFinish != nullptr ? _pGraph->findNode( pFinish->_toNode ) : nullptr;
        if ( pNext == nullptr )
            return false;
        (void)enterNode( *pNext, kLoopNo, pFinish->_blendSeconds );
        return true;
    }

    bool AnimGraphPlayer::enterNode( const AnimGraphNode& node, int8 loopWhenUnspecified, float32 blendSeconds )
    {
        _currentNodeId    = node._id;
        _currentStateName = hashed_string( node._name );
        int8 loop         = node._loopOverride >= 0 ? node._loopOverride : loopWhenUnspecified;
        if ( loop == kLoopFromPlayable )
        {
            const IAnimPlayable* pPlayable = ( _pSource != nullptr ) ? _pSource->findPlayable( _currentStateName ) : nullptr;
            loop                           = ( pPlayable != nullptr && pPlayable->isLoopingByDefault() ) ? kLoopYes : kLoopNo;
        }
        return startPlayable( _currentStateName, loop == kLoopYes, blendSeconds < 0.0f ? _defaultBlendSeconds : blendSeconds );
    }

    bool AnimGraphPlayer::startPlayable( const hashed_string& name, bool bLoop, float32 blendSeconds )
    {
        const IAnimPlayable* pPlayable = ( _pSource != nullptr && name.empty() == false ) ? _pSource->findPlayable( name ) : nullptr;
        if ( pPlayable == nullptr )
        {
            _player.stop();
            return false;
        }
        if ( blendSeconds > 0.0f )
            _player.crossfade( pPlayable, blendSeconds, bLoop );
        else
            _player.play( pPlayable, bLoop );
        return true;
    }
} // namespace sw

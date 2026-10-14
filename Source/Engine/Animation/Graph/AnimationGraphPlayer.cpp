#include "pch.h"

#include "Engine/Animation/Graph/AnimationGraphPlayer.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"

namespace sw
{
    AnimationGraphPlayer::AnimationGraphPlayer()
        : _player{}
        , _currentStateName{}
        , _pGraph{ nullptr }
        , _pSource{ nullptr }
        , _currentNodeID{ 0 }
        , _defaultBlendSeconds{ 0.15f }
    {
    }

    void AnimationGraphPlayer::setGraph( const AnimationGraphAsset* pGraph )
    {
        stop();
        _pGraph = pGraph;
    }

    bool AnimationGraphPlayer::play( const hashed_string& stateName, bool bLoop, float32 blendSeconds )
    {
        if ( _pGraph != nullptr )
        {
            // 이름이 비었으면 진입 노드, 그래프에 없는 이름이면 그래프 밖에서 그 이름을 바로 재생한다(전이 없음).
            const AnimationGraphNode* pNode = stateName.empty() ? _pGraph->findEntryNode() : _pGraph->findNodeByName( stateName.c_str() );
            if ( pNode != nullptr )
                return enterNode( *pNode, bLoop ? kLoopYes : kLoopNo, blendSeconds );
        }
        _currentNodeID    = 0;
        _currentStateName = stateName;
        return startPlayable( stateName, bLoop, blendSeconds );
    }

    void AnimationGraphPlayer::playPlayable( const hashed_string& stateName, const IAnimationPlayable* pPlayable, bool bLoop )
    {
        const AnimationGraphNode* pNode = ( _pGraph != nullptr && stateName.empty() == false ) ? _pGraph->findNodeByName( stateName.c_str() ) : nullptr;
        _currentNodeID                  = pNode != nullptr ? pNode->_id : 0;
        _currentStateName               = stateName;
        _player.play( pPlayable, bLoop );
    }

    void AnimationGraphPlayer::stop()
    {
        _player.stop();
        _currentNodeID    = 0;
        _currentStateName = hashed_string{};
    }

    void AnimationGraphPlayer::update( float32 deltaSeconds, AnimationParameterSet* pParameter, vector<AnimationFiredNotify>* pOutListFired )
    {
        const bool bCanEvaluateLinks = _pGraph != nullptr && _currentNodeID > 0 && pParameter != nullptr;
        if ( bCanEvaluateLinks )
        {
            for ( const AnimationGraphLink& link : _pGraph->_listLink )
            {
                if ( link._fromNode != _currentNodeID || link._op == AnimationConditionOp::None )
                    continue;
                if ( link.isConditionMet( pParameter->getFloat( link._parameter ) ) == false )
                    continue;
                if ( link._op == AnimationConditionOp::Trigger )
                    pParameter->consumeTrigger( link._parameter );
                const AnimationGraphNode* pTarget = _pGraph->findNode( link._toNode );
                if ( pTarget != nullptr )
                    (void)enterNode( *pTarget, kLoopFromPlayable, link._blendSeconds );
                break;
            }
        }

        _player.update( deltaSeconds, pOutListFired );

        const bool bStateNotFinished = _pGraph == nullptr || _currentNodeID <= 0 || _player.hasFinished() == false;
        if ( bStateNotFinished )
            return;
        // 재생할 것이 없는 상태는 길이가 0 이라 곧 "끝났다"가 된다. 그래도 다음으로 바로 넘어가지 않고 머문다(다음 advance 나 play 까지).
        if ( _player.getCurrentPlayable() == nullptr )
            return;
        const AnimationGraphLink* pFinish = _pGraph->findFinishLink( _currentNodeID );
        const AnimationGraphNode* pNext   = pFinish != nullptr ? _pGraph->findNode( pFinish->_toNode ) : nullptr;
        if ( pNext != nullptr )
            (void)enterNode( *pNext, kLoopNo, pFinish->_blendSeconds );
    }

    bool AnimationGraphPlayer::advance()
    {
        if ( _pGraph == nullptr )
            return false;
        if ( _currentNodeID <= 0 )
            return play( hashed_string{}, false, 0.0f );
        const AnimationGraphLink* pFinish = _pGraph->findFinishLink( _currentNodeID );
        const AnimationGraphNode* pNext   = pFinish != nullptr ? _pGraph->findNode( pFinish->_toNode ) : nullptr;
        if ( pNext == nullptr )
            return false;
        (void)enterNode( *pNext, kLoopNo, pFinish->_blendSeconds );
        return true;
    }

    bool AnimationGraphPlayer::enterNode( const AnimationGraphNode& node, int8 loopWhenUnspecified, float32 blendSeconds )
    {
        _currentNodeID    = node._id;
        _currentStateName = hashed_string( node._name );
        int8 loop         = node._loopOverride >= 0 ? node._loopOverride : loopWhenUnspecified;
        if ( loop == kLoopFromPlayable )
        {
            const IAnimationPlayable* pPlayable = ( _pSource != nullptr ) ? _pSource->findPlayable( _currentStateName ) : nullptr;
            loop                                = ( pPlayable != nullptr && pPlayable->isLoopingByDefault() ) ? kLoopYes : kLoopNo;
        }
        return startPlayable( _currentStateName, loop == kLoopYes, blendSeconds < 0.0f ? _defaultBlendSeconds : blendSeconds );
    }

    bool AnimationGraphPlayer::startPlayable( const hashed_string& name, bool bLoop, float32 blendSeconds )
    {
        const IAnimationPlayable* pPlayable = ( _pSource != nullptr && name.empty() == false ) ? _pSource->findPlayable( name ) : nullptr;
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

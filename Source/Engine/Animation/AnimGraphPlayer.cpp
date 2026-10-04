#include "pch.h"

#include "Engine/Animation/AnimGraphPlayer.h"

#include "Engine/Animation/AnimClip.h"

namespace sw
{
    AnimGraphPlayer::AnimGraphPlayer()
        : _graph{}
        , _player{}
        , _listClip{}
        , _currentNodeName{}
        , _currentNodeId{ 0 }
        , _crossfadeSeconds{ 0.15f }
    {
    }

    bool AnimGraphPlayer::loadGraph( string_view path )
    {
        stop();
        return _graph.loadFromFile( path );
    }

    void AnimGraphPlayer::setGraph( const AnimGraphAsset& graph )
    {
        stop();
        _graph = graph;
    }

    void AnimGraphPlayer::registerClip( string_view nodeName, const AnimClip* pClip )
    {
        if ( nodeName.empty() )
            return;
        for ( ClipBinding& binding : _listClip )
        {
            if ( binding._nodeName != nodeName )
                continue;
            binding._pClip = pClip;
            return;
        }
        ClipBinding binding{};
        binding._nodeName = string{ nodeName };
        binding._pClip    = pClip;
        _listClip.push_back( std::move( binding ) );
    }

    void AnimGraphPlayer::clearClips()
    {
        _listClip.clear();
    }

    bool AnimGraphPlayer::play( string_view nodeName, bool bLoopClip )
    {
        const AnimGraphNode* pNode = nullptr;
        if ( nodeName.empty() == false )
            pNode = _graph.findNodeByName( nodeName );
        if ( pNode == nullptr )
            pNode = _graph.findEntryNode();
        if ( pNode == nullptr )
            return false;
        return playNode( pNode->_id, bLoopClip, false );
    }

    void AnimGraphPlayer::stop()
    {
        _player.play( nullptr, false );
        _currentNodeId = 0;
        _currentNodeName.clear();
    }

    bool AnimGraphPlayer::advance()
    {
        if ( _currentNodeId <= 0 )
            return play( {}, false );
        const int32 nextId = _graph.findFirstOutgoingNodeId( _currentNodeId );
        if ( nextId <= 0 )
            return false;
        return playNode( nextId, false, true );
    }

    void AnimGraphPlayer::update( float32 deltaSeconds )
    {
        _player.update( deltaSeconds );
        if ( _player.hasFinished() == false )
            return;
        if ( _currentNodeId <= 0 )
            return;

        const int32 nextId = _graph.findFirstOutgoingNodeId( _currentNodeId );
        if ( nextId <= 0 )
            return;
        playNode( nextId, false, true );
    }

    AnimSample AnimGraphPlayer::evaluate() const
    {
        return _player.evaluate();
    }

    void AnimGraphPlayer::setCrossfadeSeconds( float32 seconds )
    {
        _crossfadeSeconds = ( seconds > 0.0f ) ? seconds : 0.0f;
    }

    const AnimClip* AnimGraphPlayer::findClip( string_view nodeName ) const
    {
        for ( const ClipBinding& binding : _listClip )
        {
            if ( binding._nodeName == nodeName )
                return binding._pClip;
        }
        return nullptr;
    }

    bool AnimGraphPlayer::playNode( int32 nodeId, bool bLoopClip, bool bCrossfade )
    {
        const AnimGraphNode* pNode = _graph.findNode( nodeId );
        if ( pNode == nullptr )
            return false;

        _currentNodeId        = nodeId;
        _currentNodeName      = pNode->_name;
        const AnimClip* pClip = findClip( pNode->_name );
        if ( pClip == nullptr )
        {
            // 클립이 없는 노드로 넘어가면서 플레이어를 그대로 두면, getCurrentNodeName() 은 새
            // 노드를 말하는데 evaluate() 는 이전 노드의 포즈를 계속 반환한다. 둘이 다른 말을
            // 하게 두지 않는다. 클립이 없는 노드는 길이 0 이므로 재생을 비운다.
            _player.play( nullptr, false );
            return true;
        }

        if ( bCrossfade )
            _player.crossfade( pClip, _crossfadeSeconds, bLoopClip );
        else
            _player.play( pClip, bLoopClip );
        return true;
    }
} // namespace sw

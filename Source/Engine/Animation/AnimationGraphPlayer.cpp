#include "pch.h"

#include "Engine/Animation/AnimationGraphPlayer.h"

#include "Engine/Animation/AnimClip.h"

namespace sw
{
	AnimationGraphPlayer::AnimationGraphPlayer()
		: _graph{}
		, _player{}
		, _listClip{}
		, _currentNodeName{}
		, _currentNodeId{ 0 }
		, _crossfadeSeconds{ 0.15f }
	{
	}

	bool AnimationGraphPlayer::loadGraph( string_view path )
	{
		stop();
		return _graph.loadFromFile( path );
	}

	void AnimationGraphPlayer::setGraph( const AnimationGraphAsset& graph )
	{
		stop();
		_graph = graph;
	}

	void AnimationGraphPlayer::registerClip( string_view nodeName, const AnimClip* pClip )
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
		binding._pClip	  = pClip;
		_listClip.push_back( std::move( binding ) );
	}

	void AnimationGraphPlayer::clearClips()
	{
		_listClip.clear();
	}

	bool AnimationGraphPlayer::play( string_view nodeName, bool bLoopClip )
	{
		const AnimationGraphNode* pNode = nullptr;
		if ( nodeName.empty() == false )
			pNode = _graph.findNodeByName( nodeName );
		if ( pNode == nullptr )
			pNode = _graph.findEntryNode();
		if ( pNode == nullptr )
			return false;
		return playNode( pNode->_id, bLoopClip, false );
	}

	void AnimationGraphPlayer::stop()
	{
		_player.play( nullptr, false );
		_currentNodeId = 0;
		_currentNodeName.clear();
	}

	bool AnimationGraphPlayer::advance()
	{
		if ( _currentNodeId <= 0 )
			return play( {}, false );
		const int32 nextId = _graph.findFirstOutgoingNodeId( _currentNodeId );
		if ( nextId <= 0 )
			return false;
		return playNode( nextId, false, true );
	}

	void AnimationGraphPlayer::update( float32 deltaSeconds )
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

	AnimSample AnimationGraphPlayer::evaluate() const
	{
		return _player.evaluate();
	}

	void AnimationGraphPlayer::setCrossfadeSeconds( float32 seconds )
	{
		_crossfadeSeconds = ( seconds > 0.0f ) ? seconds : 0.0f;
	}

	const AnimClip* AnimationGraphPlayer::findClip( string_view nodeName ) const
	{
		for ( const ClipBinding& binding : _listClip )
		{
			if ( binding._nodeName == nodeName )
				return binding._pClip;
		}
		return nullptr;
	}

	bool AnimationGraphPlayer::playNode( int32 nodeId, bool bLoopClip, bool bCrossfade )
	{
		const AnimationGraphNode* pNode = _graph.findNode( nodeId );
		if ( pNode == nullptr )
			return false;

		_currentNodeId		  = nodeId;
		_currentNodeName	  = pNode->_name;
		const AnimClip* pClip = findClip( pNode->_name );
		if ( pClip == nullptr )
			return true;

		if ( bCrossfade )
			_player.crossfade( pClip, _crossfadeSeconds, bLoopClip );
		else
			_player.play( pClip, bLoopClip );
		return true;
	}
} // namespace sw

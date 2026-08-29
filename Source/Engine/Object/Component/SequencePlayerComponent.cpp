#include "pch.h"

#include "Engine/Object/Component/SequencePlayerComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw
{
	SW_LOG_CALLER( "SequencePlayer" );

	SequencePlayerComponent::SequencePlayerComponent()
		: _sequencePath{}
		, _framesPerSecond{ 30.0f }
		, _bLoop{ SW_FALSE }
		, _bAutoPlay{ SW_TRUE }
		, _reserved{ 0 }
		, _player{}
	{
		setCanEverTick( true );
	}

	void SequencePlayerComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostUpdate );
		_player.setFramesPerSecond( _framesPerSecond );
		_player.setLoop( _bLoop == SW_TRUE );
		if ( _sequencePath.empty() == false )
			_player.loadFromFile( _sequencePath );
		if ( _bAutoPlay == SW_TRUE )
			play();
	}

	void SequencePlayerComponent::onEndPlay()
	{
		stop();
		Component::onEndPlay();
	}

	void SequencePlayerComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );
		if ( _player.isPlaying() == false )
			return;
		_player.update( deltaTime );
		applyTimeline();
	}

	void SequencePlayerComponent::play()
	{
		_player.setFramesPerSecond( _framesPerSecond );
		_player.setLoop( _bLoop == SW_TRUE );
		_player.play();
		applyTimeline();
	}

	void SequencePlayerComponent::stop()
	{
		_player.stop();
	}

	void SequencePlayerComponent::pause()
	{
		_player.pause();
	}

	void SequencePlayerComponent::resume()
	{
		_player.resume();
	}

	void SequencePlayerComponent::applyTimeline()
	{
		GameObject* pOwner = getOwner();
		if ( pOwner == nullptr )
			return;
		GameObjectManager* pManager = pOwner->getManager();
		if ( pManager == nullptr )
			return;

		const int32 prevFrame	 = _player.getPreviousFrame();
		const int32 currentFrame = _player.getCurrentFrame();

		SequenceTimelineUtil::applyFrame( pManager, _player.getAsset(), currentFrame, prevFrame );
	}
} // namespace sw

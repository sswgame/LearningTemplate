#include "pch.h"

#include "Engine/Object/Component/SequencePlayerComponent.h"

#include "Core/Log/Logger.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"

namespace sw
{
	SW_LOG_CALLER( "SequencePlayer" );

	SequencePlayerComponent::SequencePlayerComponent()
		: _sequencePath{}
		, _framesPerSecond{ 30.0f }
		, _bLoop{ false }
		, _bAutoPlay{ true }
		, _player{}
	{
		setCanEverTick( true );
	}

	void SequencePlayerComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::PostUpdate );
		_player.setFramesPerSecond( _framesPerSecond );
		_player.setLoop( _bLoop );
		if ( _sequencePath.empty() == false )
			_player.loadFromFile( _sequencePath );
		if ( _bAutoPlay )
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
		_player.setLoop( _bLoop );
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

		vector<const SequenceTrackItem*> listActive;
		_player.collectActiveItems( listActive );
		for ( const SequenceTrackItem* pItem : listActive )
		{
			if ( pItem == nullptr || pItem->_targetObject.empty() )
				continue;
			GameObject* pTarget = pManager->findGameObjectByName( hashed_string( pItem->_targetObject.c_str() ) );
			if ( pTarget == nullptr )
				continue;
			if ( pItem->_type == 0 )
				pTarget->setActive( true );
		}

		const vector<SequenceTrackItem>& listItem = _player.getAsset()._listItem;
		for ( const SequenceTrackItem& item : listItem )
		{
			if ( item._type != 1 )
				continue;
			if ( prevFrame >= item._start || item._start > currentFrame )
				continue;
			SW_LOG_INFO( "Sequence event %# on %#", item._name.c_str(), item._targetObject.c_str() );
		}
	}
} // namespace sw

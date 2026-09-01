#include "pch.h"

#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	SpriteAnimatorComponent::SpriteAnimatorComponent()
		: _animationGraphPath{}
		, _graph{}
		, _currentAnimation{}
		, _listAnimation{}
		, _frameRate{ 0.0f }
		, _frameTimer{ 0.0f }
		, _currentFrame{ 0 }
		, _totalFrames{ 0 }
		, _bRepeat{ SW_FALSE }
		, _bPlaying{ SW_FALSE }
		, _bPaused{ SW_FALSE }
		, _bGraphLoaded{ SW_FALSE }
		, _reserved{ 0 }
	{
		setCanEverTick( true );
	}

	void SpriteAnimatorComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		setTickGroup( TickGroup::PostPhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			pGameObject->addTag( "Animator"_tag );

		tryLoadAnimationGraph();

		if ( _listAnimation.empty() == false )
			play( _listAnimation[0], _bRepeat == SW_TRUE );
	}

	void SpriteAnimatorComponent::onEndPlay()
	{
		stop();
		Component::onEndPlay();
	}

	void SpriteAnimatorComponent::onTick( float32 deltaTime )
	{
		if ( _bPlaying == SW_FALSE || _bPaused == SW_TRUE || _frameRate <= 0.0f || _totalFrames <= 0 )
			return;

		_frameTimer += deltaTime;
		const float32 frameDuration = 1.0f / _frameRate;
		const int32	  prevFrame		= _currentFrame;
		while ( _frameTimer >= frameDuration )
		{
			_frameTimer -= frameDuration;
			_currentFrame++;

			if ( _currentFrame >= _totalFrames )
			{
				if ( _bRepeat == SW_TRUE )
					_currentFrame = 0;
				else if ( tryAdvanceGraphNode() )
					return;
				else
				{
					_currentFrame = _totalFrames - 1;
					_bPlaying	  = SW_FALSE;
					break;
				}
			}
		}

		if ( _currentFrame != prevFrame )
			updateSpriteFrame();
	}

	void SpriteAnimatorComponent::play( const string& animName, bool loop )
	{
		_currentAnimation = animName;
		_bRepeat		  = loop ? SW_TRUE : SW_FALSE;
		_bPlaying		  = SW_TRUE;
		_bPaused		  = SW_FALSE;
		_currentFrame	  = 0;
		_frameTimer		  = 0.0f;
		updateSpriteFrame();
	}

	void SpriteAnimatorComponent::stop()
	{
		_bPlaying	  = SW_FALSE;
		_bPaused	  = SW_FALSE;
		_currentFrame = 0;
		_frameTimer	  = 0.0f;
	}

	void SpriteAnimatorComponent::pause()
	{
		_bPaused = SW_TRUE;
	}

	void SpriteAnimatorComponent::resume()
	{
		_bPaused = SW_FALSE;
	}

	void SpriteAnimatorComponent::setFrame( int32 frame )
	{
		_currentFrame = frame >= 0 ? frame : 0;
		updateSpriteFrame();
	}

	string SpriteAnimatorComponent::getCurrentAnimation() const
	{
		return _currentAnimation;
	}

	void SpriteAnimatorComponent::setCurrentAnimation( const string& anim )
	{
		_currentAnimation = anim;
	}

	bool SpriteAnimatorComponent::isRepeating() const
	{
		return _bRepeat == SW_TRUE;
	}

	void SpriteAnimatorComponent::setRepeat( bool bLoop )
	{
		_bRepeat = bLoop ? SW_TRUE : SW_FALSE;
	}

	float32 SpriteAnimatorComponent::getFrameRate() const
	{
		return _frameRate;
	}

	void SpriteAnimatorComponent::setFrameRate( float32 rate )
	{
		_frameRate = rate;
	}

	int32 SpriteAnimatorComponent::getCurrentFrame() const
	{
		return _currentFrame;
	}

	bool SpriteAnimatorComponent::isPlaying() const
	{
		return _bPlaying == SW_TRUE;
	}

	bool SpriteAnimatorComponent::isPaused() const
	{
		return _bPaused == SW_TRUE;
	}

	void SpriteAnimatorComponent::tryLoadAnimationGraph()
	{
		_bGraphLoaded = SW_FALSE;
		_graph		  = AnimationGraphAsset{};
		if ( _animationGraphPath.empty() )
			return;
		if ( _graph.loadFromFile( _animationGraphPath ) == false )
			return;
		_bGraphLoaded = SW_TRUE;
		_graph.collectNodeNames( _listAnimation );
	}

	bool SpriteAnimatorComponent::tryAdvanceGraphNode()
	{
		if ( _bGraphLoaded == SW_FALSE )
			return false;
		const AnimationGraphNode* pNode = _graph.findNodeByName( _currentAnimation );
		if ( pNode == nullptr )
			return false;
		const int32				  nextId = _graph.findFirstOutgoingNodeId( pNode->_id );
		const AnimationGraphNode* pNext	 = _graph.findNode( nextId );
		if ( pNext == nullptr || pNext->_name.empty() )
			return false;
		play( pNext->_name, false );
		return true;
	}

	void SpriteAnimatorComponent::updateSpriteFrame()
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject == nullptr )
			return;

		SpriteComponent* pSpriteComp = pGameObject->getComponent<SpriteComponent>();
		if ( pSpriteComp == nullptr )
			return;

		if ( _currentAnimation.empty() )
			return;

		StringBuilder<constant::kMaxBuffer64> sb;
		sb.append( _currentAnimation ).append( '-' ).append( _currentFrame );
		pSpriteComp->setSpriteName( sb.c_str() );
	}
} // namespace sw

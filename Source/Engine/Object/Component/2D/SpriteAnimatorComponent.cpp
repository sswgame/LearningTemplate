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
		, _bRepeat{ false }
		, _bPlaying{ false }
		, _bPaused{ false }
		, _bGraphLoaded{ false }
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

		if ( _currentAnimation.empty() )
		{
			if ( _bGraphLoaded )
			{
				const AnimationGraphNode* pEntry = _graph.findEntryNode();
				if ( pEntry != nullptr )
					_currentAnimation = pEntry->_name;
			}
			else if ( _listAnimation.empty() == false )
				_currentAnimation = _listAnimation[0];
		}

		_bPlaying	  = ( _currentAnimation.empty() == false );
		_bPaused	  = false;
		_currentFrame = 0;
		_frameTimer	  = 0.0f;
		if ( _bGraphLoaded == false && _totalFrames <= 0 && _listAnimation.empty() == false )
			_totalFrames = static_cast<int32>( _listAnimation.size() );

		updateSpriteFrame();
	}

	void SpriteAnimatorComponent::onEndPlay()
	{
		SceneComponent::onEndPlay();
	}

	void SpriteAnimatorComponent::onTick( float32 deltaTime )
	{
		SceneComponent::onTick( deltaTime );

		if ( _bPlaying == false || _bPaused || _currentAnimation.empty() )
			return;

		const float32 frameDuration = ( _frameRate > 0.0f ) ? ( 1.0f / _frameRate ) : 0.1f;
		_frameTimer += deltaTime;

		while ( _frameTimer >= frameDuration )
		{
			_frameTimer -= frameDuration;
			_currentFrame++;

			if ( _totalFrames <= 0 )
			{
				_currentFrame = 0;
				_bPlaying	  = false;
				break;
			}

			if ( _currentFrame >= _totalFrames )
			{
				if ( _bRepeat )
					_currentFrame = 0;
				else if ( tryAdvanceGraphNode() )
					break;
				else
				{
					_currentFrame = _totalFrames > 0 ? ( _totalFrames - 1 ) : 0;
					_bPlaying	  = false;
					break;
				}
			}
			updateSpriteFrame();
		}
	}

	void SpriteAnimatorComponent::play( const string& animName, bool loop )
	{
		_currentAnimation = animName;
		_bRepeat		  = loop;
		_bPlaying		  = true;
		_bPaused		  = false;
		_currentFrame	  = 0;
		_frameTimer		  = 0.0f;
		updateSpriteFrame();
	}

	void SpriteAnimatorComponent::stop()
	{
		_bPlaying	  = false;
		_bPaused	  = false;
		_currentFrame = 0;
		_frameTimer	  = 0.0f;
	}

	void SpriteAnimatorComponent::pause()
	{
		_bPaused = true;
	}

	void SpriteAnimatorComponent::resume()
	{
		_bPaused = false;
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
		return _bRepeat;
	}

	void SpriteAnimatorComponent::setRepeat( bool bLoop )
	{
		_bRepeat = bLoop;
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
		return _bPlaying;
	}

	bool SpriteAnimatorComponent::isPaused() const
	{
		return _bPaused;
	}

	void SpriteAnimatorComponent::tryLoadAnimationGraph()
	{
		_bGraphLoaded = false;
		_graph		  = AnimationGraphAsset{};
		if ( _animationGraphPath.empty() )
			return;
		if ( _graph.loadFromFile( _animationGraphPath ) == false )
			return;
		_bGraphLoaded = true;
		_graph.collectNodeNames( _listAnimation );
	}

	bool SpriteAnimatorComponent::tryAdvanceGraphNode()
	{
		if ( _bGraphLoaded == false )
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

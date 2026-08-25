#include "pch.h"

#include "Engine/Object/Component/2D/SpriteAnimatorComponent.h"
#include "Engine/Object/Component/2D/SpriteComponent.h"
#include "Engine/Object/Component/TagSystem.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
	void SpriteAnimatorComponent::onBeginPlay()
	{
		SceneComponent::onBeginPlay();
		setTickGroup( TickGroup::PostPhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			pGameObject->addTag( "Animator"_tag );
			if ( pGameObject->getComponent<SpriteAnimatorData>() == nullptr )
				pGameObject->addComponent<SpriteAnimatorData>();
		}

		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
		{
			if ( pData->currentAnimation.empty() && pData->animations.empty() == false )
				pData->currentAnimation = pData->animations[0];

			pData->bPlaying		= ( pData->currentAnimation.empty() == false );
			pData->bPaused		= false;
			pData->currentFrame = 0;
			pData->frameTimer	= 0.0f;
			if ( pData->totalFrames <= 0 && pData->animations.empty() == false )
				pData->totalFrames = static_cast<int32>( pData->animations.size() );
		}

		updateSpriteFrame();
	}

	void SpriteAnimatorComponent::onEndPlay()
	{
		SceneComponent::onEndPlay();
	}

	void SpriteAnimatorComponent::onTick( float32 deltaTime )
	{
		SceneComponent::onTick( deltaTime );

		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData == nullptr || pData->bPlaying == false || pData->bPaused || pData->currentAnimation.empty() )
			return;

		const float32 frameDuration = ( pData->frameRate > 0.0f ) ? ( 1.0f / pData->frameRate ) : 0.1f;
		pData->frameTimer += deltaTime;

		while ( pData->frameTimer >= frameDuration )
		{
			pData->frameTimer -= frameDuration;
			pData->currentFrame++;

			if ( pData->totalFrames <= 0 )
			{
				pData->currentFrame = 0;
				pData->bPlaying		= false;
				break;
			}

			if ( pData->currentFrame >= pData->totalFrames )
			{
				if ( pData->repeat )
					pData->currentFrame = 0;
				else
				{
					pData->currentFrame = pData->totalFrames > 0 ? ( pData->totalFrames - 1 ) : 0;
					pData->bPlaying		= false;
					break;
				}
			}
			updateSpriteFrame();
		}
	}

	void SpriteAnimatorComponent::play( const string& animName, bool loop )
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
		{
			pData->currentAnimation = animName;
			pData->repeat			= loop;
			pData->bPlaying			= true;
			pData->bPaused			= false;
			pData->currentFrame		= 0;
			pData->frameTimer		= 0.0f;
		}
		updateSpriteFrame();
	}

	void SpriteAnimatorComponent::stop()
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
		{
			pData->bPlaying		= false;
			pData->bPaused		= false;
			pData->currentFrame = 0;
			pData->frameTimer	= 0.0f;
		}
	}

	void SpriteAnimatorComponent::pause()
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->bPaused = true;
	}

	void SpriteAnimatorComponent::resume()
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->bPaused = false;
	}

	void SpriteAnimatorComponent::setFrame( int32 frame )
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->currentFrame = frame >= 0 ? frame : 0;
		updateSpriteFrame();
	}

	string SpriteAnimatorComponent::getCurrentAnimation() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->currentAnimation;
		return "";
	}

	void SpriteAnimatorComponent::setCurrentAnimation( const string& anim )
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->currentAnimation = anim;
	}

	bool SpriteAnimatorComponent::isRepeating() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->repeat;
		return false;
	}

	void SpriteAnimatorComponent::setRepeat( bool bLoop )
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->repeat = bLoop;
	}

	float32 SpriteAnimatorComponent::getFrameRate() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->frameRate;
		return 0.0f;
	}

	void SpriteAnimatorComponent::setFrameRate( float32 rate )
	{
		SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			pData->frameRate = rate;
	}

	int32 SpriteAnimatorComponent::getCurrentFrame() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->currentFrame;
		return 0;
	}

	bool SpriteAnimatorComponent::isPlaying() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->bPlaying;
		return false;
	}

	bool SpriteAnimatorComponent::isPaused() const
	{
		const SpriteAnimatorData* pData = getAnimatorData();
		if ( pData != nullptr )
			return pData->bPaused;
		return false;
	}

	SpriteAnimatorData* SpriteAnimatorComponent::getAnimatorData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<SpriteAnimatorData>().get();
		return nullptr;
	}

	void SpriteAnimatorComponent::updateSpriteFrame()
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			SpriteComponent* pSpriteComp = pGameObject->getComponent<SpriteComponent>().get();
			if ( pSpriteComp != nullptr )
			{
				const SpriteAnimatorData* pData = getAnimatorData();
				if ( pData != nullptr )
				{
					if ( pData->currentAnimation.empty() == false )
					{
						StringBuilder<constant::kMaxBuffer64> sb;
						sb.append( pData->currentAnimation ).append( '-' ).append( pData->currentFrame );
						pSpriteComp->setSpriteName( sb.c_str() );
					}
				}
			}
		}
	}
} // namespace sw

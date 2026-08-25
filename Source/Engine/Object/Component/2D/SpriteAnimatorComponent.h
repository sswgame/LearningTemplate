/**
 * @file SpriteAnimatorComponent.h
 * @brief 2D Sprite Animator Component
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for Sprite Animation Playback
	 */
	REFLECT()
	struct SW_API SpriteAnimatorData
	{
		REFLECT_BODY();
		string		   currentAnimation{ "" };
		bool		   repeat{ false };
		vector<string> animations{};
		float32		   frameRate{ 0.0f };
		float32		   frameTimer{ 0.0f };
		int32		   currentFrame{ 0 };
		int32		   totalFrames{ 0 };
		bool		   bPlaying{ false };
		bool		   bPaused{ false };
	};

	REFLECT()
	class SW_API SpriteAnimatorComponent : public SceneComponent
	{
	public:
		REFLECT_BODY();
		SpriteAnimatorComponent()												 = default;
		virtual ~SpriteAnimatorComponent() override								 = default;
		SpriteAnimatorComponent( SpriteAnimatorComponent&& ) noexcept			 = default;
		SpriteAnimatorComponent& operator=( SpriteAnimatorComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		void play( const string& animName, bool loop = true );
		void stop();
		void pause();
		void resume();
		void setFrame( int32 frame );

		string getCurrentAnimation() const;
		void   setCurrentAnimation( const string& anim );

		bool isRepeating() const;
		void setRepeat( bool bLoop );

		float32 getFrameRate() const;
		void	setFrameRate( float32 rate );

		int32 getCurrentFrame() const;
		bool  isPlaying() const;
		bool  isPaused() const;

		SpriteAnimatorData* getAnimatorData() const;

	private:
		void updateSpriteFrame();
	};
} // namespace sw

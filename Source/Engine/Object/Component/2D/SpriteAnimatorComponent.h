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
	namespace generated
	{
		struct sw_SpriteAnimatorComponent_Registrar;
	} // namespace generated

	REFLECT()
	class SW_API SpriteAnimatorComponent : public SceneComponent
	{
		friend struct ::sw::generated::sw_SpriteAnimatorComponent_Registrar;

	public:
		REFLECT_BODY();
		SpriteAnimatorComponent();
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

	private:
		void updateSpriteFrame();

		PROPERTY()
		string _currentAnimation;
		PROPERTY()
		vector<string> _listAnimation;
		PROPERTY()
		float32 _frameRate;
		float32 _frameTimer;
		PROPERTY()
		int32 _currentFrame;
		PROPERTY()
		int32 _totalFrames;
		PROPERTY()
		bool _bRepeat;
		bool _bPlaying;
		bool _bPaused;
	};
} // namespace sw

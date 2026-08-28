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

	REFLECT( Category = "Animation 2D", DisplayName = "Sprite Animator Component", Tooltip = "2D Sprite frame animation controller" )
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
		FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
		void stop();
		FUNCTION( Category = "Playback", DisplayName = "Pause", CallInEditor )
		void pause();
		FUNCTION( Category = "Playback", DisplayName = "Resume", CallInEditor )
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

		PROPERTY( Category = "Animation", DisplayName = "Current Animation", Tooltip = "Currently playing animation name" )
		string _currentAnimation;
		PROPERTY( Category = "Animation", DisplayName = "Animation List", Tooltip = "Available animation names" )
		vector<string> _listAnimation;
		PROPERTY( Category = "Playback", DisplayName = "Frame Rate", Tooltip = "Playback speed in FPS", Min = 1.0, Max = 120.0, Meta = "Units=fps" )
		float32 _frameRate;
		float32 _frameTimer;
		PROPERTY( Category = "Playback", DisplayName = "Current Frame", Tooltip = "Current playback frame index", Min = 0.0 )
		int32 _currentFrame;
		PROPERTY( Category = "Playback", DisplayName = "Total Frames", Tooltip = "Total frame count of active animation", ReadOnly )
		int32 _totalFrames;
		PROPERTY( Category = "Playback", DisplayName = "Loop", Tooltip = "Loop playback when reaching the end" )
		bool _bRepeat;
		bool _bPlaying;
		bool _bPaused;
	};
} // namespace sw

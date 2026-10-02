/**
 * @file SpriteAnimatorComponent.h
 * @brief 2D 스프라이트 애니메이터 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Animation/AnimationGraphAsset.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SpriteClipAsset;
    class SpriteComponent;

    /**
     * @class SpriteAnimatorComponent
     * @brief 같은 오브젝트의 `SpriteComponent` 가 든 클립(`.sprite.json`)의 프레임을 시간에 맞춰 넘깁니다.
     * @details 애니메이션 이름은 클립의 이름 붙은 구간(`SpriteClipAsset::findFrameRange`)을 고르고, 프레임 수 · 프레임마다의 시간도 클립에서 옵니다.
     *          프레임에 시간이 없으면(0) `_frameRate` 로 넘깁니다. 그래프(`_animationGraphPath`)는 애니메이션 이름과 "끝나면 다음" 을 줍니다.
     *          예전에는 프레임 수(`_totalFrames`)를 손으로 넣어야 했고, 넘긴 프레임은 "<애니>-<프레임>" 문자열로 스프라이트의 읽는 곳 없는 칸에
     *          적혔습니다 — 화면에는 아무것도 바뀌지 않았습니다. 지금은 스프라이트의 프레임 번호(`SpriteComponent::setClipFrame`)를 넘깁니다.
     */
    REFLECT( Category = "Animation 2D", DisplayName = "Sprite Animator Component", Tooltip = "2D Sprite frame animation controller" )
    class SW_API SpriteAnimatorComponent : public SceneComponent
    {
    public:
        REFLECT_BODY();
        SpriteAnimatorComponent();
        virtual ~SpriteAnimatorComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 이름의 구간을 처음부터 재생합니다. 반복 여부는 클립의 구간이 정합니다(이름 없는 옛 클립은 반복). */
        void play( const string& animName );
        /** @brief 이름의 구간을 처음부터 재생합니다. 반복 여부를 직접 정합니다(그래프의 "끝나면 다음" 은 반복하지 않습니다). */
        void play( const string& animName, bool loop );
        FUNCTION( Category = "Playback", DisplayName = "Stop", CallInEditor )
        void stop();
        FUNCTION( Category = "Playback", DisplayName = "Pause", CallInEditor )
        void pause();
        FUNCTION( Category = "Playback", DisplayName = "Resume", CallInEditor )
        void resume();
        /** @brief 구간 안의 프레임으로 갑니다(0 아래는 0, 구간보다 크면 마지막). */
        void setFrame( int32 frame );

        string        getCurrentAnimation() const;
        void          setCurrentAnimation( const string& anim );
        const string& getAnimationGraphPath() const { return _animationGraphPath; }

        bool isRepeating() const;
        void setRepeat( bool bLoop );

        /** @brief 시간이 없는 프레임(0 ms)을 넘기는 속도입니다(초당 프레임). 클립 프레임에 시간이 있으면 그것이 이깁니다. */
        float32 getFrameRate() const;
        void    setFrameRate( float32 rate );

        /** @brief 지금 구간의 프레임 수입니다. 클립에서 옵니다(손으로 정하지 않습니다). 클립이 없으면 1 입니다. */
        int32 getTotalFrames() const;
        /** @brief 지금 구간이 클립의 몇 번째 프레임에서 시작하는지입니다. */
        int32 getFirstClipFrame() const { return _firstClipFrame; }

        /** @brief 구간 안의 지금 프레임입니다(0 .. getTotalFrames() - 1). 스프라이트가 보이는 클립 프레임은 `getFirstClipFrame() + getCurrentFrame()` 입니다. */
        int32 getCurrentFrame() const;
        bool  isPlaying() const;
        bool  isPaused() const;

    private:
        void               tryLoadAnimationGraph();
        [[nodiscard]] bool tryAdvanceGraphNode();
        /** @brief 같은 오브젝트의 스프라이트입니다. 없으면 nullptr 입니다. */
        SpriteComponent* findSprite() const;
        /** @brief 같은 오브젝트 스프라이트의 클립입니다. 없으면 nullptr 입니다. */
        const SpriteClipAsset* findClip() const;
        /**
         * @brief 지금 애니메이션 이름의 구간(시작 · 개수 · 반복)을 클립에서 잡습니다. 이름이 클립에 없으면 한 번 알리고 프레임 0 하나로 둡니다.
         * @param bTakeLoopFromClip true 면 반복 여부도 클립 구간의 것으로 정합니다.
         */
        void resolveFrameRange( bool bTakeLoopFromClip );
        /** @brief 구간 안의 프레임 하나를 보일 시간(초)입니다. 클립 프레임의 시간, 없으면 1 / `_frameRate` 입니다. */
        float32 getFrameDuration( int32 frameInRange ) const;
        /** @brief 스프라이트에 지금 프레임(구간 시작 + 지금 프레임)을 넘깁니다. */
        void updateSpriteFrame();

        PROPERTY( Category = "Animation", DisplayName = "Animation Graph", AssetPath, AssetType = "AnimationGraph", Tooltip = "Animation graph asset used by this animator" )
        string              _animationGraphPath;
        AnimationGraphAsset _graph;
        PROPERTY( Category = "Animation", DisplayName = "Current Animation", Tooltip = "Currently playing animation name" )
        string _currentAnimation;
        PROPERTY( Category = "Animation", DisplayName = "Animation List", Tooltip = "Available animation names" )
        vector<string> _listAnimation;
        PROPERTY( Category = "Playback", DisplayName = "Frame Rate", Tooltip = "Playback speed in FPS for frames without their own duration", Min = 1.0, Max = 120.0,
                  Meta = "Units=fps" )
        float32 _frameRate;
        float32 _frameTimer;
        PROPERTY( Category = "Playback", DisplayName = "Current Frame", Tooltip = "Current playback frame index within the animation", Min = 0.0 )
        int32 _currentFrame;
        PROPERTY( Category = "Playback", DisplayName = "Total Frames", Tooltip = "Frame count of the active animation, taken from the sprite clip", ReadOnly )
        int32 _totalFrames;
        int32 _firstClipFrame; ///< 지금 구간이 시작하는 클립 프레임입니다(저장하지 않습니다 — 클립에서 다시 잡습니다)
        /// @brief 구간을 잡을 때 본 클립입니다. **정체성 비교만** 하고 역참조하지 않습니다 — 스프라이트의 클립이 바뀌면 구간을 다시 잡습니다.
        const SpriteClipAsset* _pRangeClip;
        PROPERTY( Category = "Playback", DisplayName = "Loop", Tooltip = "Loop playback when reaching the end" )
        uint8                  _bRepeat      : 1;
        uint8                  _bPlaying     : 1;
        uint8                  _bPaused      : 1;
        uint8                  _bGraphLoaded : 1;
        [[maybe_unused]] uint8 _reserved     : 4;
    };
} // namespace sw

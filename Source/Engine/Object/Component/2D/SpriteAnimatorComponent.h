/**
 * @file SpriteAnimatorComponent.h
 * @brief 2D 스프라이트 애니메이터 컴포넌트입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Animation/AnimGraphAsset.h"
#include "Engine/Animation/AnimGraphPlayer.h"
#include "Engine/Animation/SpriteClipPlayable.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    class SpriteAnimatorComponent;
    class SpriteClipAsset;
    class SpriteComponent;

    /**
     * @class SpriteAnimatorClipSource
     * @brief 상태 기계(`AnimGraphPlayer`)가 상태 이름을 재생할 것으로 풀 때 애니메이터의 구간(`SpriteClipPlayable`)을 그 이름으로 맞춰 줍니다.
     * @details 리플렉션 컴포넌트는 기반 클래스 하나만 두므로 인터페이스는 이 작은 객체가 구현합니다(스켈레탈 애니메이터의 `SkeletalAnimatorBinding` 과 같은 자리).
     */
    class SW_API SpriteAnimatorClipSource final : public IAnimPlayableSource
    {
    public:
        explicit SpriteAnimatorClipSource( SpriteAnimatorComponent& owner );
        const IAnimPlayable* findPlayable( const hashed_string& name ) const override;

    private:
        SpriteAnimatorComponent& _owner;
    };
} // namespace sw

namespace sw
{
    /**
     * @class SpriteAnimatorComponent
     * @brief 같은 오브젝트의 `SpriteComponent` 가 든 클립(`.sprite.json`)의 프레임을 시간에 맞춰 넘깁니다.
     * @details 애니메이션 이름은 클립의 이름 붙은 구간(`SpriteClipAsset::findFrameRange`)을 고르고, 프레임 수 · 프레임마다의 시간도 클립에서 옵니다.
     *          프레임에 시간이 없으면(0) `_frameRate` 로 넘깁니다. 그래프(`_animGraphPath`)는 애니메이션 이름과 "끝나면 다음" 을 줍니다.
     *          넘긴 프레임은 스프라이트의 프레임 번호(`SpriteComponent::setClipFrame`)로 갑니다.
     *
     *          **트랜스폼 키(`transformKeys`).** 클립에 키가 있으면 재생 중 스프라이트의 로컬 위치 x · y 와 Z 축 회전을 키 값으로 정합니다
     *          (z 위치 · 다른 축 회전 · 스케일은 그대로). 키 시각은 클립 타임라인의 초이고, 지금 구간의 프레임 시작 시각 + 그 프레임 안에서 흐른
     *          시간에서 읽습니다(`SpriteClipAsset::sampleTransformKey`). 키가 없는 클립은 트랜스폼에 손대지 않습니다.
     *          **오브젝트의 루트(primary 씬 컴포넌트)는 움직이지 않습니다.** 스프라이트가 루트면 키를 적용하지 않고 재생마다 한 번 알립니다 —
     *          루트를 덮어쓰면 게임 코드 · 물리가 옮긴 오브젝트 자리가 매 틱 키 값으로 되돌아갑니다. 키로 움직일 스프라이트는 루트 아래에 둡니다
     *          (언리얼은 루트 모션을 켜지 않으면 애니메이션이 액터 루트를 옮기지 않고, 유니티 2D 도 움직이는 스프라이트를 자식에 둡니다).
     */
    REFLECT( Category = "Animation 2D", DisplayName = "Sprite Animator Component", Tooltip = "2D Sprite frame animation controller" )
    class SW_API SpriteAnimatorComponent : public SceneComponent
    {
        friend class SpriteAnimatorClipSource;

    public:
        REFLECT_BODY();
        SpriteAnimatorComponent();
        virtual ~SpriteAnimatorComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( float32 deltaTime ) override;
        /**
         * @brief 그래프 경로(`_animGraphPath`)가 바뀌면(인스펙터 · 에셋 핫 리로드 알림 — 값이 같아도) 그래프를 다시 읽습니다.
         * @details 재생 중이면 지금 애니메이션이 새 그래프에도 있을 때 그대로 잇고, 없으면 새 목록의 첫 애니메이션을 처음부터 재생합니다.
         */
        void onPropertyChanged( hashed_string propertyName ) override;

        /** @brief 이름의 구간을 처음부터 재생합니다. 반복 여부는 클립의 구간이 정합니다(구간 이름이 없는 클립은 반복). */
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
        const string& getAnimGraphPath() const { return _animGraphPath; }

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
        void tryLoadAnimGraph();
        /** @brief 상태 기계가 다른 상태로 넘어갔으면(끝나면 다음) 이름 · 반복 · 구간을 따라 맞춥니다. */
        void syncStateFromPlayer();
        /** @brief 같은 오브젝트의 스프라이트입니다. 없으면 nullptr 입니다. */
        SpriteComponent* findSprite() const;
        /** @brief 같은 오브젝트 스프라이트의 클립입니다. 없으면 nullptr 입니다. */
        const SpriteClipAsset* findClip() const;
        /**
         * @brief 이름의 구간(시작 · 개수 · 반복)을 클립에서 잡아 `_playable` 에 맞춥니다. 이름이 클립에 없으면 한 번 알리고 프레임 0 하나로 둡니다.
         * @return 클립이 정한 반복 여부입니다(구간 이름이 없는 클립은 반복).
         */
        bool configurePlayable( const string& animName );
        /** @brief 클립이 없을 때 · 시간이 없는 프레임이 머무는 시간(1 / `_frameRate`)입니다. */
        float32 getFallbackFrameSeconds() const;
        /** @brief 스프라이트에 지금 프레임(구간 시작 + 지금 프레임)을 넘깁니다. */
        void updateSpriteFrame();
        /** @brief 클립 타임라인의 지금 시각(초)입니다 — 구간 시작 시각 + 재생 시각(구간 길이를 넘지 않습니다). */
        float32 computeClipTime() const;
        /** @brief 클립의 트랜스폼 키를 지금 시각에서 읽어 스프라이트의 로컬 위치 x · y · Z 회전에 씁니다. 키가 없거나 스프라이트가 루트면 쓰지 않습니다. */
        void applyTransformKeys();

        PROPERTY( Category = "Animation", DisplayName = "Animation Graph", AssetPath, AssetType = "AnimGraph", Tooltip = "Animation graph asset used by this animator" )
        string         _animGraphPath;
        AnimGraphAsset _graph;
        PROPERTY( Category = "Animation", DisplayName = "Current Animation", Tooltip = "Currently playing animation name" )
        string _currentAnimation;
        PROPERTY( Category = "Animation", DisplayName = "Animation List", Tooltip = "Available animation names" )
        vector<string> _listAnimation;
        PROPERTY( Category = "Playback", DisplayName = "Frame Rate", Tooltip = "Playback speed in FPS for frames without their own duration", Min = 1.0, Max = 120.0,
                  Meta = "Units=fps" )
        float32                  _frameRate;
        SpriteAnimatorClipSource _clipSource;  ///< 상태 이름 → `_playable` 풀이
        SpriteClipPlayable       _playable;    ///< 지금 구간(재생할 것). 스프라이트는 섞지 않으므로 하나면 된다
        AnimGraphPlayer          _graphPlayer; ///< 시간 · 반복 · 끝 · 다음 상태 — 스켈레탈 애니메이터와 같은 코드다
        PROPERTY( Category = "Playback", DisplayName = "Current Frame", Tooltip = "Current playback frame index within the animation", Min = 0.0 )
        int32 _currentFrame;
        PROPERTY( Category = "Playback", DisplayName = "Total Frames", Tooltip = "Frame count of the active animation, taken from the sprite clip", ReadOnly )
        int32 _totalFrames;
        /// @brief 구간을 잡을 때 본 클립입니다. **정체성 비교만** 하고 역참조하지 않습니다 — 스프라이트의 클립이 바뀌면 구간을 다시 잡습니다.
        const SpriteClipAsset* _pRangeClip;
        int32                  _firstClipFrame; ///< 지금 구간이 시작하는 클립 프레임입니다(저장하지 않습니다 — 클립에서 다시 잡습니다)
        PROPERTY( Category = "Playback", DisplayName = "Loop", Tooltip = "Loop playback when reaching the end" )
        uint8                  _bRepeat      : 1;
        uint8                  _bPlaying     : 1;
        uint8                  _bPaused      : 1;
        uint8                  _bGraphLoaded : 1;
        uint8                  _bRootWarned  : 1; ///< 이번 재생에서 "루트 스프라이트에는 키를 적용하지 않는다" 를 알렸다
        [[maybe_unused]] uint8 _reserved     : 3;
    };
} // namespace sw

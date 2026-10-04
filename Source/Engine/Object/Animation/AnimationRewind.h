/**
 * @file AnimationRewind.h
 * @brief 애니메이션 되감기 기록기 — 유닛 · 스프라이트 애니메이터마다 최근 몇 초의 포즈(압축) · 그래프 상태 · 알림 · 커브 · 루트 모션을 고리 버퍼에 둡니다.
 * @details 언리얼 Rewind Debugger(Animation Insights)의 자리입니다. 기본은 꺼져 있고(`-gv_animationRewind=1` · 콘솔 `anim.rewind on` · 에디터 패널),
 *          Shipping 에서는 통째로 빠집니다(`SW_ANIMATION_REWIND_ENABLED`). 기록은 평가가 끝난 뒤 게임 스레드에서 하고, 되감기(`setScrubTime`)
 *          동안 `AnimationSystem` 은 평가를 멈추고 기록된 포즈를 유닛에 겁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

/** @brief 되감기 기록기가 컴파일되는 구성이면 1 입니다. Shipping 은 0 이고 기록기 · 명령 · 패널이 모두 빠집니다. */
#if defined( SW_SHIPPING )
    #define SW_ANIMATION_REWIND_ENABLED 0
#else
    #define SW_ANIMATION_REWIND_ENABLED 1
#endif

#if SW_ANIMATION_REWIND_ENABLED

    #include "Core/Container/ComponentHandle.h"
    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/Math/MatrixMath.h"
    #include "Core/String/hashed_string.h"

    #include "Engine/Object/Animation/AnimationDebugState.h"

namespace sw
{
    class Component;
    class Pose;
    class SkeletalMeshComponent;

    /** @brief 기록한 것의 종류입니다. */
    enum class AnimationRewindKind : uint8
    {
        Skeletal = 0, ///< 스켈레탈 유닛(포즈 + 상태)
        Sprite,       ///< 스프라이트 애니메이터(상태 · 프레임만)
    };

    /**
     * @struct AnimationRewindFrame
     * @brief 한 프레임의 기록입니다. 포즈는 본마다 회전 int16 넷(정규화 · w ≥ 0) · 이동 int16 셋(프레임의 최대 크기로 나눔) · (스케일이 1 이 아닌
     *        본이 있으면) 스케일 int16 셋입니다 — 원래 40 바이트가 14 바이트(스케일 있으면 20)입니다.
     */
    struct AnimationRewindFrame
    {
        float64             _time{ 0.0 };     ///< 기록기 시계(초)
        uint64              _frameIndex{ 0 }; ///< 애니메이션 시스템 프레임 번호
        float4x4            _worldMatrix{};   ///< 기록 때의 유닛 월드 행렬(그리기용)
        vector<uint8>       _poseByte;        ///< 압축 포즈(스프라이트는 비었다)
        AnimationDebugState _state;           ///< 그래프 상태 · 알림 · 커브 · 루트 모션 · 스프라이트 프레임
        float32             _translationRange{ 0.0f };
        float32             _scaleRange{ 0.0f }; ///< 0 이면 스케일을 싣지 않았다(모두 1)
        uint32              _boneCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationRewindTrack
     * @brief 기록 대상 하나의 고리 버퍼입니다. 대상이 사라져도 창을 벗어날 때까지 남습니다(죽기 직전을 되감아 볼 수 있다).
     */
    struct SW_API AnimationRewindTrack
    {
        ComponentHandle              _target;
        string                       _label; ///< 오브젝트 이름(기록을 시작할 때)
        AnimationRewindKind          _kind{ AnimationRewindKind::Skeletal };
        vector<int32>                _listParentIndex; ///< 스켈레톤 부모 번호(그리기용 — 기록을 시작할 때의 스켈레톤)
        vector<AnimationRewindFrame> _listFrame;       ///< 고리 — `_head` 가 가장 오래된 것
        uint32                       _head{ 0 };
        uint32                       _count{ 0 };

        /** @brief 오래된 것부터 @p order 번째 프레임입니다. */
        const AnimationRewindFrame& getFrame( uint32 order ) const { return _listFrame[( _head + order ) % _listFrame.size()]; }
        /** @brief @p time 이하의 마지막 프레임입니다(그 앞이면 첫 프레임). 비었으면 nullptr 입니다. */
        const AnimationRewindFrame* findFrame( float64 time ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationRewindRecorder
     * @brief `AnimationSystem` 하나에 하나입니다. 게임 스레드에서만 씁니다.
     */
    class SW_API AnimationRewindRecorder
    {
    public:
        AnimationRewindRecorder();

        /** @brief 기록을 켜고 끕니다. 끄면 기록을 비웁니다. */
        void setEnabled( bool bEnabled );
        bool isEnabled() const { return _bEnabled == SW_TRUE; }
        /** @brief 남길 시간(초)입니다. 줄이면 오래된 것부터 버립니다. */
        void    setWindowSeconds( float32 seconds );
        float32 getWindowSeconds() const { return _windowSeconds; }

        /**
         * @brief 프로세스 전체의 기록 요청입니다(`-gv_animationRewind` · `-gv_animationRewindSeconds` — 콘솔 `anim.rewind` · 에디터 패널이 바꾼다).
         *        씬마다 있는 기록기가 평가 앞에서 따릅니다(`syncWithRequest`).
         */
        static void    setRecordingRequested( bool bRecord );
        static bool    isRecordingRequested();
        static void    setRequestedWindowSeconds( float32 seconds );
        static float32 getRequestedWindowSeconds();
        /** @brief 요청과 다르면 켜고 끄고 창을 맞춥니다. */
        void syncWithRequest();
        /** @brief 프레임을 마칩니다 — 창 밖을 버리고 시계를 흘립니다(평가 한 번에 한 번, 기록 뒤). */
        void    endFrame( float32 deltaSeconds );
        float64 getClock() const { return _clock; }
        /** @brief 스켈레탈 유닛의 이번 프레임을 기록합니다(평가 뒤 · 게임 스레드). */
        void recordUnit( const SkeletalMeshComponent& unit, uint64 frameIndex );
        /** @brief 포즈가 없는 대상(스프라이트 애니메이터)의 상태를 기록합니다. */
        void recordState( const Component& target, AnimationRewindKind kind, const AnimationDebugState& state, uint64 frameIndex );

        /** @brief 되감기 시각을 정합니다(기록기 시계). 그동안 평가가 멈추고 기록된 포즈가 걸립니다. */
        void setScrubTime( float64 time );
        /** @brief 되감기를 끝냅니다. */
        void    clearScrub() { _bScrubbing = SW_FALSE; }
        bool    isScrubbing() const { return _bScrubbing == SW_TRUE; }
        float64 getScrubTime() const { return _scrubTime; }
        /** @brief 기록된 가장 이른 · 늦은 시각입니다(비었으면 둘 다 시계). */
        float64 getEarliestTime() const;
        float64 getLatestTime() const;

        const vector<AnimationRewindTrack>& getTracks() const { return _listTrack; }
        /** @brief 대상의 기록입니다. 없으면 nullptr 입니다. */
        const AnimationRewindTrack* findTrack( const ComponentHandle& target ) const;
        /** @brief 기록이 든 바이트(포즈 · 프레임 머리 · 상태 근사)입니다. */
        uint64 getByteCount() const;
        /** @brief 기록을 비웁니다. */
        void clear();

        /** @brief 압축 포즈를 풉니다. 본 수가 다르면 @p outPose 를 맞춥니다. */
        static void decodePose( const AnimationRewindFrame& frame, Pose& outPose );
        /** @brief 포즈를 압축합니다(시험이 오차를 잰다). */
        static void encodePose( const Pose& pose, AnimationRewindFrame& outFrame );
        /** @brief 풀어 낸 포즈의 모델 공간 위치(월드)를 구합니다 — 그리기용. */
        static void computeWorldBonePositions( const AnimationRewindTrack& track, const AnimationRewindFrame& frame, vector<float3>& outListPosition );

    private:
        /** @brief 대상의 트랙을 찾거나 만듭니다. */
        AnimationRewindTrack& acquireTrack( const Component& target, AnimationRewindKind kind );
        /** @brief 트랙에 프레임 자리를 하나 냅니다(창 밖이면 가장 오래된 것을 다시 쓴다). */
        AnimationRewindFrame& appendFrame( AnimationRewindTrack& track );
        /** @brief 창 밖 프레임과 빈 트랙을 버립니다. */
        void trimToWindow();

        vector<AnimationRewindTrack> _listTrack;
        float64                      _clock;
        float64                      _scrubTime;
        float32                      _windowSeconds;
        uint8                        _bEnabled   : 1;
        uint8                        _bScrubbing : 1;
        [[maybe_unused]] uint8       _reserved   : 6;
    };
} // namespace sw

#endif

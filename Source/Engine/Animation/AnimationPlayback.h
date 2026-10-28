/**
 * @file AnimationPlayback.h
 * @brief 2D 와 3D 가 함께 쓰는 재생 핵심입니다. 재생할 것(`IAnimationPlayable`), 시간 커서, 알림 트랙을 담습니다.
 * @details 스프라이트 클립 구간과 스켈레탈 클립은 "샘플이 무엇을 내는가"만 다르다(프레임 번호와 본 포즈). 시간을 흘리고, 되감고,
 *          끝을 알고, 지나간 알림을 고르는 일은 둘이 같으므로 여기에 한 벌만 둔다(`AnimationPlayer` 와 `AnimationGraphPlayer` 도 이것만 본다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/AnimationNotifyPhase.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct AnimationFiredNotify;
    struct AnimationTimeStep;

    class AnimationNotifyTrack;
    class IAnimationPoseNode;

    /**
     * @class IAnimationPlayable
     * @brief 재생할 수 있는 것 하나입니다(스켈레탈 `AnimationClip`, 스프라이트 클립의 이름 붙은 구간). 길이, 기본 반복, 알림만 압니다.
     */
    class SW_API IAnimationPlayable
    {
    public:
        IAnimationPlayable()                                       = default;
        virtual ~IAnimationPlayable()                              = default;
        IAnimationPlayable( const IAnimationPlayable& )            = default;
        IAnimationPlayable& operator=( const IAnimationPlayable& ) = default;

        /** @brief 한 바퀴 길이(초)를 반환합니다. 0 보다 커야 재생됩니다. */
        virtual float32 getPlayLength() const = 0;
        /** @brief 데이터가 정한 기본 반복 여부입니다. */
        virtual bool isLoopingByDefault() const = 0;
        /** @brief 알림 트랙입니다. 없으면 nullptr 입니다. */
        virtual const AnimationNotifyTrack* findNotifyTrack() const { return nullptr; }
        /**
         * @brief 커서 걸음 @p step 이 지난 알림을 @p outListFired 에 덧붙입니다. @p weight 는 이 재생할 것의 섞임 가중치입니다.
         * @details 기본은 자기 알림 트랙(`findNotifyTrack`)에서 고르고 출처는 자신이다. 여러 클립을 섞는 것(블렌드 스페이스 상태)은 걸음을
         *          표본 클립의 시각으로 옮겨 그 클립을 출처로 낸다 — 플레이어는 무엇을 섞는지 모르고 이것만 부른다.
         */
        virtual void collectFiredNotifies( const AnimationTimeStep& step, float32 weight, vector<AnimationFiredNotify>& outListFired ) const;
        /**
         * @brief 포즈를 평가해 내는 노드(`IAnimationPoseNode`, 스켈레탈 전용)면 그것을, 클립 · 스프라이트 구간처럼 시각으로 샘플하는 것이면 nullptr 입니다.
         * @details 포즈를 내는 쪽은 이 값으로 클립과 노드를 가른다(재생할 것을 클립으로 캐스팅하기 전에 묻는다).
         */
        virtual const IAnimationPoseNode* findPoseNode() const { return nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationTimeStep
     * @brief 커서가 한 번 나아간 구간입니다. 알림은 이 구간에서 고릅니다.
     * @details 구간은 `(_previousTime, _currentTime]` 이고, `_wrapCount` 번 끝을 지나 처음으로 돌아왔다. 재생 직후 첫 걸음은 시작 시각을
     *          포함한다(`_bIncludesStart`). 0 초의 알림이 시작에서 한 번 울린다.
     */
    struct AnimationTimeStep
    {
        float32 _previousTime{ 0.0f };
        float32 _currentTime{ 0.0f };
        uint32  _wrapCount{ 0 };
        uint8   _bReachedEnd{ SW_FALSE };    ///< 반복하지 않는 재생이 이번 걸음에 끝에 닿았습니다.
        uint8   _bIncludesStart{ SW_FALSE }; ///< 시작 시각 자체를 구간에 넣습니다(첫 걸음).
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationClipCursor
     * @brief 재생 위치 하나입니다. 반복이면 한 바퀴 안으로 감고(오래 켜 둔 루프의 float 정밀도를 지킵니다), 아니면 끝에서 멈춥니다.
     */
    class SW_API AnimationClipCursor
    {
    public:
        AnimationClipCursor() = default;

        /** @brief 위치를 정하고 다음 걸음이 그 시각을 포함하게 합니다. */
        void reset( float32 time = 0.0f );
        /**
         * @brief @p deltaSeconds(0 이상)만큼 나아갑니다.
         * @param playLength 한 바퀴 길이(초). 0 이하면 움직이지 않습니다.
         */
        AnimationTimeStep advance( float32 deltaSeconds, float32 playLength, bool bLoop );
        /** @brief 지금 시각(초)입니다. */
        float32 getTime() const { return _time; }
        /** @brief 정규화 위치(0..1)입니다. */
        float32 computeNormalizedTime( float32 playLength ) const;
        /** @brief 정규화 위치로 옮깁니다(동기 그룹). 알림 구간은 다음 걸음부터 셉니다. */
        void setNormalizedTime( float32 normalizedTime, float32 playLength );

    private:
        float32 _time{ 0.0f };
        uint8   _bFresh{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationNotifyEvent
     * @brief 클립 시각에 붙은 이름 하나입니다(발소리, 칼 휘두름 시작). `_duration` 이 0 보다 크면 구간 알림(NotifyState)입니다.
     *        시작 시각을 지날 때 `Begin`, 끝 시각(시작 + 길이, 한 바퀴 끝을 넘지 않음)을 지날 때 `End` 가 울립니다.
     *        스프라이트 클립 파일(`SpriteClipAsset`)은 이 구조체를 리플렉션으로 읽고 씁니다.
     */
    REFLECT()
    struct SW_API AnimationNotifyEvent
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Notify name the listeners match" )
        hashed_string _name;
        PROPERTY( Units = s, Tooltip = "Clip time of the notify, from the start of its range" )
        float32 _time{ 0.0f };
        PROPERTY( Units = s, Tooltip = "Length of a state notify (0: a one-shot notify)" )
        float32 _duration{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimationFiredNotify
     * @brief 이번 갱신에 울린 알림 하나입니다. `_weight` 는 그 클립의 섞임 가중치입니다(크로스페이드 중에는 두 클립 모두 울립니다).
     * @details `_pSource` 와 `_eventIndex` 가 구간 알림 하나를 가린다(같은 클립의 같은 알림이 `Begin` 과 `End` 에서 같은 값이다). `End` 면 `_time` 은 끝 시각이다.
     */
    struct AnimationFiredNotify
    {
        hashed_string             _name;
        const IAnimationPlayable* _pSource{ nullptr };
        float32                   _time{ 0.0f };
        float32                   _weight{ 1.0f };
        float32                   _duration{ 0.0f };
        uint32                    _eventIndex{ 0 };
        AnimationNotifyPhase      _phase{ AnimationNotifyPhase::Instant };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimationNotifyTrack
     * @brief 시각순으로 정렬된 알림 목록입니다. 커서 걸음 하나에서 지나간 것을 **정확히 한 번씩** 고릅니다(반복 경계 포함).
     */
    class SW_API AnimationNotifyTrack
    {
    public:
        AnimationNotifyTrack() = default;

        /** @brief 알림을 더합니다(시각순을 유지합니다). */
        void addEvent( const AnimationNotifyEvent& event );
        /** @brief 알림 목록입니다. */
        const vector<AnimationNotifyEvent>& getEvents() const { return _listEvent; }
        /** @brief 비었으면 true 입니다. */
        bool isEmpty() const { return _listEvent.empty(); }
        /** @brief 모두 지웁니다. */
        void clear() { _listEvent.clear(); }
        /**
         * @brief 걸음 @p step 이 지난 알림을 @p outListFired 에 시각 순서로 덧붙입니다. @p pSource 는 이 트랙을 가진 재생할 것입니다.
         * @details 반복을 한 번 넘으면 (이전, 끝] 과 [0, 현재] 를, 여러 번 넘으면 그 사이의 온 바퀴도 센다. 한 시각의 알림은 한 바퀴에 한 번이다.
         *          구간 알림은 시작 시각에서 `Begin`, 끝 시각에서 `End` 를 낸다. 같은 시각이면 `End` 가 먼저다(앞 구간이 닫히고 다음 구간이 열린다).
         */
        void collectFired( const AnimationTimeStep& step, float32 playLength, float32 weight, const IAnimationPlayable* pSource, vector<AnimationFiredNotify>& outListFired ) const;

    private:
        /** @brief 구간 [from, to] 의 알림 시작과 끝을 덧붙입니다. @p bIncludeFrom 이 false 면 from 은 뺍니다. */
        void collectRange( float32 fromTime, float32 toTime, bool bIncludeFrom, float32 playLength, float32 weight, const IAnimationPlayable* pSource,
                           vector<AnimationFiredNotify>& outListFired ) const;
        /** @brief 구간 알림의 끝 시각입니다(한 바퀴 끝을 넘지 않습니다). */
        static float32 computeEndTime( const AnimationNotifyEvent& event, float32 playLength );

        vector<AnimationNotifyEvent> _listEvent;
    };
} // namespace sw

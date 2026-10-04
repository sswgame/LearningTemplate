/**
 * @file AnimPlayback.h
 * @brief 2D · 3D 가 함께 쓰는 재생 핵심 — 재생할 것(`IAnimPlayable`), 시간 커서, 알림 트랙입니다.
 * @details 스프라이트 클립 구간과 스켈레탈 클립은 "샘플이 무엇을 내는가" 만 다릅니다(프레임 번호 ↔ 본 포즈). 시간을 흘리고 · 되감고 ·
 *          끝을 알고 · 지나간 알림을 고르는 일은 둘이 같으므로 여기 한 벌입니다(`AnimPlayer` · `AnimGraphPlayer` 도 이것만 봅니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class AnimNotifyTrack;

    /**
     * @class IAnimPlayable
     * @brief 재생할 수 있는 것 하나입니다(스켈레탈 `AnimClip`, 스프라이트 클립의 이름 붙은 구간). 길이 · 기본 반복 · 알림만 압니다.
     */
    class SW_API IAnimPlayable
    {
    public:
        IAnimPlayable()                                  = default;
        virtual ~IAnimPlayable()                         = default;
        IAnimPlayable( const IAnimPlayable& )            = default;
        IAnimPlayable& operator=( const IAnimPlayable& ) = default;

        /** @brief 한 바퀴 길이(초)입니다. 0 보다 커야 재생됩니다. */
        virtual float32 getPlayLength() const = 0;
        /** @brief 데이터가 정한 기본 반복 여부입니다. */
        virtual bool isLoopingByDefault() const = 0;
        /** @brief 알림 트랙입니다. 없으면 nullptr 입니다. */
        virtual const AnimNotifyTrack* findNotifyTrack() const { return nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimTimeStep
     * @brief 커서가 한 번 나아간 구간입니다. 알림은 이 구간에서 고릅니다.
     * @details 구간은 `(_previousTime, _currentTime]` 이고, `_wrapCount` 번 끝을 지나 처음으로 돌아왔습니다. 재생 직후 첫 걸음은 시작 시각을
     *          포함합니다(`_bIncludesStart`) — 0 초의 알림이 시작에서 한 번 울립니다.
     */
    struct AnimTimeStep
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
     * @class AnimClipCursor
     * @brief 재생 위치 하나입니다. 반복이면 한 바퀴 안으로 감고(오래 켜 둔 루프의 float 정밀도를 지킵니다), 아니면 끝에서 멈춥니다.
     */
    class SW_API AnimClipCursor
    {
    public:
        AnimClipCursor() = default;

        /** @brief 위치를 정하고 다음 걸음이 그 시각을 포함하게 합니다. */
        void reset( float32 time = 0.0f );
        /**
         * @brief @p deltaSeconds(0 이상)만큼 나아갑니다.
         * @param playLength 한 바퀴 길이(초). 0 이하면 움직이지 않습니다.
         */
        AnimTimeStep advance( float32 deltaSeconds, float32 playLength, bool bLoop );
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
     * @struct AnimNotifyEvent
     * @brief 클립 시각에 붙은 이름 하나입니다(발소리 · 칼 휘두름 시작). `_duration` 이 0 보다 크면 구간 알림(NotifyState)입니다 —
     *        시작 시각을 지날 때 `Begin`, 끝 시각(시작 + 길이, 한 바퀴 끝을 넘지 않음)을 지날 때 `End` 가 울립니다.
     */
    struct AnimNotifyEvent
    {
        hashed_string _name;
        float32       _time{ 0.0f };
        float32       _duration{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @enum AnimNotifyPhase
     * @brief 울린 알림의 종류입니다. 길이 없는 알림은 `Instant` 하나, 구간 알림은 `Begin` 과 `End` 가 따로 울립니다(언리얼 AnimNotify · AnimNotifyState).
     * @details 구간 사이의 매 프레임(`Tick`)은 트랙이 아니라 구간을 열어 둔 쪽(알림 디스패치)이 셉니다 — 트랙은 지나간 시각만 압니다.
     */
    enum class AnimNotifyPhase : uint8
    {
        Instant = 0,
        Begin,
        End,
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AnimFiredNotify
     * @brief 이번 갱신에 울린 알림 하나입니다. `_weight` 는 그 클립의 섞임 가중치입니다(크로스페이드 중 두 클립 모두 울립니다).
     * @details `_pSource` · `_eventIndex` 가 구간 알림 하나를 가립니다(같은 클립의 같은 알림이 `Begin` 과 `End` 에서 같은 값). `_time` 은 `End` 면 끝 시각입니다.
     */
    struct AnimFiredNotify
    {
        hashed_string        _name;
        const IAnimPlayable* _pSource{ nullptr };
        float32              _time{ 0.0f };
        float32              _weight{ 1.0f };
        float32              _duration{ 0.0f };
        uint32               _eventIndex{ 0 };
        AnimNotifyPhase      _phase{ AnimNotifyPhase::Instant };
    };
} // namespace sw

namespace sw
{
    /**
     * @class AnimNotifyTrack
     * @brief 시각순으로 정렬된 알림 목록입니다. 커서 걸음 하나에서 지나간 것을 **정확히 한 번씩** 고릅니다(반복 경계 포함).
     */
    class SW_API AnimNotifyTrack
    {
    public:
        AnimNotifyTrack() = default;

        /** @brief 알림을 더합니다(시각순을 유지합니다). */
        void addEvent( const AnimNotifyEvent& event );
        /** @brief 알림 목록입니다. */
        const vector<AnimNotifyEvent>& getEvents() const { return _listEvent; }
        /** @brief 비었으면 true 입니다. */
        bool isEmpty() const { return _listEvent.empty(); }
        /** @brief 모두 지웁니다. */
        void clear() { _listEvent.clear(); }
        /**
         * @brief 걸음 @p step 이 지난 알림을 @p outListFired 에 시각 순서로 덧붙입니다. @p pSource 는 이 트랙을 가진 재생할 것입니다.
         * @details 반복을 한 번 넘으면 (이전, 끝] 과 [0, 지금] 을, 여러 번 넘으면 그 사이의 온 바퀴도 셉니다. 한 시각의 알림은 한 바퀴에 한 번입니다.
         *          구간 알림은 시작 시각에서 `Begin`, 끝 시각에서 `End` 를 냅니다 — 같은 시각이면 `End` 가 먼저입니다(앞 구간이 닫히고 다음이 열린다).
         */
        void collectFired( const AnimTimeStep& step, float32 playLength, float32 weight, const IAnimPlayable* pSource, vector<AnimFiredNotify>& outListFired ) const;

    private:
        /** @brief 구간 [from, to] 의 알림 시작 · 끝을 덧붙입니다. @p bIncludeFrom 이 거짓이면 from 은 뺍니다. */
        void collectRange( float32 fromTime, float32 toTime, bool bIncludeFrom, float32 playLength, float32 weight, const IAnimPlayable* pSource,
                           vector<AnimFiredNotify>& outListFired ) const;
        /** @brief 구간 알림의 끝 시각입니다(한 바퀴 끝을 넘지 않습니다). */
        static float32 computeEndTime( const AnimNotifyEvent& event, float32 playLength );

        vector<AnimNotifyEvent> _listEvent;
    };
} // namespace sw

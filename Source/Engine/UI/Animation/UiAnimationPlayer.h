/**
 * @file UiAnimationPlayer.h
 * @brief 화면 하나의 애니메이션 재생기 — 이름 붙은 애니메이션(문서)과 코드 트윈을 프레임마다 진행해 위젯 프로퍼티에 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/BlendCurve.h"
#include "Engine/UI/Animation/UiAnimatedProperty.h"
#include "Engine/UI/Animation/UiAnimation.h"
#include "Engine/UI/Core/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /**
     * @class UiAnimationPlayer
     * @brief 위젯 트리 하나의 애니메이션 · 트윈 재생기입니다(UMG 위젯 애니메이션 재생 · Godot `Tween` 의 자리). `UiScreen` 이 하나 듭니다.
     * @details 애니메이션은 여럿이 함께 돌 수 있고, 같은 이름을 다시 틀면 처음부터입니다. 트랙의 위젯 · 경로는 틀 때 풀고(못 풀면 경고하고 그 트랙만 뺀다),
     *          값은 리플렉션으로 써서 칸의 무효화를 겁니다(렌더 변환 · 불투명도 = `kTransform` — 레이아웃 없음). 사건은 재생이 그 시각을 지날 때 한 번
     *          (반복이면 한 바퀴에 한 번) 화면의 `dispatchCommand` 로 갑니다. 트리가 화면에 없으면 사건은 버립니다.
     *
     *          `gv_uiReduceMotion`(사용자 설정 `accessibility.reduceMotion`)이면 애니메이션 · 트윈은 길이 0 — 끝 값을 바로 쓰고 사건을 모두 보냅니다.
     *          시간은 게임 시간 배율과 무관한 실제 프레임 시간입니다(정지 메뉴도 움직인다). 게임 스레드만.
     */
    class SW_API UiAnimationPlayer
    {
    public:
        explicit UiAnimationPlayer( WidgetTree& tree );
        ~UiAnimationPlayer();
        UiAnimationPlayer( const UiAnimationPlayer& )            = delete;
        UiAnimationPlayer& operator=( const UiAnimationPlayer& ) = delete;

        /** @brief 이름 붙은 애니메이션들을 둡니다(문서의 `<_listAnimation>`). 재생 중인 것은 멈춥니다. */
        void                       setAnimations( vector<UiAnimation> listAnimation );
        const vector<UiAnimation>& getAnimations() const { return _listAnimation; }
        /** @brief 이름의 애니메이션입니다. 없으면 nullptr 입니다. */
        const UiAnimation* findAnimation( const hashed_string& name ) const;

        /**
         * @brief 애니메이션 @p name 을 처음부터 틉니다 — 첫 프레임 값을 바로 씁니다(올린 화면이 한 프레임 끝 값으로 번쩍이지 않게).
         * @param speed 배속(0 보다 커야 한다).
         * @param loopCount 바퀴 수(0 이면 멈출 때까지).
         * @return 그 이름이 없으면 경고하고 false 입니다.
         */
        bool play( const hashed_string& name, float32 speed = 1.0f, uint32 loopCount = 1 );
        /** @brief 애니메이션 @p name 을 끝에서 처음으로 틉니다(`play` 와 같은 규칙). */
        bool playReverse( const hashed_string& name, float32 speed = 1.0f, uint32 loopCount = 1 );
        /** @brief 재생 중이면 그 자리에서 방향을 뒤집고, 아니면 `playReverse` 입니다. */
        void reverse( const hashed_string& name );
        /** @brief 멈춥니다(값은 지금 그대로). */
        void stop( const hashed_string& name );
        /** @brief 애니메이션 · 트윈을 모두 멈춥니다. */
        void stopAll();
        bool isPlaying( const hashed_string& name ) const;
        /** @brief 재생 중인 애니메이션 · 트윈이 있으면 true 입니다. */
        bool isAnyPlaying() const { return _listActive.empty() == false || _listTween.empty() == false; }

        /**
         * @brief 위젯 @p widget 의 프로퍼티 @p propertyPath 를 지금 값에서 @p endValue(글 표기)로 @p duration 초 동안 옮깁니다(코드 한 줄 트윈 — Godot `Tween`).
         * @details 같은 위젯 · 경로의 트윈이 있으면 그것을 대신합니다(지금 값에서 새 끝값으로). 실수 칸이 아니면 끝에서 바뀝니다.
         * @return 위젯이 없거나 경로 · 값을 읽지 못하면 경고하고 false 입니다.
         */
        bool   tween( WidgetId widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve = BlendCurve::EaseOut );
        uint32 getTweenCount() const { return static_cast<uint32>( _listTween.size() ); }

        /** @brief 시간을 @p deltaSeconds 만큼 진행해 값을 쓰고 사건을 보냅니다. 끝난 애니메이션 · 트윈은 끝 값을 쓰고 뺍니다. */
        void tick( float32 deltaSeconds );

    private:
        /** @brief 푼 트랙 하나 — 위젯 번호 · 경로 · 읽은 키 값. */
        struct ResolvedTrack
        {
            UiAnimatedProperty      _property{};
            vector<UiAnimatedValue> _listValue{}; ///< 키마다(트랙 키와 같은 순서)
            const UiAnimationTrack* _pTrack{ nullptr };
            WidgetId                _widget{ kInvalidWidgetId };
        };

        /** @brief 재생 중인 애니메이션 하나입니다. */
        struct ActiveAnimation
        {
            vector<ResolvedTrack> _listTrack{};
            hashed_string         _name{};
            uint32                _animationIndex{ 0 };
            float32               _time{ 0.0f };
            float32               _duration{ 0.0f };
            float32               _speed{ 1.0f };
            uint32                _remainingLoops{ 1 }; ///< 0 이면 끝없이
            bool                  _bReverse{ false };
        };

        /** @brief 코드 트윈 하나입니다. */
        struct ActiveTween
        {
            UiAnimatedProperty _property{};
            UiAnimatedValue    _from{};
            UiAnimatedValue    _to{};
            string             _path{};
            float32            _elapsed{ 0.0f };
            float32            _duration{ 0.0f };
            WidgetId           _widget{ kInvalidWidgetId };
            BlendCurve         _curve{ BlendCurve::EaseOut };
        };

        /** @brief 보낼 사건 하나입니다(진행이 끝난 뒤 보낸다). */
        struct PendingEvent
        {
            hashed_string _command{};
            float32       _time{ 0.0f };
        };

        /** @brief 애니메이션 @p name 을 짓고 시작합니다. */
        bool start( const hashed_string& name, float32 speed, uint32 loopCount, bool bReverse );
        /** @brief 트랙을 위젯 · 경로로 풉니다(못 풀면 경고하고 false). */
        bool resolveTrack( const UiAnimation& animation, const UiAnimationTrack& track, ResolvedTrack& outTrack ) const;
        /** @brief 재생 하나를 @p deltaSeconds 만큼 진행합니다(사건 포함). 끝났으면 true 입니다. */
        bool advance( ActiveAnimation& active, float32 deltaSeconds );
        /** @brief 재생 하나의 지금 시각 값을 위젯에 씁니다. */
        void applyAnimation( const ActiveAnimation& active ) const;
        /** @brief 시각 [@p from, @p to)(거꾸로면 (@p to, @p from])의 사건을 보낼 목록에 둡니다. @p bIncludeEnd 면 @p to 의 사건도. */
        void fireEvents( const ActiveAnimation& active, float32 from, float32 to, bool bIncludeEnd );
        /** @brief 모아 둔 사건을 화면 명령으로 보냅니다. */
        void dispatchPendingEvents();
        /** @brief 트랙 값 — 시각 @p time 의 보간 값입니다. */
        static UiAnimatedValue evaluateTrack( const ResolvedTrack& track, float32 time );

    private:
        vector<UiAnimation>     _listAnimation;
        vector<ActiveAnimation> _listActive;
        vector<ActiveTween>     _listTween;
        vector<PendingEvent>    _listPendingEvent;
        WidgetTree*             _pTree;
    };
} // namespace sw

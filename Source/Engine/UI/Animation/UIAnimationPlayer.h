/**
 * @file UIAnimationPlayer.h
 * @brief 화면 하나의 애니메이션 재생기입니다. 이름 붙은 애니메이션(문서)과 코드 트윈을 프레임마다 진행해 위젯 프로퍼티에 씁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Animation/Graph/BlendCurve.h"
#include "Engine/UI/Animation/UIAnimatedProperty.h"
#include "Engine/UI/Animation/UIAnimation.h"
#include "Engine/UI/Base/WidgetTypes.h"

namespace sw
{
    class WidgetTree;

    /**
     * @class UIAnimationPlayer
     * @brief 위젯 트리 하나의 애니메이션과 트윈 재생기입니다(UMG 위젯 애니메이션 재생기, Godot 의 `Tween` 에 해당). `UIScreen` 이 하나씩 가집니다.
     * @details 애니메이션은 여럿이 함께 돌 수 있고, 같은 이름을 다시 재생하면 처음부터 시작한다. 트랙의 위젯과 경로는 재생할 때 해석하고(해석하지 못하면 경고하고 그 트랙만 뺀다),
     *          값은 리플렉션으로 써서 필드의 무효화를 건다(렌더 변환과 불투명도는 `kTransform` 이며 레이아웃은 건드리지 않는다). 이벤트는 재생이 그 시각을 지날 때 한 번
     *          (반복이면 한 바퀴에 한 번) 화면의 `dispatchCommand` 로 보낸다. 트리가 화면에 없으면 이벤트는 버린다.
     *
     *          `gv_uiReduceMotion`(사용자 설정 `accessibility.reduceMotion`)이 켜져 있으면 애니메이션과 트윈의 길이는 0 이다. 끝 값을 바로 쓰고 이벤트를 모두 보낸다.
     *          시간은 게임 시간 배율과 무관한 실제 프레임 시간이다(정지 메뉴도 움직인다). 게임 스레드에서만 쓴다.
     */
    class SW_API UIAnimationPlayer
    {
    public:
        explicit UIAnimationPlayer( WidgetTree& tree );
        ~UIAnimationPlayer();
        UIAnimationPlayer( const UIAnimationPlayer& )            = delete;
        UIAnimationPlayer& operator=( const UIAnimationPlayer& ) = delete;

        /** @brief 이름 붙은 애니메이션을 설정합니다(문서의 `<_listAnimation>`). 재생 중인 것은 멈춥니다. */
        void                       setAnimations( vector<UIAnimation> listAnimation );
        const vector<UIAnimation>& getAnimations() const { return _listAnimation; }
        /** @brief 이름에 해당하는 애니메이션입니다. 없으면 nullptr 입니다. */
        const UIAnimation* findAnimation( const hashed_string& name ) const;

        /**
         * @brief 애니메이션 @p name 을 처음부터 재생합니다. 첫 프레임 값을 바로 씁니다(올린 화면이 한 프레임 동안 끝 값으로 번쩍이지 않게 하기 위해서입니다).
         * @param speed 배속입니다(0 보다 커야 합니다).
         * @param loopCount 반복 횟수입니다(0 이면 멈출 때까지).
         * @return 그 이름이 없으면 경고하고 false 입니다.
         */
        bool play( const hashed_string& name, float32 speed = 1.0f, uint32 loopCount = 1 );
        /** @brief 애니메이션 @p name 을 끝에서 처음으로 재생합니다(`play` 와 같은 규칙). */
        bool playReverse( const hashed_string& name, float32 speed = 1.0f, uint32 loopCount = 1 );
        /** @brief 재생 중이면 그 자리에서 방향을 뒤집고, 아니면 `playReverse` 를 부릅니다. */
        void reverse( const hashed_string& name );
        /** @brief 멈춥니다(값은 지금 그대로). */
        void stop( const hashed_string& name );
        /** @brief 애니메이션과 트윈을 모두 멈춥니다. */
        void stopAll();
        bool isPlaying( const hashed_string& name ) const;
        /**
         * @brief 애니메이션 @p name 을 시각 @p time (초, 길이로 자른다)에서 멈춘 채 두고 그 시각의 값을 씁니다(미리보기의 시간 막대).
         * @details 재생 중이 아니면 새로 열고, 재생 중이면 그 자리를 옮기며 멈춘다. 지나가는 시각의 이벤트는 보내지 않는다. 다시 재생하려면 `setPaused( name, false )` 를 부른다.
         * @return 그 이름이 없으면 경고하고 false 입니다.
         */
        bool seek( const hashed_string& name, float32 time );
        /** @brief 재생 중인 애니메이션 @p name 을 멈추거나(@p bPaused) 이어 재생합니다. 멈춘 동안은 시간이 가지 않고 값만 유지합니다. 재생 중이 아니면 아무것도 하지 않습니다. */
        void setPaused( const hashed_string& name, bool bPaused );
        /** @brief 재생 중인(멈춘 것 포함) 애니메이션 @p name 의 지금 시각과 길이(초)입니다. 재생 중이 아니면 false 입니다. */
        bool getPlaybackTime( const hashed_string& name, float32& outTime, float32& outDuration ) const;
        /** @brief 재생 중인 애니메이션이나 트윈이 있으면 true 입니다. */
        bool isAnyPlaying() const { return _listActive.empty() == false || _listTween.empty() == false; }

        /**
         * @brief 위젯 @p widget 의 프로퍼티 @p propertyPath 를 지금 값에서 @p endValue(텍스트 표기)까지 @p duration 초 동안 옮깁니다(코드 한 줄 트윈, Godot 의 `Tween` 에 해당).
         * @details 같은 위젯과 경로의 트윈이 있으면 그것을 대신한다(지금 값에서 새 끝값으로). 실수 필드가 아니면 끝에서 한 번에 바뀐다.
         * @return 위젯이 없거나 경로나 값을 읽지 못하면 경고하고 false 입니다.
         */
        bool   tween( WidgetID widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve = BlendCurve::EaseOut );
        uint32 getTweenCount() const { return static_cast<uint32>( _listTween.size() ); }

        /** @brief 시간을 @p deltaSeconds 만큼 진행해 값을 쓰고 이벤트를 보냅니다. 끝난 애니메이션과 트윈은 끝 값을 쓰고 뺍니다. */
        void update( float32 deltaSeconds );

    private:
        /** @brief 해석한 트랙 하나입니다. 위젯 번호, 경로, 읽은 키 값을 가집니다. */
        struct ResolvedTrack
        {
            UIAnimatedProperty      _property{};
            vector<UIAnimatedValue> _listValue{}; ///< 키마다의 값입니다(트랙 키와 같은 순서).
            const UIAnimationTrack* _pTrack{ nullptr };
            WidgetID                _widget{ kInvalidWidgetID };
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
            uint32                _remainingLoops{ 1 }; ///< 0 이면 끝없이 반복합니다.
            bool                  _bReverse{ false };
            bool                  _bPaused{ false }; ///< 멈춘 동안은 시간이 가지 않고 값만 씁니다(`seek`).
        };

        /** @brief 코드 트윈 하나입니다. */
        struct ActiveTween
        {
            UIAnimatedProperty _property{};
            UIAnimatedValue    _from{};
            UIAnimatedValue    _to{};
            string             _path{};
            float32            _elapsed{ 0.0f };
            float32            _duration{ 0.0f };
            WidgetID           _widget{ kInvalidWidgetID };
            BlendCurve         _curve{ BlendCurve::EaseOut };
        };

        /** @brief 보낼 이벤트 하나입니다(진행이 끝난 뒤 보냅니다). */
        struct PendingEvent
        {
            hashed_string _command{};
            float32       _time{ 0.0f };
        };

        /** @brief 애니메이션 @p name 을 만들고 시작합니다. */
        bool start( const hashed_string& name, float32 speed, uint32 loopCount, bool bReverse );
        /** @brief 애니메이션 @p animation 의 재생 상태를 만듭니다(트랙을 해석한다). 시작 시각은 방향의 처음입니다. */
        ActiveAnimation makeActive( const UIAnimation& animation, float32 speed, uint32 loopCount, bool bReverse ) const;
        /** @brief 트랙을 위젯과 경로로 해석합니다(해석하지 못하면 경고하고 false). */
        bool resolveTrack( const UIAnimation& animation, const UIAnimationTrack& track, ResolvedTrack& outTrack ) const;
        /** @brief 재생 하나를 @p deltaSeconds 만큼 진행합니다(이벤트 포함). 끝났으면 true 입니다. */
        bool advance( ActiveAnimation& active, float32 deltaSeconds );
        /** @brief 재생 하나의 지금 시각 값을 위젯에 씁니다. */
        void applyAnimation( const ActiveAnimation& active ) const;
        /** @brief 시각 [@p from, @p to)(거꾸로면 (@p to, @p from])의 이벤트를 보낼 목록에 둡니다. @p bIncludeEnd 면 @p to 의 이벤트도 포함합니다. */
        void fireEvents( const ActiveAnimation& active, float32 from, float32 to, bool bIncludeEnd );
        /** @brief 모아 둔 이벤트를 화면 명령으로 보냅니다. */
        void dispatchPendingEvents();
        /** @brief 트랙 값입니다. 시각 @p time 의 보간 값을 반환합니다. */
        static UIAnimatedValue evaluateTrack( const ResolvedTrack& track, float32 time );

    private:
        vector<UIAnimation>     _listAnimation;
        vector<ActiveAnimation> _listActive;
        vector<ActiveTween>     _listTween;
        vector<PendingEvent>    _listPendingEvent;
        WidgetTree*             _pTree;
    };
} // namespace sw

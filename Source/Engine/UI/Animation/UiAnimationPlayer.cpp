#include "pch.h"

#include "Engine/UI/Animation/UiAnimationPlayer.h"

#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UserSettings/UserSettingsVariables.h"

namespace sw
{
    SW_LOG_CALLER( "UiAnimationPlayer" );

    namespace
    {
        struct UiAnimationPlayerInternal
        {
            /** @brief 이보다 짧은 길이는 0 으로 본다(한 번에 끝). */
            static constexpr float32 kMinDuration = 1.0e-5f;
            /** @brief 한 틱에 넘길 수 있는 바퀴 수 — 아주 긴 프레임이 짧은 반복 애니메이션을 끝없이 돌리지 않게. */
            static constexpr uint32 kMaxPassesPerTick = 64;
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiAnimationPlayer::UiAnimationPlayer( WidgetTree& tree )
        : _listAnimation{}
        , _listActive{}
        , _listTween{}
        , _listPendingEvent{}
        , _pTree{ &tree }
    {
    }

    UiAnimationPlayer::~UiAnimationPlayer() = default;

    void UiAnimationPlayer::setAnimations( vector<UiAnimation> listAnimation )
    {
        _listActive.clear();
        _listAnimation = std::move( listAnimation );
    }

    const UiAnimation* UiAnimationPlayer::findAnimation( const hashed_string& name ) const
    {
        for ( const UiAnimation& animation : _listAnimation )
        {
            if ( animation._name == name )
                return &animation;
        }
        return nullptr;
    }

    bool UiAnimationPlayer::play( const hashed_string& name, float32 speed, uint32 loopCount )
    {
        return start( name, speed, loopCount, false );
    }

    bool UiAnimationPlayer::playReverse( const hashed_string& name, float32 speed, uint32 loopCount )
    {
        return start( name, speed, loopCount, true );
    }

    void UiAnimationPlayer::reverse( const hashed_string& name )
    {
        for ( ActiveAnimation& active : _listActive )
        {
            if ( active._name == name )
            {
                active._bReverse = active._bReverse == false;
                return;
            }
        }
        (void)playReverse( name );
    }

    void UiAnimationPlayer::stop( const hashed_string& name )
    {
        for ( size_t index = 0; index < _listActive.size(); ++index )
        {
            if ( _listActive[index]._name == name )
            {
                _listActive.erase( _listActive.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }

    void UiAnimationPlayer::stopAll()
    {
        _listActive.clear();
        _listTween.clear();
    }

    bool UiAnimationPlayer::isPlaying( const hashed_string& name ) const
    {
        for ( const ActiveAnimation& active : _listActive )
        {
            if ( active._name == name )
                return true;
        }
        return false;
    }

    bool UiAnimationPlayer::start( const hashed_string& name, float32 speed, uint32 loopCount, bool bReverse )
    {
        const UiAnimation* pAnimation = findAnimation( name );
        if ( pAnimation == nullptr )
        {
            SW_LOG_WARNING( "[Ui] Animation '%#' is not in this screen", name.c_str() );
            return false;
        }
        stop( name );
        ActiveAnimation active{};
        active._name           = name;
        active._animationIndex = static_cast<uint32>( pAnimation - _listAnimation.data() );
        active._duration       = pAnimation->computeDuration();
        active._speed          = speed > 0.0f ? speed : 1.0f;
        active._remainingLoops = loopCount;
        active._bReverse       = bReverse;
        active._time           = bReverse ? active._duration : 0.0f;
        for ( const UiAnimationTrack& track : pAnimation->_listTrack )
        {
            ResolvedTrack resolved{};
            if ( resolveTrack( *pAnimation, track, resolved ) )
                active._listTrack.push_back( std::move( resolved ) );
        }

        // 움직임 줄이기 · 길이 0 — 끝 값을 바로 쓰고 사건을 모두 보낸다(닫기 애니메이션의 명령도 그대로 간다).
        if ( gv_uiReduceMotion || active._duration < UiAnimationPlayerInternal::kMinDuration )
        {
            const float32 end = bReverse ? 0.0f : active._duration;
            fireEvents( active, active._time, end, true );
            active._time = end;
            applyAnimation( active );
            dispatchPendingEvents();
            return true;
        }
        applyAnimation( active );
        _listActive.push_back( std::move( active ) );
        return true;
    }

    bool UiAnimationPlayer::resolveTrack( const UiAnimation& animation, const UiAnimationTrack& track, ResolvedTrack& outTrack ) const
    {
        Widget* pWidget = _pTree->findWidgetByName( track._widget );
        if ( pWidget == nullptr )
        {
            SW_LOG_WARNING( "[Ui] Animation '%#': no widget named '%#' — the track is skipped", animation._name.c_str(), track._widget.c_str() );
            return false;
        }
        string error;
        if ( UiAnimatedProperty::resolve( *pWidget->getTypeInfo(), track._property, outTrack._property, error ) == false )
        {
            SW_LOG_WARNING( "[Ui] Animation '%#': %# — the track is skipped", animation._name.c_str(), error.c_str() );
            return false;
        }
        outTrack._listValue.resize( track._listKey.size() );
        for ( size_t index = 0; index < track._listKey.size(); ++index )
        {
            const UiAnimationKey& key = track._listKey[index];
            if ( index > 0 && key._time < track._listKey[index - 1]._time )
            {
                SW_LOG_WARNING( "[Ui] Animation '%#': keys of '%#.%#' are not in time order — the track is skipped", animation._name.c_str(), track._widget.c_str(),
                                track._property.c_str() );
                return false;
            }
            if ( outTrack._property.parseValue( key._value, outTrack._listValue[index] ) == false )
            {
                SW_LOG_WARNING( "[Ui] Animation '%#': key value '%#' cannot be read as '%#.%#' — the track is skipped", animation._name.c_str(), key._value.c_str(),
                                track._widget.c_str(), track._property.c_str() );
                return false;
            }
        }
        outTrack._pTrack = &track;
        outTrack._widget = pWidget->getId();
        return track._listKey.empty() == false;
    }

    bool UiAnimationPlayer::tween( WidgetId widget, string_view propertyPath, string_view endValue, float32 duration, BlendCurve curve )
    {
        Widget* pWidget = _pTree->findWidgetById( widget );
        if ( pWidget == nullptr )
        {
            SW_LOG_WARNING( "[Ui] Tween of '%#': widget %# is not in this tree", string( propertyPath ).c_str(), widget );
            return false;
        }
        ActiveTween tween{};
        string      error;
        if ( UiAnimatedProperty::resolve( *pWidget->getTypeInfo(), propertyPath, tween._property, error ) == false )
        {
            SW_LOG_WARNING( "[Ui] Tween: %#", error.c_str() );
            return false;
        }
        if ( tween._property.parseValue( endValue, tween._to ) == false )
        {
            SW_LOG_WARNING( "[Ui] Tween: '%#' cannot be read as '%#'", string( endValue ).c_str(), string( propertyPath ).c_str() );
            return false;
        }
        // 같은 위젯 · 경로의 옛 트윈은 지금 값에서 새 끝값으로 대신한다.
        for ( size_t index = 0; index < _listTween.size(); ++index )
        {
            if ( _listTween[index]._widget == widget && _listTween[index]._path == propertyPath )
            {
                _listTween.erase( _listTween.begin() + static_cast<ptrdiff_t>( index ) );
                break;
            }
        }
        if ( gv_uiReduceMotion || duration < UiAnimationPlayerInternal::kMinDuration )
        {
            (void)tween._property.writeValue( *pWidget, tween._to ); // false 는 값이 같거나 경로가 사라진 것 — 쓸 것이 없다
            return true;
        }
        tween._from     = tween._property.readValue( *pWidget );
        tween._path     = string( propertyPath );
        tween._duration = duration;
        tween._widget   = widget;
        tween._curve    = curve;
        _listTween.push_back( std::move( tween ) );
        return true;
    }

    void UiAnimationPlayer::tick( float32 deltaSeconds )
    {
        // 재생 중에 움직임 줄이기를 켜면 남은 것은 모두 끝으로 간다(반복도 이번 바퀴에서 끝).
        const bool    bReduceMotion = gv_uiReduceMotion;
        const float32 step          = bReduceMotion ? 0.0f : MathUtil::max( 0.0f, deltaSeconds );
        for ( size_t index = 0; index < _listActive.size(); )
        {
            ActiveAnimation& active = _listActive[index];
            bool             bFinished{ true };
            if ( bReduceMotion )
            {
                const float32 end = active._bReverse ? 0.0f : active._duration;
                fireEvents( active, active._time, end, true );
                active._time = end;
            }
            else
            {
                bFinished = advance( active, step );
            }
            applyAnimation( active );
            if ( bFinished )
                _listActive.erase( _listActive.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
        for ( size_t index = 0; index < _listTween.size(); )
        {
            ActiveTween& tween   = _listTween[index];
            Widget*      pWidget = _pTree->findWidgetById( tween._widget );
            tween._elapsed += step;
            const bool bFinished = pWidget == nullptr || bReduceMotion || tween._elapsed >= tween._duration;
            if ( pWidget != nullptr )
            {
                const float32 weight = bFinished ? 1.0f : evaluateUiCurve( tween._curve, tween._elapsed / tween._duration );
                // false 는 값이 같거나 경로가 사라진 것 — 쓸 것이 없다
                (void)tween._property.writeValue( *pWidget, UiAnimatedValue::blend( tween._from, tween._to, weight, tween._property._componentCount ) );
            }
            if ( bFinished )
                _listTween.erase( _listTween.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
        // 사건은 진행이 끝난 뒤 보낸다 — 명령 처리가 애니메이션을 틀거나 화면을 닫아도 위 목록을 건드리지 않게.
        dispatchPendingEvents();
    }

    bool UiAnimationPlayer::advance( ActiveAnimation& active, float32 deltaSeconds )
    {
        float32 remaining = deltaSeconds * active._speed;
        for ( uint32 pass = 0; pass < UiAnimationPlayerInternal::kMaxPassesPerTick; ++pass )
        {
            if ( active._bReverse == false )
            {
                const float32 target = active._time + remaining;
                if ( target < active._duration )
                {
                    fireEvents( active, active._time, target, false );
                    active._time = target;
                    return false;
                }
                fireEvents( active, active._time, active._duration, true );
                remaining    = target - active._duration;
                active._time = active._duration;
            }
            else
            {
                const float32 target = active._time - remaining;
                if ( target > 0.0f )
                {
                    fireEvents( active, active._time, target, false );
                    active._time = target;
                    return false;
                }
                fireEvents( active, active._time, 0.0f, true );
                remaining    = -target;
                active._time = 0.0f;
            }
            // 한 바퀴 끝.
            if ( active._remainingLoops == 1 )
                return true;
            if ( active._remainingLoops > 1 )
                --active._remainingLoops;
            active._time = active._bReverse ? active._duration : 0.0f;
        }
        return false;
    }

    void UiAnimationPlayer::applyAnimation( const ActiveAnimation& active ) const
    {
        for ( const ResolvedTrack& track : active._listTrack )
        {
            Widget* pWidget = _pTree->findWidgetById( track._widget );
            if ( pWidget != nullptr )
                (void)track._property.writeValue( *pWidget, evaluateTrack( track, active._time ) ); // false 는 값이 같거나 경로가 사라진 것 — 쓸 것이 없다
        }
    }

    void UiAnimationPlayer::fireEvents( const ActiveAnimation& active, float32 from, float32 to, bool bIncludeEnd )
    {
        const UiAnimation& animation = _listAnimation[active._animationIndex];
        const size_t       first     = _listPendingEvent.size();
        for ( const UiAnimationEvent& event : animation._listEvent )
        {
            const bool bInside = from <= to ? ( event._time >= from && ( event._time < to || ( bIncludeEnd && event._time <= to ) ) )
                                            : ( event._time <= from && ( event._time > to || ( bIncludeEnd && event._time >= to ) ) );
            if ( bInside )
                _listPendingEvent.push_back( PendingEvent{ event._command, event._time } );
        }
        // 지나간 순서대로(거꾸로 재생이면 늦은 것부터).
        const bool bForward = from <= to;
        std::stable_sort( _listPendingEvent.begin() + static_cast<ptrdiff_t>( first ), _listPendingEvent.end(),
                          [bForward]( const PendingEvent& lhs, const PendingEvent& rhs )
        { return bForward ? lhs._time < rhs._time : lhs._time > rhs._time; } );
    }

    void UiAnimationPlayer::dispatchPendingEvents()
    {
        if ( _listPendingEvent.empty() )
            return;
        vector<PendingEvent> listEvent;
        listEvent.swap( _listPendingEvent );
        UiScreen* pScreen = _pTree->getScreen();
        Widget*   pRoot   = _pTree->getRoot();
        if ( pScreen == nullptr || pRoot == nullptr )
            return;
        for ( const PendingEvent& event : listEvent )
        {
            pScreen->dispatchCommand( event._command, *pRoot );
        }
    }

    UiAnimatedValue UiAnimationPlayer::evaluateTrack( const ResolvedTrack& track, float32 time )
    {
        const vector<UiAnimationKey>& listKey = track._pTrack->_listKey;
        if ( time <= listKey.front()._time )
            return track._listValue.front();
        for ( size_t index = 1; index < listKey.size(); ++index )
        {
            const UiAnimationKey& next = listKey[index];
            if ( time >= next._time )
                continue;
            const UiAnimationKey& previous = listKey[index - 1];
            const float32         span     = next._time - previous._time;
            const float32         weight   = span > 0.0f ? evaluateUiCurve( next._curve, ( time - previous._time ) / span ) : 1.0f;
            return UiAnimatedValue::blend( track._listValue[index - 1], track._listValue[index], weight, track._property._componentCount );
        }
        return track._listValue.back();
    }
} // namespace sw

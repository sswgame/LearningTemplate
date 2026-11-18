#include "pch.h"

#include "Engine/Input/Feedback/HapticPlayback.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Input/Feedback/HapticEffect.h"
#include "Engine/Input/Feedback/IInputFeedbackDevice.h"

namespace sw
{
    HapticPlayback::HapticPlayback()
        : _listActive{}
        , _sentValue{ 0.0f, 0.0f }
    {
    }

    bool HapticPlayback::start( shared_ptr<const HapticEffectDef> pEffect, float32 scale, IInputFeedbackDevice& output )
    {
        if ( pEffect == nullptr )
        {
            SW_LOG_ERROR_ID( "HapticPlayback.NoEffect", "A haptic effect was played without an effect (it failed to load) - nothing plays" );
            return false;
        }
        _listActive.push_back( ActiveEffect{ SharedAssetRef<HapticEffectDef>{ std::move( pEffect ) }, 0.0f, MathUtil::clamp( scale, 0.0f, 1.0f ) } );
        const float2 value = combine();
        if ( output.setVibration( value._x, value._y ) == false )
        {
            _listActive.pop_back();
            return false;
        }
        _sentValue = value;
        if ( _listActive.size() > kMaxActiveEffects )
            _listActive.erase( _listActive.begin() ); // 가장 오래된 것 — 다음 advance 가 합성 값을 다시 낸다
        return true;
    }

    void HapticPlayback::advance( float32 deltaSeconds, IInputFeedbackDevice& output )
    {
        if ( _listActive.empty() )
            return;
        for ( size_t index = 0; index < _listActive.size(); )
        {
            ActiveEffect& active = _listActive[index];
            active._time += deltaSeconds;
            if ( active._pEffect->_bLoop == false && active._time >= active._pEffect->getDuration() )
                _listActive.erase( _listActive.begin() + static_cast<ptrdiff_t>( index ) );
            else
                ++index;
        }
        if ( _listActive.empty() )
        {
            output.stopVibration();
            clear();
            return;
        }
        const float2 value = combine();
        if ( value == _sentValue )
            return; // 고정 진동은 처음 한 번만 낸다(장치 호출을 프레임마다 되풀이하지 않는다)
        if ( output.setVibration( value._x, value._y ) )
            _sentValue = value; // 거절하는 장치(연결 끊김)는 다음 프레임에 다시 낸다
    }

    void HapticPlayback::clear()
    {
        _listActive.clear();
    }

    float2 HapticPlayback::combine() const
    {
        float2 combined{ 0.0f, 0.0f };
        for ( const ActiveEffect& active : _listActive )
        {
            const float2 value = active._pEffect->evaluate( active._time ) * active._scale;
            combined._x        = MathUtil::max( combined._x, value._x );
            combined._y        = MathUtil::max( combined._y, value._y );
        }
        return combined;
    }
} // namespace sw

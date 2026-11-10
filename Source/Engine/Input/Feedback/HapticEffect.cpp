#include "pch.h"

#include "Engine/Input/Feedback/HapticEffect.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCacheDefinition.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw
{
    bool HapticEffectDef::loadFromAsset( const AssetID& assetID )
    {
        const string_view assetPath = assetID.view();
        string            text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "HapticEffect.NotFound", "Haptic effect not found: %#", assetPath );
            return false;
        }
        return loadComposed( text, assetPath, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool HapticEffectDef::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        return loadComposed( xmlText, sourceName, ResourceContext::forType( *StaticType() ) );
    }

    bool HapticEffectDef::loadComposed( string_view xmlText, string_view sourceName, const ResourceContext& context )
    {
        *this = HapticEffectDef{};
        // 변형(`_base`)은 부모를 펼친 뒤 읽는다 — 키 목록은 통째로 바뀐다.
        string flatText;
        bool   bComposed = false;
        if ( ResourceComposer::compose( xmlText, sourceName, context, flatText, bComposed ) == false )
            return false;
        if ( XMLSerializer::deserialize( this, *StaticType(), bComposed ? string_view{ flatText } : xmlText ) == false )
        {
            SW_LOG_ERROR_ID( "HapticEffect.CouldNotRead", "%#: haptic effect could not be read or holds unknown keys / values", sourceName );
            return false;
        }
        return validate( sourceName );
    }

    bool HapticEffectDef::validate( string_view sourceName ) const
    {
        if ( _listKey.empty() )
        {
            SW_LOG_ERROR_ID( "HapticEffect.NoKeys", "%#: haptic effect has no keys", sourceName );
            return false;
        }
        if ( _listKey.front()._time != 0.0f )
        {
            SW_LOG_ERROR_ID( "HapticEffect.FirstKeyNotZero", "%#: the first haptic key must be at time 0 (it is at %#)", sourceName, _listKey.front()._time );
            return false;
        }
        for ( size_t index = 0; index < _listKey.size(); ++index )
        {
            const HapticKeyDef& key        = _listKey[index];
            const bool          bInRange   = key._low >= 0.0f && key._low <= 1.0f && key._high >= 0.0f && key._high <= 1.0f;
            const bool          bTimeOrder = index == 0 || key._time > _listKey[index - 1]._time;
            if ( bInRange == false || bTimeOrder == false )
            {
                SW_LOG_ERROR_ID( "HapticEffect.BadKey", "%#: haptic key #%# needs strengths in [0, 1] and a time after the previous key", sourceName, static_cast<uint32>( index ) );
                return false;
            }
        }
        if ( _bLoop && getDuration() <= 0.0f )
        {
            SW_LOG_ERROR_ID( "HapticEffect.LoopWithoutLength", "%#: a looping haptic effect needs two keys or more (its length is 0)", sourceName );
            return false;
        }
        return true;
    }

    float32 HapticEffectDef::getDuration() const
    {
        return _listKey.empty() ? 0.0f : _listKey.back()._time;
    }

    float2 HapticEffectDef::evaluate( float32 time ) const
    {
        if ( _listKey.empty() )
            return float2{ 0.0f, 0.0f };
        const float32 duration  = getDuration();
        float32       localTime = time;
        if ( _bLoop && duration > 0.0f )
            localTime = MathUtil::fmod( time, duration );
        const HapticKeyDef& last = _listKey.back();
        if ( localTime >= last._time )
            return float2{ last._low, last._high };
        for ( size_t index = 1; index < _listKey.size(); ++index )
        {
            const HapticKeyDef& next = _listKey[index];
            if ( localTime >= next._time )
                continue;
            const HapticKeyDef& prev  = _listKey[index - 1];
            const float32       alpha = ( localTime - prev._time ) / ( next._time - prev._time ); // 검사가 시각이 늘어남을 보장한다(0 으로 나누지 않음)
            return float2{ MathUtil::lerp( prev._low, next._low, alpha ), MathUtil::lerp( prev._high, next._high, alpha ) };
        }
        return float2{ _listKey.front()._low, _listKey.front()._high };
    }

    shared_ptr<const HapticEffectDef> HapticEffectDef::makeConstant( float32 lowFrequency, float32 highFrequency, float32 durationSeconds )
    {
        shared_ptr<HapticEffectDef> pEffect = make_shared<HapticEffectDef>();
        pEffect->_listKey.push_back( HapticKeyDef{ 0.0f, lowFrequency, highFrequency } );
        pEffect->_listKey.push_back( HapticKeyDef{ durationSeconds, lowFrequency, highFrequency } );
        return pEffect;
    }

    // 게임이 `AssetRef<HapticEffectDef>` 로 들고 공유 캐시가 나눠 준다(언리얼 `UForceFeedbackEffect` 에셋 참조 자리).
    SW_SHARED_ASSET_CACHE( HapticEffect, HapticEffectDef, HapticEffectDef::kAssetType.withLoader( AssetTypeLoader::of<HapticEffectDef>() ) );
} // namespace sw

#include "pch.h"

#include "Engine/Object/Animation/AnimationLod.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/SkeletonBoneLod.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationLod" );

    bool AnimationLodSettings::parseJson( string_view json, string_view sourceLabel )
    {
        *this = AnimationLodSettings{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        *this = AnimationLodSettings{};
        return false;
    }

    bool AnimationLodSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#' could not be read", path );
            *this = AnimationLodSettings{};
            return false;
        }
        return parseJson( text, path );
    }

    bool AnimationLodSettings::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "rate_levels", "offscreen_update_rate_divisor", "budget_milliseconds", "max_update_rate_divisor", "vertex_animation_screen_size" },
                                             sourceLabel ) == false )
            return false;
        const JsonValue levels = root.get( "rate_levels" );
        if ( levels.isArray() == false || levels.size() == 0 )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': 'rate_levels' must be a non-empty array", sourceLabel );
            return false;
        }
        for ( size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex )
        {
            const JsonValue level = levels.at( levelIndex );
            if ( AnimJsonUtil::hasOnlyKnownKeys( level, { "min_screen_size", "update_rate_divisor", "interpolate" }, sourceLabel ) == false )
                return false;
            if ( level.get( "min_screen_size" ).isNumber() == false || level.get( "update_rate_divisor" ).isNumber() == false || level.get( "interpolate" ).isBool() == false )
            {
                SW_LOG_ERROR( "Animation LOD settings '%#': rate level %# needs min_screen_size, update_rate_divisor and interpolate", sourceLabel, levelIndex );
                return false;
            }
            AnimationLodRateLevel entry{};
            entry._minScreenSize     = static_cast<float32>( level.get( "min_screen_size" ).asFloat() );
            entry._updateRateDivisor = static_cast<uint32>( MathUtil::max<int64>( level.get( "update_rate_divisor" ).asInt( 1 ), 1 ) );
            entry._bInterpolate      = level.get( "interpolate" ).asBool() ? SW_TRUE : SW_FALSE;
            if ( _listRateLevel.empty() == false && entry._minScreenSize >= _listRateLevel.back()._minScreenSize )
            {
                SW_LOG_ERROR( "Animation LOD settings '%#': rate level %# min_screen_size must be smaller than the previous level's", sourceLabel, levelIndex );
                return false;
            }
            _listRateLevel.push_back( entry );
        }
        if ( _listRateLevel.back()._minScreenSize > 0.0f )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': the last rate level must have min_screen_size 0", sourceLabel );
            return false;
        }

        const JsonValue offscreen = root.get( "offscreen_update_rate_divisor" );
        const JsonValue budget    = root.get( "budget_milliseconds" );
        const JsonValue maxRate   = root.get( "max_update_rate_divisor" );
        const JsonValue vertex    = root.get( "vertex_animation_screen_size" );
        if ( offscreen.isNumber() == false || budget.isNumber() == false || maxRate.isNumber() == false || vertex.isNumber() == false )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': offscreen_update_rate_divisor, budget_milliseconds, max_update_rate_divisor and "
                          "vertex_animation_screen_size are required numbers",
                          sourceLabel );
            return false;
        }
        _offscreenUpdateRateDivisor = static_cast<uint32>( MathUtil::max<int64>( offscreen.asInt( 0 ), 0 ) );
        _budgetMilliseconds         = MathUtil::max( static_cast<float32>( budget.asFloat() ), 0.0f );
        _maxUpdateRateDivisor       = static_cast<uint32>( MathUtil::max<int64>( maxRate.asInt( 16 ), 1 ) );
        _vertexAnimationScreenSize  = MathUtil::max( static_cast<float32>( vertex.asFloat() ), 0.0f );
        return true;
    }

    uint32 AnimationLodSettings::selectRateLevel( float32 screenSize ) const
    {
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( _listRateLevel.size() ); ++levelIndex )
        {
            if ( screenSize >= _listRateLevel[levelIndex]._minScreenSize )
                return levelIndex;
        }
        return _listRateLevel.empty() ? 0u : static_cast<uint32>( _listRateLevel.size() - 1 );
    }

    float32 AnimationLodUtil::computeScreenSize( const AnimationLodView& view, const float3& center, float32 radius )
    {
        const float4x4& m = view._viewProj;
        // 절두체 밖이면 0 — 판정 식은 GPU 컬링과 같다(Frustum 의 여섯 평면).
        if ( view._frustum.overlapsSphere( center, radius ) == false )
            return 0.0f;
        const float32 clipW      = center._x * m._14 + center._y * m._24 + center._z * m._34 + m._44;
        const float32 scaleY     = MathUtil::sqrt( m._12 * m._12 + m._22 * m._22 + m._32 * m._32 );
        const float3  toEye      = center - view._position;
        const bool    bEyeInside = toEye.getLengthSquared() <= radius * radius;
        if ( bEyeInside || clipW <= MathUtil::kEpsilon )
            return 1.0f;
        return radius * scaleY / clipW;
    }

    float32 AnimationLodUtil::allocateBudget( AnimationBudgetItem* pItem, uint32 itemCount, float32 costPerEvaluationMicroseconds, float32 budgetMicroseconds,
                                              uint32 maxUpdateRateDivisor )
    {
        float32 expected = 0.0f;
        for ( uint32 itemIndex = 0; itemIndex < itemCount; ++itemIndex )
            expected += costPerEvaluationMicroseconds / static_cast<float32>( MathUtil::max( pItem[itemIndex]._updateRateDivisor, 1u ) );
        if ( budgetMicroseconds <= 0.0f || expected <= budgetMicroseconds || costPerEvaluationMicroseconds <= 0.0f )
            return expected;

        // 덜 중요한 것부터 — 같으면 앞의 것(등록 순서)이 먼저 늘어난다(결정적).
        vector<uint32> listOrder( itemCount );
        for ( uint32 itemIndex = 0; itemIndex < itemCount; ++itemIndex )
            listOrder[itemIndex] = itemIndex;
        std::stable_sort( listOrder.begin(), listOrder.end(),
                          [pItem]( uint32 left, uint32 right )
        { return pItem[left]._significance < pItem[right]._significance; } );
        const uint32 maxDivisor = MathUtil::max( maxUpdateRateDivisor, 1u );
        for ( const uint32 itemIndex : listOrder )
        {
            AnimationBudgetItem& item = pItem[itemIndex];
            while ( expected > budgetMicroseconds && item._updateRateDivisor < maxDivisor )
            {
                const uint32 doubled = MathUtil::min( MathUtil::max( item._updateRateDivisor, 1u ) * 2u, maxDivisor );
                expected -= costPerEvaluationMicroseconds / static_cast<float32>( MathUtil::max( item._updateRateDivisor, 1u ) );
                expected += costPerEvaluationMicroseconds / static_cast<float32>( doubled );
                item._updateRateDivisor = doubled;
            }
            if ( expected <= budgetMicroseconds )
                break;
        }
        return expected;
    }

    AnimationLodState AnimationLodUtil::makeState( const AnimationLodSettings& settings, float32 screenSize, bool bVisible, const SkeletonBoneLod* pBoneLod )
    {
        AnimationLodState state{};
        state._bVisible     = bVisible ? SW_TRUE : SW_FALSE;
        state._screenSize   = bVisible ? screenSize : 0.0f;
        state._significance = state._screenSize;
        if ( settings._listRateLevel.empty() == false )
        {
            const uint32                 levelIndex = settings.selectRateLevel( state._screenSize );
            const AnimationLodRateLevel& level      = settings._listRateLevel[levelIndex];
            state._rateLevel                        = static_cast<uint8>( levelIndex );
            state._updateRateDivisor                = level._updateRateDivisor;
            state._bInterpolate                     = level._bInterpolate;
        }
        if ( bVisible == false && settings._offscreenUpdateRateDivisor > 0 )
        {
            state._updateRateDivisor = settings._offscreenUpdateRateDivisor;
            state._bInterpolate      = SW_FALSE;
        }
        if ( pBoneLod != nullptr )
            state._boneLodLevel = static_cast<uint8>( pBoneLod->selectLevel( state._screenSize ) );
        state._bVertexAnimation = ( settings._vertexAnimationScreenSize > 0.0f && state._screenSize < settings._vertexAnimationScreenSize ) ? SW_TRUE : SW_FALSE;
        return state;
    }
} // namespace sw

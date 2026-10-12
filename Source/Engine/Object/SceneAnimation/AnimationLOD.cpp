#include "pch.h"

#include "Engine/Object/SceneAnimation/AnimationLOD.h"

#include "Core/Log/Logger.h"
#include "Core/Math/Frustum.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Skeletal/SkeletonBoneLOD.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "AnimationLOD" );

    bool AnimationLODSettings::parseJSON( string_view json, string_view sourceLabel )
    {
        *this = AnimationLODSettings{};
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigJSON` 이 로그로 알린다).
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) && validate( sourceLabel ) )
            return true;
        *this = AnimationLODSettings{};
        return false;
    }

    bool AnimationLODSettings::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#' could not be read", path );
            *this = AnimationLODSettings{};
            return false;
        }
        return parseJSON( text, path );
    }

    bool AnimationLODSettings::validate( string_view sourceLabel ) const
    {
        if ( _listRateLevel.empty() )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': '_listRateLevel' must be a non-empty array", sourceLabel );
            return false;
        }
        for ( size_t levelIndex = 1; levelIndex < _listRateLevel.size(); ++levelIndex )
        {
            if ( _listRateLevel[levelIndex]._minScreenSize >= _listRateLevel[levelIndex - 1]._minScreenSize )
            {
                SW_LOG_ERROR( "Animation LOD settings '%#': rate level %# _minScreenSize must be smaller than the previous level's", sourceLabel, levelIndex );
                return false;
            }
        }
        if ( _listRateLevel.back()._minScreenSize > 0.0f )
        {
            SW_LOG_ERROR( "Animation LOD settings '%#': the last rate level must have _minScreenSize 0", sourceLabel );
            return false;
        }
        return true;
    }

    uint32 AnimationLODSettings::selectRateLevel( float32 screenSize ) const
    {
        for ( uint32 levelIndex = 0; levelIndex < static_cast<uint32>( _listRateLevel.size() ); ++levelIndex )
        {
            if ( screenSize >= _listRateLevel[levelIndex]._minScreenSize )
                return levelIndex;
        }
        return _listRateLevel.empty() ? 0u : static_cast<uint32>( _listRateLevel.size() - 1 );
    }

    float32 AnimationLODUtil::computeScreenSize( const AnimationLODView& view, const float3& center, float32 radius )
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

    float32 AnimationLODUtil::allocateBudget( AnimationBudgetItem* pItem, uint32 itemCount, float32 costPerEvaluationMicroseconds, float32 budgetMicroseconds,
                                              uint32 maxUpdateRateDivisor )
    {
        float32 expected = 0.0f;
        for ( uint32 itemIndex = 0; itemIndex < itemCount; ++itemIndex )
        {
            expected += costPerEvaluationMicroseconds / static_cast<float32>( MathUtil::max( pItem[itemIndex]._updateRateDivisor, 1u ) );
        }
        if ( budgetMicroseconds <= 0.0f || expected <= budgetMicroseconds || costPerEvaluationMicroseconds <= 0.0f )
            return expected;

        // 덜 중요한 것부터 — 같으면 앞의 것(등록 순서)이 먼저 늘어난다(결정적).
        vector<uint32> listOrder( itemCount );
        for ( uint32 itemIndex = 0; itemIndex < itemCount; ++itemIndex )
        {
            listOrder[itemIndex] = itemIndex;
        }
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

    AnimationLODState AnimationLODUtil::makeState( const AnimationLODSettings& settings, float32 screenSize, bool bVisible, const SkeletonBoneLOD* pBoneLOD )
    {
        AnimationLODState state{};
        state._bVisible     = bVisible ? SW_TRUE : SW_FALSE;
        state._screenSize   = bVisible ? screenSize : 0.0f;
        state._significance = state._screenSize;
        if ( settings._listRateLevel.empty() == false )
        {
            const uint32                 levelIndex = settings.selectRateLevel( state._screenSize );
            const AnimationLODRateLevel& level      = settings._listRateLevel[levelIndex];
            state._rateLevel                        = static_cast<uint8>( levelIndex );
            state._updateRateDivisor                = level._updateRateDivisor;
            state._bInterpolate                     = level._bInterpolate ? SW_TRUE : SW_FALSE;
        }
        if ( bVisible == false && settings._offscreenUpdateRateDivisor > 0 )
        {
            state._updateRateDivisor = settings._offscreenUpdateRateDivisor;
            state._bInterpolate      = SW_FALSE;
        }
        if ( pBoneLOD != nullptr )
            state._boneLODLevel = static_cast<uint8>( pBoneLOD->selectLevel( state._screenSize ) );
        state._bVertexAnimation = ( settings._vertexAnimationScreenSize > 0.0f && state._screenSize < settings._vertexAnimationScreenSize ) ? SW_TRUE : SW_FALSE;
        return state;
    }
} // namespace sw

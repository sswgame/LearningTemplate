#include "pch.h"

#include "Engine/Animation/Facial/WrinkleMap.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    WrinkleMap::WrinkleMap()
        : _listRegion{}
        , _intensity{ 1.0f }
    {
    }

    WrinkleMap::WrinkleMap( vector<WrinkleRegionDef> listRegion, float32 intensity )
        : _listRegion{ std::move( listRegion ) }
        , _intensity{ intensity }
    {
    }

    void WrinkleMap::evaluate( span<const hashed_string> listExpressionName, span<const float32> listExpressionWeight, WrinkleWeights& outWeights ) const
    {
        float32 arrChannel[kMaskChannelCount] = {};
        for ( const WrinkleRegionDef& region : _listRegion )
        {
            if ( region._mask >= kMaskChannelCount )
                continue; // 범위는 PROPERTY( Max ) 가 로드에서 막았다
            float32 sum = 0.0f;
            for ( const WrinkleSourceDef& source : region._listSource )
            {
                for ( size_t index = 0; index < listExpressionName.size() && index < listExpressionWeight.size(); ++index )
                {
                    if ( listExpressionName[index] == source._expression )
                        sum += source._weight * listExpressionWeight[index];
                }
            }
            arrChannel[region._mask] += sum * _intensity;
        }
        outWeights._maskA = float4{ MathUtil::saturate( arrChannel[0] ), MathUtil::saturate( arrChannel[1] ), MathUtil::saturate( arrChannel[2] ), MathUtil::saturate( arrChannel[3] ) };
        outWeights._maskB = float4{ MathUtil::saturate( arrChannel[4] ), MathUtil::saturate( arrChannel[5] ), MathUtil::saturate( arrChannel[6] ), MathUtil::saturate( arrChannel[7] ) };
    }

    bool WrinkleMap::checkExpressions( span<const hashed_string> listExpressionName, vector<hashed_string>* pOutMissing ) const
    {
        bool bValid = true;
        for ( const WrinkleRegionDef& region : _listRegion )
        {
            for ( const WrinkleSourceDef& source : region._listSource )
            {
                bool bFound = false;
                for ( const hashed_string& name : listExpressionName )
                    bFound = bFound || name == source._expression;
                if ( bFound )
                    continue;
                bValid = false;
                if ( pOutMissing != nullptr && VectorUtil::contains( *pOutMissing, source._expression ) == false )
                    pOutMissing->push_back( source._expression );
            }
        }
        return bValid;
    }
} // namespace sw

#include "pch.h"

#include "Engine/Animation/Graph/BlendSpace.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct BlendSpaceInternal
        {
            /** @brief 1D 가중치입니다. 표본은 x 오름차순입니다. */
            static void computeLinearWeights( const vector<BlendSpaceSample>& listSample, float32 parameter, vector<float32>& outListWeight )
            {
                const size_t count = listSample.size();
                if ( parameter <= listSample.front()._position._x )
                {
                    outListWeight.front() = 1.0f;
                    return;
                }
                if ( parameter >= listSample.back()._position._x )
                {
                    outListWeight.back() = 1.0f;
                    return;
                }
                for ( size_t index = 0; index + 1 < count; ++index )
                {
                    const float32 lower = listSample[index]._position._x;
                    const float32 upper = listSample[index + 1]._position._x;
                    if ( lower <= parameter && parameter <= upper )
                    {
                        const float32 span       = upper - lower;
                        const float32 alpha      = ( span > MathUtil::kEpsilon ) ? ( parameter - lower ) / span : 0.0f;
                        outListWeight[index]     = 1.0f - alpha;
                        outListWeight[index + 1] = alpha;
                        return;
                    }
                }
                outListWeight.back() = 1.0f;
            }

            /** @brief 2D 역거리 가중치입니다. 파라미터는 표본 범위로 먼저 가둡니다. */
            static void computeInverseDistanceWeights( const vector<BlendSpaceSample>& listSample, const float2& parameter, vector<float32>& outListWeight )
            {
                float2 minParameter = listSample.front()._position;
                float2 maxParameter = listSample.front()._position;
                for ( const BlendSpaceSample& sample : listSample )
                {
                    minParameter = float2{ MathUtil::min( minParameter._x, sample._position._x ), MathUtil::min( minParameter._y, sample._position._y ) };
                    maxParameter = float2{ MathUtil::max( maxParameter._x, sample._position._x ), MathUtil::max( maxParameter._y, sample._position._y ) };
                }
                const float2 target{ MathUtil::clamp( parameter._x, minParameter._x, maxParameter._x ), MathUtil::clamp( parameter._y, minParameter._y, maxParameter._y ) };

                // 1/d² 를 그대로 더해 절대 임계값과 비교하면 단위가 큰 공간(cm/s, 표본 0~600)에서 합이 작아 엉뚱한 표본으로 튄다.
                // 가장 가까운 표본의 거리로 나눠 그 표본의 상대 가중치를 1 로 둔다.
                float32 nearestDistanceSquared = MathUtil::kMaxFloat;
                for ( size_t index = 0; index < listSample.size(); ++index )
                {
                    const float32 distanceSquared = float2::getDistanceSquared( target, listSample[index]._position );
                    if ( distanceSquared < MathUtil::kEpsilon )
                    {
                        outListWeight[index] = 1.0f;
                        return;
                    }
                    nearestDistanceSquared = MathUtil::min( nearestDistanceSquared, distanceSquared );
                }
                float32 totalWeight = 0.0f;
                for ( size_t index = 0; index < listSample.size(); ++index )
                {
                    outListWeight[index] = nearestDistanceSquared / float2::getDistanceSquared( target, listSample[index]._position );
                    totalWeight += outListWeight[index];
                }
                const float32 inverseTotal = 1.0f / totalWeight; // 가장 가까운 표본이 1 이라 합은 1 이상이다
                for ( float32& weight : outListWeight )
                {
                    weight *= inverseTotal;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BlendSpace::BlendSpace()
        : _listSample{}
        , _axisCount{ 1 }
    {
    }

    void BlendSpace::addSample( const float2& position, const hashed_string& clipName )
    {
        _listSample.push_back( BlendSpaceSample{ position, clipName } );
        if ( _axisCount == 1 )
        {
            std::stable_sort( _listSample.begin(), _listSample.end(), []( const BlendSpaceSample& lhs, const BlendSpaceSample& rhs )
            {
                return lhs._position._x < rhs._position._x;
            } );
        }
    }

    void BlendSpace::computeSampleWeights( const float2& parameter, vector<float32>& outListWeight ) const
    {
        outListWeight.assign( _listSample.size(), 0.0f );
        if ( _listSample.empty() )
            return;
        if ( _listSample.size() == 1 )
        {
            outListWeight.front() = 1.0f;
            return;
        }
        if ( _axisCount == 1 )
        {
            BlendSpaceInternal::computeLinearWeights( _listSample, parameter._x, outListWeight );
            return;
        }
        BlendSpaceInternal::computeInverseDistanceWeights( _listSample, parameter, outListWeight );
    }

    float32 BlendSpace::computeBlendedLength( const vector<float32>& listSampleLength, const vector<float32>& listWeight )
    {
        const size_t count       = MathUtil::min( listSampleLength.size(), listWeight.size() );
        float32      length      = 0.0f;
        float32      totalWeight = 0.0f;
        for ( size_t index = 0; index < count; ++index )
        {
            length += listSampleLength[index] * listWeight[index];
            totalWeight += listWeight[index];
        }
        return ( totalWeight > MathUtil::kEpsilon ) ? length / totalWeight : 0.0f;
    }
} // namespace sw

#include "pch.h"

#include "Engine/Renderer/VFX/VFXParticleOrder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        /** @brief 입자 번호 0..count-1 을 목록 끝에 붙이고 그 구간을 돌려줍니다. */
        VFXParticleOrder appendIdentity( size_t count, vector<uint32>& outListOrder )
        {
            VFXParticleOrder range;
            range._first = static_cast<uint32>( outListOrder.size() );
            range._count = static_cast<uint32>( count );
            for ( uint32 particleIndex = 0; particleIndex < range._count; ++particleIndex )
            {
                outListOrder.push_back( particleIndex );
            }
            return range;
        }
    } // namespace

    VFXParticleOrder VFXParticleOrder::appendViewDepth( vector_reference<const GPUParticle> listParticle, const float4x4& world, const float3& eye, const float3& axis,
                                                        vector<uint32>& outListOrder )
    {
        // 깊이 규칙은 메시 투명 후보와 같다(GPUTransparentSorter::makeTransparentSortKey) — 축이 있으면 축 위의 거리, 없으면 눈까지의 거리 제곱.
        const bool      bUseAxis = axis.getLengthSquared() > MathUtil::kEpsilon;
        vector<float32> listDepth( listParticle.size() );
        for ( size_t particleIndex = 0; particleIndex < listParticle.size(); ++particleIndex )
        {
            const float4& positionSize = listParticle[particleIndex]._positionSize;
            const float3  position     = float3::transform( float3{ positionSize._x, positionSize._y, positionSize._z }, world );
            listDepth[particleIndex]   = bUseAxis ? ( position - eye ).dot( axis ) : float3::getDistanceSquared( position, eye );
        }
        const VFXParticleOrder   range = appendIdentity( listParticle.size(), outListOrder );
        vector_reference<uint32> listRange( outListOrder.data() + range._first, range._count );
        // 먼 것부터 — 같은 깊이는 번호 순(안정 정렬)이라 프레임마다 깜빡이지 않는다.
        VectorUtil::stableSort( listRange, [&listDepth]( uint32 left, uint32 right )
        { return listDepth[left] > listDepth[right]; } );
        return range;
    }

    VFXParticleOrder VFXParticleOrder::appendByAge( vector_reference<const GPUParticle> listParticle, bool bOldestFirst, vector<uint32>& outListOrder )
    {
        const VFXParticleOrder   range = appendIdentity( listParticle.size(), outListOrder );
        vector_reference<uint32> listRange( outListOrder.data() + range._first, range._count );
        // 나이는 수명 비율(0 = 갓 태어남, 1 = 사라짐)이다. 눈 · 뷰를 보지 않는다.
        VectorUtil::stableSort( listRange,
                                [&listParticle, bOldestFirst]( uint32 left, uint32 right )
        {
            const float32 leftLife  = listParticle[left]._rotationLife._y;
            const float32 rightLife = listParticle[right]._rotationLife._y;
            return bOldestFirst ? leftLife > rightLife : leftLife < rightLife;
        } );
        return range;
    }
} // namespace sw

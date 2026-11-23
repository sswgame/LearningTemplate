#include "pch.h"

#include "Engine/Renderer/VFX/VFXParticleOrder.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct VFXParticleOrderInternal
        {
            /// 이보다 많으면 기수 정렬이다(언리얼 나이아가라 `fx.Niagara.RadixSortThreshold` 의 기본값 400).
            static constexpr uint32 kBucketSortThreshold = 400;
            static constexpr uint32 kBucketBits          = 11; ///< 32 비트를 세 번에(11 · 11 · 10)
            static constexpr uint32 kBucketCount         = 1u << kBucketBits;
            static constexpr uint32 kPassCount           = 3;

            /** @brief 실수를 크기 순서가 보존되는 uint32 로 바꿉니다(음수는 모두 뒤집고 양수는 부호 비트를 켠다). -0 은 +0 으로 본다. */
            static uint32 makeOrderedBits( float32 value )
            {
                constexpr uint32 kSignBit     = 0x80000000u;
                const float32    positiveZero = value + 0.0f;
                uint32           bits{ 0 };
                Memory::copy( &bits, &positiveZero, sizeof( bits ) );
                return ( bits & kSignBit ) != 0 ? ~bits : ( bits | kSignBit );
            }

            /** @brief 입자 번호 0..count-1 을 목록 끝에 붙이고 그 구간을 돌려줍니다. */
            static VFXParticleOrder appendIdentity( size_t count, vector<uint32>& outListOrder )
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

            /** @brief 번호 구간 @p pOrder 를 `scratch._listKey` 오름차순으로 안정 정렬합니다(같은 키는 번호 순). 많으면 입자 수에 비례하는 기수 정렬이다. */
            static void sortByKey( uint32* pOrder, uint32 count, VFXParticleSortScratch& scratch )
            {
                const vector<uint32>& listKey = scratch._listKey;
                if ( count <= kBucketSortThreshold )
                {
                    vector_reference<uint32> listRange( pOrder, count );
                    VectorUtil::stableSort( listRange, [&listKey]( uint32 left, uint32 right )
                    { return listKey[left] < listKey[right]; } );
                    return;
                }
                // 아래 자리부터 세 번 흩는다 — 키와 번호를 함께 옮긴다(번호로 키를 다시 찾으면 캐시를 놓친다). 홀수 번이라 끝은 건너편이다 — 마지막에 되옮긴다.
                scratch._listKeySwap.resize( count );
                scratch._listOrderSwap.resize( count );
                uint32* pKeyFrom   = scratch._listKey.data();
                uint32* pKeyTo     = scratch._listKeySwap.data();
                uint32* pOrderFrom = pOrder;
                uint32* pOrderTo   = scratch._listOrderSwap.data();
                for ( uint32 pass = 0; pass < kPassCount; ++pass )
                {
                    const uint32 shift = pass * kBucketBits;
                    uint32       arrOffset[kBucketCount]{};
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        ++arrOffset[( pKeyFrom[index] >> shift ) & ( kBucketCount - 1u )];
                    }
                    uint32 running{ 0 };
                    for ( uint32& offset : arrOffset )
                    {
                        const uint32 bucketCount = offset;
                        offset                   = running;
                        running += bucketCount;
                    }
                    for ( uint32 index = 0; index < count; ++index )
                    {
                        const uint32 target = arrOffset[( pKeyFrom[index] >> shift ) & ( kBucketCount - 1u )]++;
                        pKeyTo[target]      = pKeyFrom[index];
                        pOrderTo[target]    = pOrderFrom[index];
                    }
                    std::swap( pKeyFrom, pKeyTo );
                    std::swap( pOrderFrom, pOrderTo );
                }
                static_assert( kPassCount % 2 == 1, "짝수 번이면 끝이 원래 자리라 되옮기지 않는다" );
                Memory::copy( pOrder, pOrderFrom, static_cast<size_t>( count ) * sizeof( uint32 ) );
            }
        };
    } // namespace

    VFXParticleOrder VFXParticleOrder::appendViewDepth( vector_reference<const GPUParticle> listParticle, const float4x4& world, const float3& eye, const float3& axis,
                                                        VFXParticleSortScratch& scratch, vector<uint32>& outListOrder )
    {
        using Internal = VFXParticleOrderInternal;
        // 깊이 규칙은 메시 투명 후보와 같다(GPUTransparentSorter::makeTransparentSortKey) — 축이 있으면 축 위의 거리, 없으면 눈까지의 거리 제곱.
        // 먼 것부터 — 키를 뒤집어 오름차순으로 정렬한다. 같은 깊이는 번호 순(안정 정렬)이라 프레임마다 깜빡이지 않는다.
        const bool bUseAxis = axis.getLengthSquared() > MathUtil::kEpsilon;
        scratch._listKey.resize( listParticle.size() ); // 모든 칸을 아래에서 덮어쓴다
        for ( size_t particleIndex = 0; particleIndex < listParticle.size(); ++particleIndex )
        {
            const float4& positionSize      = listParticle[particleIndex]._positionSize;
            const float3  position          = float3::transform( float3{ positionSize._x, positionSize._y, positionSize._z }, world );
            const float32 depth             = bUseAxis ? ( position - eye ).dot( axis ) : float3::getDistanceSquared( position, eye );
            scratch._listKey[particleIndex] = ~Internal::makeOrderedBits( depth );
        }
        const VFXParticleOrder range = Internal::appendIdentity( listParticle.size(), outListOrder );
        Internal::sortByKey( outListOrder.data() + range._first, range._count, scratch );
        return range;
    }

    VFXParticleOrder VFXParticleOrder::appendByAge( vector_reference<const GPUParticle> listParticle, bool bOldestFirst, VFXParticleSortScratch& scratch, vector<uint32>& outListOrder )
    {
        using Internal = VFXParticleOrderInternal;
        // 나이는 수명 비율(0 = 갓 태어남, 1 = 사라짐)이다. 눈 · 뷰를 보지 않는다.
        scratch._listKey.resize( listParticle.size() );
        for ( size_t particleIndex = 0; particleIndex < listParticle.size(); ++particleIndex )
        {
            const uint32 bits               = Internal::makeOrderedBits( listParticle[particleIndex]._rotationLife._y );
            scratch._listKey[particleIndex] = bOldestFirst ? ~bits : bits;
        }
        const VFXParticleOrder range = Internal::appendIdentity( listParticle.size(), outListOrder );
        Internal::sortByKey( outListOrder.data() + range._first, range._count, scratch );
        return range;
    }
} // namespace sw

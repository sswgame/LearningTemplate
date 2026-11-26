// GPUSortStageTest — 큰 투명 배치의 다단 정렬 단계(`GPUScene::collectSortStages`)를 셰이더(instance_sort.hlsl `SW_SORT_MULTI_PASS`)와 같은 비교로 CPU 에서 돌려 정렬되는지 봅니다(nogpu).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Scene/GPUScene.h"

#include <algorithm>
#include <numeric>

using namespace sw;

namespace
{
    /** @brief 낮은 칸 @p low 와 높은 칸 @p high 를 오름차순으로 맞춥니다 — 높은 칸이 개수 밖이면 무한대다(셰이더와 같다). */
    void exchange( vector<uint32>& listValue, uint32 low, uint32 high )
    {
        if ( high < static_cast<uint32>( listValue.size() ) && listValue[low] > listValue[high] )
            std::swap( listValue[low], listValue[high] );
    }

    /** @brief 거리 @p distance 의 모든 쌍을 견줍니다(@p pairCount 쌍). */
    void exchangeDistance( vector<uint32>& listValue, uint32 distance, uint32 pairCount )
    {
        for ( uint32 pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const uint32 low = ( ( pairIndex & ~( distance - 1u ) ) << 1u ) + ( pairIndex & ( distance - 1u ) );
            exchange( listValue, low, low + distance );
        }
    }

    /** @brief 블록 @p span 의 앞 절반과 뒤집은 뒤 절반을 견줍니다(@p pairCount 쌍). */
    void flip( vector<uint32>& listValue, uint32 span, uint32 pairCount )
    {
        const uint32 halfSpan = span >> 1u;
        for ( uint32 pairIndex = 0; pairIndex < pairCount; ++pairIndex )
        {
            const uint32 offset = pairIndex & ( halfSpan - 1u );
            const uint32 low    = ( ( pairIndex & ~( halfSpan - 1u ) ) << 1u ) + offset;
            exchange( listValue, low, low + ( span - 1u ) - 2u * offset );
        }
    }

    /** @brief 단계 하나를 CPU 에서 돕니다. 스레드 수 밖의 쌍은 돌지 않는다 — 단계가 모자란 스레드를 내면 정렬이 깨진다. */
    void runStage( vector<uint32>& listValue, const GPUSortStage& stage )
    {
        constexpr uint32 kBlock = kGPUSortMaxElements;
        switch ( stage._kind )
        {
            case GPUSortStageKind::Block:
            {
                for ( uint32 span = 2; span <= kBlock; span <<= 1u )
                {
                    flip( listValue, span, stage._threadCount );
                    for ( uint32 distance = span >> 2u; distance > 0; distance >>= 1u )
                    {
                        exchangeDistance( listValue, distance, stage._threadCount );
                    }
                }
                break;
            }
            case GPUSortStageKind::Flip:
                flip( listValue, stage._span, stage._threadCount );
                break;
            case GPUSortStageKind::Distance:
                exchangeDistance( listValue, stage._span, stage._threadCount );
                break;
            case GPUSortStageKind::Merge:
            {
                for ( uint32 distance = kBlock >> 1u; distance > 0; distance >>= 1u )
                {
                    exchangeDistance( listValue, distance, stage._threadCount );
                }
                break;
            }
            case GPUSortStageKind::Resolve:
                SW_EXPECT_TRUE( stage._threadCount >= static_cast<uint32>( listValue.size() ) );
                break;
        }
    }
} // namespace

/**
 * @brief [GPUSortStageTest] 배치 크기마다(2 의 거듭제곱 · 그 밖) 단계가 모두 끝나면 오름차순이다 — 컬링이 개수를 줄여도 같다
 * @details 단계는 배치 크기로 정하고 실제 개수는 GPU 만 안다 — 크기 4097 의 단계로 개수 700 · 4097 을 모두 정렬해야 한다.
 *          변이: 뒤집기의 짝을 반대로 잡거나 펼치기 거리를 하나 빠뜨리면 진다. 블록 단계의 스레드 수를 개수로 나누어 버림하면 마지막 블록이 정렬되지 않아 진다.
 */
SW_TEST_CASE( GPUSortStageTest, StagesSortEveryCount )
{
    for ( const uint32 batchCount : { 513u, 1000u, 1024u, 1025u, 2048u, 3000u, 4097u, 9000u } )
    {
        vector<GPUSortStage> listStage;
        GPUScene::collectSortStages( batchCount, listStage );
        for ( const uint32 visibleCount : { batchCount, batchCount - 1u, 700u, 1u } )
        {
            if ( visibleCount > batchCount )
                continue;
            vector<uint32> listValue( visibleCount );
            std::iota( listValue.begin(), listValue.end(), 0u );
            // 결정적인 섞기 — 실행마다 같은 배열이다.
            uint32 state = 0x9E3779B9u ^ batchCount;
            for ( uint32 index = visibleCount; index > 1; --index )
            {
                state = state * 1664525u + 1013904223u;
                std::swap( listValue[index - 1], listValue[( state >> 8u ) % index] );
            }
            for ( const GPUSortStage& stage : listStage )
            {
                runStage( listValue, stage );
            }
            SW_EXPECT_TRUE( std::is_sorted( listValue.begin(), listValue.end() ) );
        }
    }
}

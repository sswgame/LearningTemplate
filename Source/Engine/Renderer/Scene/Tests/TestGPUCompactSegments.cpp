// GPUCompactSegmentTest — 간접 인자 압축 구간(`GPUScene::collectCompactSegments`)이 함께 그리는 배치만 잇고, 묶음이 구간으로 빈틈없이 나뉠 때만 개수 버퍼로 그리는지 봅니다(nogpu).
#include "pch.h"

#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Scene/GPUScene.h"

using namespace sw;

namespace
{
    /** @brief 같은 버퍼 · 변형의 그릴 수 있는 배치 @p count 개입니다. */
    void makeBatches( uint32 count, vector<GPUMeshBatch>& outListBatch, vector<GPUMeshBatchBinding>& outListBinding )
    {
        outListBatch.assign( count, GPUMeshBatch{} );
        outListBinding.assign( count, GPUMeshBatchBinding{} );
        for ( GPUMeshBatch& batch : outListBatch )
        {
            batch._visibleCapacity   = 1;
            batch._shaderPermutation = 3;
        }
        for ( GPUMeshBatchBinding& binding : outListBinding )
        {
            binding._vertexBuffer = RHIBufferHandle{ 11 };
            binding._indexBuffer  = RHIBufferHandle{ 12 };
        }
    }

    vector<uint32> collectSegments( const vector<GPUMeshBatch>& listBatch, const vector<GPUMeshBatchBinding>& listBinding, uint32 opaqueCount )
    {
        vector<uint32> listSegmentCount;
        GPUScene::collectCompactSegments( vector_reference<const GPUMeshBatch>( listBatch ), vector_reference<const GPUMeshBatchBinding>( listBinding ), opaqueCount, listSegmentCount );
        return listSegmentCount;
    }
} // namespace

/**
 * @brief [GPUCompactSegmentTest] 구간은 상한에서 끊고, 투명 경계 · 변형 · 정점 버퍼 · 그릴 수 없는 배치에서 끊는다. 짧으면 구간이 아니다
 * @details 변이: 상한 · 경계 · 그릴 수 있음 중 하나를 보지 않으면 그 자리의 단언이 진다.
 */
SW_TEST_CASE( GPUCompactSegmentTest, SegmentsJoinOnlyBatchesDrawnTogether )
{
    vector<GPUMeshBatch>        listBatch;
    vector<GPUMeshBatchBinding> listBinding;
    makeBatches( kMaxCompactSegment + 40u, listBatch, listBinding );
    const uint32   opaqueCount = kMaxCompactSegment + 20u;
    vector<uint32> listSegment = collectSegments( listBatch, listBinding, opaqueCount );
    SW_EXPECT_EQUAL( kMaxCompactSegment, listSegment[0] );
    SW_EXPECT_EQUAL( 20u, listSegment[kMaxCompactSegment] ); // 불투명 끝에서 끊긴다
    SW_EXPECT_EQUAL( 20u, listSegment[opaqueCount] );        // 투명

    // 다른 변형 · 다른 정점 버퍼 · 그릴 수 없는 배치가 구간을 끊는다.
    makeBatches( 30, listBatch, listBinding );
    listBatch[5]._shaderPermutation = 4;
    for ( uint32 index = 12; index < 30; ++index )
    {
        listBinding[index]._vertexBuffer = RHIBufferHandle{ 21 };
    }
    listBinding[20]._bDeformRefused = SW_TRUE;
    listBatch[25]._visibleCapacity  = 0;
    listSegment                     = collectSegments( listBatch, listBinding, 30 );
    SW_EXPECT_EQUAL( 5u, listSegment[0] );
    SW_EXPECT_EQUAL( 0u, listSegment[5] ); // 하나뿐이라 구간이 아니다
    SW_EXPECT_EQUAL( 6u, listSegment[6] );
    SW_EXPECT_EQUAL( 8u, listSegment[12] );
    SW_EXPECT_EQUAL( 0u, listSegment[20] );
    SW_EXPECT_EQUAL( 4u, listSegment[21] );
    SW_EXPECT_EQUAL( 4u, listSegment[26] );
}

/**
 * @brief [GPUCompactSegmentTest] 묶음이 구간 여럿으로 빈틈없이 나뉠 때만 모은 인자로 그린다
 * @details 변이: 구간 머리가 아닌 자리나 구간 중간에서 끝나는 묶음을 받으면 남의 배치를 그린다.
 */
SW_TEST_CASE( GPUCompactSegmentTest, GroupsMustCoverWholeSegments )
{
    vector<uint32> listSegment( 20, 0u );
    listSegment[0] = 8;
    listSegment[8] = 6;
    SW_EXPECT_TRUE( GPUScene::isCompactSegmentSpan( listSegment, 0, 8 ) );
    SW_EXPECT_TRUE( GPUScene::isCompactSegmentSpan( listSegment, 0, 14 ) );
    SW_EXPECT_TRUE( GPUScene::isCompactSegmentSpan( listSegment, 8, 6 ) );
    SW_EXPECT_FALSE( GPUScene::isCompactSegmentSpan( listSegment, 0, 10 ) ); // 구간 중간에서 끝난다
    SW_EXPECT_FALSE( GPUScene::isCompactSegmentSpan( listSegment, 2, 6 ) );  // 머리가 아니다
    SW_EXPECT_FALSE( GPUScene::isCompactSegmentSpan( listSegment, 8, 8 ) );  // 구간 뒤의 배치가 섞였다
    SW_EXPECT_FALSE( GPUScene::isCompactSegmentSpan( listSegment, 0, 0 ) );
}

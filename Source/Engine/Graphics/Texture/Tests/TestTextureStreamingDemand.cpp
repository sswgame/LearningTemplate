// TextureStreamingDemandTest — 씬 스트리밍 (라) 우선순위 공유: 텍스처 요구는 큰 화면 크기와 급한 스트리밍 우선순위를 남기고, 우선순위는 셀 읽기와 같은 IO 우선순위로 바뀐다.
#include "pch.h"

#include "Core/File/AsyncFileIO.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/StreamingSourcePriority.h"
#include "Engine/Graphics/Texture/TextureCache.h"

using namespace sw;

/** @brief [TextureStreamingDemandTest] 합치면 화면 크기는 큰 값, 우선순위는 급한 값이 남는다 — 둘은 따로 고른다 */
SW_TEST_CASE( TextureStreamingDemandTest, MergeKeepsLargestSizeAndMostUrgentPriority )
{
    TextureStreamingDemand demand{ 0.25f, StreamingSourcePriority::Normal };
    demand.merge( TextureStreamingDemand{ 0.1f, StreamingSourcePriority::High } );
    SW_EXPECT_EQUAL( 0.25f, demand._screenSize );
    SW_EXPECT_TRUE( demand._priority == StreamingSourcePriority::High );
    demand.merge( TextureStreamingDemand{ 0.5f, StreamingSourcePriority::Lowest } );
    SW_EXPECT_EQUAL( 0.5f, demand._screenSize );
    SW_EXPECT_TRUE( demand._priority == StreamingSourcePriority::High );
}

/** @brief [TextureStreamingDemandTest] 스트리밍 우선순위 다섯 단계는 IO 우선순위 셋으로 접히고 `Critical`(프레임이 기다리는 읽기)은 쓰지 않는다 */
SW_TEST_CASE( TextureStreamingDemandTest, PriorityFoldsIntoIOPriority )
{
    SW_EXPECT_TRUE( toAsyncIOPriority( StreamingSourcePriority::Highest ) == AsyncIOPriority::High );
    SW_EXPECT_TRUE( toAsyncIOPriority( StreamingSourcePriority::High ) == AsyncIOPriority::High );
    SW_EXPECT_TRUE( toAsyncIOPriority( StreamingSourcePriority::Normal ) == AsyncIOPriority::Normal );
    SW_EXPECT_TRUE( toAsyncIOPriority( StreamingSourcePriority::Low ) == AsyncIOPriority::Low );
    SW_EXPECT_TRUE( toAsyncIOPriority( StreamingSourcePriority::Lowest ) == AsyncIOPriority::Low );
}

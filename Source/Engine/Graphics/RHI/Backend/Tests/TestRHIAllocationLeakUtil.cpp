/**
 * @file TestRHIAllocationLeakUtil.cpp
 * @brief 종료 때 남은 할당의 이름을 라이브러리(VMA · D3D12MA)의 통계 JSON 에서 모으는 일을 봅니다 — 두 라이브러리가 적는 모양 그대로다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/Backend/RHIAllocationLeakUtil.h"

using namespace sw;

/**
 * @brief [RHIAllocationLeakUtilTest] 할당마다 적힌 이름만 나온 차례로 모으고, 이름 없는 할당 · 다른 칸의 글자는 모으지 않는다
 * @details 변이: `"Name"` 이 아닌 글자 칸(`"GPU"` · `"Type"`)까지 모으면 수가 어긋난다.
 */
SW_TEST_CASE( RHIAllocationLeakUtilTest, CollectsAllocationNamesOnly )
{
    const utf8* const pStats =
        "{ \"General\": { \"API\": \"Vulkan\", \"GPU\": \"Any\" }, \"DefaultPools\": { \"Type 0\": { \"Blocks\": { \"0\": { \"Suballocations\": ["
        "{\"Offset\": 0, \"Type\": \"BUFFER\", \"Size\": 256, \"Usage\": 18, \"Name\": \"ConstantBuffer\"},"
        "{\"Offset\": 256, \"Type\": \"FREE\", \"Size\": 1024}] } }, \"DedicatedAllocations\": ["
        "{\"Type\": \"IMAGE_OPTIMAL\", \"Size\": 8294400, \"Usage\": 23},\n"
        "{\"Type\": \"IMAGE_OPTIMAL\", \"Size\": 8294400, \"Usage\": 23, \"Name\" : \"RenderTarget\"}] } } }";
    const vector<string> listName = RHIAllocationLeakUtil::collectNames( pStats );
    SW_REQUIRE_EQUAL( size_t( 2 ), listName.size() );
    SW_EXPECT_TRUE( listName[0] == "ConstantBuffer" );
    SW_EXPECT_TRUE( listName[1] == "RenderTarget" );

    SW_EXPECT_TRUE( RHIAllocationLeakUtil::collectNames( nullptr ).empty() );
    SW_EXPECT_TRUE( RHIAllocationLeakUtil::collectNames( "\"Name\": \"cut" ).empty() ); // 잘린 글에서 멈춘다
}

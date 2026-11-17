/**
 * @file TestPackEntryStorageRule.cpp
 * @brief 팩 항목의 저장 방식을 고르는 한 곳(`PackEntryStorageRule`)입니다 — 쿠커 · 모드 팩 빌더가 같은 함수를 부른다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/Pack/PackConfig.h"

using namespace sw;

/**
 * @brief [PackEntryStorageRuleTest] 목록의 확장자는 Stored, 나머지는 Blocks 이고, 범위로 읽는 텍스처(`.dds`)가 Blocks 가 되면 오류다(MR-3)
 * @details 변이: 확장자 비교가 대소문자를 가리면 `.DDS` 가 목록을 지나쳐 오류가 된다. `.dds` 검사를 빼면 마지막 단언이 진다.
 */
SW_TEST_CASE( PackEntryStorageRuleTest, CompressedTextureEntry )
{
    SW_EXPECT_LOG( "PackEntryStorageRule.CompressedTextureEntry" );
    const vector<string> listStored{ ".dds", ".ogg" };
    SW_EXPECT_TRUE( PackEntryStorageRule::select( "engine/textures/grass.dds", listStored ).value() == PackEntryStorage::Stored );
    SW_EXPECT_TRUE( PackEntryStorageRule::select( "engine/textures/GRASS.DDS", listStored ).value() == PackEntryStorage::Stored );
    SW_EXPECT_TRUE( PackEntryStorageRule::select( "engine/data/table.xml", listStored ).value() == PackEntryStorage::Blocks );
    SW_EXPECT_FALSE( PackEntryStorageRule::select( "engine/textures/grass.dds", vector<string>{ ".ogg" } ).has_value() );
}

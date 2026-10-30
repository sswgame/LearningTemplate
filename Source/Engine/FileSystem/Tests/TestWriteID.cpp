#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

SW_TEST_CASE( WriteIDTest, GlobalIDKeepsItsDomain )
{
    const std::optional<DomainAssetID> id = engine::getSubsystem<AssetPaths>().toWriteID( "common/materials/new_not_yet.material" );
    SW_REQUIRE_TRUE( id.has_value() );
    SW_EXPECT_EQUAL( string( "common/materials/new_not_yet.material" ), id->str() );
}

SW_TEST_CASE( WriteIDTest, RelativeKeyWritesTheFileReadingPicked )
{
    // engine 에만 있는 상대 키 — 읽기가 고른 그 파일에 쓴다(검색 순서의 앞 도메인에 새로 만들지 않는다).
    const std::optional<DomainAssetID> id = engine::getSubsystem<AssetPaths>().toWriteID( "shaders/forwardlit.hlsl" );
    SW_REQUIRE_TRUE( id.has_value() );
    SW_EXPECT_EQUAL( string( "engine/shaders/forwardlit.hlsl" ), id->str() );
}

SW_TEST_CASE( WriteIDTest, MissingRelativeKeyHasNoDomain )
{
    SW_EXPECT_LOG( "AssetPaths.WriteRelativeKeyMissing" );
    const AssetPaths& paths = engine::getSubsystem<AssetPaths>();
    SW_EXPECT_FALSE( paths.toWriteID( "materials/no_such_file_anywhere.material" ).has_value() );
    SW_EXPECT_TRUE( paths.getWritePath( "materials/no_such_file_anywhere.material" ).empty() ); // 작업 폴더로 물러나지 않는다
}

SW_TEST_CASE( WriteIDTest, ParentComponentLeavesTheTree )
{
    SW_EXPECT_LOG( "AssetPaths.WriteOutsideTree" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().toWriteID( "engine/../../x.txt" ).has_value() );
}

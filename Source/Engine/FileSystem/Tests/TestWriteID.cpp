#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

// 쓰기 입구(`AssetPaths::resolveWritePath`)는 리소스 id 만 받는다 — 전역 id 는 그 도메인 루트 아래(없는 파일도), 상대 키는 읽기가 고른 있는 파일이다.

using namespace sw;

SW_TEST_CASE( WriteIDTest, GlobalIDKeepsItsDomain )
{
    const std::optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().resolveWritePath( "common/materials/new_not_yet.material" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( engine::getSubsystem<AssetPaths>().toAssetID( path->str() ), string( "common/materials/new_not_yet.material" ) );
}

SW_TEST_CASE( WriteIDTest, RelativeKeyWritesTheFileReadingPicked )
{
    // engine 에만 있는 상대 키 — 읽기가 고른 그 파일에 쓴다(검색 순서의 앞 도메인에 새로 만들지 않는다).
    const std::optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().resolveWritePath( "shaders/forwardlit.hlsl" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( engine::getSubsystem<AssetPaths>().toAssetID( path->str() ), string( "engine/shaders/forwardlit.hlsl" ) );
}

SW_TEST_CASE( WriteIDTest, MissingRelativeKeyHasNoDomain )
{
    SW_EXPECT_LOG( "AssetPaths.WriteRelativeKeyMissing" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().resolveWritePath( "materials/no_such_file_anywhere.material" ).has_value() ); // 작업 폴더로 물러나지 않는다
}

SW_TEST_CASE( WriteIDTest, ParentComponentLeavesTheTree )
{
    SW_EXPECT_LOG( "AssetPaths.WriteNotResourceID" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().resolveWritePath( "engine/../../x.txt" ).has_value() );
}

SW_TEST_CASE( WriteIDTest, DiskPathIsNotAResourceID )
{
    // 디스크 경로는 쓰기 입구에 들어오지 않는다 — 디스크에 쓰는 쪽은 `save*(AbsolutePath)` 를 바로 부른다.
    SW_EXPECT_LOG( "AssetPaths.WriteNotResourceID" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().resolveWritePath( FileUtil::joinPath( ProcessRoots::get()._content.str(), "engine/x.txt" ) ).has_value() );
    SW_EXPECT_TRUE( AssetPaths::normalizeAssetID( ProcessRoots::get()._content.str() ).empty() );
}

#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

// 쓰기 입구(`AssetPaths::toDiskPath`)는 리소스 id 만 받는다 — 전역 id 는 그 도메인 루트 아래(없는 파일도), 상대 키는 읽기가 고른 있는 파일이다.

using namespace sw;

SW_TEST_CASE( WriteIDTest, GlobalIDKeepsItsDomain )
{
    const optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().toDiskPath( "common/materials/new_not_yet.material" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( engine::getSubsystem<AssetPaths>().toAssetID( path->str() ), string( "common/materials/new_not_yet.material" ) );
}

/**
 * @brief [WriteIDTest] 리소스 트리에 쓰는 자리는 모든 플랫폼에서 소문자다 — 대문자가 섞인 id 를 풀어도 도메인 루트 아래 부분은 소문자이고, `toAssetID` 왕복도 소문자 id 다
 * @details 사용자 규칙("리소스 경로 아래에 저장할 때는 무조건 소문자")의 그물. 비교 자리를 OS 대소문자 규칙으로 바꿔도 쓰는 자리는 이 규칙을 지킨다.
 *          변이: `toDiskPath` 가 id 를 접지 않고(`normalizeAssetID` 를 건너뛰고) 이으면 리눅스 · Windows 모두 `Test/MixedCase.XML` 이 남아 진다.
 */
SW_TEST_CASE( WriteIDTest, MixedCaseIDWritesLowercaseInTheResourceTree )
{
    const AssetPaths&            assetPaths = engine::getSubsystem<AssetPaths>();
    const optional<AbsolutePath> domainRoot = assetPaths.toDiskPath( "engine" );
    const optional<AbsolutePath> path       = assetPaths.toDiskPath( "engine/Write_ID_Test_Not_Yet/MixedCase.XML" );
    SW_REQUIRE_TRUE( domainRoot.has_value() && path.has_value() );
    string relative;
    SW_REQUIRE_TRUE( FileUtil::tryMakePathUnderRoot( domainRoot->str(), path->str(), relative ) );
    SW_EXPECT_EQUAL( string( "write_id_test_not_yet/mixedcase.xml" ), relative ); // 원래 철자를 돌려주는 판정이라 대소문자가 그대로 보인다
    SW_EXPECT_EQUAL( string( "engine/write_id_test_not_yet/mixedcase.xml" ), assetPaths.toAssetID( path->str() ) );
}

/**
 * @brief [WriteIDTest] 리소스 트리에 새로 생기는 이름은 쓰기 경로가 막는다 — 대문자가 든 새 자리는 거절, 소문자 · 트리 밖은 통과. 입구가 이름을 접지 않아도 대문자 파일이 생기지 않는다
 * @details 변이: 규칙을 걸지 않거나(`AssetPaths::initialize`) 상대 부분이 아닌 전체 경로를 보면(`Content` 의 C) 첫 · 둘째 단언이 진다.
 */
SW_TEST_CASE( WriteIDTest, NewNamesUnderContentMustBeLowercase )
{
    const string content = ProcessRoots::get()._content.str();
    SW_EXPECT_FALSE( AssetPaths::checkLowercaseResourceName( content + "/engine/write_id_test_not_yet/Upper.txt" ).has_value() );
    SW_EXPECT_TRUE( AssetPaths::checkLowercaseResourceName( content + "/engine/write_id_test_not_yet/lower.txt" ).has_value() );
    SW_EXPECT_TRUE( AssetPaths::checkLowercaseResourceName( ProcessRoots::get()._saved.str() + "/Automation/Upper.txt" ).has_value() );

    // 쓰기 경로가 그 규칙을 지난다(엔진이 선 프로세스는 규칙이 걸려 있다) — 파일이 생기지 않는다.
    const string upperFile = content + "/scratch/write_id_test_not_yet/Upper.txt";
    SW_EXPECT_LOG( "File.CreateRefused" );
    SW_EXPECT_FALSE( FileUtil::writeTextFile( upperFile, "x" ) );
    SW_EXPECT_FALSE( FileUtil::exists( upperFile ) );
}

SW_TEST_CASE( WriteIDTest, RelativeKeyWritesTheFileReadingPicked )
{
    // engine 에만 있는 상대 키 — 읽기가 고른 그 파일에 쓴다(검색 순서의 앞 도메인에 새로 만들지 않는다).
    const optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().toDiskPath( "shaders/forward_lit.hlsl" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( engine::getSubsystem<AssetPaths>().toAssetID( path->str() ), string( "engine/shaders/forward_lit.hlsl" ) );
}

// 작업 도메인은 폴더가 아직 없어도 도메인이다(빈 트리의 첫 쓰기) — 상대 키로 거절하지 않는다.
SW_TEST_CASE( WriteIDTest, ScratchDomainNeedsNoFolderYet )
{
    const optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().toDiskPath( "scratch/write_id_test_not_yet/new.appearance.xml" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_EQUAL( engine::getSubsystem<AssetPaths>().toAssetID( path->str() ), string( "scratch/write_id_test_not_yet/new.appearance.xml" ) );
}

SW_TEST_CASE( WriteIDTest, MissingRelativeKeyHasNoDomain )
{
    SW_EXPECT_LOG( "AssetPaths.WriteRelativeKeyMissing" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().toDiskPath( "materials/no_such_file_anywhere.material" ).has_value() ); // 작업 폴더로 물러나지 않는다
}

SW_TEST_CASE( WriteIDTest, ParentComponentLeavesTheTree )
{
    SW_EXPECT_LOG( "AssetPaths.WriteNotResourceID" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().toDiskPath( "engine/../../x.txt" ).has_value() );
}

SW_TEST_CASE( WriteIDTest, DiskPathIsNotAResourceID )
{
    // 디스크 경로는 쓰기 입구에 들어오지 않는다 — 디스크에 쓰는 쪽은 `save*(AbsolutePath)` 를 바로 부른다.
    SW_EXPECT_LOG( "AssetPaths.WriteNotResourceID" );
    SW_EXPECT_FALSE( engine::getSubsystem<AssetPaths>().toDiskPath( FileUtil::joinPath( ProcessRoots::get()._content.str(), "engine/x.txt" ) ).has_value() );
    SW_EXPECT_TRUE( AssetPaths::normalizeAssetID( ProcessRoots::get()._content.str() ).empty() );
}

SW_TEST_CASE( WriteIDTest, BareDomainIsTheDomainRoot )
{
    // 도메인 이름만인 id 는 그 도메인 루트다(에디터의 도메인 폴더 - 콘텐츠 브라우저 루트). `game` 은 팩 이름까지가 도메인이다.
    const optional<AbsolutePath> path = engine::getSubsystem<AssetPaths>().toDiskPath( "engine" );
    SW_REQUIRE_TRUE( path.has_value() );
    SW_EXPECT_TRUE( FileUtil::isDirectory( path->str() ) );
    SW_EXPECT_EQUAL( FileUtil::joinPath( ProcessRoots::get()._content.str(), "engine" ), path->str() );
}

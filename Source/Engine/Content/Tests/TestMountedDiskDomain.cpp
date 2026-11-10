// 붙인 디스크 도메인(`AssetPaths::mountDiskDomain`) — 쓰기 자리의 디스크 표기, 경로 캐시의 낡은 답, 리소스 입구의 OS 경로 거절, 떼기(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentSource.h"
#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;
using sw::test::ScopedScratchDomain;

namespace
{
    /** @brief 콘텐츠 정책을 바꿨다가 소멸자에서 되돌리는 가드입니다(정책은 프로세스 전역 팩 매니저의 값이다). */
    struct ScopedContentSource
    {

        PackManager&  _packs;
        ContentSource _previous;
        explicit ScopedContentSource( PackManager& packs )
            : _packs{ packs }
            , _previous{ packs.getContentSource() }
        {
        }
        ~ScopedContentSource() { _packs.setContentSource( _previous ); }
        ScopedContentSource( const ScopedContentSource& )            = delete;
        ScopedContentSource& operator=( const ScopedContentSource& ) = delete;
    };

    optional<AbsolutePath> findLoose( string_view id )
    {
        return engine::getSubsystem<AssetPaths>().findLooseFile( id );
    }
} // namespace

/**
 * @brief [MountedDiskDomainTest] 붙인 디스크 도메인에 id 로 쓰는 경로는 이미 있는 폴더의 디스크 표기를 쓰고, 없는 폴더부터는 id 의 표기를 쓴다
 * @details 소문자 id 를 그대로 이으면 대소문자를 구분하는 파일 시스템(Linux)에서 `Saved/Automation` 옆에 `saved/automation` 이 새로 생긴다. Windows 는 소문자로 이어도 같은 폴더다.
 */
SW_TEST_CASE( MountedDiskDomainTest, WritePathKeepsTheDiskSpellingOfExistingFolders )
{
    const ScopedScratchDomain domain;
    const AssetPaths&         paths     = engine::getSubsystem<AssetPaths>();
    const string              directory = domain.getFolder();
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( directory + "/Saved/Automation" ) );
    const string diskFile = directory + "/Saved/Automation/Out.txt";
    const string diskID   = ScopedScratchDomain::toID( diskFile ).str();
    SW_REQUIRE_TRUE( diskID.empty() == false );

    // 소문자 id: 있는 폴더 둘은 디스크 표기이고 아직 없는 파일 이름은 id 의 표기다.
    const string                 lowerID   = FileUtil::normalizePath( diskID );
    const optional<AbsolutePath> writePath = paths.toDiskPath( lowerID );
    SW_REQUIRE_TRUE( writePath.has_value() );
    SW_EXPECT_TRUE( FileUtil::isSameDiskPath( diskFile, writePath->str() ) );
#if defined( SW_PLATFORM_LINUX )
    SW_EXPECT_EQUAL( directory + "/Saved/Automation/out.txt", writePath->str() );
#endif

    // 없는 폴더부터는 id 의 표기를 쓴다(소문자로 바꾸지 않는다). Windows 는 소문자로 잇는다 — 같은 폴더다.
    const optional<AbsolutePath> freshPath = paths.toDiskPath( ScopedScratchDomain::makeID( "Saved/Fresh/Deep/New.txt" ).str() );
    SW_REQUIRE_TRUE( freshPath.has_value() );
    SW_EXPECT_TRUE( FileUtil::isSameDiskPath( directory + "/Saved/Fresh/Deep/New.txt", freshPath->str() ) );

    // 쓴 뒤에는 같은 id 를 대소문자만 다르게 물어도 같은 파일이다(둘째 쓰기가 옆 폴더를 만들지 않는다).
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( writePath->str(), "first" ) );
    const optional<AbsolutePath> again = paths.toDiskPath( diskID );
    SW_REQUIRE_TRUE( again.has_value() );
    SW_EXPECT_TRUE( FileUtil::isSameDiskPath( writePath->str(), again->str() ) );
}

/**
 * @brief [MountedDiskDomainTest] 경로 캐시는 이 프로세스가 만들거나 지운 파일 · 폴더를 낡은 답으로 돌려주지 않는다
 * @details "없음" 은 파일이 생기면(`FileUtil::getCreationEpoch`), "있음" 은 파일이 지워지면(`getRemovalEpoch`) 버린다. 폴더도 같다.
 */
SW_TEST_CASE( MountedDiskDomainTest, PathCacheDropsAnswersWhenThisProcessCreatesOrRemovesFiles )
{
    const ScopedScratchDomain domain;
    const string              filePath   = domain.makeDiskPath( "Late.txt" );
    const string              folderPath = domain.makeDiskPath( "LateFolder" );
    const string              fileID     = ScopedScratchDomain::makeID( "late.txt" ).str();
    const string              folderID   = ScopedScratchDomain::makeID( "latefolder" ).str();

    SW_EXPECT_FALSE( findLoose( fileID ).has_value() ); // "없음" 을 기억한다
    SW_EXPECT_FALSE( findLoose( folderID ).has_value() );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( filePath, "late" ) );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folderPath ) );
    SW_EXPECT_TRUE( findLoose( fileID ).has_value() );
    SW_EXPECT_TRUE( findLoose( folderID ).has_value() );

    SW_REQUIRE_TRUE( FileUtil::removeFile( filePath ) ); // "있음" 을 기억한 칸이 낡는다
    SW_REQUIRE_TRUE( FileUtil::removeDirectory( folderPath ) );
    SW_EXPECT_FALSE( findLoose( fileID ).has_value() );
    SW_EXPECT_FALSE( findLoose( folderID ).has_value() );
}

/**
 * @brief [MountedDiskDomainTest] 리소스 입구는 OS 경로를 콘텐츠 정책과 무관하게 거절하고, 디스크 파일은 디스크 입구 · 붙인 디스크 도메인으로 읽는다
 * @details 예전에는 OS 절대 경로를 디스크에서 바로 읽고 팩 전용 모드만 루트 안 경로를 거절해, 잘못 넣은 호출이 Dev 를 지나 배포 테스트에서야
 *          드러났다. 이제 두 정책 모두 거절하고, 루트 밖 파일은 `readTextFile` 이나 `mountDiskDomain` 의 id 로 읽는다.
 */
SW_TEST_CASE( MountedDiskDomainTest, ResourceEntryRefusesOSPathsInEveryContentSource )
{
    const ScopedScratchDomain domain;
    PackManager*              pPacks = engine::findSubsystem<PackManager>(); // 테스트 호스트는 에셋 매니저가 팩 매니저를 건다
    SW_REQUIRE_NOT_NULL( pPacks );
    const ScopedContentSource contentSource{ *pPacks };
    AssetPaths&               paths = engine::getSubsystem<AssetPaths>();

    const string insideRoot  = FileUtil::joinPath( paths.getRootFolderPath(), "game/empty/maps/editortest.scene.xml" );
    const string outsideRoot = domain.makeDiskPath( "outside.txt" );
    SW_REQUIRE_TRUE( FileUtil::exists( insideRoot ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( outsideRoot, "OUTSIDE" ) );
    const AssetID outsideID = ScopedScratchDomain::makeID( "outside.txt" );

    SW_EXPECT_LOG( "AssetIO.OSPathRejected" );
    string content;
    for ( const ContentSource source : { ContentSource::Cooked, ContentSource::Source } )
    {
        contentSource._packs.setContentSource( source );
        SW_EXPECT_FALSE( AssetIO::readTextAsset( AssetID{ insideRoot }, content ) );
        SW_EXPECT_FALSE( AssetIO::readTextAsset( AssetID{ outsideRoot }, content ) );
        SW_EXPECT_FALSE( AssetIO::hasAsset( AssetID{ outsideRoot } ) );
        // 붙인 디스크 도메인은 쿠킹본 정책에서도 디스크에서 읽는다.
        content.clear();
        SW_EXPECT_TRUE( AssetIO::readTextAsset( outsideID, content ) );
        SW_EXPECT_EQUAL( string( "OUTSIDE" ), content );
        SW_EXPECT_TRUE( AssetIO::hasAsset( outsideID ) );
    }
    content.clear();
    SW_EXPECT_TRUE( AssetIO::readTextFile( outsideRoot, content ) );
    SW_EXPECT_EQUAL( string( "OUTSIDE" ), content );

    // 리소스 루트 안 폴더는 붙일 수 없다 — 팩 전용 규칙을 지나 낱개 에셋을 읽는 길이 된다.
    SW_EXPECT_LOG( "AssetPaths.MountInsideContentRoot" );
    SW_EXPECT_FALSE( paths.mountDiskDomain( "insideroot", FileUtil::joinPath( paths.getRootFolderPath(), "game" ) ) );
}

/**
 * @brief [MountedDiskDomainTest] 뗀 도메인의 id 는 더 풀리지 않고, 같은 이름을 다른 폴더에 다시 붙일 수 있다 — 붙지 않은 이름을 떼면 오류다
 * @details 떼기가 없으면 케이스마다 다른 폴더를 같은 이름으로 붙일 수 없어(겹침 거절) 테스트가 프로세스 폴더 하나를 떼지 않고 나눠 썼다.
 */
SW_TEST_CASE( MountedDiskDomainTest, UnmountedDomainNoLongerResolves )
{
    AssetPaths&  paths  = engine::getSubsystem<AssetPaths>();
    const string first  = sw::test::makeScratchPath( "first" );
    const string second = sw::test::makeScratchPath( "second" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( first ) && FileUtil::ensureDirectoryExists( second ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( first, "a.txt" ), "A" ) );

    SW_REQUIRE_TRUE( paths.mountDiskDomain( "unmounttest", first ) );
    SW_EXPECT_TRUE( findLoose( "unmounttest/a.txt" ).has_value() );
    SW_EXPECT_LOG( "AssetPaths.MountConflict" );
    SW_EXPECT_FALSE( paths.mountDiskDomain( "unmounttest", second ) );

    SW_REQUIRE_TRUE( paths.unmountDiskDomain( "unmounttest" ) );
    SW_EXPECT_FALSE( paths.isMountedDiskDomainID( "unmounttest/a.txt" ) );
    SW_EXPECT_FALSE( findLoose( "unmounttest/a.txt" ).has_value() );

    SW_REQUIRE_TRUE( paths.mountDiskDomain( "unmounttest", second ) );
    SW_EXPECT_TRUE( paths.unmountDiskDomain( "unmounttest" ) );

    SW_EXPECT_LOG( "AssetPaths.DomainNotMounted" );
    SW_EXPECT_FALSE( paths.unmountDiskDomain( "unmounttest" ) );
}

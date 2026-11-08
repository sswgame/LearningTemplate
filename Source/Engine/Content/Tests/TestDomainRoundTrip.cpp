// 같은 상대 경로가 두 도메인(common · game)에 있을 때 도메인을 비우거나 정해 읽고 고친다 — 연 도메인의 파일만 바뀐다(R3 · R4 · R7).
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"

using namespace sw;

namespace
{
    /**
     * @brief 리소스 트리의 두 도메인에 같은 상대 경로의 파일을 두고 사라질 때 지우는 픽스처입니다.
     * @details 도메인 덮어쓰기는 리소스 트리의 도메인(common · game/<팩>)에서만 일어난다 — 붙인 디스크 도메인은 검색 순서에 들지 않아 케이스 폴더로 흉내 낼 수 없다.
     *          그래서 트리 안에 시험 전용 폴더를 만들고, 앞 실행이 남긴 것까지 시작과 끝에 지운다.
     */
    struct DomainRoundTripInternal
    {
        static constexpr const utf8* kFolder   = "tests_domain_roundtrip";
        static constexpr const utf8* kRelative = "tests_domain_roundtrip/shared.txt";
        static constexpr const utf8* kGamePack = "game/empty"; ///< 테스트 호스트의 활성 팩(`ResourceEntryRefusesOSPathsInEveryContentSource` 와 같은 팩)

        static string makeDomainFolder( const string& domain ) { return FileUtil::joinPath( FileUtil::joinPath( ProcessRoots::get()._content.str(), domain ), kFolder ); }
        static string makeID( const string& domain ) { return domain + "/" + kRelative; }

        static string readDisk( const string& domain )
        {
            string text;
            (void)FileUtil::readTextFile( FileUtil::joinPath( makeDomainFolder( domain ), "shared.txt" ), text );
            return text;
        }
    };

    class ScopedSharedFiles
    {
    public:
        ScopedSharedFiles()
        {
            removeAll();
            for ( const string& domain : { string( "common" ), string( DomainRoundTripInternal::kGamePack ) } )
            {
                const string folder = DomainRoundTripInternal::makeDomainFolder( domain );
                if ( FileUtil::ensureDirectoryExists( folder ) == false || FileUtil::writeTextFile( FileUtil::joinPath( folder, "shared.txt" ), domain ) == false )
                    test::reportFailure( __FILE__, __LINE__, "could not write the shared file under " + folder );
            }
        }
        ~ScopedSharedFiles() { removeAll(); }
        ScopedSharedFiles( const ScopedSharedFiles& )            = delete;
        ScopedSharedFiles& operator=( const ScopedSharedFiles& ) = delete;

    private:
        static void removeAll()
        {
            for ( const string& domain : { string( "common" ), string( DomainRoundTripInternal::kGamePack ) } )
                (void)FileUtil::removeDirectory( DomainRoundTripInternal::makeDomainFolder( domain ) );
        }
    };
} // namespace

/**
 * @brief [DomainRoundTripTest] 도메인을 비우면 검색 순서로 이긴 파일, 정하면 그 도메인 파일을 읽고 고친다 — 고친 쪽 파일만 바뀐다
 * @details 예전에는 문자열에서 도메인을 추측해(`mapGlobalIDToRoot` 의 폴더 존재) 상대 키와 전역 id 가 다른 파일을 가리킬 수 있었다.
 *          읽기(`AssetIO::readTextAsset`)와 쓰기 자리(`AssetPaths::toDiskPath`)가 같은 해석을 쓰는지(R7)도 여기서 본다.
 */
SW_TEST_CASE( DomainRoundTripTest, OnlyTheOpenedDomainFileChanges )
{
    using Internal = DomainRoundTripInternal;
    const ScopedSharedFiles files;
    const AssetPaths&       paths = engine::getSubsystem<AssetPaths>();
    const string            game{ Internal::kGamePack };
    const string            common{ "common" };

    // 도메인을 비운 읽기의 승자는 검색 순서에서 앞선 도메인이다.
    const vector<string>& listPriority = paths.getSearchPriority();
    const auto            gameRank     = VectorUtil::findIndex( listPriority, string( "game" ) );
    const auto            commonRank   = VectorUtil::findIndex( listPriority, common );
    SW_REQUIRE_TRUE( gameRank.has_value() && commonRank.has_value() );
    const string winner = *gameRank < *commonRank ? game : common;
    const string loser  = winner == game ? common : game;

    string text;
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ Internal::kRelative }, text ) );
    SW_EXPECT_EQUAL( winner, text );
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ Internal::makeID( common ) }, text ) );
    SW_EXPECT_EQUAL( common, text );
    SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ Internal::makeID( game ) }, text ) );
    SW_EXPECT_EQUAL( game, text );

    // 도메인을 비운 편집은 읽은 그 파일(승자)에 쓴다 — 다른 도메인 파일은 그대로다.
    const optional<AbsolutePath> relativeTarget = paths.toDiskPath( Internal::kRelative );
    SW_REQUIRE_TRUE( relativeTarget.has_value() );
    SW_EXPECT_EQUAL( Internal::makeID( winner ), paths.toAssetID( relativeTarget->str() ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( relativeTarget->str(), "edited-relative" ) );
    SW_EXPECT_EQUAL( string( "edited-relative" ), Internal::readDisk( winner ) );
    SW_EXPECT_EQUAL( loser, Internal::readDisk( loser ) );

    // 도메인을 정한 편집은 그 도메인 파일만 바꾼다(승자가 아니어도).
    for ( const string& domain : { common, game } )
    {
        const optional<AbsolutePath> target = paths.toDiskPath( Internal::makeID( domain ) );
        SW_REQUIRE_TRUE( target.has_value() );
        const string other  = domain == game ? common : game;
        const string before = Internal::readDisk( other );
        SW_REQUIRE_TRUE( FileUtil::writeTextFile( target->str(), "edited-" + domain ) );
        SW_EXPECT_EQUAL( "edited-" + domain, Internal::readDisk( domain ) );
        SW_EXPECT_EQUAL( before, Internal::readDisk( other ) );
        SW_REQUIRE_TRUE( AssetIO::readTextAsset( AssetID{ Internal::makeID( domain ) }, text ) );
        SW_EXPECT_EQUAL( "edited-" + domain, text );
    }
}

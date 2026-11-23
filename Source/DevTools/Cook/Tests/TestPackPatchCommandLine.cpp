#include "pch.h"

#include "Core/Encoding/HexUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Security/INetSecurityProvider.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Cook/PackPatchBuilder.h"

#include "Engine/Content/Pack/PackManager.h"
#include "Engine/Content/Pack/PackPatch.h"
#include "Engine/Content/Pack/Tests/PackTestUtil.h"
#include "Engine/Foundation/EngineServices.h"

// `App -make-patch` 명령줄 길(ctest `Pack.MakePatch` — Test/Pack/RunMakePatch.cmake)의 앞뒤 단계다. 팩 쿠킹은 셰이더까지 굽느라 수백 초라 입력 팩은 이 역할이 작게 쓰고,
// 명령줄이 만든 패치는 런타임 적용 길(`PackManager::setPatchTrust` → `scanAndMountPacks`)로 얹어 새 빌드의 내용과 견준다 — 언리얼 `-generatepatch` 의 기준 → 새 빌드 → 패치 → 적용과 같은 한 바퀴.
// 서명 키는 실행마다 새로 만드는 테스트 키다(저장소에 개인 키를 두지 않는다).

using namespace sw;

namespace
{
    struct PackPatchCommandLineTestUtil
    {
        static constexpr string_view kOldFolder          = "old";
        static constexpr string_view kNewFolder          = "new";
        static constexpr string_view kOutFolder          = "out";
        static constexpr string_view kKeyFileName        = "signing.pem";
        static constexpr string_view kPublicKeyXFileName = "signing.x";
        static constexpr string_view kPublicKeyYFileName = "signing.y";
        static constexpr string_view kNewBuild           = "2"; // RunMakePatch.cmake 의 -pack-build 와 같다

        static bool writePack( const string& folder, const vector<std::pair<string, string>>& listEntry )
        {
            return FileUtil::ensureDirectoryExists( FileUtil::toDiskPath( folder ) ) && PackTestUtil::writeTextPack( FileUtil::joinPath( folder, "game.pack" ), "game", listEntry );
        }

        static string read( const PackManager& packs, string_view path )
        {
            string text;
            return packs.readTextFile( path, text ) ? text : string{ "<missing>" };
        }
    };
    using Util = PackPatchCommandLineTestUtil;
} // namespace

/** @brief [PackPatchCommandLineTest] `-make-patch` 의 입력 — 이전 빌드 팩 · 그 빌드 기록(1) · 새 빌드 팩과 테스트 서명 키를 인자 폴더에 쓴다 */
SW_TEST_CHILD_ROLE( PackPatchCommandLineTest, WriteInputs )
{
    const string root = test::getChildRoleArgument();
    SW_REQUIRE_TRUE( root.empty() == false );
    INetSecurityProvider* const pProvider = engine::findSubsystem<INetSecurityProvider>();
    SW_REQUIRE_TRUE_MSG( pProvider != nullptr, "no INetSecurityProvider is published - GameFramework is not loaded" );

    const string oldFolder = FileUtil::joinPath( root, Util::kOldFolder );
    SW_REQUIRE_TRUE( Util::writePack( oldFolder, {
                                                     {"keep.txt", "same"},
                                                     {"edit.txt",  "old"},
                                                     {"gone.txt",  "bye"}
    } ) );
    SW_REQUIRE_TRUE( Util::writePack( FileUtil::joinPath( root, Util::kNewFolder ), {
                                                                                        {"keep.txt", "same"},
                                                                                        {"edit.txt",  "new"},
                                                                                        {"made.txt",   "hi"}
    } ) );
    SW_REQUIRE_TRUE( PackPatchBuilder::writeBuildRecord( oldFolder, "1", FileUtil::joinPath( oldFolder, PackBuildRecord::kFileName ) ) );

    string       privateKeyPem;
    NetPublicKey publicKey;
    SW_REQUIRE_TRUE( pProvider->createSigningKeyPair( NetSignatureAlgorithm::EcdsaP256Sha256, privateKeyPem, publicKey ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, Util::kKeyFileName ) ), privateKeyPem ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, Util::kPublicKeyXFileName ) ), HexUtil::encodeBytes( publicKey._x.data(), publicKey._x.size() ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, Util::kPublicKeyYFileName ) ), HexUtil::encodeBytes( publicKey._y.data(), publicKey._y.size() ) ) );
}

/** @brief [PackPatchCommandLineTest] `-make-patch` 의 산출 — 서명된 패치를 이전 빌드 폴더에 얹어 마운트하면 새 빌드의 내용이 보이고, 새 빌드 기록(2)이 다음 패치의 기준이 된다 */
SW_TEST_CHILD_ROLE( PackPatchCommandLineTest, CheckPatch )
{
    const string root = test::getChildRoleArgument();
    SW_REQUIRE_TRUE( root.empty() == false );
    INetSecurityProvider* const pProvider = engine::findSubsystem<INetSecurityProvider>();
    SW_REQUIRE_TRUE_MSG( pProvider != nullptr, "no INetSecurityProvider is published - GameFramework is not loaded" );

    const string outFolder    = FileUtil::joinPath( root, Util::kOutFolder );
    const string manifestName = PackPatchBuilder::makeManifestFileName( Util::kNewBuild );
    string       manifestText;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( outFolder, manifestName ) ), manifestText ) );
    PackPatchManifest manifest;
    SW_REQUIRE_TRUE( PackPatchManifest::parse( manifestText, manifestName, manifest ) );
    SW_EXPECT_EQUAL( string( "1" ), manifest._fromBuild );
    SW_EXPECT_EQUAL( string( Util::kNewBuild ), manifest._toBuild );
    SW_REQUIRE_TRUE_MSG( manifest._signatureHex.empty() == false, "-patch-sign-key was given but the manifest is not signed" );

    string xHex;
    string yHex;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, Util::kPublicKeyXFileName ) ), xHex ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( root, Util::kPublicKeyYFileName ) ), yHex ) );
    PackPatchTrust trust{};
    trust._pProvider            = pProvider;
    trust._publicKey._algorithm = NetSignatureAlgorithm::EcdsaP256Sha256;
    SW_REQUIRE_TRUE( HexUtil::decodeBytes( xHex, trust._publicKey._x ) && HexUtil::decodeBytes( yHex, trust._publicKey._y ) );

    // 런타임 적용 길 그대로 — 패치 팩 · 목록을 설치된 빌드(1) 폴더에 두고 폴더를 훑는다. 서명이 틀리면 마운트가 진다.
    const string oldFolder = FileUtil::joinPath( root, Util::kOldFolder );
    SW_REQUIRE_TRUE( FileUtil::copyFile( FileUtil::toDiskPath( FileUtil::joinPath( outFolder, manifest._packFileName ) ), FileUtil::toDiskPath( FileUtil::joinPath( oldFolder, manifest._packFileName ) ) ) );
    SW_REQUIRE_TRUE( FileUtil::copyFile( FileUtil::toDiskPath( FileUtil::joinPath( outFolder, manifestName ) ), FileUtil::toDiskPath( FileUtil::joinPath( oldFolder, manifestName ) ) ) );
    PackManager packs;
    SW_REQUIRE_TRUE( packs.setPatchTrust( trust ) );
    SW_REQUIRE_TRUE( packs.scanAndMountPacks( oldFolder ) );
    SW_EXPECT_EQUAL( string( "same" ), Util::read( packs, "game/keep.txt" ) );
    SW_EXPECT_EQUAL( string( "new" ), Util::read( packs, "game/edit.txt" ) );
    SW_EXPECT_EQUAL( string( "hi" ), Util::read( packs, "game/made.txt" ) );
    SW_EXPECT_FALSE( packs.hasFile( "game/gone.txt" ) );

    string                                  recordText;
    fixed_string<constant::buffer::kMax128> recordFileName;
    formatstring( recordFileName.data(), recordFileName.capacity(), "%#%#", Util::kNewBuild, PackBuildRecord::kExtension );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::toDiskPath( FileUtil::joinPath( outFolder, recordFileName ) ), recordText ) );
    PackBuildRecord record;
    SW_REQUIRE_TRUE( PackBuildRecord::parse( recordText, "record", record ) );
    SW_EXPECT_EQUAL( string( Util::kNewBuild ), record._build );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( record._listEntry.size() ) );
}

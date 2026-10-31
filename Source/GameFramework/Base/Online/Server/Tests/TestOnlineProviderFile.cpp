#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Base/Online/Server/OnlineProviderFile.h"

// OnlineProviderFileTest — 키트 제공자 설정 파일(`{ "providers": [ … ] }`) 읽기: 파일 없음 = 제공자 없음, 신뢰 PEM 만 정함 거절,
// 항목을 적은 순서대로 읽기, 못 읽는 파일 · JSON 아님 · 배열 없음 · 객체 아닌 항목 · 항목 읽기 실패 거절. 케이스 폴더에 작은 파일을 쓴다(nogpu).

namespace
{
    struct FakeProviderConfig
    {
        sw::string _name;
    };

    bool readFakeEntry( const sw::JSONValue& entry, FakeProviderConfig& outConfig, sw::string& outError )
    {
        const sw::JSONValue name = entry.get( "name" );
        if ( name.isString() == false )
        {
            outError = "entry needs a 'name' string";
            return false;
        }
        outConfig._name = name.asString();
        return true;
    }

    /** @brief 케이스 폴더에 @p text 를 담은 파일을 쓰고 그 절대 경로를 돌려줍니다. */
    std::optional<sw::AbsolutePath> writeScratch( sw::string_view fileName, sw::string_view text )
    {
        const sw::string path = sw::test::makeScratchPath( fileName );
        SW_EXPECT_TRUE( sw::FileUtil::writeTextFile( path, text ) );
        return sw::AbsolutePath::tryMake( path );
    }

    bool readProviders( const std::optional<sw::AbsolutePath>& providersFile, const std::optional<sw::AbsolutePath>& trustFile,
                        sw::vector<FakeProviderConfig>& outList, sw::string& outError )
    {
        return sw::OnlineProviderFile::read<FakeProviderConfig>( providersFile, trustFile, "pushProvidersFile", "pushTrustFile", &readFakeEntry, outList, outError );
    }
} // namespace

/**
 * @brief [OnlineProviderFileTest] 두 파일을 모두 정하지 않으면 제공자 없음으로 통과하고 앞서 담긴 목록은 비운다. 신뢰 글도 빈 글이다
 */
SW_TEST_CASE( OnlineProviderFileTest, NoFilesMeansNoProviders )
{
    sw::vector<FakeProviderConfig> listConfig( 2 );
    sw::string                     error;
    SW_REQUIRE_TRUE( readProviders( std::nullopt, std::nullopt, listConfig, error ) );
    SW_EXPECT_TRUE( listConfig.empty() );
    SW_EXPECT_TRUE( error.empty() );
    SW_EXPECT_TRUE( sw::OnlineProviderFile::getTrustText( std::nullopt ).empty() );
}

/**
 * @brief [OnlineProviderFileTest] 신뢰 PEM 만 정하고 제공자 파일을 정하지 않으면 두 옵션 이름을 적은 오류로 거절한다
 */
SW_TEST_CASE( OnlineProviderFileTest, TrustWithoutProvidersIsRejected )
{
    const std::optional<sw::AbsolutePath> trustFile = writeScratch( "trust.pem", "pem" );
    SW_REQUIRE_TRUE( trustFile.has_value() );
    sw::vector<FakeProviderConfig> listConfig;
    sw::string                     error;
    SW_EXPECT_FALSE( readProviders( std::nullopt, trustFile, listConfig, error ) );
    SW_EXPECT_EQUAL( sw::string( "pushTrustFile is set but pushProvidersFile is not" ), error );
    SW_EXPECT_TRUE( listConfig.empty() );
}

/**
 * @brief [OnlineProviderFileTest] 항목을 적은 순서대로 읽고, 신뢰 글은 정한 PEM 경로 그대로다
 */
SW_TEST_CASE( OnlineProviderFileTest, ReadsEveryEntryInOrder )
{
    const std::optional<sw::AbsolutePath> providersFile = writeScratch( "providers.json", R"({ "providers": [ { "name": "apns" }, { "name": "fcm" } ] })" );
    const std::optional<sw::AbsolutePath> trustFile     = writeScratch( "trust.pem", "pem" );
    SW_REQUIRE_TRUE( providersFile.has_value() );
    SW_REQUIRE_TRUE( trustFile.has_value() );
    sw::vector<FakeProviderConfig> listConfig;
    sw::string                     error;
    SW_REQUIRE_TRUE( readProviders( providersFile, trustFile, listConfig, error ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listConfig.size() );
    SW_EXPECT_EQUAL( sw::string( "apns" ), listConfig[0]._name );
    SW_EXPECT_EQUAL( sw::string( "fcm" ), listConfig[1]._name );
    SW_EXPECT_EQUAL( trustFile->str(), sw::string( sw::OnlineProviderFile::getTrustText( trustFile ) ) );

    // 빈 배열은 제공자 없음이다
    const std::optional<sw::AbsolutePath> emptyFile = writeScratch( "empty.json", R"({ "providers": [] })" );
    SW_REQUIRE_TRUE( emptyFile.has_value() );
    SW_REQUIRE_TRUE( readProviders( emptyFile, std::nullopt, listConfig, error ) );
    SW_EXPECT_TRUE( listConfig.empty() );
}

/**
 * @brief [OnlineProviderFileTest] JSON 아님 · `providers` 배열 없음 · 객체 아닌 항목은 제공자 옵션 이름을 적은 오류이고, 항목 읽기의 오류는 그대로 올라온다
 */
SW_TEST_CASE( OnlineProviderFileTest, RejectsMalformedFiles )
{
    struct Case
    {
        const utf8* _pFileName;
        const utf8* _pText;
        const utf8* _pErrorPart;
    };
    const Case arrCase[] = {
        {  "notjson.json",                                "{ providers",                                "is not JSON"},
        {  "noarray.json",                     R"({ "providers": {} })",                  "needs a 'providers' array"},
        {"notobject.json", R"({ "providers": [ { "name": "a" }, 3 ] })", "has a provider entry that is not an object"},
        {   "noname.json",    R"({ "providers": [ { "kind": "x" } ] })",                "entry needs a 'name' string"},
    };
    for ( const Case& testCase : arrCase )
    {
        const std::optional<sw::AbsolutePath> providersFile = writeScratch( testCase._pFileName, testCase._pText );
        SW_REQUIRE_TRUE( providersFile.has_value() );
        sw::vector<FakeProviderConfig> listConfig;
        sw::string                     error;
        SW_EXPECT_FALSE( readProviders( providersFile, std::nullopt, listConfig, error ) );
        SW_EXPECT_TRUE( error.find( testCase._pErrorPart ) != sw::string::npos );
    }
}

/**
 * @brief [OnlineProviderFileTest] 정한 제공자 파일이 없으면 옵션 이름과 경로를 적은 오류로 거절한다(파일 읽기가 `File.NotFound` 를 남긴다)
 */
SW_TEST_CASE( OnlineProviderFileTest, MissingProvidersFileIsRejected )
{
    SW_EXPECT_LOG( "File.NotFound" );

    const std::optional<sw::AbsolutePath> providersFile = sw::AbsolutePath::tryMake( sw::test::makeScratchPath( "absent.json" ) );
    SW_REQUIRE_TRUE( providersFile.has_value() );
    sw::vector<FakeProviderConfig> listConfig;
    sw::string                     error;
    SW_EXPECT_FALSE( readProviders( providersFile, std::nullopt, listConfig, error ) );
    SW_EXPECT_TRUE( error.find( "cannot read the pushProvidersFile file" ) != sw::string::npos );
}

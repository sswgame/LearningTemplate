// ShaderCookStampTest — 쿠킹 스탬프는 컴파일이 찾아봤지만 없던 앞자리 후보도 의존으로 적는다: 그 자리에 같은 이름의 헤더가 생기면(가림) 소스가 그대로여도 낡음이다(nogpu, 케이스 폴더만 쓴다).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderCookStamp.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

using namespace sw;

namespace
{
    /** @brief 케이스 폴더 아래 자기 도메인(game) · engine · common 의 `shaders` 와 쿠킹 폴더입니다. */
    struct StampFixture
    {
        string _root;
        string _ownShaders;
        string _engineShaders;
        string _commonShaders;
        string _binDir;
        string _source;

        StampFixture()
            : _root{ FileUtil::normalizeSeparators( test::makeScratchPath( "stamp" ) ) }
            , _ownShaders{ FileUtil::joinPath( _root, "game/shaders" ) }
            , _engineShaders{ FileUtil::joinPath( _root, "engine/shaders" ) }
            , _commonShaders{ FileUtil::joinPath( _root, "common/shaders" ) }
            , _binDir{ FileUtil::joinPath( _root, "cooked/bin/vk" ) }
            , _source{ FileUtil::normalizeSeparators( FileUtil::joinPath( _ownShaders, "a.hlsl" ) ) }
        {
        }

        [[nodiscard]] bool create() const
        {
            return FileUtil::ensureDirectoryExists( _ownShaders ) && FileUtil::ensureDirectoryExists( _engineShaders ) && FileUtil::ensureDirectoryExists( _commonShaders ) &&
                   FileUtil::ensureDirectoryExists( _binDir ) && FileUtil::writeTextFile( _source, "#include \"x.hlsli\"\n" ) &&
                   FileUtil::writeTextFile( FileUtil::joinPath( _commonShaders, "x.hlsli" ), "float4 x() { return 1; }\n" );
        }

        [[nodiscard]] optional<ShaderIncludeResolver> makeResolver() const
        {
            const optional<AbsolutePath> engine = AbsolutePath::tryMake( _engineShaders );
            const optional<AbsolutePath> common = AbsolutePath::tryMake( _commonShaders );
            if ( engine.has_value() == false || common.has_value() == false )
                return nullopt;
            return ShaderIncludeResolver{
                vector<ShaderIncludeRootDomain>{ ShaderIncludeRootDomain{ "engine", *engine }, ShaderIncludeRootDomain{ "common", *common } }
            };
        }

        /** @brief 컴파일이 기댄 파일: 검색 순서(자기 폴더 → engine → common)의 앞 두 자리는 없었고 common 의 헤더를 열었다. */
        [[nodiscard]] vector<string> makeDependencies() const
        {
            return vector<string>{ FileUtil::normalizeSeparators( FileUtil::joinPath( _ownShaders, "x.hlsli" ) ), FileUtil::normalizeSeparators( FileUtil::joinPath( _engineShaders, "x.hlsli" ) ),
                                   FileUtil::normalizeSeparators( FileUtil::joinPath( _commonShaders, "x.hlsli" ) ) };
        }
    };
} // namespace

/** @brief [ShaderCookStampTest] 없던 앞자리 후보에 같은 이름의 헤더가 생기면 낡음이고, 다시 없애면 최신이다. 연 헤더의 내용이 바뀌어도 낡음이다 */
SW_TEST_CASE( ShaderCookStampTest, AbsentCandidateThatAppearsMakesTheCookStale )
{
    const StampFixture fixture;
    SW_REQUIRE_TRUE( fixture.create() );
    const optional<ShaderIncludeResolver> resolver = fixture.makeResolver();
    SW_REQUIRE_TRUE( resolver.has_value() );

    unordered_map<string, vector<string>> mapIncluded;
    mapIncluded[fixture._source] = fixture.makeDependencies();
    {
        ShaderCookStampCache writeCache;
        SW_REQUIRE_TRUE( ShaderCookStamp::writeCookStamp( *resolver, fixture._ownShaders, fixture._binDir, nullptr, mapIncluded, writeCache ) );
    }

    {
        ShaderCookStampCache cache;
        vector<string>       listDependency;
        SW_EXPECT_TRUE( ShaderCookStamp::isCookedOutputCurrent( *resolver, fixture._binDir, fixture._source, fixture._ownShaders, cache, &listDependency ) );
        SW_EXPECT_EQUAL( size_t( 3 ), listDependency.size() ); // 없던 둘도 되돌아와 다음 스탬프가 다시 적는다
    }

    // engine 에 같은 이름이 생겼다 — 다음 컴파일은 그것을 연다(가림). 소스와 연 헤더는 그대로다.
    const string engineHeader = FileUtil::joinPath( fixture._engineShaders, "x.hlsli" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( engineHeader, "float4 x() { return 2; }\n" ) );
    {
        ShaderCookStampCache cache;
        SW_EXPECT_FALSE( ShaderCookStamp::isCookedOutputCurrent( *resolver, fixture._binDir, fixture._source, fixture._ownShaders, cache ) );
    }
    SW_REQUIRE_TRUE( FileUtil::removeFile( engineHeader ) );
    {
        ShaderCookStampCache cache;
        SW_EXPECT_TRUE( ShaderCookStamp::isCookedOutputCurrent( *resolver, fixture._binDir, fixture._source, fixture._ownShaders, cache ) );
    }

    // 연 헤더의 내용이 바뀌면 낡음이다(예전과 같다).
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( fixture._commonShaders, "x.hlsli" ), "float4 x() { return 3; }\n" ) );
    {
        ShaderCookStampCache cache;
        SW_EXPECT_FALSE( ShaderCookStamp::isCookedOutputCurrent( *resolver, fixture._binDir, fixture._source, fixture._ownShaders, cache ) );
    }
}

/** @brief [ShaderCookStampTest] 컴파일 캐시의 의존 기록도 없던 후보를 적는다 — 그 자리에 헤더가 생기면 기록이 맞지 않는다(다시 컴파일) */
SW_TEST_CASE( ShaderCookStampTest, IncludeRecordKeepsAbsentCandidates )
{
    const StampFixture fixture;
    SW_REQUIRE_TRUE( fixture.create() );
    const string recordPath = FileUtil::joinPath( fixture._root, "entry.deps" );
    SW_REQUIRE_TRUE( ShaderCookStamp::writeIncludeRecord( recordPath, fixture.makeDependencies(), 0x1234u ) );

    vector<string> listDependency;
    uint64         bytecodeHash = 0;
    SW_EXPECT_TRUE( ShaderCookStamp::readIncludeRecord( recordPath, listDependency, bytecodeHash ) );
    SW_EXPECT_EQUAL( size_t( 3 ), listDependency.size() );
    SW_EXPECT_EQUAL( uint64( 0x1234u ), bytecodeHash );

    const uint64 before = ShaderCookStamp::computeSourceFingerprint( fixture._source, fixture.makeDependencies() );
    SW_EXPECT_TRUE( before != 0 );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( fixture._ownShaders, "x.hlsli" ), "float4 x() { return 4; }\n" ) );
    SW_EXPECT_FALSE( ShaderCookStamp::readIncludeRecord( recordPath, listDependency, bytecodeHash ) );
    ShaderCookStamp::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( ShaderCookStamp::computeSourceFingerprint( fixture._source, fixture.makeDependencies() ) != before );
}

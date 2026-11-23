// ShaderIncludeRecordTest — include 규칙이 읽은 헤더와 찾아봤지만 없던 앞자리 후보를 쿠킹 작업의 입력으로 적는다: 그 자리에 같은 이름의 헤더가 생기면 기록이 낡는다(nogpu).
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Content/CookJob.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"

using namespace sw;

namespace
{
    /** @brief 임시 도메인 아래 자기 · engine · common 역할의 `shaders` 폴더와 include 규칙입니다. 헤더 `x.hlsli` 는 common 에만 있다. */
    struct IncludeFixture
    {
        test::ScopedScratchDomain _domain;
        AbsolutePath              _ownShaders;
        AbsolutePath              _engineShaders;
        AbsolutePath              _commonShaders;

        IncludeFixture()
            : _domain{}
            , _ownShaders{ _domain.getFolder().join( "own/shaders" ) }
            , _engineShaders{ _domain.getFolder().join( "engine/shaders" ) }
            , _commonShaders{ _domain.getFolder().join( "common/shaders" ) }
        {
        }

        [[nodiscard]] bool create() const
        {
            return FileUtil::ensureDirectoryExists( _ownShaders ) && FileUtil::ensureDirectoryExists( _engineShaders ) && FileUtil::ensureDirectoryExists( _commonShaders ) &&
                   FileUtil::writeTextFile( _commonShaders.join( "x.hlsli" ), "float4 x() { return 1; }\n" );
        }
        ShaderIncludeResolver makeResolver() const
        {
            return ShaderIncludeResolver{
                vector<ShaderIncludeRootDomain>{ ShaderIncludeRootDomain{ "engine", _engineShaders }, ShaderIncludeRootDomain{ "common", _commonShaders } }
            };
        }
        ShaderCompileInput makeInput() const { return ShaderCompileInput{
            _ownShaders.join( "a.hlsl" ), _ownShaders, vector<AbsolutePath>{ _ownShaders, _engineShaders, _commonShaders }
 }; }
    };
} // namespace

/**
 * @brief [ShaderIncludeRecordTest] 앞자리 후보 둘의 "없음" 과 연 헤더가 입력이다 — 후보가 생기거나 헤더가 바뀌면 낡고, 후보를 다시 없애면 새것이다
 */
SW_TEST_CASE( ShaderIncludeRecordTest, AbsentCandidateThatAppearsMakesTheRecordStale )
{
    const IncludeFixture fixture;
    SW_REQUIRE_TRUE( fixture.create() );
    CookJobDesc desc;
    desc._cookerName      = "Shader";
    desc._codeFingerprint = 1;
    desc._target          = "vulkan";
    CookRecord record;
    {
        CookJob       job{ desc };
        vector<uint8> bytes;
        SW_REQUIRE_TRUE( fixture.makeResolver().findInclude( fixture.makeInput(), "x.hlsli", bytes ).has_value() );
        SW_EXPECT_EQUAL( size_t( 3 ), job.getInputs().size() );
        record = CookRecord::make( job, 1 );
    }
    SW_EXPECT_TRUE( record.isFresh( desc ) );

    // engine 에 같은 이름이 생겼다 — 다음 컴파일은 그것을 연다(가림). 연 헤더는 그대로다.
    const AbsolutePath engineHeader = fixture._engineShaders.join( "x.hlsli" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( engineHeader, "float4 x() { return 2; }\n" ) );
    SW_EXPECT_FALSE( record.isFresh( desc ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( engineHeader ) );
    SW_EXPECT_TRUE( record.isFresh( desc ) );

    SW_REQUIRE_TRUE( FileUtil::writeTextFile( fixture._commonShaders.join( "x.hlsli" ), "float4 x() { return 3; }\n" ) );
    SW_EXPECT_FALSE( record.isFresh( desc ) );
}

/** @brief [ShaderIncludeRecordTest] 자기 폴더의 헤더가 include 루트의 같은 이름을 가리면 문제로 적는다 */
SW_TEST_CASE( ShaderIncludeRecordTest, ShadowedHeaderIsAProblem )
{
    const IncludeFixture fixture;
    SW_REQUIRE_TRUE( fixture.create() );
    vector<string> listProblem;
    SW_EXPECT_TRUE( fixture.makeResolver().findShadowedHeaders( fixture._ownShaders, listProblem ) );
    SW_EXPECT_TRUE( listProblem.empty() );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( fixture._ownShaders.join( "x.hlsli" ), "float4 x() { return 4; }\n" ) );
    SW_EXPECT_FALSE( fixture.makeResolver().findShadowedHeaders( fixture._ownShaders, listProblem ) );
    SW_EXPECT_EQUAL( size_t( 1 ), listProblem.size() );
}

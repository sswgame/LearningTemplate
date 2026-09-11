#include "pch.h"

#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) LiveShaderTest — 수동 리로드 대상 수집
// ------------------------------------------------------------------------------
/**
 * @brief [LiveShaderTest] 리로드 대상이 `ShaderCache` 에서 나오는지 검증.
 * @details 이 테스트의 요점은 **대상이 비어 있지 않다**는 것이다. 예전에는 `LiveShaderManager` 가
 *          `watchShader` 로 채우는 자기 등록표를 봤는데 그 함수의 호출부가 하나도 없어서,
 *          `ReloadShaders`(Ctrl+F8) 단축키가 빈 표를 돌고 아무 일도 하지 않았다. 그때도 테스트는
 *          `update()` 만 불러 빈 큐를 도는 것이 전부라 초록이었다.
 */

SW_TEST_CASE( LiveShaderTest, ReloadTargetsComeFromShaderCache )
{
    sw::ShaderCompileDesc vsDesc{};
    vsDesc._filePath     = "engine/shaders/forwardlit.hlsl";
    vsDesc._entryPoint   = "VSMain";
    vsDesc._stage        = sw::ShaderStage::Vertex;
    vsDesc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    sw::ShaderCache cache;
    SW_ASSERT_TRUE( cache.initialize() );

    sw::vector<sw::ShaderCompileDesc> listBefore;
    cache.collectCompiledDescs( listBefore );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listBefore.size() ) );

    // 컴파일이 성공하든(툴체인 있음) 실패하든, 성공한 경우에만 항목이 남는다.
    const sw::ShaderCompileResult result = cache.getOrCompile( vsDesc );

    sw::vector<sw::ShaderCompileDesc> listAfter;
    cache.collectCompiledDescs( listAfter );

    if ( result._bSuccess )
    {
        SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listAfter.size() ) );
        // 리로드가 이 요청 그대로 다시 컴파일할 수 있어야 한다 — 경로만이 아니라 진입점·스테이지·포맷까지.
        SW_EXPECT_EQUAL( vsDesc._filePath, listAfter[0]._filePath );
        SW_EXPECT_EQUAL( vsDesc._entryPoint, listAfter[0]._entryPoint );
        SW_EXPECT_TRUE( vsDesc._stage == listAfter[0]._stage );
        SW_EXPECT_TRUE( vsDesc._targetFormat == listAfter[0]._targetFormat );
    }

    cache.shutdown();
}

/**
 * @brief [LiveShaderTest] 대상이 없을 때 수동 리로드가 조용히 끝나는지 검증.
 */
SW_TEST_CASE( LiveShaderTest, ManualReloadWithNoTargetsIsNoop )
{
    sw::LiveShaderManager manager;
    SW_ASSERT_TRUE( manager.initialize() );

    manager.triggerReloadAll();
    manager.update();

    manager.shutdown();
}

/**
 * @brief [LiveShaderTest] `.hlsli` 만 고쳐도 수동 리로드가 새 바이트코드를 만드는지 검증.
 * @details 이게 이 기능의 전부다. 함정은 `ShaderCompiler` 의 디스크 캐시인데, 키가
 *          `max(.hlsl mtime, 공유 헤더 타임스탬프)` 이고 뒤쪽은
 *          `ShaderBaker::getSharedHeaderTimestamp` 의 함수 지역 static 이라 **프로세스당 한 번만**
 *          계산된다. `.hlsli` 만 고치면 두 값이 다 그대로라 옛 바이트코드가 그대로 돌아온다 —
 *          로그는 "Succeeded" 를 찍는데 화면은 안 바뀌는, 가장 조용한 종류의 어긋남이다.
 *          그래서 리로드는 컴파일하는 동안 디스크 캐시를 우회한다.
 */
SW_TEST_CASE( LiveShaderTest, EditedIncludeChangesRecompiledBytecode )
{
    const sw::string includeRel = "engine/shaders/livereloadprobe.hlsli";
    const sw::string shaderRel  = "engine/shaders/livereloadprobe.hlsl";

    const sw::string engineFolder = sw::ResourceUtil::getDomainFolderPath( "engine" );
    SW_ASSERT_TRUE( engineFolder.empty() == false );
    const sw::string includeAbs = sw::FileUtil::joinPath( engineFolder, "shaders/livereloadprobe.hlsli" );
    const sw::string shaderAbs  = sw::FileUtil::joinPath( engineFolder, "shaders/livereloadprobe.hlsl" );

    sw::FileUtil::createParentDirectory( shaderAbs );
    sw::FileUtil::writeTextFile( includeAbs, "#define SW_PROBE_SCALE 1.0\n" );
    sw::FileUtil::writeTextFile( shaderAbs,
                                 "#include \"livereloadprobe.hlsli\"\n"
                                 "float4 VSMain( float3 pos : POSITION ) : SV_POSITION\n"
                                 "{\n"
                                 "    return float4( pos * SW_PROBE_SCALE, 1.0 );\n"
                                 "}\n" );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [includeAbs, shaderAbs]()
    {
        sw::FileUtil::removeFile( includeAbs );
        sw::FileUtil::removeFile( shaderAbs );
    } ) );

    sw::ShaderCompileDesc desc{};
    desc._filePath     = shaderRel;
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;

    const sw::ShaderCompileResult first = sw::ShaderCompiler::compileHLSL( desc );
    SW_ASSERT_TRUE( first._bSuccess );
    SW_ASSERT_TRUE( first._bytecode.empty() == false );

    // `.hlsli` 만 고친다 — `.hlsl` 의 mtime 은 그대로다.
    sw::FileUtil::writeTextFile( includeAbs, "#define SW_PROBE_SCALE 7.0\n" );

    // 디스크 캐시를 켜 둔 채로는 **옛 바이트코드가 그대로 돌아온다** (위 @details 참고).
    const sw::ShaderCompileResult cached = sw::ShaderCompiler::compileHLSL( desc );
    SW_ASSERT_TRUE( cached._bSuccess );
    SW_EXPECT_TRUE( cached._bytecode == first._bytecode );

    // 리로드가 하는 것과 같은 우회 — 이때는 새 바이트코드가 나와야 한다.
    const bool bPrevEnabled = sw::ShaderCompiler::isDiskCacheEnabled();
    sw::ShaderCompiler::enableDiskCache( false );
    const sw::ShaderCompileResult reloaded = sw::ShaderCompiler::compileHLSL( desc );
    sw::ShaderCompiler::enableDiskCache( bPrevEnabled );

    SW_ASSERT_TRUE( reloaded._bSuccess );
    SW_EXPECT_TRUE( reloaded._bytecode != first._bytecode );
}

#include "pch.h"

#include "Engine/Graphics/Shader/Compile/LiveShaderManager.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

#include "TestFramework/TestFramework.h"

#include <chrono>
#include <filesystem>

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
 *          그래서 리로드는 그 타임스탬프를 한 번 버린다 — 캐시를 통째로 우회하지는 않는다.
 *          우회하면 이번 편집과 무관한 셰이더까지 전부 다시 컴파일된다.
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

    // `FileUtil::getFileTimestamp` 는 **초 단위**라, 같은 초에 두 번 쓰면 값이 같다. 테스트가
    // 실행 속도에 따라 흔들리지 않도록 수정 시각을 명시적으로 밀어 둔다.
    // (실사용에서는 저장하고 단축키를 누르기까지 1초 이상 걸리므로 문제가 되지 않는다.)
    {
        const std::filesystem::file_time_type written = std::filesystem::last_write_time( includeAbs.c_str() );
        std::filesystem::last_write_time( includeAbs.c_str(), written + std::chrono::seconds( 5 ) );
    }

    // 공유 헤더 타임스탬프가 얼어붙어 있으면 캐시 키가 그대로라 **옛 바이트코드가 돌아온다**.
    const sw::ShaderCompileResult stale = sw::ShaderCompiler::compileHLSL( desc );
    SW_ASSERT_TRUE( stale._bSuccess );
    SW_EXPECT_TRUE( stale._bytecode == first._bytecode );

    // 수동 리로드가 하는 것과 같다 — 타임스탬프를 한 번 버리면 키가 달라져 실제로 다시 컴파일된다.
    // **캐시를 우회하지 않는다**는 점이 중요하다. 우회하면 이번 편집과 무관한 셰이더까지 전부
    // 다시 컴파일된다.
    sw::ShaderBaker::invalidateSharedHeaderTimestamp();
    const sw::ShaderCompileResult reloaded = sw::ShaderCompiler::compileHLSL( desc );

    SW_ASSERT_TRUE( reloaded._bSuccess );
    SW_EXPECT_TRUE( reloaded._bytecode != first._bytecode );
}

#include "pch.h"

// 셰이더 라이브 리로드는 개발 도구라 Shipping 빌드에는 **코드 자체가 없다**(Engine CMake 제외 목록).
// 없는 기능의 테스트도 그 빌드에는 없다 — 여기서 끊지 않으면 Shipping 링크가 심볼을 못 찾는다.

#if !defined( SW_SHIPPING )

    #include "Core/File/FileUtil.h"

    #include "Engine/Common/EngineServices.h"
    #include "Engine/Graphics/Shader/Compile/ShaderRecompiler.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCache.h"
    #include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

    #include "TestFramework/TestFramework.h"

// 셰이더 핫 리로드 — 디스크의 .hlsli 를 고치고 바이트코드가 실제로 다시 쿠킹되는지 본다.
//
// 재컴파일에 DXC 가 필요하고, 공유 헤더 캐시를 더럽혔다 되돌리므로 같은 프로세스의 다른 셰이더 테스트와 순서를 탄다.
SW_TEST_REQUIRES_HOST( LiveShaderTest, "recompiles shaders with DXC and edits the shared include cache" );

// ------------------------------------------------------------------------------
// 1) LiveShaderTest — 수동 리로드 대상 수집
// ------------------------------------------------------------------------------
/**
 * @brief [LiveShaderTest] 리로드 대상이 `ShaderCache` 에서 나오는지 검증.
 * @details 이 테스트의 요점은 **대상이 비어 있지 않다**는 것이다. `ShaderRecompiler` 가 따로 채우는 자기 등록표를 보면,
 *          그 표를 채우는 호출부가 없을 때 `ReloadShaders`(Ctrl+F8) 단축키가 빈 표를 돌고 아무 일도 하지 않는다.
 *          `update()` 만 불러 빈 큐를 도는 시험은 그 상태에서도 초록이다.
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
    sw::ShaderRecompiler manager;
    SW_ASSERT_TRUE( manager.initialize() );

    manager.triggerReloadAll();
    manager.update();

    manager.shutdown();
}

/**
 * @brief [LiveShaderTest] `.hlsli` 만 고쳐도 수동 리로드가 새 바이트코드를 만드는지 검증.
 * @details 이게 이 기능의 전부다. 함정은 `ShaderCompiler` 의 디스크 캐시인데, 키에 드는 공유 헤더 해시
 *          (`ShaderCooker::getSharedHeaderContentHash`)는 `.hlsli` 집합을 한 번 훑고 **캐시된다**.
 *          `.hlsli` 만 고치면 키가 그대로라 옛 바이트코드가 그대로 돌아온다 —
 *          로그는 "Succeeded" 를 찍는데 화면은 안 바뀌는, 가장 조용한 종류의 어긋남이다.
 *          그래서 리로드는 그 캐시를 한 번 버린다(`invalidateSharedHeaderCache`) — 컴파일 캐시를 통째로 우회하지는 않는다.
 *          우회하면 이번 편집과 무관한 셰이더까지 전부 다시 컴파일된다.
 */
SW_TEST_CASE( LiveShaderTest, EditedIncludeChangesRecompiledBytecode )
{
    const sw::string includeRel = "engine/shaders/livereloadprobe.hlsli";
    const sw::string shaderRel  = "engine/shaders/livereloadprobe.hlsl";

    const sw::string engineFolder = sw::ResourceUtil::getDomainFolderPath( "engine" );
    SW_ASSERT_FALSE( engineFolder.empty() );
    const sw::string includeAbs = sw::FileUtil::joinPath( engineFolder, "shaders/livereloadprobe.hlsli" );
    const sw::string shaderAbs  = sw::FileUtil::joinPath( engineFolder, "shaders/livereloadprobe.hlsl" );

    sw::FileUtil::ensureParentDirectoryExists( shaderAbs );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( includeAbs, "#define SW_PROBE_SCALE 1.0\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( shaderAbs,
                                                 "#include \"livereloadprobe.hlsli\"\n"
                                                 "float4 VSMain( float3 pos : POSITION ) : SV_POSITION\n"
                                                 "{\n"
                                                 "    return float4( pos * SW_PROBE_SCALE, 1.0 );\n"
                                                 "}\n" ) );

    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [includeAbs, shaderAbs]()
    {
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( includeAbs ) );
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( shaderAbs ) );
        // **파일만 지우면 부족하다.** 공유 헤더 해시는 `.hlsli` 집합을 한 번 훑고 캐시하므로, 프로브가 있던
        // 동안의 값이 다음 테스트로 샌다 — `ShaderCookStampTest` 가 그 값을 기준으로 잡고 스스로 무효화한 뒤
        // 비교하면 진다. `.hlsli` 를 건드린 쪽이 자기가 더럽힌 캐시를 비운다.
        sw::ShaderCooker::invalidateSharedHeaderCache();
    } ) );

    sw::ShaderCompileDesc desc{};
    desc._filePath   = shaderRel;
    desc._entryPoint = "VSMain";
    desc._stage      = sw::ShaderStage::Vertex;

    // **이 케이스가 보는 것은 포맷이 아니라 "`.hlsli` 를 고치면 다시 쿠킹된 바이트코드가 달라지는가"** 다.
    // DXBC 를 낼 수 있는 것은 윈도우의 FXC 뿐이라 타깃을 `DXBC_D3D11` 로 고정하면 **리눅스에서는 늘 진다**.
    // 포맷을 플랫폼이 낼 수 있는 것으로 고르면 같은 계약을 양쪽에서 실제로 검사한다.
    #if defined( SW_PLATFORM_WINDOWS )
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;
    #else
    desc._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;
    #endif

    const sw::ShaderCompileResult first = sw::ShaderCompiler::compileHlsl( desc );

    // 그래도 컴파일러가 아예 없는 기계는 있다(DXC 미설치 등). 그때는 **결함이 아니라 환경**이므로
    // 지지 말고 건너뛴다 — 늘 빨간 케이스는 새 회귀를 가린다. 다른 실패는 그대로 진다.
    if ( first._bSuccess == false && first._errorMessage.find( "unavailable" ) != sw::string::npos )
        SW_TEST_SKIP( first._errorMessage.c_str() );

    SW_ASSERT_TRUE( first._bSuccess );
    SW_ASSERT_FALSE( first._bytecode.empty() );

    // `.hlsli` 만 고친다 — `.hlsl` 의 mtime 은 그대로다.
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( includeAbs, "#define SW_PROBE_SCALE 7.0\n" ) );

    // `FileUtil::getFileTimestamp` 는 **초 단위**라, 같은 초에 두 번 쓰면 값이 같다. 테스트가
    // 실행 속도에 따라 흔들리지 않도록 수정 시각을 명시적으로 밀어 둔다.
    // (실사용에서는 저장하고 단축키를 누르기까지 1초 이상 걸리므로 문제가 되지 않는다.)
    {
        int64 written{ 0 };
        SW_ASSERT_TRUE( sw::FileUtil::getFileWriteTime( includeAbs, written ) );
        SW_ASSERT_TRUE( sw::FileUtil::setFileWriteTime( includeAbs, written + 5 * sw::FileUtil::kFileTimeTicksPerSecond ) );
    }

    // 공유 헤더 해시가 캐시돼 있으면 캐시 키가 그대로라 **옛 바이트코드가 돌아온다**.
    const sw::ShaderCompileResult stale = sw::ShaderCompiler::compileHlsl( desc );
    SW_ASSERT_TRUE( stale._bSuccess );
    SW_EXPECT_TRUE( stale._bytecode == first._bytecode );

    // 수동 리로드가 하는 것과 같다 — 공유 헤더 해시를 한 번 버리면 키가 달라져 실제로 다시 컴파일된다.
    // **캐시를 우회하지 않는다**는 점이 중요하다. 우회하면 이번 편집과 무관한 셰이더까지 전부
    // 다시 컴파일된다.
    sw::ShaderCooker::invalidateSharedHeaderCache();
    const sw::ShaderCompileResult reloaded = sw::ShaderCompiler::compileHlsl( desc );

    SW_ASSERT_TRUE( reloaded._bSuccess );
    SW_EXPECT_TRUE( reloaded._bytecode != first._bytecode );
}

/**
 * @brief [LiveShaderTest] 캐시의 실시간 컴파일과 핫 리로드가 같은 코드젠으로, 그 코드젠의 자리에 쓴다
 * @details Debug 의 실시간 컴파일은 디버그 코드젠(RenderDoc 에서 한 줄씩)이다. 캐시가 그 바이트코드를 **요청**의 자리(`-opt`)에 쓰거나 핫 리로드가
 *          요청 그대로(최적화) 다시 컴파일하면 `-opt` 폴더에 두 코드젠이 섞이고 리로드한 셰이더만 디버그 정보를 잃는다. 둘 다
 *          `ShaderCache::makeLiveCompileDesc` 하나로 정한다. Debug 가 아니면 두 요청이 같아 이 케이스가 볼 차이가 없다.
 */
SW_TEST_CASE( LiveShaderTest, LiveCompileWritesUnderItsOwnCodegen )
{
    // 쿠킹된 바이너리가 없는 셰이더라야 실시간 컴파일을 탄다.
    const sw::string engineFolder = sw::ResourceUtil::getDomainFolderPath( "engine" );
    SW_ASSERT_FALSE( engineFolder.empty() );
    const sw::string shaderAbs = sw::FileUtil::joinPath( engineFolder, "shaders/livecodegenprobe.hlsl" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( shaderAbs, "float4 VSMain( float3 pos : POSITION ) : SV_POSITION { return float4( pos * 3.0, 1.0 ); }\n" ) );

    sw::ShaderCompileDesc desc{};
    desc._filePath   = "engine/shaders/livecodegenprobe.hlsl";
    desc._entryPoint = "VSMain";
    desc._stage      = sw::ShaderStage::Vertex;
    #if defined( SW_PLATFORM_WINDOWS )
    desc._targetFormat = sw::ShaderTargetFormat::DXBC_D3D11;
    #else
    desc._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;
    #endif

    const sw::ShaderCompileDesc liveDesc    = sw::ShaderCache::makeLiveCompileDesc( desc );
    const sw::string            livePath    = sw::ShaderCache::makeLocalCachePath( liveDesc ); // 작업 폴더 기준(캐시가 쓰는 그대로)
    const sw::string            requestPath = sw::ShaderCache::makeLocalCachePath( desc );
    SW_TEST_DEFER_CLEANUP( SW_DELEGATE_LAMBDA( sw::Delegate<void()>, [shaderAbs, livePath, requestPath]()
    {
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( shaderAbs ) );
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( livePath ) );
        SW_EXPECT_TRUE( sw::FileUtil::removeFile( requestPath ) );
    } ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( livePath ) );
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( requestPath ) );
    #if defined( SW_DEBUG )
    SW_EXPECT_TRUE( liveDesc._bDebugCodegen != SW_FALSE );
    SW_EXPECT_TRUE( livePath != requestPath );
    #endif

    sw::ShaderCache               cache;
    const sw::ShaderCompileResult result = cache.getOrCompile( desc );
    if ( result._bSuccess == false && result._errorMessage.find( "unavailable" ) != sw::string::npos )
        SW_TEST_SKIP( result._errorMessage.c_str() );
    SW_ASSERT_TRUE( result._bSuccess );
    SW_EXPECT_TRUE_MSG( sw::FileUtil::fileExists( livePath ), "실시간 컴파일이 자기 코드젠의 자리에 쓰지 않았습니다" );
    if ( livePath != requestPath )
        SW_EXPECT_FALSE_MSG( sw::FileUtil::fileExists( requestPath ), "디버그 코드젠 바이트코드를 최적화 자리(-opt)에 썼습니다" );

    // 핫 리로드도 같은 요청으로 다시 컴파일한다 — 소스가 그대로이니 같은 자리에 같은 바이트다.
    if ( sw::engine::areEngineServicesBound() == false )
        return;
    sw::engine::getShaderCache().clearCache();
    SW_ASSERT_TRUE( sw::engine::getShaderCache().getOrCompile( desc )._bSuccess );
    sw::ShaderRecompiler manager;
    SW_ASSERT_TRUE( manager.initialize() );
    manager.triggerReloadAll();
    manager.update();
    manager.shutdown();
    sw::vector<uint8> listReloaded;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( livePath, listReloaded ) );
    SW_EXPECT_TRUE_MSG( listReloaded == result._bytecode, "핫 리로드가 캐시와 다른 코드젠으로 다시 컴파일했습니다" );
    if ( livePath != requestPath )
        SW_EXPECT_FALSE_MSG( sw::FileUtil::fileExists( requestPath ), "핫 리로드가 최적화 자리(-opt)에 썼습니다" );
}

#endif // !SW_SHIPPING

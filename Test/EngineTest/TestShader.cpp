#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipelineResource.h"
#include "Engine/Graphics/Shader/Compile/ShaderBaker.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#include <thread>

// 셰이더 주변 — 스테이지 비트 연산 · 굽기(퍼뮤테이션 해시·파일 이름) · 캐시 동시성 · 굽기 도장.
// 전부 디스크와 순수 계산만 본다. 실제 컴파일은 TestShaderCompiler.cpp (CI 가 못 돌린다).
/**
 * @brief [ShaderStageTest] 8대 스테이지 플래그 변환 및 경계값/오버플로 엣지 케이스 검증
 */

SW_TEST_CASE( ShaderStageTest, StageFlagConversionAndBoundary )
{
    // 1) 8대 정상 스테이지가 각각 올바른 1 << N 비트플래그로 1:1 변환되는지 검증
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Vertex ) == sw::ShaderStageFlag::Vertex );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Pixel ) == sw::ShaderStageFlag::Pixel );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Compute ) == sw::ShaderStageFlag::Compute );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Geometry ) == sw::ShaderStageFlag::Geometry );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Hull ) == sw::ShaderStageFlag::Hull );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Domain ) == sw::ShaderStageFlag::Domain );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Mesh ) == sw::ShaderStageFlag::Mesh );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Amplification ) == sw::ShaderStageFlag::Amplification );

    // 2) 경계값 및 범위 초과(오버플로) 엣지 케이스 검증
    SW_EXPECT_TRUE( sw::toShaderStageFlag( sw::ShaderStage::Count ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 8 ) ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 99 ) ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( sw::toShaderStageFlag( static_cast<sw::ShaderStage>( 255 ) ) == sw::ShaderStageFlag::None );
}

/**
 * @brief [ShaderStageTest] hasShaderStage 비트 포함 여부 및 AllGraphics/All 마스킹 검증
 */
SW_TEST_CASE( ShaderStageTest, HasShaderStageAndMasking )
{
    const sw::ShaderStageFlag flags = sw::ShaderStageFlag::Vertex | sw::ShaderStageFlag::Pixel | sw::ShaderStageFlag::Compute;

    // 포함된 스테이지 검사
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Pixel ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Compute ) );

    // 미포함 스테이지 검사
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Geometry ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Hull ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( flags, sw::ShaderStageFlag::Mesh ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::None, sw::ShaderStageFlag::Vertex ) );

    // AllGraphics (7대 그래픽스 스테이지 포함, Compute 미포함)
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Pixel ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Geometry ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Hull ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Domain ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Mesh ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Amplification ) );
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::AllGraphics, sw::ShaderStageFlag::Compute ) );

    // All (Compute 포함 8대 전 스테이지 포함)
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Compute ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Vertex ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( sw::ShaderStageFlag::All, sw::ShaderStageFlag::Amplification ) );
}

/**
 * @brief [ShaderStageTest] 비트 연산자 대수적 공리(항등원, 영원, 멱등성, 여원, XOR) 검증
 */
SW_TEST_CASE( ShaderStageTest, BitwiseOperatorAlgebraicLaws )
{
    const sw::ShaderStageFlag flagVs = sw::ShaderStageFlag::Vertex;
    const sw::ShaderStageFlag flagPs = sw::ShaderStageFlag::Pixel;
    const sw::ShaderStageFlag flagCs = sw::ShaderStageFlag::Compute;

    // 항등원 (Identity)
    SW_EXPECT_TRUE( ( flagVs | sw::ShaderStageFlag::None ) == flagVs );
    SW_EXPECT_TRUE( ( flagVs & sw::ShaderStageFlag::All ) == flagVs );

    // 영원 (Annihilation)
    SW_EXPECT_TRUE( ( flagVs & sw::ShaderStageFlag::None ) == sw::ShaderStageFlag::None );

    // 멱등성 (Idempotence)
    SW_EXPECT_TRUE( ( flagCs | flagCs ) == flagCs );
    SW_EXPECT_TRUE( ( flagCs & flagCs ) == flagCs );

    // 여원 (Complement)
    SW_EXPECT_TRUE( ( flagVs & ~flagVs ) == sw::ShaderStageFlag::None );

    // XOR
    SW_EXPECT_TRUE( ( flagVs ^ flagVs ) == sw::ShaderStageFlag::None );
    SW_EXPECT_TRUE( ( flagVs ^ sw::ShaderStageFlag::None ) == flagVs );

    // 복합 대입 (Compound assignment)
    sw::ShaderStageFlag testFlag = flagVs;
    testFlag |= flagPs;
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( testFlag, flagVs ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( testFlag, flagPs ) );

    testFlag &= ~flagVs;
    SW_EXPECT_FALSE( sw::EnumUtil::hasFlag( testFlag, flagVs ) );
    SW_EXPECT_TRUE( sw::EnumUtil::hasFlag( testFlag, flagPs ) );

    testFlag ^= flagPs;
    SW_EXPECT_TRUE( testFlag == sw::ShaderStageFlag::None );
}

/**
 * @brief [ShaderStageTest] 10만 회 비트 연산 반복 평가 무결성 스트레스 테스트
 */
SW_TEST_CASE( ShaderStageTest, BitwiseStressEvaluation )
{
    sw::ShaderStageFlag flags           = sw::ShaderStageFlag::None;
    constexpr uint32    kIterationCount = 100000;
    for ( uint32 index = 0; index < kIterationCount; ++index )
    {
        const uint8               stageIndex = static_cast<uint8>( index % static_cast<uint8>( sw::ShaderStage::Count ) );
        const sw::ShaderStage     stage      = static_cast<sw::ShaderStage>( stageIndex );
        const sw::ShaderStageFlag stageFlag  = sw::toShaderStageFlag( stage );

        flags ^= stageFlag;
        const bool bHas = sw::EnumUtil::hasFlag( flags, stageFlag );
        SW_EXPECT_TRUE( bHas == ( ( flags & stageFlag ) != sw::ShaderStageFlag::None ) );
    }
}

/**
 * @brief [ShaderBakerTest] 순열 해시 순서 불변성, 빈 원소 무시, 구조체 오버로드 일관성 검증
 */
SW_TEST_CASE( ShaderBakerTest, PermutationHashOrderInvarianceAndEdgeCases )
{
    // 1) 빈 순열은 0 반환
    const sw::vector<sw::string> listEmpty;
    SW_EXPECT_EQUAL( 0ull, sw::ShaderBaker::computePermutationHash( listEmpty ) );

    // 2) 순서 불변성 (Order Invariance): 정의 순서가 달라도 동일 해시 산출
    const sw::vector<sw::string> listPerm1 = { "FEATURE_A=1", "FEATURE_B=2", "USE_HDR=1" };
    const sw::vector<sw::string> listPerm2 = { "USE_HDR=1", "FEATURE_A=1", "FEATURE_B=2" };
    const sw::vector<sw::string> listPerm3 = { "FEATURE_B=2", "USE_HDR=1", "FEATURE_A=1" };

    const uint64 hash1 = sw::ShaderBaker::computePermutationHash( listPerm1 );
    const uint64 hash2 = sw::ShaderBaker::computePermutationHash( listPerm2 );
    const uint64 hash3 = sw::ShaderBaker::computePermutationHash( listPerm3 );

    SW_EXPECT_TRUE( hash1 != 0ull );
    SW_EXPECT_EQUAL( hash1, hash2 );
    SW_EXPECT_EQUAL( hash2, hash3 );

    // 3) 빈 문자열 원소 무시 안전성
    const sw::vector<sw::string> listWithEmpty = { "", "FEATURE_A=1", "", "FEATURE_B=2", "USE_HDR=1", "" };
    SW_EXPECT_EQUAL( hash1, sw::ShaderBaker::computePermutationHash( listWithEmpty ) );

    // 4) vector<ShaderMacroDefine> 오버로드와 vector<string> 간의 해시 일치
    const sw::vector<sw::ShaderMacroDefine> listDefine = {
        {"FEATURE_A", "1"},
        {"FEATURE_B", "2"},
        {  "USE_HDR", "1"}
    };
    SW_EXPECT_EQUAL( hash1, sw::ShaderBaker::computePermutationHash( listDefine ) );

    // 5) 값이 빈 매크로 ("DEBUG_MODE")의 단일 문자열 vs 구조체 일치
    const sw::vector<sw::string>            listValueless    = { "DEBUG_MODE" };
    const sw::vector<sw::ShaderMacroDefine> listDefValueless = {
        { "DEBUG_MODE", "" }
    };
    SW_EXPECT_EQUAL( sw::ShaderBaker::computePermutationHash( listValueless ),
                     sw::ShaderBaker::computePermutationHash( listDefValueless ) );
}

/**
 * @brief [ShaderBakerTest] 8대 스테이지 표준 태그, 커스텀 진입점, 해시 접미사 및 대소문자 정규화 검증
 */
SW_TEST_CASE( ShaderBakerTest, BinaryFileNameGenerationAndStageTags )
{
    // 1) 8대 스테이지 기본 진입점 축약 태그 검증
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "deferredlighting_ps.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "deferredlighting", sw::ShaderStage::Pixel, "PSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "gpucull_cs.dxbc" ),
                     sw::ShaderBaker::computeBinaryFileName( "gpucull", sw::ShaderStage::Compute, "CSMain", 0ull, ".dxbc" ) );
    SW_EXPECT_EQUAL( sw::string( "terrain_gs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "terrain", sw::ShaderStage::Geometry, "GSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_hs.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "tess", sw::ShaderStage::Hull, "HSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_ds.spv" ),
                     sw::ShaderBaker::computeBinaryFileName( "tess", sw::ShaderStage::Domain, "DSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_ms.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "cluster", sw::ShaderStage::Mesh, "MSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_as.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "cluster", sw::ShaderStage::Amplification, "ASMain", 0ull, ".dxil" ) );

    // 2) 커스텀 진입점 전달 시 소문자 결합
    SW_EXPECT_EQUAL( sw::string( "forwardlit_customentry.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "CustomEntry", 0ull, ".dxil" ) );

    // 3) 기본 진입점 대소문자 무시 (vsmain -> vs)
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "vsmain", 0ull, ".dxil" ) );

    // 4) 순열 해시 접미사 (_%08x)
    const sw::string fileNameWithHash = sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0x1234ABCDull, ".dxil" );
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs_1234abcd.dxil" ), fileNameWithHash );
}

/**
 * @brief [ShaderBakerTest] 4대 RHI 서브폴더, 확장자 매핑 및 별칭 역산출 검증
 */
SW_TEST_CASE( ShaderBakerTest, SubfolderAndFormatMappingAliases )
{
    // 서브폴더 및 확장자
    SW_EXPECT_EQUAL( string_view( "dx11" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( string_view( "dx12" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( string_view( "vulkan" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( string_view( "opengl" ), sw::ShaderBaker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    SW_EXPECT_EQUAL( string_view( ".dxbc" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( string_view( ".dxil" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( string_view( ".spv" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( string_view( ".spv" ), sw::ShaderBaker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    // 별칭 역산출
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "dx11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "d3d11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "directx11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "dx12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "d3d12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "directx12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "vulkan" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "vk" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "spirv" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "opengl" ) == sw::ShaderTargetFormat::SPIRV_OpenGL );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "gl" ) == sw::ShaderTargetFormat::SPIRV_OpenGL );

    // 미지원/미지의 서브폴더 -> Count
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "unknown" ) == sw::ShaderTargetFormat::Count );
    SW_EXPECT_TRUE( sw::ShaderBaker::getFormatForSubfolder( "" ) == sw::ShaderTargetFormat::Count );
}

/**
 * @brief [ShaderBakerTest] 1만 건 순열 해시 무결성 및 충돌 스트레스 테스트
 */
SW_TEST_CASE( ShaderBakerTest, PermutationHashCollisionStressTest )
{
    constexpr uint32                  kPermutationCount = 10000;
    sw::unordered_map<uint64, uint32> mapHashToId;
    mapHashToId.reserve( kPermutationCount );

    sw::fixed_string<sw::constant::kMaxBuffer64> defBuf;
    for ( uint32 index = 0; index < kPermutationCount; ++index )
    {
        sw::formatstring( defBuf.data(), defBuf.capacity(), "PERM_MACRO_%u=%u", index, ( index * 31u + 7u ) );
        const sw::vector<sw::string> listPerm = { "COMMON_DEFINE=1", defBuf.c_str() };
        const uint64                 hash     = sw::ShaderBaker::computePermutationHash( listPerm );

        SW_EXPECT_TRUE( hash != 0ull );
        auto iter = mapHashToId.find( hash );
        SW_EXPECT_TRUE( iter == mapHashToId.end() );
        mapHashToId.insert_or_assign( hash, index );
    }
    SW_EXPECT_EQUAL( kPermutationCount, static_cast<uint32>( mapHashToId.size() ) );
}

/**
 * @brief [ShaderBakerTest] 잘못된 파일 경로 및 디렉터리에 대한 방어적 실패 처리 검증
 */
SW_TEST_CASE( ShaderBakerTest, DefensiveFileOperations )
{
    test::ScopedLogSuppressor suppressor;

    sw::ShaderBakeResult result{};
    const bool           bBakeInvalid = sw::ShaderBaker::bakeShader(
        "non_existent_shader_file_12345.hlsl",
        "output_dummy.bin",
        "VSMain",
        sw::ShaderStage::Vertex,
        sw::ShaderTargetFormat::DXIL_D3D12,
        nullptr,
        &result );

    SW_EXPECT_FALSE( bBakeInvalid );
    SW_EXPECT_FALSE( result._bSuccess == SW_TRUE );

    const uint32 bakedCount = sw::ShaderBaker::bakeAllShaders(
        "invalid_resource_root_path_99999",
        sw::ShaderTargetFormat::DXIL_D3D12,
        false );
    SW_EXPECT_EQUAL( 0u, bakedCount );
}

/**
 * @brief [ShaderCacheStressTest] 8스레드 동시 캐시 쿼리 동기화 무결성 스트레스
 */
SW_TEST_CASE( ShaderCacheStressTest, MultiThreadedCacheAccessStress )
{
    sw::ShaderCache     cache;
    constexpr uint32    kThreadCount    = 8;
    constexpr uint32    kCallsPerThread = 100;
    std::atomic<uint32> totalQueries{ 0 };

    sw::vector<std::thread> listThread;
    listThread.reserve( kThreadCount );

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&cache, threadIndex, &totalQueries]()
        {
            for ( uint32 callIndex = 0; callIndex < kCallsPerThread; ++callIndex )
            {
                sw::ShaderCompileDesc desc{};
                const uint32          pathId = ( threadIndex + callIndex ) % 4;
                if ( pathId == 0 )
                    desc._filePath = "engine/shaders/fullscreentriangle.hlsl";
                else if ( pathId == 1 )
                    desc._filePath = "engine/shaders/forwardlit.hlsl";
                else if ( pathId == 2 )
                    desc._filePath = "engine/shaders/shadowdepth.hlsl";
                else
                    desc._filePath = "engine/shaders/tonemap.hlsl";

                desc._entryPoint   = ( ( callIndex % 2 ) == 0 ) ? "VSMain" : "PSMain";
                desc._stage        = ( ( callIndex % 2 ) == 0 ) ? sw::ShaderStage::Vertex : sw::ShaderStage::Pixel;
                desc._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;

                sw::ShaderCompileResult res = cache.getOrCompile( desc );
                (void)res;
                totalQueries.fetch_add( 1, std::memory_order_relaxed );
            }
        } );
    }

    for ( std::thread& thread : listThread )
    {
        if ( thread.joinable() )
            thread.join();
    }

    SW_EXPECT_EQUAL( kThreadCount * kCallsPerThread, totalQueries.load() );
}

/**
 * @brief [ShaderCacheStressTest] 동시 쿼리와 clearCache 간의 레이스 컨디션 스트레스
 */
SW_TEST_CASE( ShaderCacheStressTest, MultiThreadedClearAndQueryStress )
{
    sw::ShaderCache     cache;
    constexpr uint32    kWorkerCount = 6;
    constexpr uint32    kIterations  = 80;
    std::atomic<bool>   bRunning{ true };
    std::atomic<uint32> clearsDone{ 0 };

    std::thread clearerThread( [&cache, &bRunning, &clearsDone]()
    {
        while ( bRunning.load( std::memory_order_relaxed ) )
        {
            cache.clearCache();
            clearsDone.fetch_add( 1, std::memory_order_relaxed );
            std::this_thread::yield();
        }
    } );

    sw::vector<std::thread> listWorker;
    listWorker.reserve( kWorkerCount );
    for ( uint32 workerIndex = 0; workerIndex < kWorkerCount; ++workerIndex )
    {
        listWorker.emplace_back( [&cache]()
        {
            for ( uint32 index = 0; index < kIterations; ++index )
            {
                sw::ShaderCompileDesc desc{};
                desc._filePath              = "engine/shaders/fullscreentriangle.hlsl";
                desc._entryPoint            = "VSMain";
                desc._stage                 = sw::ShaderStage::Vertex;
                desc._targetFormat          = sw::ShaderTargetFormat::DXIL_D3D12;
                sw::ShaderCompileResult res = cache.getOrCompile( desc );
                (void)res;
            }
        } );
    }

    for ( std::thread& worker : listWorker )
    {
        if ( worker.joinable() )
            worker.join();
    }

    bRunning.store( false, std::memory_order_relaxed );
    if ( clearerThread.joinable() )
        clearerThread.join();

    SW_EXPECT_TRUE( clearsDone.load() > 0 );
}

/**
 * @brief [ShaderBakerTest] 컬러 출력이 없는 패스(그림자·뎁스 프리패스)에는 픽셀 스테이지가 없다.
 * @details Shipping 실기동의 `리플렉션 매니페스트에 'engine/shaders/shadowdepth.hlsl' 가 없습니다` 가 이 자리였다.
 *          베이커는 타입 **문자열**로 "그림자엔 PS 없음" 을 정하고, 런타임은 PS 경로를 늘 채워서 머티리얼 define 을
 *          얹은 그림자 변형이 DX12 에서 PS 리플렉션을 요구했다. 이제 둘 다 `FrameRendererUtil::hasPixelStage` 하나를
 *          본다 — 실제 파이프라인 XML 둘과 합성 선언으로 그 규칙을 고정한다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( ShaderBakerTest, DepthOnlyPassesHaveNoPixelStage )
{
    sw::ResourceUtil::initialize();

    // 1) 실제 파이프라인 — 그림자 패스는 PS 없음, 씬 메시를 그리는 나머지는 PS 있음.
    const utf8* arrPipeline[] = { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml" };
    for ( const utf8* pPipeline : arrPipeline )
    {
        const sw::string absPath = sw::ResourceUtil::getResourcePath( pPipeline );
        SW_EXPECT_TRUE_MSG( absPath.empty() == false, pPipeline );
        if ( absPath.empty() )
            continue;

        sw::RenderPipelineResource pipelineRes;
        SW_EXPECT_TRUE_MSG( pipelineRes.loadFromXmlFile( absPath ), pPipeline );

        uint32 depthOnlyCount{ 0 };
        uint32 colorPassCount{ 0 };
        for ( const sw::RenderGraphPassDesc& pass : pipelineRes.getGraphPass() )
        {
            const bool bHasPixelStage = sw::FrameRendererUtil::hasPixelStage( pass, pipelineRes.getDesc()._listAttachment );
            if ( sw::FrameRendererUtil::isDepthOnlyPassType( pass._resolvedType ) )
            {
                SW_EXPECT_TRUE_MSG( bHasPixelStage == false,
                                    ( sw::string( pPipeline ) + ": " + pass._name + " 에 픽셀 스테이지가 있다" ).c_str() );
                ++depthOnlyCount;
            }
            else if ( sw::FrameRendererUtil::drawsSceneMeshes( pass._resolvedType ) )
            {
                SW_EXPECT_TRUE_MSG( bHasPixelStage,
                                    ( sw::string( pPipeline ) + ": " + pass._name + " 에 픽셀 스테이지가 없다" ).c_str() );
                ++colorPassCount;
            }
        }
        SW_EXPECT_TRUE_MSG( depthOnlyCount >= 1,
                            ( sw::string( pPipeline ) + ": 그림자 패스가 없다 — 이 검증이 아무것도 보지 않았다" ).c_str() );
        SW_EXPECT_TRUE_MSG( colorPassCount >= 1, ( sw::string( pPipeline ) + ": 컬러 패스가 없다" ).c_str() );
    }

    // 2) 합성 선언 — 규칙 자체를 고정한다.
    sw::vector<sw::RenderPassAttachment> listAttachment( 3 );
    listAttachment[0]._name   = "Depth";
    listAttachment[0]._format = "D24_UNORM_S8_UINT";
    listAttachment[1]._name   = "ColorA";
    listAttachment[1]._format = "R16G16B16A16_FLOAT";
    listAttachment[2]._name   = "ColorB";
    listAttachment[2]._format = "R8G8B8A8_UNORM";

    // 뎁스만 출력 → 컬러 0, PS 없음.
    sw::RenderGraphPassDesc depthPass;
    depthPass._resolvedType = sw::RenderPassType::Shadow;
    depthPass._listOutput   = { "Depth" };
    SW_EXPECT_FALSE( sw::FrameRendererUtil::hasPixelStage( depthPass, listAttachment ) );

    // 컬러 둘 + 뎁스 → 2, 포맷은 선언 순서대로(뎁스는 건너뛴다).
    sw::RenderGraphPassDesc mrtPass;
    mrtPass._resolvedType = sw::RenderPassType::GBuffer;
    mrtPass._listOutput   = { "ColorA", "Depth", "ColorB" };
    sw::RHIFormat arrFormat[sw::kMaxColorAttachments]{};
    SW_EXPECT_EQUAL( 2u, sw::FrameRendererUtil::collectColorOutputFormats( mrtPass, listAttachment, arrFormat, sw::kMaxColorAttachments, 1 ) );
    SW_EXPECT_TRUE( arrFormat[0] == sw::RHIFormat::R16G16B16A16_FLOAT );
    SW_EXPECT_TRUE( arrFormat[1] == sw::RHIFormat::R8G8B8A8_UNORM );
    SW_EXPECT_TRUE( sw::FrameRendererUtil::hasPixelStage( mrtPass, listAttachment ) );

    // 출력 선언이 없으면 타입이 정한다 — 그림자·뎁스 프리패스는 0, 그 밖은 1 (registerPso 가 넘기는 기본값과 같다).
    sw::RenderGraphPassDesc barePass;
    barePass._resolvedType = sw::RenderPassType::DepthPrepass;
    SW_EXPECT_FALSE( sw::FrameRendererUtil::hasPixelStage( barePass, listAttachment ) );
    barePass._resolvedType = sw::RenderPassType::ForwardOpaque;
    SW_EXPECT_TRUE( sw::FrameRendererUtil::hasPixelStage( barePass, listAttachment ) );

    // 선언은 있는데 어태치먼트를 못 찾으면 알 수 없으므로 물러난 값이다.
    sw::RenderGraphPassDesc unknownPass;
    unknownPass._resolvedType = sw::RenderPassType::ForwardOpaque;
    unknownPass._listOutput   = { "NoSuchAttachment" };
    SW_EXPECT_EQUAL( 1u, sw::FrameRendererUtil::collectColorOutputFormats( unknownPass, listAttachment, nullptr, sw::kMaxColorAttachments, 1 ) );
}

/**
 * @brief [ShaderBakerTest] 셰이더 캐시가 읽는 파일 이름은 베이커가 쓰는 이름과 같고, 퍼뮤테이션마다 다르다.
 * @details 예전엔 캐시가 스템과 스테이지만으로 이름을 만들어 **모든 퍼뮤테이션이 해시 0 바이너리를 읽었다** —
 *          베이크 바이너리가 있는 한 SW_FORWARD·MATERIAL_BLEND_TRANSLUCENT·SW_VIEWMODE_UNLIT 이 GPU 에 닿지
 *          않았다. 리플렉션 매니페스트는 해시로 찾았으니 레이아웃만 맞고 바이트코드는 틀린 어긋남이었다.
 *          Vulkan 에서 Lit/Unlit 스크린샷이 잡음 바닥과 같게 나와 드러났다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( ShaderBakerTest, CachePathCarriesPermutationHash )
{
    sw::ShaderCompileDesc plain{};
    plain._filePath     = "engine/shaders/forwardlit.hlsl";
    plain._entryPoint   = "PSMain";
    plain._stage        = sw::ShaderStage::Pixel;
    plain._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;

    sw::ShaderCompileDesc unlit = plain;
    unlit._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    unlit._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_VIEWMODE_UNLIT=1" ) );

    const sw::string plainPath = sw::ShaderCache::makePrebakedRelativePath( plain );
    const sw::string unlitPath = sw::ShaderCache::makePrebakedRelativePath( unlit );
    SW_EXPECT_STREQ( "engine/shaders/bin/vulkan/forwardlit_ps.spv", plainPath.c_str() );
    SW_EXPECT_TRUE_MSG( unlitPath != plainPath, "퍼뮤테이션이 다른데 같은 바이너리를 읽는다 — define 이 GPU 에 닿지 않는다" );

    // 베이커가 굽는 이름과 글자 단위로 같아야 한다(다른 규칙이 하나라도 있으면 그 규칙이 정본을 이긴다).
    const sw::string bakedName = sw::ShaderBaker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Pixel, "PSMain",
                                                                         sw::ShaderBaker::computePermutationHash( unlit._listDefine ), ".spv" );
    SW_EXPECT_STREQ( ( sw::string( "engine/shaders/bin/vulkan/" ) + bakedName ).c_str(), unlitPath.c_str() );

    // 로컬 라이브 캐시도 같은 이름을 쓴다 — 처음 컴파일된 퍼뮤테이션이 나머지를 덮으면 안 된다.
    const sw::string plainLocal = sw::ShaderCache::makeLocalCachePath( plain );
    const sw::string unlitLocal = sw::ShaderCache::makeLocalCachePath( unlit );
    SW_EXPECT_TRUE( plainLocal != unlitLocal );
    SW_EXPECT_TRUE( unlitLocal.find( bakedName ) != sw::string::npos );

    // define 순서는 해시에 영향이 없다 — 런타임이 어떤 순서로 얹든 같은 파일이어야 한다.
    sw::ShaderCompileDesc reordered = plain;
    reordered._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_VIEWMODE_UNLIT=1" ) );
    reordered._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    SW_EXPECT_STREQ( unlitPath.c_str(), sw::ShaderCache::makePrebakedRelativePath( reordered ).c_str() );
}

/**
 * @brief 구운 산출물의 신선도를 **파일 시간이 아니라 내용 해시**로 판정하는지 봅니다.
 * @details 이 저장소는 구운 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의
 *          순서로 덮어쓴다. 예전 판정(`산출물 mtime >= 소스 mtime`)은 그때 "이미 최신" 이라 답했고,
 *          그래서 `forwardlit` 바이너리가 라이트 버퍼 이전 것으로 커밋된 채 돌았다 — Vulkan 만 다른
 *          그림을 내는 것을 백엔드 버그로 오인해 오래 쫓았다. 여기서 막는다.
 */
SW_TEST_CASE( ShaderBakeStampTest, FreshnessIsJudgedByContentNotFileTime )
{
    const sw::string shaderPath = sw::ResourceUtil::getResourcePath( "engine/shaders/forwardlit.hlsl" );
    const sw::string binDir     = sw::ResourceUtil::getResourcePath( "engine/shaders/bin/dx12" );
    // 구운 트리가 없는 환경(클린 체크아웃 직후)에서는 볼 것이 없다.
    if ( shaderPath.empty() || binDir.empty() )
        return;

    SW_EXPECT_TRUE( sw::ShaderBaker::isBakedOutputCurrent( binDir, shaderPath ) );

    // 스탬프에 없는 소스는 최신일 수 없다 — "파일이 있으니 최신" 으로 새지 않는지 본다.
    const sw::string shadersDir = sw::FileUtil::getDirectoryPart( sw::FileUtil::normalizeSeparators( shaderPath ) );
    SW_EXPECT_FALSE( sw::ShaderBaker::isBakedOutputCurrent( binDir, sw::FileUtil::joinPath( shadersDir, "nosuchshader.hlsl" ) ) );

    // 유효 소스 해시는 셰이더마다 달라야 한다(같으면 캐시 키가 셰이더를 못 가른다).
    const uint64 hashForward = sw::ShaderBaker::computeEffectiveSourceHash( shaderPath );
    const uint64 hashTonemap = sw::ShaderBaker::computeEffectiveSourceHash( sw::FileUtil::joinPath( shadersDir, "tonemap.hlsl" ) );
    SW_EXPECT_TRUE( hashForward != 0 );
    SW_EXPECT_TRUE( hashTonemap != 0 );
    SW_EXPECT_TRUE( hashForward != hashTonemap );

    // **공유 헤더가 바뀌면 전부 낡은 것이 되어야 한다.** `.hlsl` 은 그대로인데 include 한 `.hlsli` 만
    // 바뀌는 것이 실제로 일어난 경우다 — 파일 시간으로는 이 조합이 조용히 통과했다.
    const sw::string tempHeader = sw::FileUtil::joinPath( shadersDir, "baketemp.hlsli" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( tempHeader, "// bake staleness test\n" ) );
    sw::ShaderBaker::invalidateSharedHeaderCache();
    const bool   bCurrentAfterHeaderAdded = sw::ShaderBaker::isBakedOutputCurrent( binDir, shaderPath );
    const uint64 hashAfterHeaderAdded     = sw::ShaderBaker::computeEffectiveSourceHash( shaderPath );

    // 넣은 헤더는 반드시 되돌린다 — 실패해도 소스 트리를 더럽힌 채 끝나면 안 된다.
    sw::FileUtil::removeFile( tempHeader );
    sw::ShaderBaker::invalidateSharedHeaderCache();

    SW_EXPECT_FALSE( bCurrentAfterHeaderAdded );
    SW_EXPECT_TRUE( hashAfterHeaderAdded != hashForward );
    // 되돌리면 다시 최신이다.
    SW_EXPECT_TRUE( sw::ShaderBaker::isBakedOutputCurrent( binDir, shaderPath ) );
    SW_EXPECT_EQUAL( hashForward, sw::ShaderBaker::computeEffectiveSourceHash( shaderPath ) );
}

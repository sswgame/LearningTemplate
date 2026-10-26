#include "pch.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/RHI/RHIBackendInfo.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/Shader/Compile/ShaderCache.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Renderer/Cook/ShaderCookDriver.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/ShaderCookTestUtil.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestFramework.h"

#include <thread>

// 셰이더 주변 — 스테이지 비트 연산 · 쿠킹(퍼뮤테이션 해시·파일 이름) · 캐시 동시성 · 쿠킹 스탬프.
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
 * @brief [ShaderStageTest] 스테이지 비트 포함 여부(`EnumUtil::hasFlag`) 및 AllGraphics/All 마스킹 검증
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
 * @brief [ShaderCookerTest] 순열 해시 순서 불변성, 빈 원소 무시, 구조체 오버로드 일관성 검증
 */
SW_TEST_CASE( ShaderCookerTest, PermutationHashOrderInvarianceAndEdgeCases )
{
    // 1) 빈 순열은 0 반환
    const sw::vector<sw::string> listEmpty;
    SW_EXPECT_EQUAL( 0ull, sw::ShaderCooker::computePermutationHash( listEmpty ) );

    // 2) 순서 불변성 (Order Invariance): 정의 순서가 달라도 동일 해시 산출
    const sw::vector<sw::string> listPerm1 = { "FEATURE_A=1", "FEATURE_B=2", "USE_HDR=1" };
    const sw::vector<sw::string> listPerm2 = { "USE_HDR=1", "FEATURE_A=1", "FEATURE_B=2" };
    const sw::vector<sw::string> listPerm3 = { "FEATURE_B=2", "USE_HDR=1", "FEATURE_A=1" };

    const uint64 hash1 = sw::ShaderCooker::computePermutationHash( listPerm1 );
    const uint64 hash2 = sw::ShaderCooker::computePermutationHash( listPerm2 );
    const uint64 hash3 = sw::ShaderCooker::computePermutationHash( listPerm3 );

    SW_EXPECT_TRUE( hash1 != 0ull );
    SW_EXPECT_EQUAL( hash1, hash2 );
    SW_EXPECT_EQUAL( hash2, hash3 );

    // 3) 빈 문자열 원소 무시 안전성
    const sw::vector<sw::string> listWithEmpty = { "", "FEATURE_A=1", "", "FEATURE_B=2", "USE_HDR=1", "" };
    SW_EXPECT_EQUAL( hash1, sw::ShaderCooker::computePermutationHash( listWithEmpty ) );

    // 4) vector<ShaderMacroDefine> 오버로드와 vector<string> 간의 해시 일치
    const sw::vector<sw::ShaderMacroDefine> listDefine = {
        {"FEATURE_A", "1"},
        {"FEATURE_B", "2"},
        {  "USE_HDR", "1"}
    };
    SW_EXPECT_EQUAL( hash1, sw::ShaderCooker::computePermutationHash( listDefine ) );

    // 5) 값이 빈 매크로 ("DEBUG_MODE")의 단일 문자열 vs 구조체 일치
    const sw::vector<sw::string>            listValueless    = { "DEBUG_MODE" };
    const sw::vector<sw::ShaderMacroDefine> listDefValueless = {
        { "DEBUG_MODE", "" }
    };
    SW_EXPECT_EQUAL( sw::ShaderCooker::computePermutationHash( listValueless ),
                     sw::ShaderCooker::computePermutationHash( listDefValueless ) );
}

/**
 * @brief [ShaderCookerTest] 8대 스테이지 표준 태그, 커스텀 진입점, 해시 접미사 및 대소문자 정규화 검증
 */
SW_TEST_CASE( ShaderCookerTest, BinaryFileNameGenerationAndStageTags )
{
    // 1) 8대 스테이지 기본 진입점 축약 태그 검증
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "deferredlighting_ps.spv" ),
                     sw::ShaderCooker::computeBinaryFileName( "deferredlighting", sw::ShaderStage::Pixel, "PSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "gpucull_cs.dxbc" ),
                     sw::ShaderCooker::computeBinaryFileName( "gpucull", sw::ShaderStage::Compute, "CSMain", 0ull, ".dxbc" ) );
    SW_EXPECT_EQUAL( sw::string( "terrain_gs.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "terrain", sw::ShaderStage::Geometry, "GSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_hs.spv" ),
                     sw::ShaderCooker::computeBinaryFileName( "tess", sw::ShaderStage::Hull, "HSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "tess_ds.spv" ),
                     sw::ShaderCooker::computeBinaryFileName( "tess", sw::ShaderStage::Domain, "DSMain", 0ull, ".spv" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_ms.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "cluster", sw::ShaderStage::Mesh, "MSMain", 0ull, ".dxil" ) );
    SW_EXPECT_EQUAL( sw::string( "cluster_as.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "cluster", sw::ShaderStage::Amplification, "ASMain", 0ull, ".dxil" ) );

    // 2) 커스텀 진입점 전달 시 소문자 결합
    SW_EXPECT_EQUAL( sw::string( "forwardlit_customentry.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "CustomEntry", 0ull, ".dxil" ) );

    // 3) 기본 진입점 대소문자 무시 (vsmain -> vs)
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs.dxil" ),
                     sw::ShaderCooker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "vsmain", 0ull, ".dxil" ) );

    // 4) 순열 해시 접미사 (_%08x)
    const sw::string fileNameWithHash = sw::ShaderCooker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Vertex, "VSMain", 0x1234ABCDull, ".dxil" );
    SW_EXPECT_EQUAL( sw::string( "forwardlit_vs_1234abcd.dxil" ), fileNameWithHash );
}

/**
 * @brief [ShaderCookerTest] 4대 RHI 서브폴더, 확장자 매핑 및 별칭 역산출 검증
 */
SW_TEST_CASE( ShaderCookerTest, SubfolderAndFormatMappingAliases )
{
    // 서브폴더 및 확장자
    SW_EXPECT_EQUAL( sw::string_view( "dx11" ), sw::ShaderCooker::getSubfolderForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( sw::string_view( "dx12" ), sw::ShaderCooker::getSubfolderForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( sw::string_view( "vulkan" ), sw::ShaderCooker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( sw::string_view( "opengl" ), sw::ShaderCooker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    SW_EXPECT_EQUAL( sw::string_view( ".dxbc" ), sw::ShaderCooker::getExtensionForFormat( sw::ShaderTargetFormat::DXBC_D3D11 ) );
    SW_EXPECT_EQUAL( sw::string_view( ".dxil" ), sw::ShaderCooker::getExtensionForFormat( sw::ShaderTargetFormat::DXIL_D3D12 ) );
    SW_EXPECT_EQUAL( sw::string_view( ".spv" ), sw::ShaderCooker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) );
    SW_EXPECT_EQUAL( sw::string_view( ".spv" ), sw::ShaderCooker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_OpenGL ) );

    // 폴더 이름 역산출 — 폴더 이름 하나만 받는다(명령줄 이름 `vk` · `gl` 은 폴더가 아니다)
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "dx11" ) == sw::ShaderTargetFormat::DXBC_D3D11 );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "dx12" ) == sw::ShaderTargetFormat::DXIL_D3D12 );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "vulkan" ) == sw::ShaderTargetFormat::SPIRV_Vulkan );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "opengl" ) == sw::ShaderTargetFormat::SPIRV_OpenGL );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "vk" ) == sw::ShaderTargetFormat::Count );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "d3d12" ) == sw::ShaderTargetFormat::Count );

    // 미지원/미지의 서브폴더 -> Count
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "unknown" ) == sw::ShaderTargetFormat::Count );
    SW_EXPECT_TRUE( sw::ShaderCooker::getFormatForSubfolder( "" ) == sw::ShaderTargetFormat::Count );
}

/**
 * @brief [ShaderCookerTest] 1만 건 순열 해시 무결성 및 충돌 스트레스 테스트
 */
SW_TEST_CASE( ShaderCookerTest, PermutationHashCollisionStressTest )
{
    constexpr uint32                  kPermutationCount = 10000;
    sw::unordered_map<uint64, uint32> mapHashToID;
    mapHashToID.reserve( kPermutationCount );

    sw::fixed_string<sw::constant::kMaxBuffer64> defBuf;
    for ( uint32 index = 0; index < kPermutationCount; ++index )
    {
        sw::formatstring( defBuf.data(), defBuf.capacity(), "PERM_MACRO_%u=%u", index, ( index * 31u + 7u ) );
        const sw::vector<sw::string> listPerm = { "COMMON_DEFINE=1", defBuf.c_str() };
        const uint64                 hash     = sw::ShaderCooker::computePermutationHash( listPerm );

        SW_EXPECT_TRUE( hash != 0ull );
        auto iter = mapHashToID.find( hash );
        SW_EXPECT_TRUE( iter == mapHashToID.end() );
        mapHashToID.insert_or_assign( hash, index );
    }
    SW_EXPECT_EQUAL( kPermutationCount, static_cast<uint32>( mapHashToID.size() ) );
}

/**
 * @brief [ShaderCookerTest] 소스 해시 캐시는 파일이 바뀌면 다시 읽는다
 * @details 셰이더 요청 하나가 같은 소스를 여러 번 해시한다(메모리 캐시 확인 · 로컬 캐시 경로 · 쿠킹 신선도). 그래서 내용 해시는
 *          파일의 크기 · 쓰기 시각과 함께 캐시되고, 두 번째부터는 파일을 열지 않는다. 캐시가 편집을 놓치면 **바뀐 셰이더가 옛
 *          바이너리로 그려진다** — 로그는 성공을 찍는 가장 조용한 어긋남이다. 되돌린 내용은 처음과 같은 해시로 돌아와야 한다(내용 해시).
 */
SW_TEST_CASE( ShaderCookerTest, CachedSourceHashNoticesEditedFile )
{
    const sw::string dir  = test::makeTempDirectory( "SwShaderSourceHashDir" );
    const sw::string path = sw::FileUtil::joinPath( dir, "edit.hlsl" );

    const sw::string original = "float4 main() : SV_Target { return 1; }\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( original.data() ), original.size() ) );
    const sw::vector<sw::string> listNoHeader;
    const uint64                 firstHash = sw::ShaderCooker::computeSourceFingerprint( path, listNoHeader );
    SW_ASSERT_TRUE( firstHash != 0u );
    SW_EXPECT_EQUAL( firstHash, sw::ShaderCooker::computeSourceFingerprint( path, listNoHeader ) ); // 캐시 적중

    const sw::string edited = "float4 main() : SV_Target { return float4( 0.25, 0.5, 0.75, 1.0 ); }\n";
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( edited.data() ), edited.size() ) );
    const uint64 editedHash = sw::ShaderCooker::computeSourceFingerprint( path, listNoHeader );
    SW_EXPECT_TRUE( editedHash != 0u );
    SW_EXPECT_TRUE( editedHash != firstHash );

    SW_ASSERT_TRUE( sw::FileUtil::writeFile( path, reinterpret_cast<const uint8*>( original.data() ), original.size() ) );
    SW_EXPECT_EQUAL( firstHash, sw::ShaderCooker::computeSourceFingerprint( path, listNoHeader ) );

    SW_ASSERT_TRUE( sw::FileUtil::removeFile( path ) );
    SW_EXPECT_EQUAL( uint64( 0 ), sw::ShaderCooker::computeSourceFingerprint( path, listNoHeader ) );
}

/**
 * @brief [ShaderCookerTest] 소스 지문은 소스가 연 헤더만 반영한다 — 목록의 헤더를 고치면 달라지고, 목록에 없는 파일을 고쳐도 그대로이며, 순서와 무관하다
 * @details 예전에는 모든 `.hlsli` 의 해시 하나를 섞어서 헤더 하나를 고치면 모든 셰이더의 캐시 키가 바뀌었다. 헤더를 읽지 못하면 같은 지문이 나올 수 없으므로 0 이다.
 */
SW_TEST_CASE( ShaderCookerTest, SourceFingerprintFollowsOnlyTheListedHeaders )
{
    const sw::string dir        = test::makeTempDirectory( "SwSourceFingerprintDir" );
    const sw::string sourcePath = sw::FileUtil::joinPath( dir, "fp.hlsl" );
    const sw::string headerA    = sw::FileUtil::joinPath( dir, "fpa.hlsli" );
    const sw::string headerB    = sw::FileUtil::joinPath( dir, "fpb.hlsli" );
    const sw::string unrelated  = sw::FileUtil::joinPath( dir, "fpother.hlsli" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sourcePath, "float4 main() : SV_Target { return 1; }\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerA, "#define SW_FP_A 1\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, "#define SW_FP_B 1\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( unrelated, "#define SW_FP_OTHER 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();

    const sw::vector<sw::string> listAB{ headerA, headerB };
    const sw::vector<sw::string> listBA{ headerB, headerA };
    const sw::vector<sw::string> listAOnly{ headerA };
    const uint64                 baseline = sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAB );
    SW_ASSERT_TRUE( baseline != 0u );
    SW_EXPECT_EQUAL( baseline, sw::ShaderCooker::computeSourceFingerprint( sourcePath, listBA ) ); // 순서와 무관하다
    const uint64 aOnlyBaseline = sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAOnly );
    SW_EXPECT_TRUE( baseline != aOnlyBaseline );

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( unrelated, "#define SW_FP_OTHER 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_EQUAL( baseline, sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAB ) ); // 목록에 없는 파일은 보지 않는다

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, "#define SW_FP_B 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( baseline != sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAB ) );
    SW_EXPECT_EQUAL( aOnlyBaseline, sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAOnly ) ); // A 만 연 셰이더는 B 가 바뀌어도 그대로다

    SW_ASSERT_TRUE( sw::FileUtil::removeFile( headerB ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_EQUAL( uint64( 0 ), sw::ShaderCooker::computeSourceFingerprint( sourcePath, listAB ) ); // 헤더를 읽지 못하면 같은 지문이 나올 수 없다
}

/**
 * @brief [ShaderCookerTest] 의존 기록은 기록한 헤더가 모두 그대로일 때만 맞는다 — 컴파일 캐시 항목 옆에 둔다
 * @details 캐시 키는 컴파일 전에 필요하고 연 헤더 목록은 컴파일 뒤에야 알아서 헤더를 키에 넣지 못한다. 대신 이 기록이 헤더의 변경 · 삭제를 잡아 캐시 미스로 만든다.
 */
SW_TEST_CASE( ShaderCookerTest, IncludeRecordIsCurrentOnlyWhileEveryHeaderIsUnchanged )
{
    const sw::string dir        = test::makeTempDirectory( "SwIncludeRecordDir" );
    const sw::string headerA    = sw::FileUtil::joinPath( dir, "reca.hlsli" );
    const sw::string headerB    = sw::FileUtil::joinPath( dir, "recb.hlsli" );
    const sw::string recordPath = sw::FileUtil::joinPath( dir, "entry.bin.deps" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerA, "#define SW_REC_A 1\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, "#define SW_REC_B 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();

    sw::vector<sw::string> listRead;
    SW_EXPECT_FALSE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) ); // 기록이 없으면 맞지 않는다(캐시 미스)

    SW_ASSERT_TRUE( sw::ShaderCooker::writeIncludeRecord( recordPath, sw::vector<sw::string>{ headerA, headerB } ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) );
    SW_EXPECT_EQUAL( size_t( 2 ), listRead.size() );

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, "#define SW_REC_B 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );

    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( headerB, "#define SW_REC_B 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) ); // 내용이 돌아오면 다시 맞는다

    SW_ASSERT_TRUE( sw::FileUtil::removeFile( headerA ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) ); // 헤더가 사라지면 맞지 않는다

    // 헤더를 열지 않은 셰이더의 기록은 비어 있고, 그것은 맞는 기록이다(캐시 적중).
    SW_ASSERT_TRUE( sw::ShaderCooker::writeIncludeRecord( recordPath, sw::vector<sw::string>{} ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::readIncludeRecord( recordPath, listRead ) );
    SW_EXPECT_TRUE( listRead.empty() );
}

/**
 * @brief [ShaderCookerTest] 쿠킹하지 못한 소스는 스탬프에서 빠진다 — 다음 쿠킹이 그것을 최신으로 보지 않고 다시 쿠킹한다
 * @details 컴파일에 실패해도 쿠킹이 폴더 전체의 지금 소스 해시로 스탬프를 찍으면 그 셰이더의 **옛 바이너리**가 최신으로 판정된다 — 다음 쿠킹도
 *          건너뛰고 `--cook-shaders` 는 종료 코드 0 이라 배포본에 옛 바이너리가 실린다.
 */
SW_TEST_CASE( ShaderCookerTest, FailedSourcesAreLeftOutOfTheCookStamp )
{
    const sw::string root       = test::makeTempDirectory( "cook_stamp" );
    const sw::string shadersDir = sw::FileUtil::joinPath( root, "shaders" );
    const sw::string binDir     = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shadersDir, "bin" ), "dx12" );
    const sw::string goodPath   = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( shadersDir, "good.hlsl" ) );
    const sw::string brokenPath = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( shadersDir, "broken.hlsl" ) );
    SW_EXPECT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( goodPath, "float4 main() : SV_Target { return 1; }\n" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, "float4 main() : SV_Target { return }\n" ) );

    const sw::unordered_set<sw::string> uniqueFailedSource{ brokenPath };
    SW_ASSERT_TRUE( sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, &uniqueFailedSource, {} ) ); // 다른 include 루트도 이 임시 루트에서만 보고, 우선순위도 전역이 아니라 넘긴 것만 본다
    SW_ASSERT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "cook.stamp" ) ) );                                      // 스탬프를 못 썼으면 여기서 멈춘다 — 아래 판정의 false 와 구별한다
    sw::ShaderCooker::invalidateSharedHeaderCache();                                                                               // 스탬프 읽기 캐시를 비워 방금 쓴 스탬프를 읽게 한다

    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, goodPath ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, brokenPath ) );
}

/**
 * @brief [ShaderCookerTest] `removeUnlistedBinaries` 는 목록에 없는 셰이더 바이너리만 지운다 — 스탬프 · 매니페스트 · 다른 파일은 그대로다
 * @details 퍼뮤테이션 해시가 바뀌어 다시 구우면 옛 바이너리가 매니페스트 밖에 남아 `--verify-shaders` 가 졌다(통합이 손으로 지웠다).
 */
SW_TEST_CASE( ShaderCookerTest, RemoveUnlistedBinariesKeepsOnlyListedOnes )
{
    const sw::string binDir    = test::makeTempDirectory( "cook_prune" );
    const utf8*      arrName[] = { "keep_ps.dxil", "old_ps_0badcafe.dxil", "old_ps_0badcafe.dxbc", "old_vs_00000001.spv", "cook.stamp", "reflection.manifest", "notes.txt" };
    for ( const utf8* pName : arrName )
    {
        SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( sw::FileUtil::joinPath( binDir, pName ), "x" ) );
    }

    uint32 removedCount{ 99 };
    SW_ASSERT_TRUE( sw::ShaderCooker::removeUnlistedBinaries( binDir, sw::unordered_set<sw::string>{ "keep_ps.dxil" }, removedCount ) );
    SW_EXPECT_EQUAL( 3u, removedCount );
    SW_EXPECT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "keep_ps.dxil" ) ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "old_ps_0badcafe.dxil" ) ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "old_ps_0badcafe.dxbc" ) ) );
    SW_EXPECT_FALSE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "old_vs_00000001.spv" ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "cook.stamp" ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "reflection.manifest" ) ) );
    SW_EXPECT_TRUE( sw::FileUtil::exists( sw::FileUtil::joinPath( binDir, "notes.txt" ) ) );
}

/**
 * @brief [ShaderCookDriverTest] 컴파일에 실패하는 요청이 있으면 쿠킹은 실패로 끝나고, 매니페스트에 없는 옛 바이너리는 치워진다
 * @details `App --cook-shaders` 의 종료 코드는 `ShaderCookSummary::isClean` 이 정한다. 실패한 쿠킹이 깨끗하다고 답하면 종료 코드 0 으로 낡은 산출물이 배포된다.
 *          컴파일러가 없는 환경에서도 같은 결과(실패)라 컴파일러 유무와 상관없이 돈다.
 */
SW_TEST_CASE( ShaderCookDriverTest, FailingRequestFailsTheCookAndStaleBinaryIsRemoved )
{
    test::ScopedLogSuppressor suppressor;

    const sw::string root       = test::makeTempDirectory( "cook_driver_fail" );
    const sw::string shadersDir = sw::FileUtil::joinPath( root, "shaders" );
    const sw::string binDir     = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shadersDir, "bin" ), "dx12" );
    const sw::string brokenPath = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( shadersDir, "broken.hlsl" ) );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, "float4 main() : SV_Target { return }\n" ) );
    const sw::string staleBinary = sw::FileUtil::joinPath( binDir, "broken_ps_0badcafe.dxil" ); // 옛 퍼뮤테이션 해시로 구운 바이너리
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( staleBinary, "stale" ) );

    sw::ShaderCookRequest request{};
    request._shaderPath = brokenPath;
    request._entryPoint = "main";
    request._stage      = sw::ShaderStage::Pixel;

    const sw::ShaderCookSummary summary = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), { request }, sw::ShaderTargetFormat::DXIL_D3D12, false );
    SW_EXPECT_FALSE( summary.isClean() );
    SW_EXPECT_EQUAL( 1u, summary._failedCount );
    SW_EXPECT_EQUAL( 0u, summary._cookedCount );
    SW_EXPECT_EQUAL( 1u, summary._removedCount );
    SW_EXPECT_FALSE( sw::FileUtil::exists( staleBinary ) );
}

/**
 * @brief [ShaderCookDriverTest] 소스가 없거나 `shaders` 폴더 밖인 요청은 건너뛰지 않고 실패로 센다
 * @details 조용히 건너뛰면 요청과 소스가 어긋난 채 쿠킹이 깨끗하다고 답해 종료 코드 0 이 된다.
 */
SW_TEST_CASE( ShaderCookDriverTest, RequestWithoutUsableSourceIsAFailure )
{
    test::ScopedLogSuppressor suppressor;

    const sw::string root        = test::makeTempDirectory( "cook_driver_source" );
    const sw::string outsidePath = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( root, "elsewhere.hlsl" ) ); // 'shaders' 폴더 밖
    const sw::string missingPath = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( sw::FileUtil::joinPath( root, "shaders" ), "missing.hlsl" ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( outsidePath, "float4 main() : SV_Target { return 1; }\n" ) );

    sw::vector<sw::ShaderCookRequest> listRequest( 2 );
    listRequest[0]._shaderPath = outsidePath;
    listRequest[1]._shaderPath = missingPath;
    for ( sw::ShaderCookRequest& request : listRequest )
    {
        request._entryPoint = "main";
        request._stage      = sw::ShaderStage::Pixel;
    }

    const sw::ShaderCookSummary summary = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), listRequest, sw::ShaderTargetFormat::DXIL_D3D12, false );
    SW_EXPECT_FALSE( summary.isClean() );
    SW_EXPECT_EQUAL( 2u, summary._failedCount );
    SW_EXPECT_EQUAL( 0u, summary._cookedCount );
}

/**
 * @brief [ShaderCookDriverTest] 컴파일에 실패하는 요청이 여럿이면 병렬로 돌아도 실패가 모두 세어지고, 실패한 소스는 스탬프에 오르지 않는다
 * @details 컴파일러가 없는 환경에서도 같은 결과(실패)라 컴파일러 유무와 상관없이 돈다. 요청마다 파일이 다른 병렬 쿠킹에서 카운터가 요청 수와 어긋나지 않는지 본다.
 */
SW_TEST_CASE( ShaderCookDriverTest, ManyFailingRequestsAreAllCountedAndLeftOutOfTheStamp )
{
    test::ScopedLogSuppressor suppressor;

    const sw::string root       = test::makeTempDirectory( "cook_driver_many_fail" );
    const sw::string shadersDir = sw::FileUtil::joinPath( root, "shaders" );
    const sw::string binDir     = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shadersDir, "bin" ), "dx12" );
    const sw::string brokenPath = sw::FileUtil::normalizeSeparators( sw::FileUtil::joinPath( shadersDir, "broken.hlsl" ) );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( brokenPath, "float4 main() : SV_Target { return }\n" ) );

    constexpr uint32                  kRequestCount = 8;
    sw::vector<sw::ShaderCookRequest> listRequest( kRequestCount );
    for ( uint32 requestIndex = 0; requestIndex < kRequestCount; ++requestIndex )
    {
        sw::ShaderCookRequest& request = listRequest[requestIndex];
        request._shaderPath            = brokenPath;
        request._entryPoint            = "main";
        request._stage                 = sw::ShaderStage::Pixel;
        request._listPermutation       = { "SW_BROKEN_" + sw::to_string( requestIndex ) }; // 퍼뮤테이션마다 출력 파일이 다르다
        request._permutationHash       = sw::ShaderCooker::computePermutationHash( request._listPermutation );
    }

    const sw::ShaderCookSummary summary = sw::ShaderCookDriver::cookRequests( root, test::makeShaderConfigPriority(), listRequest, sw::ShaderTargetFormat::DXIL_D3D12, false );
    SW_EXPECT_FALSE( summary.isClean() );
    SW_EXPECT_EQUAL( kRequestCount, summary._failedCount );
    SW_EXPECT_EQUAL( 0u, summary._cookedCount );

    sw::string stampText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( binDir, "cook.stamp" ), stampText ) );
    SW_EXPECT_TRUE_MSG( stampText.find( "broken.hlsl" ) == sw::string::npos, "컴파일에 실패한 소스는 스탬프에서 빠져 다음 쿠킹이 다시 시도해야 한다" );
}

/**
 * @brief [ShaderCookerTest] 잘못된 파일 경로 및 디렉터리에 대한 방어적 실패 처리 검증
 */
SW_TEST_CASE( ShaderCookerTest, DefensiveFileOperations )
{
    test::ScopedLogSuppressor suppressor;

    sw::ShaderCookResult result{};
    const bool           bCookInvalid = sw::ShaderCooker::cookShader(
        "non_existent_shader_file_12345.hlsl",
        "output_dummy.bin",
        "VSMain",
        sw::ShaderStage::Vertex,
        sw::ShaderTargetFormat::DXIL_D3D12,
        nullptr,
        &result );

    SW_EXPECT_FALSE( bCookInvalid );
    SW_EXPECT_FALSE( result._bSuccess == SW_TRUE );

    const sw::ShaderCookSummary summary = sw::ShaderCookDriver::cookAllShaders(
        "invalid_resource_root_path_99999",
        sw::ShaderTargetFormat::DXIL_D3D12,
        false );
    SW_EXPECT_EQUAL( 0u, summary._cookedCount );
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
                const uint32          pathID = ( threadIndex + callIndex ) % 4;
                if ( pathID == 0 )
                    desc._filePath = test::getEngineDefaultAssets()._shaderFullscreenTriangle.get();
                else if ( pathID == 1 )
                    desc._filePath = test::getEngineDefaultAssets()._shaderForwardLit.get();
                else if ( pathID == 2 )
                    desc._filePath = test::getEngineDefaultAssets()._shaderShadowDepth.get();
                else
                    desc._filePath = test::getEngineDefaultAssets()._shaderTonemap.get();

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
 * @details 지우는 스레드가 **돌기 시작한 뒤에** 쿼리를 띄운다. 붐비는 기계(CI 의 ctest 병렬)에서는 스레드가 늦게 떠서, 쿼리가 다 끝날
 *          때까지 한 번도 지우지 못하면 아무것도 겨루지 않은 채 `clearsDone == 0` 으로 진다(`FileTest.ReadersNeverObserveHalfWrittenFile`
 *          과 같은 모양).
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

    const sw::Deadline waitDeadline = sw::Deadline::afterMilliseconds( 10000 );
    while ( clearsDone.load() == 0 && waitDeadline.isExpired() == false )
    {
        std::this_thread::yield();
    }
    SW_EXPECT_TRUE_MSG( clearsDone.load() > 0, "지우는 스레드가 10 초 안에 돌지 않았다" );

    sw::vector<std::thread> listWorker;
    listWorker.reserve( kWorkerCount );
    for ( uint32 workerIndex = 0; workerIndex < kWorkerCount; ++workerIndex )
    {
        listWorker.emplace_back( [&cache]()
        {
            for ( uint32 index = 0; index < kIterations; ++index )
            {
                sw::ShaderCompileDesc desc{};
                desc._filePath              = test::getEngineDefaultAssets()._shaderFullscreenTriangle.get();
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
}

/**
 * @brief [ShaderCookerTest] 컬러 출력이 없는 패스(그림자·뎁스 프리패스)에는 픽셀 스테이지가 없다.
 * @details 쿠커와 런타임이 둘 다 `FrameRendererUtil::hasPixelStage` 하나를 본다. 쿠커가 타입 **문자열**로 "그림자엔 PS 없음" 을 정하고
 *          런타임이 PS 경로를 늘 채우면, 머티리얼 define 을 얹은 그림자 변형이 DX12 에서 PS 리플렉션을 요구해 Shipping 실기동이
 *          "리플렉션 매니페스트에 없다" 로 진다. 실제 파이프라인 XML 둘과 합성 선언으로 그 규칙을 고정한다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( ShaderCookerTest, DepthOnlyPassesHaveNoPixelStage )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    // 1) 실제 파이프라인 — 그림자 패스는 PS 없음, 씬 메시를 그리는 나머지는 PS 있음.
    const utf8* arrPipeline[] = { "engine/pipeline/forwardpipeline.xml", "engine/pipeline/deferredpipeline.xml" };
    for ( const utf8* pPipeline : arrPipeline )
    {
        const sw::string absPath = sw::ResourcePaths::findLooseFile( pPipeline ).getDiskPath();
        SW_EXPECT_TRUE_MSG( absPath.empty() == false, pPipeline );
        if ( absPath.empty() )
            continue;

        sw::RenderPipelineAsset pipelineRes;
        SW_EXPECT_TRUE_MSG( pipelineRes.loadFromDiskFile( absPath ), pPipeline );

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

    // 출력 선언이 없으면 타입이 정한다 — 그림자·뎁스 프리패스는 0, 그 밖은 1 (PSO 를 만들 때 넘기는 기본값과 같다).
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
 * @brief [ShaderCookerTest] 파이프라인 XML 의 첨부 포맷 이름은 RHIFormat 의 모든 열거자를 그 이름 그대로 읽는다
 * @details 로드 검증(`RenderPipelineAsset::validate`)과 실제 생성(`parseAttachmentFormat`)이 같은 이름을 봐야 한다. 이름이 둘이면
 *          검증은 통과하는 "R32_FLOAT" 가 생성에서 조용히 R8G8B8A8_UNORM 이 된다. 이름 → 값 → 이름이 열거자마다 제자리로 돌아오는지 본다.
 */
SW_TEST_CASE( ShaderCookerTest, AttachmentFormatNameRoundTripsEveryRHIFormat )
{
    for ( uint32 value = 0; value <= static_cast<uint32>( sw::RHIFormat::BC6H_UF16 ); ++value )
    {
        const sw::RHIFormat format = static_cast<sw::RHIFormat>( value );
        const utf8*         pName  = sw::FrameRendererUtil::attachmentFormatName( format );
        SW_ASSERT_NOT_NULL( pName );
        SW_EXPECT_TRUE_MSG( sw::string_view( pName ) != "(unknown)", ( sw::string( "이름이 없는 RHIFormat 값: " ) + sw::to_string( value ) ).c_str() );
        SW_EXPECT_TRUE_MSG( sw::FrameRendererUtil::parseAttachmentFormat( pName ) == format, pName );
    }

    // 대소문자는 가리지 않는다(XML 을 손으로 쓴다). 모르는 이름은 정해진 폴백이다 — 오류는 로드 검증이 낸다.
    SW_EXPECT_TRUE( sw::FrameRendererUtil::parseAttachmentFormat( "r32_float" ) == sw::RHIFormat::R32_FLOAT );
    SW_EXPECT_TRUE( sw::FrameRendererUtil::parseAttachmentFormat( "NoSuchFormat" ) == sw::RHIFormat::R8G8B8A8_UNORM );
}

/**
 * @brief [ShaderCookerTest] 셰이더 캐시가 읽는 파일 이름은 쿠커가 쓰는 이름과 같고, 퍼뮤테이션마다 다르다.
 * @details 캐시가 스템과 스테이지만으로 이름을 만들면 **모든 퍼뮤테이션이 해시 0 바이너리를 읽는다** — 쿠킹 바이너리가
 *          있는 한 SW_FORWARD·MATERIAL_BLEND_TRANSLUCENT·SW_VIEWMODE_UNLIT 이 GPU 에 닿지 않는다. 리플렉션 매니페스트는
 *          해시로 찾으므로 레이아웃만 맞고 바이트코드는 틀린 어긋남이라 그림으로만 드러난다. GPU 가 필요 없다(nogpu).
 */
SW_TEST_CASE( ShaderCookerTest, CachePathCarriesPermutationHash )
{
    sw::ShaderCompileDesc plain{};
    plain._filePath     = test::getEngineDefaultAssets()._shaderForwardLit.get();
    plain._entryPoint   = "PSMain";
    plain._stage        = sw::ShaderStage::Pixel;
    plain._targetFormat = sw::ShaderTargetFormat::SPIRV_Vulkan;

    sw::ShaderCompileDesc unlit = plain;
    unlit._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    unlit._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_VIEWMODE_UNLIT=1" ) );

    const sw::string plainPath = sw::ShaderCache::makePrecookedRelativePath( plain );
    const sw::string unlitPath = sw::ShaderCache::makePrecookedRelativePath( unlit );
    SW_EXPECT_STREQ( "engine/shaders/bin/vulkan/forwardlit_ps.spv", plainPath.c_str() );
    SW_EXPECT_TRUE_MSG( unlitPath != plainPath, "퍼뮤테이션이 다른데 같은 바이너리를 읽는다 — define 이 GPU 에 닿지 않는다" );

    // 쿠커가 쿠킹하는 이름과 글자 단위로 같아야 한다(다른 규칙이 하나라도 있으면 그 규칙이 원본을 이긴다).
    const sw::string cookedName = sw::ShaderCooker::computeBinaryFileName( "forwardlit", sw::ShaderStage::Pixel, "PSMain",
                                                                           sw::ShaderCooker::computePermutationHash( unlit._listDefine ), ".spv" );
    SW_EXPECT_STREQ( ( sw::string( "engine/shaders/bin/vulkan/" ) + cookedName ).c_str(), unlitPath.c_str() );

    // 로컬 라이브 캐시도 같은 이름을 쓴다 — 처음 컴파일된 퍼뮤테이션이 나머지를 덮으면 안 된다.
    const sw::string plainLocal = sw::ShaderCache::makeLocalCachePath( plain );
    const sw::string unlitLocal = sw::ShaderCache::makeLocalCachePath( unlit );
    SW_EXPECT_TRUE( plainLocal != unlitLocal );
    SW_EXPECT_TRUE( unlitLocal.find( cookedName ) != sw::string::npos );

    // define 순서는 해시에 영향이 없다 — 런타임이 어떤 순서로 얹든 같은 파일이어야 한다.
    sw::ShaderCompileDesc reordered = plain;
    reordered._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_VIEWMODE_UNLIT=1" ) );
    reordered._listDefine.push_back( sw::ShaderMacroDefine::parse( "SW_FORWARD=1" ) );
    SW_EXPECT_STREQ( unlitPath.c_str(), sw::ShaderCache::makePrecookedRelativePath( reordered ).c_str() );
}

/**
 * @brief 쿠킹된 산출물의 신선도를 **파일 시간이 아니라 내용 해시**로 판정하는지 봅니다.
 * @details 이 저장소는 쿠킹된 바이너리까지 커밋하므로 `git pull` 이 소스와 산출물의 mtime 을 임의의
 *          순서로 덮어쓴다. `산출물 mtime >= 소스 mtime` 으로 판정하면 그때 "이미 최신" 이라 답해 낡은 바이너리가
 *          커밋된 채 돌고, 한 백엔드만 다른 그림을 내는 것이 백엔드 버그처럼 보인다. 여기서 막는다.
 *          트리는 케이스 임시 루트에 짓는다. 소스 트리 `Resource/engine/shaders` 에 헤더를 넣었다 빼면 같은 폴더를 훑는
 *          다른 테스트 프로세스(`ctest -j`)가 그 사이에 "낡음" 을 본다.
 */
SW_TEST_CASE( ShaderCookStampTest, FreshnessIsJudgedByContentNotFileTime )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( ::test::getEngineDefaultAssets()._shaderForwardLit.c_str(), "#include \"common.hlsli\"\nfloat4 VSMain() : SV_Position { return 1; }\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( ::test::getEngineDefaultAssets()._shaderTonemap.c_str(), "#include \"common.hlsli\"\nfloat4 PSMain() : SV_Target { return 0.5; }\n" ) );
    const sw::string shaderPath  = resourceRoot.makePath( ::test::getEngineDefaultAssets()._shaderForwardLit.c_str() );
    const sw::string tonemapPath = resourceRoot.makePath( ::test::getEngineDefaultAssets()._shaderTonemap.c_str() );
    const sw::string binDir      = resourceRoot.makePath( "engine/shaders/bin/dx12" );
    const sw::string headerPath  = resourceRoot.makePath( "engine/shaders/common.hlsli" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );

    // 쿠커는 컴파일이 연 헤더를 소스별로 넘긴다(여기서는 두 셰이더 모두 common.hlsli 하나를 열었다고 한다).
    const sw::unordered_map<sw::string, sw::vector<sw::string>> mapIncludedFile{
        { sw::FileUtil::normalizeSeparators( shaderPath ), { headerPath }},
        {sw::FileUtil::normalizeSeparators( tonemapPath ), { headerPath }},
    };
    SW_ASSERT_TRUE( sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, nullptr, mapIncludedFile ) );
    sw::ShaderCooker::invalidateSharedHeaderCache(); // 스탬프 읽기 캐시를 비워 방금 쓴 스탬프를 읽게 한다
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );

    // 스탬프에 없는 소스는 최신일 수 없다 — "파일이 있으니 최신" 으로 새지 않는지 본다.
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, resourceRoot.makePath( "engine/shaders/nosuchshader.hlsl" ) ) );

    // 소스 지문은 셰이더마다 달라야 한다(같으면 캐시 항목이 셰이더를 못 가른다).
    const sw::vector<sw::string> listHeader{ headerPath };
    const uint64                 hashForward = sw::ShaderCooker::computeSourceFingerprint( shaderPath, listHeader );
    const uint64                 hashTonemap = sw::ShaderCooker::computeSourceFingerprint( tonemapPath, listHeader );
    SW_EXPECT_TRUE( hashForward != 0 );
    SW_EXPECT_TRUE( hashTonemap != 0 );
    SW_EXPECT_TRUE( hashForward != hashTonemap );

    // **소스가 연 헤더가 바뀌면 낡은 것이 되어야 한다.** `.hlsl` 은 그대로인데 include 한 `.hlsli` 만
    // 바뀌는 경우다 — 파일 시간으로는 이 조합이 조용히 통과한다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, tonemapPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::computeSourceFingerprint( shaderPath, listHeader ) != hashForward );

    // 되돌리면 다시 최신이다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
    SW_EXPECT_EQUAL( hashForward, sw::ShaderCooker::computeSourceFingerprint( shaderPath, listHeader ) );
}

/**
 * @brief [ShaderCookStampTest] 헤더 A 만 연 셰이더는 A 가 바뀔 때만 낡고, B 만 연 셰이더는 그대로다 — 전이 include 와 목록 밖의 파일도 같은 규칙이다
 * @details 예전에는 모든 `.hlsli` 의 해시 하나를 섞어 헤더 하나만 고쳐도 모든 셰이더가 다시 쿠킹됐다. 지금은 컴파일러가 소스를 읽으며 실제로 연 헤더(전이 include 포함)만 소스별로 스탬프에 적힌다.
 *          컴파일러가 연 목록은 쿠커가 `writeCookStamp` 에 넘기는 값이고, 여기서는 그 값을 손으로 지어 넘겨 판정 규칙만 고정한다(실제 목록은 `LiveShaderTest` 가 컴파일러로 본다).
 *          옛 판의 스탬프는 읽지 않고 낡음이며, 헤더가 사라지면 낡음이다.
 */
SW_TEST_CASE( ShaderCookStampTest, OnlyHeadersTheShaderOpenedMakeItStale )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheadera.hlsli", "#include \"stampheaderc.hlsli\"\n#define SW_STAMP_A 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheaderb.hlsli", "#define SW_STAMP_B 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheaderc.hlsli", "#define SW_STAMP_C 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampunrelated.hlsli", "#define SW_STAMP_UNRELATED 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampusesa.hlsl", "#include \"stampheadera.hlsli\"\nfloat4 PSMain() : SV_Target { return SW_STAMP_A; }\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampusesb.hlsl", "#include \"stampheaderb.hlsli\"\nfloat4 PSMain() : SV_Target { return SW_STAMP_B; }\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampplain.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );
    const sw::string             usesAPath = resourceRoot.makePath( "engine/shaders/stampusesa.hlsl" );
    const sw::string             usesBPath = resourceRoot.makePath( "engine/shaders/stampusesb.hlsl" );
    const sw::string             plainPath = resourceRoot.makePath( "engine/shaders/stampplain.hlsl" );
    const sw::string             headerA   = resourceRoot.makePath( "engine/shaders/stampheadera.hlsli" );
    const sw::string             headerB   = resourceRoot.makePath( "engine/shaders/stampheaderb.hlsli" );
    const sw::string             headerC   = resourceRoot.makePath( "engine/shaders/stampheaderc.hlsli" );
    const sw::string             binDir    = resourceRoot.makePath( "engine/shaders/bin/dx12" );
    const sw::vector<sw::string> priority  = test::makeShaderConfigPriority();
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );

    // 컴파일러가 열었을 헤더: stampusesa → A → C(전이), stampusesb → B, stampplain 은 없음.
    const sw::unordered_map<sw::string, sw::vector<sw::string>> mapIncludedFile{
        {sw::FileUtil::normalizeSeparators( usesAPath ), { headerA, headerC }},
        {sw::FileUtil::normalizeSeparators( usesBPath ),          { headerB }},
    };
    SW_ASSERT_TRUE( sw::ShaderCooker::writeCookStamp( root, priority, binDir, nullptr, mapIncludedFile ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    sw::vector<sw::string> listStamped;
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath, &listStamped ) );
    SW_EXPECT_EQUAL( size_t( 2 ), listStamped.size() ); // 건너뛴 요청의 몫으로 쿠커가 되받는 목록이다
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, plainPath ) );

    // A 만 고치면 A 를 연 셰이더만 낡다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheadera.hlsli", "#include \"stampheaderc.hlsli\"\n#define SW_STAMP_A 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath, &listStamped ) );
    SW_EXPECT_TRUE( listStamped.empty() );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, plainPath ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheadera.hlsli", "#include \"stampheaderc.hlsli\"\n#define SW_STAMP_A 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath ) );

    // 전이 include(소스 → A → C)의 C 를 고쳐도 A 를 연 셰이더는 낡다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheaderc.hlsli", "#define SW_STAMP_C 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheaderc.hlsli", "#define SW_STAMP_C 1\n" ) );

    // 목록에 없는 파일(같은 폴더의 다른 헤더)을 고치거나 새로 만들어도 모두 최신이다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampunrelated.hlsli", "#define SW_STAMP_UNRELATED 22222\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampbrandnew.hlsli", "// nobody opens this\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, plainPath ) );

    // 소스가 연 헤더가 사라지면 낡음이다(읽지 못하는 헤더를 최신으로 보지 않는다).
    SW_ASSERT_TRUE( sw::FileUtil::removeFile( headerB ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/stampheaderb.hlsli", "#define SW_STAMP_B 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesBPath ) );

    // 옛 판(4)의 스탬프는 읽지 않고 낡음이다 — 조용히 해석하지 않는다. 판정(verify)도 같은 이유로 알린다.
    sw::string       stampText;
    const sw::string stampPath = sw::FileUtil::joinPath( binDir, "cook.stamp" );
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( stampPath, stampText ) );
    SW_ASSERT_TRUE( stampText.find( "SWCOOK 5" ) == 0u );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( stampPath, sw::string( "SWCOOK 4" ) + stampText.substr( 8 ) ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, usesAPath ) );
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, priority, binDir, plainPath ) );
    sw::vector<sw::string> listProblem;
    sw::ShaderCooker::verifyCookedShaders( root, priority, "dx12", listProblem );
    bool bReportedOldFormat = false;
    for ( const sw::string& problem : listProblem )
    {
        bReportedOldFormat = bReportedOldFormat || problem.find( "unknown header" ) != sw::string::npos;
    }
    SW_EXPECT_TRUE( bReportedOldFormat );
}

/**
 * @brief [ShaderCookStampTest] 다른 include 루트(engine)의 헤더가 바뀌면 common 셰이더 산출물도 낡은 것이 된다
 * @details common 셰이더는 `common.hlsli` · `binding.hlsli` 를 engine/shaders 에서 include 한다. 스탬프가 자기 도메인 헤더만 보면 engine 헤더를
 *          고쳐도 common 바이너리가 "최신" 으로 남아, 한쪽만 옛 바인딩으로 도는 바이너리가 커밋된다. 다른 도메인은 넘긴 루트 아래에서만 찾는다.
 */
SW_TEST_CASE( ShaderCookStampTest, HeaderInAnotherIncludeRootMakesOutputStale )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( ::test::getEngineDefaultAssets()._shaderProbeSampleCompute.c_str(), "#include \"common.hlsli\"\n[numthreads(1,1,1)] void CSMain() {}\n" ) );
    const sw::string shaderPath = resourceRoot.makePath( ::test::getEngineDefaultAssets()._shaderProbeSampleCompute.c_str() );
    const sw::string binDir     = resourceRoot.makePath( "common/shaders/bin/dx12" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );

    // 컴파일러가 engine/shaders 에서 연 헤더다(자기 도메인 헤더가 아니므로 스탬프에 `engine:` 키로 적힌다).
    const sw::unordered_map<sw::string, sw::vector<sw::string>> mapIncludedFile{
        { sw::FileUtil::normalizeSeparators( shaderPath ), { resourceRoot.makePath( "engine/shaders/common.hlsli" ) } },
    };
    SW_ASSERT_TRUE( sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, nullptr, mapIncludedFile ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_ASSERT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
    sw::string stampText;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( sw::FileUtil::joinPath( binDir, "cook.stamp" ), stampText ) );
    SW_EXPECT_TRUE( stampText.find( "?engine:common.hlsli" ) != sw::string::npos );

    // 이 셰이더가 열지 않은 engine 헤더는 새로 생겨도 고쳐도 상관없다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/cooktemp_otherroot.hlsli", "// not opened by this shader\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );

    // 연 engine 헤더가 바뀌면 낡다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_FALSE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );

    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/common.hlsli", "#define SW_COMMON 1\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    SW_EXPECT_TRUE( sw::ShaderCooker::isCookedOutputCurrent( root, test::makeShaderConfigPriority(), binDir, shaderPath ) );
}

/**
 * @brief [ShaderCookStampTest] 커밋된 engine · common 산출물이 지금 소스와 맞는다 — 소스 트리는 읽기만 한다
 * @details 셰이더를 고치고 다시 쿠킹하지 않으면 커밋된 바이너리가 옛 소스의 것으로 남는다. 판정 규칙은 위 두 케이스가 임시 트리에서 고정하고,
 *          여기서는 실제 트리에 그 규칙을 대어 보기만 한다(쓰지 않는다).
 */
SW_TEST_CASE( ShaderCookStampTest, CommittedCookedOutputIsCurrent )
{
    const sw::string arrShaderPath[] = { sw::ResourcePaths::findLooseFile( test::getEngineDefaultAssets()._shaderForwardLit.get() ).getDiskPath(),
                                         sw::ResourcePaths::findLooseFile( test::getEngineDefaultAssets()._shaderProbeSampleCompute.get() ).getDiskPath() };
    const sw::string arrBinDir[]     = { sw::ResourcePaths::findLooseFile( "engine/shaders/bin/dx12" ).getDiskPath(),
                                         sw::ResourcePaths::findLooseFile( "common/shaders/bin/dx12" ).getDiskPath() };
    for ( uint32 index = 0; index < 2u; ++index )
    {
        // 쿠킹된 트리가 없는 환경(클린 체크아웃 직후)에서는 볼 것이 없다.
        if ( arrShaderPath[index].empty() || arrBinDir[index].empty() )
            continue;
        SW_EXPECT_TRUE_MSG( sw::ShaderCooker::isCookedOutputCurrent( arrBinDir[index], arrShaderPath[index] ),
                            ( arrShaderPath[index] + " 의 산출물이 지금 소스와 다르다 — App --cook-shaders 로 다시 쿠킹한다" ).c_str() );
    }
}

/**
 * @brief [ShaderCookStampTest] 판정(`verifyCookedShaders`)이 빠진 백엔드 · 바이너리 · 매니페스트 · 바뀐 소스를 잡고, 맞는 트리는 통과시킨다
 * @details 예전에는 같은 판정을 파이썬(`CheckShaderStamps` 게이트)이 따로 들었다. 판정은 스탬프를 쓰는 쪽과 같은 C++ 한 곳이고, 규칙은 이 케이스가 임시 트리에서 고정한다.
 */
SW_TEST_CASE( ShaderCookStampTest, VerifyCatchesEveryStaleShape )
{
    const test::ScopedTempResourceRoot resourceRoot;
    const sw::string&                  root = resourceRoot.getRootFolderPath();
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probe.hlsl", "float4 PSMain() : SV_Target { return 0; }\n" ) );
    const sw::string binDir = resourceRoot.makePath( "engine/shaders/bin/dx12" );
    SW_ASSERT_TRUE( sw::FileUtil::ensureDirectoryExists( binDir ) );

    // 1) 한 번도 베이크하지 않은 백엔드는 문제다.
    sw::vector<sw::string> listProblem;
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "vulkan", listProblem );
    SW_EXPECT_EQUAL( size_t( 1 ), listProblem.size() );

    // 2) 스탬프만 있고 바이너리 · 매니페스트가 없으면 문제다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probeheader.hlsli", "#define SW_PROBE 1\n" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/unopened.hlsli", "#define SW_UNOPENED 1\n" ) );
    const sw::unordered_map<sw::string, sw::vector<sw::string>> mapIncludedFile{
        { sw::FileUtil::normalizeSeparators( resourceRoot.makePath( "engine/shaders/probe.hlsl" ) ), { resourceRoot.makePath( "engine/shaders/probeheader.hlsli" ) } },
    };
    SW_ASSERT_TRUE( sw::ShaderCooker::writeCookStamp( root, test::makeShaderConfigPriority(), binDir, nullptr, mapIncludedFile ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    listProblem.clear();
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_EQUAL( size_t( 2 ), listProblem.size() );

    // 3) 바이너리와 그것을 담은 매니페스트가 있으면 통과다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/bin/dx12/probe_ps_psmain_0.dxil", "DXIL" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/bin/dx12/reflection.manifest", "probe_ps_psmain_0.dxil\n" ) );
    listProblem.clear();
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_TRUE( listProblem.empty() );

    // 3-1) 이 셰이더가 열지 않은 헤더가 바뀐 것은 문제가 아니다. 연 헤더가 바뀐 것은 문제다(소스는 그대로여도).
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/unopened.hlsli", "#define SW_UNOPENED 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    listProblem.clear();
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_TRUE( listProblem.empty() );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probeheader.hlsli", "#define SW_PROBE 22222\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    listProblem.clear();
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_EQUAL( size_t( 1 ), listProblem.size() );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probeheader.hlsli", "#define SW_PROBE 1\n" ) );

    // 4) 매니페스트에 없는 바이너리와 바뀐 소스는 각각 문제다.
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/bin/dx12/probe_ps_psmain_1.dxil", "DXIL" ) );
    SW_ASSERT_TRUE( resourceRoot.writeTextFile( "engine/shaders/probe.hlsl", "float4 PSMain() : SV_Target { return 1; }\n" ) );
    sw::ShaderCooker::invalidateSharedHeaderCache();
    listProblem.clear();
    sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), "dx12", listProblem );
    SW_EXPECT_EQUAL( size_t( 2 ), listProblem.size() );
}

/**
 * @brief [ShaderCookStampTest] 커밋된 셰이더 바이너리가 쿠킹 표의 **모든 백엔드**에서 지금 소스와 맞는다 — 소스 트리는 읽기만 한다
 * @details 병합이 한쪽의 소스와 다른 쪽의 바이너리를 섞으면 한 백엔드만 다른 그림을 내거나 Shipping 이 빈 화면을 낸다. 예전 린트 게이트(`CheckShaderStamps`)가 하던
 *          검사를 같은 판정 함수로 한다. 커밋 훅은 셰이더를 고친 커밋에서 `App --verify-shaders` 로 같은 판정을 부른다.
 */
SW_TEST_CASE( ShaderCookStampTest, CommittedBinariesAreCurrentForEveryBackend )
{
    const sw::string& root = sw::ResourcePaths::getRootFolderPath();
    SW_ASSERT_FALSE( root.empty() );
    for ( const sw::RHIBackendInfo& info : sw::RHIBackendInfoTable::kArrInfo )
    {
        const sw::string_view  shaderFolder = sw::ShaderCooker::getSubfolderForFormat( info._shaderTarget );
        sw::vector<sw::string> listProblem;
        sw::ShaderCooker::verifyCookedShaders( root, test::makeShaderConfigPriority(), shaderFolder, listProblem );
        for ( const sw::string& problem : listProblem )
        {
            SW_EXPECT_TRUE_MSG( false, ( sw::string( shaderFolder ) + ": " + problem + " — App --cook-shaders 로 다시 쿠킹하고 바이너리를 함께 커밋한다" ).c_str() );
        }
    }
}

/**
 * @brief [ShaderCookStampTest] 도구 지문은 상수(컴파일 옵션 판 · Vulkan 타깃 판 · 컴파일러 버전 글)만으로 정해진다 — OS · 파일에 따라 달라지지 않는다
 * @details 쿠킹본은 Windows 에서 만들어 커밋하고 리눅스도 같은 스탬프를 쓴다. 지문이 실행 파일 옆 컴파일러 라이브러리의 내용 해시였을 때는
 *          리눅스에서 모든 스탬프가 낡은 것으로 판정됐다. 같은 상수로 다시 계산한 값과 같아야 하고, 셋 중 하나만 바뀌어도 달라야 한다.
 */
SW_TEST_CASE( ShaderCookStampTest, ToolchainFingerprintComesFromConstantsOnly )
{
    constexpr uint32 kVulkanTargetVersion = sw::constant::kVulkanRequiredMajor * 1000 + sw::constant::kVulkanRequiredMinor;
    const uint64     expected             = sw::ShaderCooker::makeToolchainFingerprint( sw::ShaderCooker::kCompileOptionsRevision, kVulkanTargetVersion, sw::ShaderCooker::kCompilerVersion );
    SW_EXPECT_EQUAL( expected, sw::ShaderCooker::getToolchainFingerprint() );
    SW_EXPECT_TRUE( expected != sw::ShaderCooker::makeToolchainFingerprint( sw::ShaderCooker::kCompileOptionsRevision + 1, kVulkanTargetVersion, sw::ShaderCooker::kCompilerVersion ) );
    SW_EXPECT_TRUE( expected != sw::ShaderCooker::makeToolchainFingerprint( sw::ShaderCooker::kCompileOptionsRevision, kVulkanTargetVersion + 1, sw::ShaderCooker::kCompilerVersion ) );
    SW_EXPECT_TRUE( expected != sw::ShaderCooker::makeToolchainFingerprint( sw::ShaderCooker::kCompileOptionsRevision, kVulkanTargetVersion, "directx-dxc other" ) );
}

/**
 * @brief [ShaderCookerTest] 리플렉션 매니페스트의 바이트는 항목을 넣은 순서와 상관없다
 * @details 쿠커는 요청을 모은 순서대로 매니페스트 맵에 넣고, 맵은 넣은 순서로 돈다. 그 순서로 파일을 쓰면 같은 항목 집합이라도
 *          요청 순서만 바뀐 변경(쿠커가 패스 표를 훑는 순서 등)이 커밋된 매니페스트 넷을 모두 바꿔 놓는다. 키 순서로 써야 같은 바이트다.
 */
SW_TEST_CASE( ShaderCookerTest, ReflectionManifestBytesIgnoreInsertionOrder )
{
    const utf8*      arrKey[]  = { "forwardlit_vs_vsmain_0.dxil", "bloom_ps_psmain_0.dxil", "shadowdepth_vs_vsmain_1.dxil",
                                   "tonemap_ps_psmain_0.dxil", "gbuffer_ps_psmain_7.dxil", "ssao_vs_vsmain_0.dxil" };
    constexpr uint32 kKeyCount = static_cast<uint32>( sizeof( arrKey ) / sizeof( arrKey[0] ) );

    sw::ShaderReflectionLibrary::EntryMap mapForward;
    sw::ShaderReflectionLibrary::EntryMap mapBackward;
    for ( uint32 keyIndex = 0; keyIndex < kKeyCount; ++keyIndex )
    {
        mapForward.emplace( sw::string( arrKey[keyIndex] ), sw::ShaderReflectionData{} );
        mapBackward.emplace( sw::string( arrKey[kKeyCount - 1u - keyIndex] ), sw::ShaderReflectionData{} );
    }

    const sw::string forwardDir  = test::makeTempDirectory( "manifest_forward" );
    const sw::string backwardDir = test::makeTempDirectory( "manifest_backward" );
    SW_ASSERT_TRUE( sw::ShaderReflectionLibrary::save( mapForward, forwardDir ) );
    SW_ASSERT_TRUE( sw::ShaderReflectionLibrary::save( mapBackward, backwardDir ) );

    sw::vector<uint8> forwardBytes;
    sw::vector<uint8> backwardBytes;
    SW_ASSERT_TRUE( sw::FileUtil::readFile( sw::FileUtil::joinPath( forwardDir, sw::ShaderReflectionLibrary::getManifestFileName() ), forwardBytes ) );
    SW_ASSERT_TRUE( sw::FileUtil::readFile( sw::FileUtil::joinPath( backwardDir, sw::ShaderReflectionLibrary::getManifestFileName() ), backwardBytes ) );
    SW_EXPECT_TRUE_MSG( forwardBytes == backwardBytes, "같은 항목인데 넣은 순서가 다르다고 매니페스트 바이트가 달라졌다" );
}

/**
 * @brief [ShaderReflectionCacheTest] 셰이더 캐시를 내리면 리플렉션 매니페스트 캐시도 비는지.
 * @details 매니페스트 캐시는 프로세스 정적 저장소라 엔진 종료 뒤에도 남는다. 백엔드를 바꾼 실행은 기동 기준선 뒤에 두 번째 RHI 의
 *          매니페스트(~1.1 MB)를 읽으므로, 종료가 비우지 않으면 메모리 누수 검사(`MemoryProfiler::reportMemoryLeaks`)가 그것을 잡는다.
 *          없는 소스를 물어도 그 RHI 폴더의 매니페스트 칸은 (빈 채로) 캐시된다 — 그것으로 캐시를 채운다.
 */
SW_TEST_CASE( ShaderReflectionCacheTest, ShaderCacheShutdownClearsManifestCache )
{
    sw::ShaderCompileDesc desc;
    desc._filePath     = "engine/shaders/reflection_cache_probe_missing.hlsl";
    desc._entryPoint   = "VSMain";
    desc._stage        = sw::ShaderStage::Vertex;
    desc._targetFormat = sw::ShaderTargetFormat::DXIL_D3D12;
    sw::ShaderReflectionData reflection;
    SW_EXPECT_FALSE( sw::ShaderReflectionLibrary::tryGet( desc, reflection ) );
    SW_ASSERT_TRUE( sw::ShaderReflectionLibrary::getCachedManifestCount() > 0u );

    const uint32    generationBefore = sw::ShaderReflectionLibrary::getCacheGeneration();
    sw::ShaderCache cache;
    SW_ASSERT_TRUE( cache.initialize() );
    cache.shutdown();

    SW_EXPECT_EQUAL( 0u, sw::ShaderReflectionLibrary::getCachedManifestCount() );
    SW_EXPECT_TRUE( sw::ShaderReflectionLibrary::getCacheGeneration() != generationBefore );
}

/**
 * @brief [ShaderStageTest] 스테이지 표는 빠진 줄이 없고, 태그 · 진입점 · 프로파일이 서로 맞는다
 */
SW_TEST_CASE( ShaderStageTest, StageInfoTableIsComplete )
{
    for ( uint8 stageIndex = 0; stageIndex < static_cast<uint8>( sw::ShaderStage::Count ); ++stageIndex )
    {
        const sw::ShaderStage      stage = static_cast<sw::ShaderStage>( stageIndex );
        const sw::ShaderStageInfo& info  = sw::getShaderStageInfo( stage );
        SW_ASSERT_TRUE( info._pTag != nullptr && info._pEntryPoint != nullptr && info._pProfileSm6 != nullptr );
        SW_ASSERT_TRUE( sw::string_view{ info._pTag }.size() == 2 );

        // 진입점은 태그의 대문자 + "Main", 프로파일은 태그 + "_5_0" / "_6_6" — 표의 줄이 서로 어긋나면 여기서 잡힌다.
        sw::string expectedEntry;
        expectedEntry += static_cast<utf8>( info._pTag[0] - 'a' + 'A' );
        expectedEntry += static_cast<utf8>( info._pTag[1] - 'a' + 'A' );
        expectedEntry += "Main";
        SW_EXPECT_EQUAL( expectedEntry, sw::string{ info._pEntryPoint } );
        SW_EXPECT_EQUAL( sw::string{ info._pTag } + "_6_6", sw::string{ info._pProfileSm6 } );
        if ( info._pProfileSm5 != nullptr )
            SW_EXPECT_EQUAL( sw::string{ info._pTag } + "_5_0", sw::string{ info._pProfileSm5 } );
    }

    // SM5 에 없는 두 스테이지, 그리고 범위 밖은 버텍스 줄이다
    SW_EXPECT_TRUE( sw::getShaderStageInfo( sw::ShaderStage::Mesh )._pProfileSm5 == nullptr );
    SW_EXPECT_TRUE( sw::getShaderStageInfo( sw::ShaderStage::Amplification )._pProfileSm5 == nullptr );
    SW_EXPECT_TRUE( sw::getShaderStageInfo( sw::ShaderStage::Vertex )._pProfileSm5 != nullptr );
    SW_EXPECT_EQUAL( sw::string{ "VSMain" }, sw::string{ sw::getShaderStageInfo( sw::ShaderStage::Count )._pEntryPoint } );
}

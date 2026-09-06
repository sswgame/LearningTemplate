/**
 * @file TestShaderBindingContract.cpp
 * @brief 바인딩 계약 검증기 — 합성 위반을 잡는지, 그리고 구운 바이너리 전부가 계약과 맞는지.
 * @details GPU 가 필요 없다 (바이트코드 리플렉션만). 그래서 nogpu 라벨의 EngineTest_NoGPU 에 포함된다 —
 *          셰이더/헤더/백엔드 상수 어느 쪽이 어긋나도 CI 에서 이름과 숫자로 실패한다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Graphics/Shader/ShaderBaker.h"
#include "Engine/Graphics/Shader/ShaderBindingContract.h"
#include "Engine/Graphics/Shader/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace
{
    sw::ShaderBufferInfo makeCb( const utf8* pName, uint32 space, uint32 bindPoint )
    {
        sw::ShaderBufferInfo cb{};
        cb._name          = pName;
        cb._registerSpace = space;
        cb._bindPoint     = bindPoint;
        return cb;
    }

    sw::ShaderResourceBinding makeRes( const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint )
    {
        sw::ShaderResourceBinding res{};
        res._name          = pName;
        res._type          = pType;
        res._registerSpace = space;
        res._bindPoint     = bindPoint;
        res._bindCount     = 1;
        return res;
    }
} // namespace

/**
 * @brief 계약과 맞는 리플렉션은 위반 0, 어긋난 것은 각각의 규칙에 걸린다.
 */
SW_TEST_CASE( ShaderBindingContractTest, SyntheticViolationsAreDetected )
{
    SW_TEST_SUPPRESS_LOGS();
    sw::vector<sw::ShaderBindingContractIssue> listIssue;

    // 1) GL: 계약대로 — PassCB binding 0, MaterialCB binding 1, 인스턴스 SSBO 4, 텍스처 유닛 0..3/5..8
    {
        sw::ShaderReflectionData ok{};
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, sw::shaderslot::kPassConstantBuffer ) );
        ok._listConstantBuffer.push_back( makeCb( "MaterialCB", 0, sw::shaderslot::kMaterialConstantBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwSlot0", "TextureOrSampler", 0, sw::shaderslot::kEngineTexture0 ) );
        ok._listResource.push_back( makeRes( "g_SwMaterialTex0", "TextureOrSampler", 0, sw::shaderslot::kMaterialTexture0 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingContract::validate( ok, sw::ShaderTargetFormat::SPIRV_OpenGL, "ok.gl", &listIssue ) );
    }

    // 2) GL: MaterialCB 가 set 10 binding 0 — GL 은 set 을 버리므로 PassCB(binding 0) 와 충돌. (실제로 났던 사고)
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "MaterialCB", 10, 0 ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_OpenGL, "bad.gl", &listIssue );
        SW_EXPECT_TRUE_MSG( count >= 2, "위치 불일치 + set!=0 + 충돌 중 최소 둘은 잡혀야 한다" );
        bool bCollision{ false };
        for ( const sw::ShaderBindingContractIssue& issue : listIssue )
            bCollision |= ( issue._message.find( "같은 자리" ) != sw::string::npos );
        SW_EXPECT_TRUE_MSG( bCollision, "UBO binding 0 충돌이 보고돼야 한다" );
    }

    // 3) Vulkan: MaterialCB 가 set 0 binding 1 — 파이프라인 레이아웃은 set 10 을 기대한다.
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "MaterialCB", 0, 1 ) );
        listIssue.clear();
        SW_EXPECT_TRUE( sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad.vk", &listIssue ) >= 1 );
    }

    // 4) GL: 인스턴스 구조버퍼가 상수버퍼로 분류됨 — SPIR-V 1.3 BufferBlock 오분류 사고의 재현.
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "g_SwInstances", 0, sw::shaderslot::kInstanceBuffer ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_OpenGL, "bad2.gl", &listIssue );
        SW_EXPECT_TRUE( count >= 1 );
        bool bKind{ false };
        for ( const sw::ShaderBindingContractIssue& issue : listIssue )
            bKind |= ( issue._message.find( "종류" ) != sw::string::npos );
        SW_EXPECT_TRUE_MSG( bKind, "종류 불일치가 보고돼야 한다" );
    }

    // 5) Vulkan: 레이아웃 밖 세트 참조 / 세트 타입 불일치
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "g_Foo", "StorageBuffer", sw::shaderslot::vk::kBoundSetCount + 3, 0 ) );
        bad._listResource.push_back( makeRes( "g_Bar", "StorageBuffer", sw::shaderslot::vk::kSetBindlessTexture, 0 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 2u, sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad2.vk", &listIssue ) );
    }

    // 6) DX11: 계약대로 (텍스처+샘플러 짝, 인스턴스 t4)
    {
        sw::ShaderReflectionData ok{};
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSlot0", "Texture", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSlot0Sampler", "Sampler", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StructuredBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingContract::validate( ok, sw::ShaderTargetFormat::DXBC_D3D11, "ok.dx11", &listIssue ) );
    }
}

/**
 * @brief 리포지토리에 구운 4백엔드 바이너리 전부가 계약과 맞는다.
 * @details 셰이더를 고치고 리베이크하지 않았거나, 헤더 매크로/백엔드 상수를 한쪽만 바꾸면 여기서 실패한다.
 */
SW_TEST_CASE( ShaderBindingContractTest, AllBakedShadersMatchContract )
{
    sw::ResourceUtil::initialize();

    struct Target
    {
        sw::ShaderTargetFormat _format;
    };
    const Target arrTarget[] = {
        { sw::ShaderTargetFormat::DXBC_D3D11 },
        { sw::ShaderTargetFormat::DXIL_D3D12 },
        { sw::ShaderTargetFormat::SPIRV_Vulkan },
        { sw::ShaderTargetFormat::SPIRV_OpenGL },
    };
    const utf8* arrDomain[] = { "engine", "common" };

    uint32 checkedCount{ 0 };
    uint32 violationCount{ 0 };
    for ( const utf8* pDomain : arrDomain )
    {
        const sw::string shaderDir = sw::ResourceUtil::getDomainFolderPath( pDomain, "shaders" );
        if ( shaderDir.empty() )
            continue;
        for ( const Target& target : arrTarget )
        {
            const sw::string binDir = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shaderDir, "bin" ),
                                                              sw::string( sw::ShaderBaker::getSubfolderForFormat( target._format ) ) );
            if ( sw::FileUtil::directoryExists( binDir ) == false )
                continue;
            sw::vector<sw::string> listFile;
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderBaker::getExtensionForFormat( target._format ) ), listFile, false );
            for ( const sw::string& path : listFile )
            {
                sw::vector<uint8> bytecode;
                if ( sw::FileUtil::readFile( path, bytecode ) == false || bytecode.empty() )
                    continue;
                const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( bytecode, target._format );
                if ( reflection._listConstantBuffer.empty() && reflection._listResource.empty() )
                    continue; // 리플렉션 불가(예: 이 플랫폼에 컴파일러 DLL 없음) — 검사 대상이 아니다
                sw::vector<sw::ShaderBindingContractIssue> listIssue;
                const uint32                               issueCount = sw::ShaderBindingContract::validate( reflection, target._format, path, &listIssue );
                for ( const sw::ShaderBindingContractIssue& issue : listIssue )
                    SW_LOG_WARNING( "%# — %#: %#", path.c_str(), issue._resource.c_str(), issue._message.c_str() );
                violationCount += issueCount;
                ++checkedCount;
            }
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "구운 셰이더 바이너리를 찾지 못했습니다 (App.exe --bake-shaders 필요)" );
    SW_EXPECT_TRUE_MSG( checkedCount >= 8, "네 백엔드 × 엔진 셰이더가 있어야 한다" );
    SW_EXPECT_EQUAL( 0u, violationCount );
}

/**
 * @brief 계약 표 자체의 일관성 — 이름이 비지 않고 겹치지 않는다. 자리 충돌은 백엔드별로 뜻이 달라
 *        (DX11 은 g_SwSlot0Sampler=s0, DX12 는 g_SwSamplerLinearWrap=s0 처럼 서로 다른 셰이더에 산다) 여기서
 *        따지지 않고 AllBakedShadersMatchContract 가 실제 바이너리로 잡는다.
 */
SW_TEST_CASE( ShaderBindingContractTest, ReservedTableIsConsistent )
{
    const sw::vector<sw::ShaderReservedBinding>& list = sw::ShaderBindingContract::getReservedBindings();
    SW_EXPECT_TRUE( list.size() >= 8 );
    for ( size_t indexA = 0; indexA < list.size(); ++indexA )
    {
        SW_EXPECT_TRUE( list[indexA]._name != nullptr && list[indexA]._name[0] != '\0' );
        for ( size_t indexB = indexA + 1; indexB < list.size(); ++indexB )
            SW_EXPECT_TRUE_MSG( sw::string( list[indexA]._name ) != list[indexB]._name, list[indexA]._name );
    }
}

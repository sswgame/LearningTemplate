/**
 * @file TestShaderBindingContract.cpp
 * @brief 바인딩 계약 검증기 — 합성 위반을 잡는지, 그리고 구운 바이너리 전부가 계약과 맞는지.
 * @details GPU 가 필요 없다 (바이트코드 리플렉션만). 그래서 nogpu 라벨의 EngineTest_NoGPU 에 포함된다 —
 *          셰이더/헤더/백엔드 상수 어느 쪽이 어긋나도 CI 에서 이름과 숫자로 실패한다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Renderer/Scene/GpuScene.h"
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

    sw::ShaderResourceBinding makeRes( const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint, uint32 bindCount = 1 )
    {
        sw::ShaderResourceBinding res{};
        res._name          = pName;
        res._type          = pType;
        res._registerSpace = space;
        res._bindPoint     = bindPoint;
        res._bindCount     = bindCount;
        return res;
    }

    bool hasIssueContaining( const sw::vector<sw::ShaderBindingContractIssue>& listIssue, const utf8* pText )
    {
        for ( const sw::ShaderBindingContractIssue& issue : listIssue )
        {
            if ( issue._message.find( pText ) != sw::string::npos )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief 계약과 맞는 리플렉션은 위반 0, 어긋난 것은 각각의 규칙에 걸린다.
 */
SW_TEST_CASE( ShaderBindingContractTest, SyntheticViolationsAreDetected )
{
    SW_TEST_SUPPRESS_LOGS();
    namespace vk       = sw::shaderslot::vk;
    namespace bindless = sw::shaderslot::bindless;
    sw::vector<sw::ShaderBindingContractIssue> listIssue;

    // 1) GL: 계약대로 — PassCB binding 0, MaterialCB binding 1, 인스턴스 SSBO 4, 머티리얼 SSBO 9, 텍스처 유닛 0..3/5..8
    {
        sw::ShaderReflectionData ok{};
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, sw::shaderslot::kPassConstantBuffer ) );
        ok._listConstantBuffer.push_back( makeCb( "MaterialCB", 0, sw::shaderslot::kMaterialConstantBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StorageBuffer", 0, sw::shaderslot::kMaterialBuffer ) );
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
        SW_EXPECT_TRUE_MSG( hasIssueContaining( listIssue, "같은 자리" ), "UBO binding 0 충돌이 보고돼야 한다" );
    }

    // 3) Vulkan: 계약대로 — 세트 0 은 b/t/u 밴드(0/16/32 시프트), 세트 1 은 텍스처 배열(무제한)과 정적 샘플러.
    {
        sw::ShaderReflectionData ok{};
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, vk::kBShift + sw::shaderslot::kPassConstantBuffer ) );
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, vk::kBShift + sw::shaderslot::kPassConstantBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 0, vk::kTShift + sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StorageBuffer", 0, vk::kTShift + sw::shaderslot::kMaterialBuffer ) );
        ok._listResource.push_back( makeRes( "g_IndirectArgs", "StorageBuffer", 0, vk::kUShift + 0 ) );
        ok._listResource.push_back( makeRes( "g_SwBindlessTex2D", "TextureOrSampler", bindless::kVkTextureSet, bindless::kVkTextureBinding, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSamplerLinearWrap", "Sampler", bindless::kVkTextureSet, bindless::kVkSamplerBinding ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingContract::validate( ok, sw::ShaderTargetFormat::SPIRV_Vulkan, "ok.vk", &listIssue ) );
    }

    // 4) Vulkan: 옛 모델(PassCB set 0 / MaterialCB set 10 / 인스턴스 set 6) — 위치 불일치 + 레이아웃 밖 세트가 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        bad._listResource.push_back( makeRes( "MaterialCB", "ConstantBuffer", 10, 0 ) );
        bad._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 6, 0 ) );
        listIssue.clear();
        SW_EXPECT_TRUE( sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad.vk", &listIssue ) >= 4 );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "위치가 계약과" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "레이아웃에 없는 descriptor set" ) );
    }

    // 5) Vulkan: 밴드 밖 binding / 밴드 종류 불일치(UBO 밴드에 SSBO) / 세트 1 오용 — 세 규칙이 각각 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "g_Foo", "StorageBuffer", 0, vk::kSlotBindingCount + 3 ) ); // 밴드 밖
        bad._listResource.push_back( makeRes( "g_Bar", "StorageBuffer", 0, vk::kBShift + 2 ) );           // b 밴드에 SSBO
        bad._listResource.push_back( makeRes( "g_Baz", "StorageBuffer", bindless::kVkTextureSet, 0 ) );   // 세트 1 에 버퍼
        listIssue.clear();
        SW_EXPECT_EQUAL( 3u, sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad2.vk", &listIssue ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "밴드 밖" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "밴드인데" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "세트 1" ) );
    }

    // 6) GL: 인스턴스 구조버퍼가 상수버퍼로 분류됨 — SPIR-V 1.3 BufferBlock 오분류 사고의 재현.
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "g_SwInstances", 0, sw::shaderslot::kInstanceBuffer ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::SPIRV_OpenGL, "bad2.gl", &listIssue );
        SW_EXPECT_TRUE( count >= 1 );
        SW_EXPECT_TRUE_MSG( hasIssueContaining( listIssue, "종류" ), "종류 불일치가 보고돼야 한다" );
    }

    // 7) DX11: 계약대로 (텍스처+샘플러 짝, 인스턴스 t4, 머티리얼 t9)
    {
        sw::ShaderReflectionData ok{};
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSlot0", "Texture", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSlot0Sampler", "Sampler", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StructuredBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StructuredBuffer", 0, sw::shaderslot::kMaterialBuffer ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingContract::validate( ok, sw::ShaderTargetFormat::DXBC_D3D11, "ok.dx11", &listIssue ) );
    }

    // 8) DX12: 계약대로 — 슬롯은 space0, 텍스처 배열은 t0 space1 무제한, 정적 샘플러 s0.
    {
        sw::ShaderReflectionData ok{};
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StructuredBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StructuredBuffer", 0, sw::shaderslot::kMaterialBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwBindlessTex2D", "Texture", bindless::kTextureSpace, 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSamplerLinearWrap", "Sampler", 0, 0 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingContract::validate( ok, sw::ShaderTargetFormat::DXIL_D3D12, "ok.dx12", &listIssue ) );
    }

    // 9) DX12: 에뮬 슬롯 선언(g_SwSlot0) / 루트 시그니처 슬롯 수 초과(t12) / 없는 space(7) — 각각 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        bad._listResource.push_back( makeRes( "g_SwSlot0", "Texture", 0, 0 ) );
        bad._listResource.push_back( makeRes( "g_Big", "StructuredBuffer", 0, sw::shaderslot::kSrvSlotCount + 2 ) );
        bad._listResource.push_back( makeRes( "g_Elsewhere", "StructuredBuffer", 7, 0 ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingContract::validate( bad, sw::ShaderTargetFormat::DXIL_D3D12, "bad.dx12", &listIssue );
        SW_EXPECT_TRUE( count >= 3 );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "없는 예약 리소스" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "슬롯 수" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "없는 register space" ) );
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
 * @brief 구운 바이너리의 리플렉션이 네 백엔드에서 **같은 레이아웃**을 준다 — PassCB 멤버(이름·오프셋), 그리고 머티리얼
 *        데이터 구조버퍼(g_SwMaterials)의 원소 레이아웃(이름·오프셋·크기·stride).
 * @details 엔진은 머티리얼 바이트를 리플렉션 하나로 패킹해 네 백엔드에 그대로 올린다. SPIR-V 를 std430 으로 구우면
 *          float3 정렬과 struct stride 가 DX 자연 패킹과 달라져 원소 1 부터 어긋난다 — 그래서 ShaderCompiler 가
 *          -fvk-use-dx-layout 으로 굽고, 이 테스트가 같은 셰이더의 네 바이너리를 비교해 어긋남을 이름과 숫자로 보고한다.
 */
SW_TEST_CASE( ShaderBindingContractTest, ReflectionNamesAreUniformAcrossBackends )
{
    sw::ResourceUtil::initialize();
    const sw::string shaderDir = sw::ResourceUtil::getDomainFolderPath( "engine", "shaders" );
    if ( shaderDir.empty() )
        SW_TEST_SKIP( "engine/shaders 를 찾지 못했습니다" );

    constexpr uint32             kFormatCount            = 4;
    const sw::ShaderTargetFormat arrFormat[kFormatCount] = {
        sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12, sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };
    const utf8* arrFormatName[kFormatCount] = { "dx11", "dx12", "vulkan", "opengl" };

    struct PerFormat
    {
        bool                               bFound{ false };
        bool                               bHasPass{ false };
        bool                               bHasMaterial{ false };
        sw::string                         passMembers;
        sw::vector<sw::ShaderVariableInfo> listElement;
        uint32                             stride{ 0 };
    };
    sw::unordered_map<sw::string, sw::vector<PerFormat>> mapShader;

    for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
    {
        const sw::ShaderTargetFormat format = arrFormat[formatIndex];
        const sw::string             binDir = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shaderDir, "bin" ),
                                                                      sw::string( sw::ShaderBaker::getSubfolderForFormat( format ) ) );
        sw::vector<sw::string>       listFile;
        if ( sw::FileUtil::directoryExists( binDir ) )
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderBaker::getExtensionForFormat( format ) ), listFile, false );
        for ( const sw::string& path : listFile )
        {
            sw::vector<uint8> bytecode;
            if ( sw::FileUtil::readFile( path, bytecode ) == false || bytecode.empty() )
                continue;
            sw::string   stem = sw::FileUtil::getFileNamePart( path );
            const size_t dot  = stem.rfind( '.' );
            if ( dot != sw::string::npos )
                stem = stem.substr( 0, dot );

            const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( bytecode, format );
            sw::vector<PerFormat>&         listPer    = mapShader[stem];
            if ( listPer.size() != kFormatCount )
                listPer.resize( kFormatCount );
            PerFormat& per = listPer[formatIndex];
            per.bFound     = true;
            for ( const sw::ShaderBufferInfo& cb : reflection._listConstantBuffer )
            {
                if ( cb._name != sw::shaderslot::cbname::kPass )
                    continue;
                per.bHasPass = true;
                for ( const sw::ShaderVariableInfo& var : cb._listVariable )
                    per.passMembers += var._name + "@" + sw::to_string( var._offset ) + ";";
            }
            for ( const sw::ShaderBufferInfo& element : reflection._listStructuredElement )
            {
                if ( element._name != sw::shaderslot::resname::kMaterials )
                    continue;
                per.bHasMaterial = true;
                per.listElement  = element._listVariable;
                per.stride       = element._totalSize;
            }
        }
    }

    uint32 comparedCount{ 0 };
    bool   bForwardLitChecked{ false };
    for ( const auto& [stem, listPer] : mapShader )
    {
        const PerFormat* pRef{ nullptr };
        uint32           refIndex{ 0 };
        for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
        {
            if ( listPer[formatIndex].bFound )
            {
                pRef     = &listPer[formatIndex];
                refIndex = formatIndex;
                break;
            }
        }
        if ( pRef == nullptr )
            continue;
        for ( uint32 formatIndex = refIndex + 1; formatIndex < kFormatCount; ++formatIndex )
        {
            const PerFormat& per = listPer[formatIndex];
            if ( per.bFound == false )
                continue;
            ++comparedCount;
            const sw::string label = stem + " (" + arrFormatName[refIndex] + " vs " + arrFormatName[formatIndex] + ")";
            // 유무는 비교하지 않는다 — FXC 는 안 쓰는 cbuffer/리소스를 리플렉션에서 빼고 DXC SPIR-V 는 남긴다. 둘 다 있을 때 레이아웃만 본다.
            if ( per.bHasPass && pRef->bHasPass )
                SW_EXPECT_TRUE_MSG( per.passMembers == pRef->passMembers, ( label + " PassCB: " + pRef->passMembers + " != " + per.passMembers ).c_str() );
            if ( per.bHasMaterial == false || pRef->bHasMaterial == false )
                continue;
            SW_EXPECT_TRUE_MSG( per.stride == pRef->stride, ( label + " g_SwMaterials stride " + sw::to_string( pRef->stride ) + " != " + sw::to_string( per.stride ) ).c_str() );
            SW_EXPECT_TRUE_MSG( per.listElement.size() == pRef->listElement.size(), ( label + " g_SwMaterials 멤버 수" ).c_str() );
            const size_t count = sw::MathUtil::min( per.listElement.size(), pRef->listElement.size() );
            for ( size_t varIndex = 0; varIndex < count; ++varIndex )
            {
                const sw::ShaderVariableInfo& a     = pRef->listElement[varIndex];
                const sw::ShaderVariableInfo& b     = per.listElement[varIndex];
                const bool                    bSame = ( a._name == b._name && a._offset == b._offset && a._size == b._size );
                SW_EXPECT_TRUE_MSG( bSame, ( label + " g_SwMaterials." + a._name + " " + sw::to_string( a._offset ) + "/" + sw::to_string( a._size ) +
                                             " != " + b._name + " " + sw::to_string( b._offset ) + "/" + sw::to_string( b._size ) )
                                               .c_str() );
            }
        }
        if ( stem == "forwardlit_ps" )
        {
            // 네 백엔드 모두 원소 레이아웃을 내야 한다 — DX11 도 FXC 의 RESOURCE_BIND_INFO 로 낸다. 하나라도 빠지면 그 백엔드의
            // Material stride 가 0 이 되어 버퍼가 CB 크기 stride 로 만들어진다.
            for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
            {
                if ( listPer[formatIndex].bFound )
                    SW_EXPECT_TRUE_MSG( listPer[formatIndex].bHasMaterial, ( sw::string( "forwardlit_ps g_SwMaterials 원소 없음: " ) + arrFormatName[formatIndex] ).c_str() );
            }
        }
        if ( stem == "forwardlit_ps" && pRef->bHasMaterial )
        {
            bForwardLitChecked = true;
            bool bHasColor{ false };
            bool bHasAlbedo{ false };
            for ( const sw::ShaderVariableInfo& var : pRef->listElement )
            {
                bHasColor |= ( var._name == "color" && var._size == 16 && var._offset == 0 );
                bHasAlbedo |= ( var._name == "albedoMap" && var._size == 4 );
            }
            SW_EXPECT_TRUE( bHasColor && bHasAlbedo );
            SW_EXPECT_TRUE( pRef->stride > 0 );
        }
    }
    if ( comparedCount == 0 )
        SW_TEST_SKIP( "같은 셰이더의 바이너리를 둘 이상 찾지 못했습니다 (App.exe --bake-shaders 필요)" );
    SW_EXPECT_TRUE_MSG( bForwardLitChecked, "forwardlit_ps 의 g_SwMaterials 원소를 찾지 못했다" );
}

/**
 * @brief 계약 표 자체의 일관성 — 이름이 비지 않고 겹치지 않는다. 자리 충돌은 백엔드별로 뜻이 달라
 *        (DX11 은 g_SwSlot0Sampler=s0, DX12 는 g_SwSamplerLinearWrap=s0 처럼 서로 다른 셰이더에 산다) 여기서
 *        따지지 않고 AllBakedShadersMatchContract 가 실제 바이너리로 잡는다.
 */
/**
 * @brief [ShaderBindingContractTest] DX12 루트 시그니처 예산 — 슬롯 수를 늘려도 64 dword 안에 있어야 한다.
 * @details 루트 배치는 계약(shaderslot::dx12)에서 나온다: CB 는 루트 CBV(2 dword), t/u 슬롯과 텍스처 배열은 테이블(1 dword),
 *          루트 상수는 dword 수. 예전엔 t/u 도 루트 디스크립터라 51 이었고, 슬롯 하나가 2 dword 씩 예산을 먹었다.
 *          이 테스트는 "슬롯을 늘리면 예산이 느는가" 를 숫자로 고정한다 — 테이블 안의 슬롯 수는 예산에 들지 않는다.
 */
SW_TEST_CASE( ShaderBindingContractTest, Dx12RootSignatureFitsBudget )
{
    namespace dx12 = sw::shaderslot::dx12;
    SW_EXPECT_TRUE( dx12::kRootSignatureDwords <= dx12::kRootBudgetDwords );
    // t 슬롯을 두 배로 늘려도 테이블이라 예산은 그대로다 (루트 디스크립터였다면 +20 dword).
    const uint32 withDoubledSrvSlots = dx12::kRootCbvCount * dx12::kRootDescriptorDwords + dx12::kRootTableCount * dx12::kRootTableDwords + sw::shaderslot::kRootConstantDwords;
    SW_EXPECT_EQUAL( dx12::kRootSignatureDwords, withDoubledSrvSlots );
    // 루트 상수(16) + CBV 셋(6) + 테이블 셋(3) = 25 — 계약 값이 바뀌면 여기서 먼저 걸린다.
    SW_EXPECT_EQUAL( 3u * 2u + 3u * 1u + 16u, dx12::kRootSignatureDwords );
}

/**
 * @brief [ShaderBindingContractTest] GPUScene 인스턴스 원소 레이아웃이 C++ `GpuInstance` 와 같다 (구운 바이너리, 4 백엔드).
 * @details `g_SwInstances`(t4)는 **C++ 이 쓰고 셰이더가 읽는** 유일한 구조체다 — 한쪽만 바뀌면 컴파일도 검증 레이어도
 *          아무 말을 하지 않고 월드 행렬·머티리얼 인덱스가 원소 1 부터 어긋난다(머티리얼 버퍼가 stride 0 으로 그랬던 것과 같은 함정).
 *          그래서 stride 와 필드 오프셋을 구운 바이너리의 리플렉션에서 읽어 C++ 구조체와 대조한다. GPU 가 필요 없다.
 */
SW_TEST_CASE( ShaderBindingContractTest, InstanceElementLayoutMatchesCpuStruct )
{
    sw::ResourceUtil::initialize();
    const sw::string shaderDir = sw::ResourceUtil::getDomainFolderPath( "engine", "shaders" );
    if ( shaderDir.empty() )
        SW_TEST_SKIP( "engine/shaders 를 찾지 못했습니다" );

    struct ExpectedField
    {
        const utf8* _pName;
        uint32      _offset;
    };
    // HLSL `SwInstanceData`(binding.hlsli) ↔ C++ `GpuInstance`(GpuScene.h). 이름은 셰이더 쪽 표기다.
    const ExpectedField arrExpected[] = {
        {         "world", static_cast<uint32>( offsetof( sw::GpuInstance,          _world ) )},
        {  "boundsCenter", static_cast<uint32>( offsetof( sw::GpuInstance,   _boundsCenter ) )},
        {  "boundsRadius", static_cast<uint32>( offsetof( sw::GpuInstance,   _boundsRadius ) )},
        {"meshBatchIndex", static_cast<uint32>( offsetof( sw::GpuInstance, _meshBatchIndex ) )},
        { "materialIndex", static_cast<uint32>( offsetof( sw::GpuInstance,  _materialIndex ) )},
        {     "blendMode", static_cast<uint32>( offsetof( sw::GpuInstance,      _blendMode ) )},
    };

    constexpr uint32             kFormatCount            = 4;
    const sw::ShaderTargetFormat arrFormat[kFormatCount] = {
        sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12, sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };
    const utf8* arrFormatName[kFormatCount] = { "dx11", "dx12", "vulkan", "opengl" };

    uint32 checkedCount{ 0 };
    for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
    {
        const sw::ShaderTargetFormat format = arrFormat[formatIndex];
        const sw::string             binDir = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shaderDir, "bin" ),
                                                                      sw::string( sw::ShaderBaker::getSubfolderForFormat( format ) ) );
        sw::vector<sw::string>       listFile;
        if ( sw::FileUtil::directoryExists( binDir ) )
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderBaker::getExtensionForFormat( format ) ), listFile, false );

        for ( const sw::string& path : listFile )
        {
            sw::vector<uint8> bytecode;
            if ( sw::FileUtil::readFile( path, bytecode ) == false || bytecode.empty() )
                continue;

            const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( bytecode, format );
            for ( const sw::ShaderBufferInfo& element : reflection._listStructuredElement )
            {
                if ( element._name != sw::shaderslot::resname::kInstances )
                    continue;

                const sw::string label = sw::string( arrFormatName[formatIndex] ) + "/" + sw::FileUtil::getFileNamePart( path );
                SW_EXPECT_TRUE_MSG( element._totalSize == static_cast<uint32>( sizeof( sw::GpuInstance ) ),
                                    ( label + " g_SwInstances stride " + sw::to_string( element._totalSize ) + " != sizeof(GpuInstance) " +
                                      sw::to_string( static_cast<uint32>( sizeof( sw::GpuInstance ) ) ) )
                                        .c_str() );

                for ( const ExpectedField& expected : arrExpected )
                {
                    const sw::ShaderVariableInfo* pFound = nullptr;
                    for ( const sw::ShaderVariableInfo& var : element._listVariable )
                    {
                        if ( var._name == expected._pName )
                        {
                            pFound = &var;
                            break;
                        }
                    }
                    SW_EXPECT_TRUE_MSG( pFound != nullptr, ( label + " g_SwInstances 에 " + expected._pName + " 가 없습니다" ).c_str() );
                    if ( pFound == nullptr )
                        continue;
                    SW_EXPECT_TRUE_MSG( pFound->_offset == expected._offset,
                                        ( label + " g_SwInstances." + expected._pName + " 오프셋 " + sw::to_string( pFound->_offset ) +
                                          " != C++ " + sw::to_string( expected._offset ) )
                                            .c_str() );
                }
                ++checkedCount;
            }
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "g_SwInstances 를 선언한 구운 셰이더가 없습니다 (--bake-shaders 를 먼저 돌리세요)" );
}

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

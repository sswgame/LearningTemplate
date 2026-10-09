/**
 * @file TestShaderBindingValidator.cpp
 * @brief 바인딩 계약 검증기 — 합성 위반을 잡는지, 그리고 쿠킹된 바이너리 전부가 계약과 맞는지.
 * @details GPU 가 필요 없다 (바이트코드 리플렉션만). 그래서 nogpu 라벨의 EngineTest_NoGPU 에 포함된다 —
 *          셰이더/헤더/백엔드 상수 어느 쪽이 어긋나도 CI 에서 이름과 숫자로 실패한다.
 */
#include "pch.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Graphics/RHI/Support/RHIShaderRequest.h"
#include "Engine/Graphics/Shader/Binding/GpuSpriteInstanceData.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidator.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Compile/ShaderCooker.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionLibrary.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Scene/GpuSceneSnapshot.h"
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

    sw::ShaderReflectedBinding makeRes( const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint, uint32 bindCount = 1 )
    {
        sw::ShaderReflectedBinding res{};
        res._name          = pName;
        res._type          = pType;
        res._registerSpace = space;
        res._bindPoint     = bindPoint;
        res._bindCount     = bindCount;
        return res;
    }

    sw::ShaderVertexInputInfo makeVertexInput( const utf8* pSemantic, uint32 location, uint32 semanticIndex = 0 )
    {
        sw::ShaderVertexInputInfo input{};
        input._semantic      = pSemantic;
        input._semanticIndex = semanticIndex;
        input._location      = location;
        return input;
    }

    bool hasIssueContaining( const sw::vector<sw::ShaderBindingValidatorIssue>& listIssue, const utf8* pText )
    {
        for ( const sw::ShaderBindingValidatorIssue& issue : listIssue )
        {
            if ( issue._message.find( pText ) != sw::string::npos )
                return true;
        }
        return false;
    }

    /// @brief 쿠킹된 리플렉션 매니페스트에 나오는 이름들 — 바인딩(cbuffer · 리소스 · 구조버퍼 원소) 이름과 cbuffer 멤버 이름.
    struct CookedNameSet
    {
        sw::unordered_set<sw::string> _uniqueBindingName;
        sw::unordered_set<sw::string> _uniqueMemberName;
        uint32                        _entryCount{ 0 };
    };

    void collectCookedNames( const sw::ShaderReflectionLibrary::EntryMap& mapEntry, CookedNameSet& outNameSet )
    {
        for ( const auto& entry : mapEntry )
        {
            const sw::ShaderReflectionData& reflection = entry.second;
            ++outNameSet._entryCount;
            for ( const sw::ShaderBufferInfo& constantBuffer : reflection._listConstantBuffer )
            {
                outNameSet._uniqueBindingName.insert( constantBuffer._name );
                for ( const sw::ShaderVariableInfo& variable : constantBuffer._listVariable )
                {
                    outNameSet._uniqueMemberName.insert( variable._name );
                }
            }
            for ( const sw::ShaderReflectedBinding& resource : reflection._listResource )
            {
                outNameSet._uniqueBindingName.insert( resource._name );
            }
            for ( const sw::ShaderBufferInfo& element : reflection._listStructuredElement )
            {
                outNameSet._uniqueBindingName.insert( element._name );
            }
        }
    }

    bool hasName( const sw::unordered_set<sw::string>& uniqueName, const sw::string& name )
    {
        return uniqueName.find( name ) != uniqueName.end();
    }

    /// @brief 이름의 숫자 자리를 `#` 로 바꾼 묶음 열쇠입니다. `g_SwSampler3` 과 `g_SwSampler0` 은 같은 묶음(`g_SwSampler#`)입니다.
    sw::string makeNumberedFamilyKey( const sw::string& name )
    {
        sw::string key;
        for ( const utf8 character : name )
        {
            const bool bDigit = ( '0' <= character && character <= '9' );
            if ( bDigit == false )
                key += character;
            else if ( key.empty() || key.back() != '#' )
                key += '#';
        }
        return key;
    }

    bool hasNumberedFamilyMember( const sw::unordered_set<sw::string>& uniqueName, const sw::string& name )
    {
        const sw::string familyKey = makeNumberedFamilyKey( name );
        for ( const sw::string& candidate : uniqueName )
        {
            if ( makeNumberedFamilyKey( candidate ) == familyKey )
                return true;
        }
        return false;
    }
} // namespace

/**
 * @brief 계약과 맞는 리플렉션은 위반 0, 어긋난 것은 각각의 규칙에 걸린다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, SyntheticViolationsAreDetected )
{
    SW_TEST_SUPPRESS_LOGS();
    namespace vk       = sw::shaderslot::vk;
    namespace bindless = sw::shaderslot::bindless;
    sw::vector<sw::ShaderBindingValidatorIssue> listIssue;

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
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( ok, sw::ShaderTargetFormat::SPIRV_OpenGL, "ok.gl", &listIssue ) );
    }

    // 2) GL: MaterialCB 가 set 10 binding 0 — GL 은 set 을 버리므로 PassCB(binding 0) 와 충돌. (실제로 났던 사고)
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "MaterialCB", 10, 0 ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingValidator::validate( bad, sw::ShaderTargetFormat::SPIRV_OpenGL, "bad.gl", &listIssue );
        SW_EXPECT_TRUE_MSG( count >= 2, "위치 불일치 + set!=0 + 충돌 중 최소 둘은 잡혀야 한다" );
        SW_EXPECT_TRUE_MSG( hasIssueContaining( listIssue, "같은 자리" ), "UBO binding 0 충돌이 보고돼야 한다" );
    }

    // 3) Vulkan: 계약대로 — 세트 0 은 b/t/u 범위(0/16/32 시프트), 세트 1 은 텍스처 배열(무제한)과 정적 샘플러.
    {
        sw::ShaderReflectionData ok{};
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, vk::kBShift + sw::shaderslot::kPassConstantBuffer ) );
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, vk::kBShift + sw::shaderslot::kPassConstantBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 0, vk::kTShift + sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StorageBuffer", 0, vk::kTShift + sw::shaderslot::kMaterialBuffer ) );
        ok._listResource.push_back( makeRes( "g_IndirectArgs", "StorageBuffer", 0, vk::kUShift + 0 ) );
        ok._listResource.push_back( makeRes( "g_SwBindlessTex2D", "TextureOrSampler", bindless::kVkTextureSet, bindless::kVkTextureBinding, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSamplers", "Sampler", bindless::kVkTextureSet, bindless::kVkSamplerBinding ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( ok, sw::ShaderTargetFormat::SPIRV_Vulkan, "ok.vk", &listIssue ) );
    }

    // 4) Vulkan: 계약과 다른 배치(PassCB set 0 / MaterialCB set 10 / 인스턴스 set 6) — 위치 불일치 + 레이아웃 밖 세트가 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        bad._listResource.push_back( makeRes( "MaterialCB", "ConstantBuffer", 10, 0 ) );
        bad._listResource.push_back( makeRes( "g_SwInstances", "StorageBuffer", 6, 0 ) );
        listIssue.clear();
        SW_EXPECT_TRUE( sw::ShaderBindingValidator::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad.vk", &listIssue ) >= 4 );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "위치가 계약과" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "레이아웃에 없는 descriptor set" ) );
    }

    // 5) Vulkan: 범위 밖 binding / 범위 종류 불일치(UBO 범위에 SSBO) / 세트 1 오용 — 세 규칙이 각각 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "g_Foo", "StorageBuffer", 0, vk::kSlotBindingCount + 3 ) ); // 범위 밖
        bad._listResource.push_back( makeRes( "g_Bar", "StorageBuffer", 0, vk::kBShift + 2 ) );           // b 범위에 SSBO
        bad._listResource.push_back( makeRes( "g_Baz", "StorageBuffer", bindless::kVkTextureSet, 0 ) );   // 세트 1 에 버퍼
        listIssue.clear();
        SW_EXPECT_EQUAL( 3u, sw::ShaderBindingValidator::validate( bad, sw::ShaderTargetFormat::SPIRV_Vulkan, "bad2.vk", &listIssue ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "범위 밖" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "범위인데" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "세트 1" ) );
    }

    // 6) GL: 인스턴스 구조버퍼가 상수버퍼로 분류됨 — SPIR-V 1.3 BufferBlock 오분류 사고의 재현.
    {
        sw::ShaderReflectionData bad{};
        bad._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        bad._listConstantBuffer.push_back( makeCb( "g_SwInstances", 0, sw::shaderslot::kInstanceBuffer ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingValidator::validate( bad, sw::ShaderTargetFormat::SPIRV_OpenGL, "bad2.gl", &listIssue );
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
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( ok, sw::ShaderTargetFormat::DXBC_D3D11, "ok.dx11", &listIssue ) );
    }

    // 8) DX12: 계약대로 — 슬롯은 space0, 텍스처 배열은 t0 space1 무제한, 정적 샘플러 s0.
    {
        sw::ShaderReflectionData ok{};
        ok._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        ok._listConstantBuffer.push_back( makeCb( "PassCB", 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwInstances", "StructuredBuffer", 0, sw::shaderslot::kInstanceBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwMaterials", "StructuredBuffer", 0, sw::shaderslot::kMaterialBuffer ) );
        ok._listResource.push_back( makeRes( "g_SwBindlessTex2D", "Texture", bindless::kTextureSpace, 0, 0 ) );
        ok._listResource.push_back( makeRes( "g_SwSampler0", "Sampler", 0, 0 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( ok, sw::ShaderTargetFormat::DXIL_D3D12, "ok.dx12", &listIssue ) );
    }

    // 9) DX12: 에뮬 슬롯 선언(g_SwSlot0) / 루트 시그니처 슬롯 수 초과(t12) / 없는 space(7) — 각각 잡힌다.
    {
        sw::ShaderReflectionData bad{};
        bad._listResource.push_back( makeRes( "PassCB", "ConstantBuffer", 0, 0 ) );
        bad._listResource.push_back( makeRes( "g_SwSlot0", "Texture", 0, 0 ) );
        bad._listResource.push_back( makeRes( "g_Big", "StructuredBuffer", 0, sw::shaderslot::kSrvSlotCount + 2 ) );
        bad._listResource.push_back( makeRes( "g_Elsewhere", "StructuredBuffer", 7, 0 ) );
        listIssue.clear();
        const uint32 count = sw::ShaderBindingValidator::validate( bad, sw::ShaderTargetFormat::DXIL_D3D12, "bad.dx12", &listIssue );
        SW_EXPECT_TRUE( count >= 3 );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "없는 예약 리소스" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "슬롯 수" ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "없는 register space" ) );
    }

    // 10) 정점 입력 — `struct VSInput { pos; col }` 처럼 중간 속성을 뺀 선언은 Vulkan·GL 에서 col 이 location 1(노멀) 을
    //     읽는다(그러면 검게 그려진다). DX 는 시맨틱으로 묶어 같은 선언이 위반이 아니다.
    {
        sw::ShaderReflectionData skipped{};
        skipped._listVertexInput.push_back( makeVertexInput( "POSITION", 0 ) );
        skipped._listVertexInput.push_back( makeVertexInput( "COLOR", 1 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 1u, sw::ShaderBindingValidator::validate( skipped, sw::ShaderTargetFormat::SPIRV_Vulkan, "skipped.vk", &listIssue ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "location 이 정점 레이아웃 표와" ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 1u, sw::ShaderBindingValidator::validate( skipped, sw::ShaderTargetFormat::SPIRV_OpenGL, "skipped.gl", &listIssue ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( skipped, sw::ShaderTargetFormat::DXIL_D3D12, "skipped.dx12", &listIssue ) );

        sw::ShaderReflectionData full{};
        full._listVertexInput.push_back( makeVertexInput( "POSITION", 0 ) );
        full._listVertexInput.push_back( makeVertexInput( "NORMAL", 1 ) );
        full._listVertexInput.push_back( makeVertexInput( "TEXCOORD", 2 ) );
        full._listVertexInput.push_back( makeVertexInput( "COLOR", 3 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 0u, sw::ShaderBindingValidator::validate( full, sw::ShaderTargetFormat::SPIRV_Vulkan, "full.vk", &listIssue ) );

        sw::ShaderReflectionData unknown{};
        unknown._listVertexInput.push_back( makeVertexInput( "TANGENT", 0 ) );
        unknown._listVertexInput.push_back( makeVertexInput( "TEXCOORD", 1, 1 ) );
        listIssue.clear();
        SW_EXPECT_EQUAL( 2u, sw::ShaderBindingValidator::validate( unknown, sw::ShaderTargetFormat::DXBC_D3D11, "unknown.dx11", &listIssue ) );
        SW_EXPECT_TRUE( hasIssueContaining( listIssue, "정점 레이아웃 표(constant::arrVertexAttribute)에 없는" ) );
    }
}

/**
 * @brief 리포지토리에 쿠킹된 4백엔드 바이너리 전부가 계약과 맞는다.
 * @details 셰이더를 고치고 다시 쿠킹하지 않았거나, 헤더 매크로/백엔드 상수를 한쪽만 바꾸면 여기서 실패한다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, AllCookedShadersMatchContract )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

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
                                                              sw::string( sw::ShaderCooker::getSubfolderForFormat( target._format ) ) );
            if ( sw::FileUtil::isDirectory( binDir ) == false )
                continue;
            sw::vector<sw::string> listFile;
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderCooker::getExtensionForFormat( target._format ) ), listFile, false );
            for ( const sw::string& path : listFile )
            {
                sw::vector<uint8> bytecode;
                if ( sw::FileUtil::readFile( path, bytecode ) == false || bytecode.empty() )
                    continue;
                const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( bytecode, target._format );
                // 리플렉션 불가(예: 이 플랫폼에 컴파일러 DLL 없음) — 검사 대상이 아니다. 정점 입력만 있는 VS(fullscreentriangle 처럼
                // 상수버퍼가 PS 에만 있는 것)까지 여기서 빼면 location 어긋남이 통과한다 — 세 목록이 다 비어야 건너뛴다.
                if ( reflection._listConstantBuffer.empty() && reflection._listResource.empty() && reflection._listVertexInput.empty() )
                    continue;
                sw::vector<sw::ShaderBindingValidatorIssue> listIssue;
                const uint32                                issueCount = sw::ShaderBindingValidator::validate( reflection, target._format, path, &listIssue );
                for ( const sw::ShaderBindingValidatorIssue& issue : listIssue )
                {
                    SW_LOG_WARNING( "%# — %#: %#", path.c_str(), issue._resource.c_str(), issue._message.c_str() );
                }
                violationCount += issueCount;
                ++checkedCount;
            }
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "쿠킹된 셰이더 바이너리를 찾지 못했습니다 (App.exe --cook-shaders 필요)" );
    SW_EXPECT_TRUE_MSG( checkedCount >= 8, "네 백엔드 × 엔진 셰이더가 있어야 한다" );
    SW_EXPECT_EQUAL( 0u, violationCount );
}

/**
 * @brief 쿠킹된 바이너리의 리플렉션이 네 백엔드에서 **같은 레이아웃**을 준다 — PassCB 멤버(이름·오프셋), 그리고 머티리얼
 *        데이터 구조버퍼(g_SwMaterials)의 원소 레이아웃(이름·오프셋·크기·stride).
 * @details 엔진은 머티리얼 바이트를 리플렉션 하나로 패킹해 네 백엔드에 그대로 올린다. SPIR-V 를 std430 으로 구우면
 *          float3 정렬과 struct stride 가 DX 자연 패킹과 달라져 원소 1 부터 어긋난다 — 그래서 ShaderCompiler 가
 *          -fvk-use-dx-layout 으로 쿠킹하고, 이 테스트가 같은 셰이더의 네 바이너리를 비교해 어긋남을 이름과 숫자로 보고한다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ReflectionNamesAreUniformAcrossBackends )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
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
                                                                      sw::string( sw::ShaderCooker::getSubfolderForFormat( format ) ) );
        sw::vector<sw::string>       listFile;
        if ( sw::FileUtil::isDirectory( binDir ) )
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderCooker::getExtensionForFormat( format ) ), listFile, false );
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
            // **리플렉션이 빈 것은 "레이아웃이 없다" 가 아니라 "이 플랫폼에 그 포맷의 리플렉터가 없다" 다.**
            // DXBC/DXIL 리플렉션은 Windows 전용(FXC/DXC)이라 리눅스에서는 dx11·dx12 바이너리가 통째로 빈
            // 결과를 낸다. 그걸 "찾았다" 로 세면 g_SwMaterials 가 없다며 리눅스에서만 진다 — 셰이더가
            // 아니라 도구가 없어서 나는 실패다. AllCookedShadersMatchContract 가 이미 같은 규칙을 쓴다.
            if ( reflection._listConstantBuffer.empty() && reflection._listResource.empty() &&
                 reflection._listStructuredElement.empty() )
                continue;
            sw::vector<PerFormat>& listPer = mapShader[stem];
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
                {
                    per.passMembers += var._name + "@" + sw::to_string( var._offset ) + ";";
                }
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
        SW_TEST_SKIP( "같은 셰이더의 바이너리를 둘 이상 찾지 못했습니다 (App.exe --cook-shaders 필요)" );
    SW_EXPECT_TRUE_MSG( bForwardLitChecked, "forwardlit_ps 의 g_SwMaterials 원소를 찾지 못했다" );
}

/**
 * @brief [ShaderBindingValidatorTest] DX12 루트 시그니처 예산 — 슬롯 수를 늘려도 64 dword 안에 있어야 한다.
 * @details 루트 배치는 계약(shaderslot::dx12)에서 나온다: CB 는 루트 CBV(2 dword), t/u 슬롯과 텍스처 배열은 테이블(1 dword),
 *          루트 상수는 dword 수. t/u 를 루트 디스크립터로 두면 슬롯 하나가 2 dword 씩 예산을 먹는다.
 *          이 테스트는 "슬롯을 늘리면 예산이 느는가" 를 숫자로 고정한다 — 테이블 안의 슬롯 수는 예산에 들지 않는다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, Dx12RootSignatureFitsBudget )
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
 * @brief [ShaderBindingValidatorTest] GPUScene 인스턴스 원소 레이아웃이 C++ `GpuInstance` 와 같다 (쿠킹된 바이너리, 4 백엔드, 그래픽스 · 컴퓨트 셋).
 * @details `g_SwInstances`(t4)는 **C++ 이 쓰고 셰이더가 읽는** 유일한 구조체다 — 한쪽만 바뀌면 컴파일도 검증 레이어도
 *          아무 말을 하지 않고 월드 행렬·머티리얼 인덱스가 원소 1 부터 어긋난다(stride 가 어긋난 구조 버퍼와 같은 함정).
 *          그래서 stride 와 필드 오프셋을 쿠킹한 바이너리의 리플렉션에서 읽어 C++ 구조체와 대조한다. GPU 가 필요 없다.
 *          컴퓨트 셋(gpucull · instancesort 의 `g_Instances`, instanceanim 의 `g_InstancesRW`)도 같은 원소를 읽고 쓴다. 셋 다 `instancedata.hlsli`
 *          하나를 쓰고, 여기서 세 이름을 모두 대조한다 — 구조체를 각자 베끼면 칸을 더할 때 한 곳만 고쳐 컬링 · 정렬 · 회전이 원소 1 부터
 *          어긋난다. 이름마다 적어도 한
 *          바이너리가 있어야 한다(없으면 그 셰이더가 이름을 바꿨고 검사가 눈을 감은 것이다).
 */
SW_TEST_CASE( ShaderBindingValidatorTest, InstanceElementLayoutMatchesCpuStruct )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
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
        { "world", static_cast<uint32>( offsetof( sw::GpuInstance, _world ) ) },
        { "boundsCenter", static_cast<uint32>( offsetof( sw::GpuInstance, _boundsCenter ) ) },
        { "boundsRadius", static_cast<uint32>( offsetof( sw::GpuInstance, _boundsRadius ) ) },
        { "meshBatchIndex", static_cast<uint32>( offsetof( sw::GpuInstance, _meshBatchIndex ) ) },
        { "materialIndex", static_cast<uint32>( offsetof( sw::GpuInstance, _materialIndex ) ) },
        { "blendMode", static_cast<uint32>( offsetof( sw::GpuInstance, _blendMode ) ) },
        { "spinSeed", static_cast<uint32>( offsetof( sw::GpuInstance, _spinSeed ) ) },
        { "uvStart", static_cast<uint32>( offsetof( sw::GpuInstance, _sprite ) + offsetof( sw::GpuSpriteInstanceData, _uvStart ) ) },
        { "uvEnd", static_cast<uint32>( offsetof( sw::GpuInstance, _sprite ) + offsetof( sw::GpuSpriteInstanceData, _uvEnd ) ) },
        { "tint", static_cast<uint32>( offsetof( sw::GpuInstance, _sprite ) + offsetof( sw::GpuSpriteInstanceData, _tint ) ) },
        { "pixelSnap", static_cast<uint32>( offsetof( sw::GpuInstance, _sprite ) + offsetof( sw::GpuSpriteInstanceData, _pixelSnap ) ) },
        { "vertexAnimationPhase", static_cast<uint32>( offsetof( sw::GpuInstance, _vertexAnimationPhase ) ) },
    };
    // 인스턴스 원소를 담는 버퍼 이름 — 그래픽스(t4)와 컴퓨트 셋(읽기 g_Instances · 고쳐 쓰기 g_InstancesRW).
    const utf8* arrInstanceBufferName[] = { sw::shaderslot::resname::kInstances, "g_Instances", "g_InstancesRW" };
    uint32      arrCheckedPerName[3]    = {};

    constexpr uint32             kFormatCount            = 4;
    const sw::ShaderTargetFormat arrFormat[kFormatCount] = {
        sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12, sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };
    const utf8* arrFormatName[kFormatCount] = { "dx11", "dx12", "vulkan", "opengl" };

    uint32 checkedCount{ 0 };
    for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
    {
        const sw::ShaderTargetFormat format = arrFormat[formatIndex];
        const sw::string             binDir = sw::FileUtil::joinPath( sw::FileUtil::joinPath( shaderDir, "bin" ),
                                                                      sw::string( sw::ShaderCooker::getSubfolderForFormat( format ) ) );
        sw::vector<sw::string>       listFile;
        if ( sw::FileUtil::isDirectory( binDir ) )
            sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderCooker::getExtensionForFormat( format ) ), listFile, false );

        for ( const sw::string& path : listFile )
        {
            sw::vector<uint8> bytecode;
            if ( sw::FileUtil::readFile( path, bytecode ) == false || bytecode.empty() )
                continue;

            const sw::ShaderReflectionData reflection = sw::ShaderReflection::reflect( bytecode, format );
            for ( const sw::ShaderBufferInfo& element : reflection._listStructuredElement )
            {
                uint32 nameIndex = 0;
                while ( nameIndex < 3 && element._name != arrInstanceBufferName[nameIndex] )
                {
                    ++nameIndex;
                }
                if ( nameIndex == 3 )
                    continue;
                ++arrCheckedPerName[nameIndex];

                const sw::string label = sw::string( arrFormatName[formatIndex] ) + "/" + sw::FileUtil::getFileNamePart( path ) + " " + element._name;
                SW_EXPECT_TRUE_MSG( element._totalSize == static_cast<uint32>( sizeof( sw::GpuInstance ) ),
                                    ( label + " stride " + sw::to_string( element._totalSize ) + " != sizeof(GpuInstance) " +
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
                    SW_EXPECT_TRUE_MSG( pFound != nullptr, ( label + " 에 " + expected._pName + " 가 없습니다" ).c_str() );
                    if ( pFound == nullptr )
                        continue;
                    SW_EXPECT_TRUE_MSG( pFound->_offset == expected._offset,
                                        ( label + "." + expected._pName + " 오프셋 " + sw::to_string( pFound->_offset ) + " != C++ " +
                                          sw::to_string( expected._offset ) )
                                            .c_str() );
                }
                ++checkedCount;
            }
        }
    }

    if ( checkedCount == 0 )
        SW_TEST_SKIP( "g_SwInstances 를 선언한 쿠킹된 셰이더가 없습니다 (--cook-shaders 를 먼저 돌리세요)" );
    for ( uint32 nameIndex = 0; nameIndex < 3; ++nameIndex )
    {
        SW_EXPECT_TRUE_MSG( arrCheckedPerName[nameIndex] > 0, ( sw::string( arrInstanceBufferName[nameIndex] ) + " 를 담은 쿠킹된 셰이더가 없다 — 이름이 바뀌어 검사가 눈을 감았다" ).c_str() );
    }
}

/**
 * @brief [ShaderBindingValidatorTest] C++ 가 이름으로 묶는 셰이더 이름이 쿠킹된 리플렉션(reflection.manifest)에 **실제로 있다**.
 * @details validate 는 리플렉션의 이름을 계약 표에서 찾고, 표에 없는 이름은 **조용히 지나친다**. 그래서 셰이더 쪽에서
 *          `g_SwBatches` 를 다른 이름으로 바꾸면 그 리소스의 자리 · 종류 검사가 통째로 꺼지고(위반 0), 엔진의 이름 바인딩도
 *          아무 말 없이 빈다. 셰이더 이름을 고칠 때 문자열로 묶인 이름이 그렇게 사라질 수 있다.
 *          여기서는 반대 방향을 본다. C++ 가 아는 이름 — 계약 표, 계약 표 밖의 예약 리소스 이름, PassCB · 루트 상수 멤버
 *          (`PassConstantNames`), 레지스트리 이름(`g_<이름>`), 패스 텍스처 역할(`g_<역할>Index`) — 이 매니페스트 어딘가에 나와야 한다.
 *          계약 표에서 샘플러가 아닌 이름은 **그 백엔드 계약에 선언된 백엔드마다** 그 백엔드의 매니페스트에 있어야 한다.
 *          샘플러는 두 이유로 백엔드마다 빠질 수 있어, 같은 번호 묶음(`g_SwSampler#`)이 어딘가 쿠킹돼 있으면 된다.
 *          컴파일러가 안 쓰는 샘플러를 리플렉션에서 지우고(DX12 정적 샘플러 s1 · s2 · s4..s6 은 지금 어느 셰이더도 고르지 않는다),
 *          GL 은 결합 이미지 샘플러를 텍스처 이름 하나로 보고한다. 묶음의 이름을 바꾸면 여전히 걸린다.
 *          매니페스트는 리플렉터 없이 읽히므로 DXBC · DXIL 리플렉터가 없는 플랫폼에서도 네 백엔드를 다 본다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, EveryBoundNameIsInCookedReflection )
{
#if defined( SW_SHIPPING )
    // 배포본은 팩에서 읽고, 팩에는 정적으로 링크한 백엔드(`SW_SHIPPING_RHI_BACKEND`) 하나의 셰이더만 든다 — 네 백엔드 대조는 개발 빌드가 한다.
    SW_TEST_SKIP( "Shipping packs hold the cooked shaders of one backend only" );
#endif
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );

    constexpr uint32             kFormatCount            = 4;
    const sw::ShaderTargetFormat arrFormat[kFormatCount] = {
        sw::ShaderTargetFormat::DXBC_D3D11, sw::ShaderTargetFormat::DXIL_D3D12, sw::ShaderTargetFormat::SPIRV_Vulkan, sw::ShaderTargetFormat::SPIRV_OpenGL };
    const utf8* arrFormatName[kFormatCount] = { "dx11", "dx12", "vulkan", "opengl" };
    const utf8* arrDomain[]                 = { "engine", "common" };

    CookedNameSet arrNameSet[kFormatCount];
    CookedNameSet allNameSet;
    for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
    {
        for ( const utf8* pDomain : arrDomain )
        {
            const sw::string                      binDirectory = sw::string( pDomain ) + "/shaders/bin/" + sw::string( sw::ShaderCooker::getSubfolderForFormat( arrFormat[formatIndex] ) );
            sw::ShaderReflectionLibrary::EntryMap mapEntry;
            if ( sw::ShaderReflectionLibrary::loadManifest( binDirectory, mapEntry ) == false )
                continue;
            collectCookedNames( mapEntry, arrNameSet[formatIndex] );
            collectCookedNames( mapEntry, allNameSet );
        }
    }
    if ( allNameSet._entryCount == 0 )
        SW_TEST_SKIP( "쿠킹된 리플렉션 매니페스트를 찾지 못했습니다 (App.exe --cook-shaders 필요)" );
    for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
    {
        SW_EXPECT_TRUE_MSG( arrNameSet[formatIndex]._entryCount > 0, ( sw::string( arrFormatName[formatIndex] ) + " 매니페스트가 없다 — 그 백엔드의 이름은 검사되지 않는다" ).c_str() );
    }

    // 1) 계약 표
    for ( const sw::ShaderReservedBinding& reserved : sw::ShaderBindingValidator::getReservedBindings() )
    {
        const sw::string name( reserved._name );
        if ( reserved._kind == sw::ShaderBindingKind::Sampler )
        {
            const bool bFound = hasName( allNameSet._uniqueBindingName, name ) || hasNumberedFamilyMember( allNameSet._uniqueBindingName, name );
            SW_EXPECT_TRUE_MSG( bFound, ( "계약 샘플러 '" + name + "' 의 묶음이 어느 매니페스트에도 없다 — 셰이더 쪽 이름이 바뀌었다" ).c_str() );
            continue;
        }
        const sw::ShaderReservedLocation* arrLocation[kFormatCount] = { &reserved._dx11, &reserved._dx12, &reserved._vulkan, &reserved._opengl };
        for ( uint32 formatIndex = 0; formatIndex < kFormatCount; ++formatIndex )
        {
            const bool bChecked = arrLocation[formatIndex]->_bDeclared && arrNameSet[formatIndex]._entryCount > 0;
            if ( bChecked == false )
                continue;
            SW_EXPECT_TRUE_MSG( hasName( arrNameSet[formatIndex]._uniqueBindingName, name ),
                                ( sw::string( arrFormatName[formatIndex] ) + " 매니페스트에 계약 이름 '" + name +
                                  "' 가 없다 — 셰이더 쪽 이름이 바뀌면 validate 는 그 리소스를 조용히 건너뛴다" )
                                    .c_str() );
        }
    }

    // 2) 계약 표 밖에서 C++ 가 이름으로 부르는 리소스(meshmorph · meshskin 의 레스트 · 결과 · 가중치 · 팔레트 버퍼)
    const utf8* arrResourceName[] = { sw::shaderslot::resname::kMorphRestVertices, sw::shaderslot::resname::kMorphVerticesRw, sw::shaderslot::resname::kSkinWeights,
                                      sw::shaderslot::resname::kSkinPalette, sw::shaderslot::resname::kSkinInstances };
    for ( const utf8* pName : arrResourceName )
    {
        SW_EXPECT_TRUE_MSG( hasName( allNameSet._uniqueBindingName, pName ), ( sw::string( "리소스 '" ) + pName + "' 가 어느 매니페스트에도 없다" ).c_str() );
    }

    // 3) PassCB · 루트 상수 멤버 — 엔진이 리플렉션 멤버 이름으로 값을 채운다
    const sw::PassConstantNames& passNames       = sw::passConstantNames();
    const sw::hashed_string*     arrMemberName[] = {
        &passNames._lightViewProj,
        &passNames._viewProj,
        &passNames._invViewProj,
        &passNames._world,
        &passNames._keyLightDirIntensity,
        &passNames._keyLightColor,
        &passNames._shadowParams,
        &passNames._bloomParams,
        &passNames._outlineColor,
        &passNames._outlineParams,
        &passNames._flags,
        &passNames._swInstanceCount,
        &passNames._swMaterialCount,
        &passNames._swMorphVertexCount,
        &passNames._swBatchCount,
        &passNames._swLightCount,
        &passNames._swVertexAnimationCount,
        &passNames._swVertexAnimationTime,
    };
    for ( const sw::hashed_string* pName : arrMemberName )
    {
        SW_EXPECT_TRUE_MSG( hasName( allNameSet._uniqueMemberName, pName->c_str() ), ( sw::string( "cbuffer 멤버 '" ) + pName->c_str() + "' 가 어느 매니페스트에도 없다" ).c_str() );
    }

    // 4) 레지스트리 이름 — 리소스 `g_<이름>` 으로 걸린다(ShaderBindingLayout 의 canonical 이름)
    const sw::hashed_string* arrRegistryName[] = {
        &passNames._swInstances,
        &passNames._swMorphVertices,
        &passNames._swVertexAnimation,
        &passNames._swVisibleInstanceIds,
        &passNames._swMaterials,
        &passNames._swLights,
        &passNames._swBatches,
    };
    for ( const sw::hashed_string* pName : arrRegistryName )
    {
        const sw::string resourceName = sw::string( "g_" ) + pName->c_str();
        SW_EXPECT_TRUE_MSG( hasName( allNameSet._uniqueBindingName, resourceName ), ( "레지스트리 리소스 '" + resourceName + "' 가 어느 매니페스트에도 없다" ).c_str() );
    }

    // 5) 패스 텍스처 역할 — PassCB 의 `g_<역할>Index` 를 엔진이 bindless 인덱스로 채운다
    const sw::AttachmentNames& attachment    = sw::attachmentNames();
    const sw::hashed_string*   arrRoleName[] = {
        &attachment._shadowMap,
        &attachment._gbufferAlbedo,
        &attachment._gbufferNormal,
        &attachment._sceneDepth,
        &attachment._sourceColor,
        &attachment._ambientOcclusion,
    };
    for ( const sw::hashed_string* pRole : arrRoleName )
    {
        const sw::string memberName = sw::string( "g_" ) + pRole->c_str() + "Index";
        SW_EXPECT_TRUE_MSG( hasName( allNameSet._uniqueMemberName, memberName ), ( "패스 텍스처 인덱스 '" + memberName + "' 가 어느 매니페스트에도 없다" ).c_str() );
    }
}

/**
 * @brief [ShaderBindingValidatorTest] 씬 구조버퍼 슬롯(t4 · t9..t13)은 모두 계약 표에 있다
 * @details validate 는 표에 없는 이름을 건너뛴다. 표에서 빠진 슬롯은 셰이더가 엉뚱한 레지스터에 선언해도 아무 검사도 지지 않는다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, EverySceneStructuredBufferSlotIsReserved )
{
    const uint32 arrSlot[] = {
        sw::shaderslot::kInstanceBuffer,
        sw::shaderslot::kMaterialBuffer,
        sw::shaderslot::kVisibleInstanceBuffer,
        sw::shaderslot::kMorphVertexBuffer,
        sw::shaderslot::kLightBuffer,
        sw::shaderslot::kBatchBuffer,
    };
    const sw::vector<sw::ShaderReservedBinding>& list = sw::ShaderBindingValidator::getReservedBindings();
    for ( const uint32 slot : arrSlot )
    {
        bool bFound = false;
        for ( const sw::ShaderReservedBinding& reserved : list )
        {
            const bool bSameSlot = reserved._kind == sw::ShaderBindingKind::StructuredBuffer && reserved._dx12._bDeclared &&
                                   reserved._dx12._space == 0 && reserved._dx12._bind == slot;
            if ( bSameSlot )
                bFound = true;
        }
        SW_EXPECT_TRUE_MSG( bFound, ( "t" + sw::to_string( slot ) + " 구조버퍼가 계약 표에 없다" ).c_str() );
    }
}

/**
 * @brief [ShaderBindingValidatorTest] 계약 표 자체의 일관성 — 이름이 비지 않고 겹치지 않는다. 자리 충돌은 백엔드별로 뜻이 달라
 *        (DX11 은 g_SwSlot0Sampler=s0, DX12 는 g_SwSampler0=s0 처럼 서로 다른 셰이더에 산다) 여기서
 *        따지지 않고 AllCookedShadersMatchContract 가 실제 바이너리로 잡는다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ReservedTableIsConsistent )
{
    const sw::vector<sw::ShaderReservedBinding>& list = sw::ShaderBindingValidator::getReservedBindings();
    SW_EXPECT_TRUE( list.size() >= 8 );
    for ( size_t indexA = 0; indexA < list.size(); ++indexA )
    {
        SW_EXPECT_TRUE( list[indexA]._name != nullptr && list[indexA]._name[0] != '\0' );
        for ( size_t indexB = indexA + 1; indexB < list.size(); ++indexB )
        {
            SW_EXPECT_TRUE_MSG( sw::string( list[indexA]._name ) != list[indexB]._name, list[indexA]._name );
        }
    }
}

/**
 * @brief [ShaderBindingValidatorTest] PSO 가 거는 정점 속성은 정점 셰이더가 읽는 것뿐이다 — 쿠킹된 Vulkan 정점 셰이더마다 마스크 = 리플렉션 입력
 * @details 셰이더는 모두 `SwVertexInput` 을 선언하지만 컴파일러가 안 쓰는 입력을 뗀다. Vulkan PSO 가 표 전체를 걸면 검증 레이어가
 *          "Vertex attribute at location N not consumed by vertex shader" 를 PSO 마다 낸다. 풀스크린 삼각형은 위치만 읽고, 씬 셰이더는
 *          위치 · 노멀 · 인스턴스 슬롯을 읽는다. 셰이더가 읽는 속성을 마스크가 빠뜨리면(반대 방향) 미정의라, 비트 수가 입력 수와 같은지도 본다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ConsumedVertexAttributeMaskFollowsShaderInputs )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    const sw::string binDir = sw::FileUtil::joinPath(
        sw::FileUtil::joinPath( sw::ResourceUtil::getDomainFolderPath( "engine", "shaders" ), "bin" ),
        sw::string( sw::ShaderCooker::getSubfolderForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) ) );
    auto maskOf = [&binDir]( const utf8* pFileName, uint32& outInputCount ) -> uint32
    {
        sw::vector<uint8> bytecode;
        if ( sw::FileUtil::readFile( sw::FileUtil::joinPath( binDir, pFileName ), bytecode ) == false )
            return 0u;
        outInputCount = static_cast<uint32>( sw::ShaderReflection::reflect( bytecode, sw::ShaderTargetFormat::SPIRV_Vulkan )._listVertexInput.size() );
        return sw::RHIShaderRequest::computeConsumedVertexAttributeMask( bytecode, sw::ShaderTargetFormat::SPIRV_Vulkan );
    };
    auto bitOf = []( const utf8* pSemantic ) -> uint32
    {
        for ( uint32 attributeIndex = 0; attributeIndex < sw::constant::kVertexAttributeCount; ++attributeIndex )
        {
            if ( sw::string( sw::constant::arrVertexAttribute[attributeIndex]._pSemanticName ) == pSemantic )
                return 1u << attributeIndex;
        }
        return 0u;
    };

    uint32       fullscreenInputCount{ 0 };
    const uint32 fullscreenMask = maskOf( "fullscreentriangle_vs.spv", fullscreenInputCount );
    SW_EXPECT_TRUE( ( fullscreenMask & bitOf( "POSITION" ) ) != 0 );
    SW_EXPECT_TRUE_MSG( ( fullscreenMask & bitOf( "NORMAL" ) ) == 0, "풀스크린 셰이더는 노멀을 읽지 않는데 PSO 가 건다" );
    SW_EXPECT_TRUE_MSG( ( fullscreenMask & bitOf( "TEXCOORD" ) ) == 0, "풀스크린 셰이더는 UV 를 읽지 않는데 PSO 가 건다" );

    uint32       sceneInputCount{ 0 };
    const uint32 sceneMask = maskOf( "forwardlit_vs.spv", sceneInputCount );
    SW_EXPECT_TRUE( ( sceneMask & bitOf( "POSITION" ) ) != 0 );
    SW_EXPECT_TRUE( ( sceneMask & bitOf( "NORMAL" ) ) != 0 );
    SW_EXPECT_TRUE( ( sceneMask & bitOf( "SW_INSTANCESLOT" ) ) != 0 );

    // 쿠킹된 Vulkan 정점 셰이더 전부: 셰이더가 읽는 입력은 모두 마스크에 있다(입력 수 = 비트 수).
    sw::vector<sw::string> listFile;
    sw::FileUtil::collectFiles( binDir, sw::string( sw::ShaderCooker::getExtensionForFormat( sw::ShaderTargetFormat::SPIRV_Vulkan ) ), listFile, false );
    uint32 checkedCount{ 0 };
    for ( const sw::string& path : listFile )
    {
        if ( path.find( "_vs" ) == sw::string::npos )
            continue;
        const sw::string fileName = sw::FileUtil::getFileNamePart( path );
        uint32           inputCount{ 0 };
        const uint32     mask = maskOf( fileName.c_str(), inputCount );
        uint32           bitCount{ 0 };
        for ( uint32 attributeIndex = 0; attributeIndex < sw::constant::kVertexAttributeCount; ++attributeIndex )
        {
            bitCount += ( mask >> attributeIndex ) & 1u;
        }
        SW_EXPECT_TRUE_MSG( bitCount == inputCount, ( fileName + ": 셰이더가 읽는 정점 입력 중 PSO 가 걸지 않는 것이 있다" ).c_str() );
        ++checkedCount;
    }
    SW_EXPECT_TRUE( checkedCount >= 3 );
}

/**
 * @brief [ShaderBindingValidatorTest] C++ 와 셰이더가 같아야 하는 배치 수는 bindingslots.hlsli 에만 정의된다
 * @details 모프 · 스킨 버퍼 배치 · VAT 노멀 칸 · 거스트너 파도 수를 셰이더 파일마다 `#define` 으로 다시 적고 C++ 에 사본을 두면, 한쪽만 바뀐 날
 *          컴파일은 되고 화면 · 부력만 조용히 틀어진다. 정의는 계약 파일 하나이고 C++ 는 `shaderslot::k*` 로 같은 정의를 읽는다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, LayoutNumbersAreDefinedOnlyInBindingSlots )
{
    const utf8* const arrMacro[] = {
        "SW_MORPH_FLOAT4_PER_VERTEX",
        "SW_SKIN_FLOAT4_PER_VERTEX",
        "SW_SKIN_FLOAT4_PER_BONE",
        "SW_SKIN_UINT4_PER_INSTANCE",
        "SW_VERTEX_ANIMATION_NORMAL_STEPS",
        "SW_GERSTNER_WAVE_COUNT",
    };
    sw::vector<sw::string> listFile;
    for ( const utf8* pDomain : { "engine", "common" } )
    {
        const sw::string shaderFolder = sw::ResourceUtil::getDomainFolderPath( pDomain, "shaders" );
        SW_ASSERT_FALSE( shaderFolder.empty() );
        sw::FileUtil::collectFiles( shaderFolder, ".hlsl", listFile, true );
        sw::FileUtil::collectFiles( shaderFolder, ".hlsli", listFile, true );
    }
    SW_ASSERT_TRUE( listFile.size() >= 10 );
    for ( const utf8* pMacro : arrMacro )
    {
        const sw::string define = sw::string( "#define " ) + pMacro + " ";
        sw::string       listDefiner;
        uint32           defineCount{ 0 };
        bool             bInContract{ false };
        for ( const sw::string& path : listFile )
        {
            sw::string text;
            SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, text ) );
            if ( text.find( define ) == sw::string::npos )
                continue;
            ++defineCount;
            listDefiner += sw::FileUtil::getFileNamePart( path ) + " ";
            bInContract = bInContract || sw::FileUtil::getFileNamePart( path ) == "bindingslots.hlsli";
        }
        SW_EXPECT_TRUE_MSG( defineCount == 1 && bInContract, ( sw::string( pMacro ) + " 정의가 bindingslots.hlsli 하나가 아니다: " + listDefiner ).c_str() );
    }
}

/**
 * @brief [ShaderBindingValidatorTest] 루트 상수 dword 수는 네 백엔드가 셰이더 계약(shaderslot::kRootConstantDwords) 하나를 읽는다
 * @details 백엔드가 상한을 숫자로 따로 적으면(DX11 · GL 64, DX12 · Vulkan 16) 오프셋 16 이상의 쓰기가 백엔드마다 다르게 된다 — 셰이더는 16 dword 만 읽는다.
 *          백엔드 소스에 루트 상수 상한을 숫자 리터럴로 정의한 줄이 없어야 한다.
 */
SW_TEST_CASE( ShaderBindingValidatorTest, RootConstantLimitIsTheShaderContract )
{
    const sw::string       rhiFolder = sw::FileUtil::joinPath( sw::ResourceUtil::getProjectFolderPath(), "Source/Engine/Graphics/RHI" );
    sw::vector<sw::string> listFile;
    sw::FileUtil::collectFiles( rhiFolder, ".h", listFile, true );
    sw::FileUtil::collectFiles( rhiFolder, ".cpp", listFile, true );
    SW_ASSERT_TRUE( listFile.size() >= 20 );
    sw::string listOffender;
    for ( const sw::string& path : listFile )
    {
        sw::string text;
        SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, text ) );
        size_t lineStart = 0;
        while ( lineStart < text.size() )
        {
            size_t lineEnd = text.find( '\n', lineStart );
            lineEnd        = lineEnd == sw::string::npos ? text.size() : lineEnd;
            const sw::string_view line( text.data() + lineStart, lineEnd - lineStart );
            const size_t          equal = line.find( '=' );
            if ( line.find( "constexpr" ) != sw::string_view::npos && line.find( "RootConstant" ) != sw::string_view::npos && equal != sw::string_view::npos )
            {
                size_t valueStart = equal + 1;
                while ( valueStart < line.size() && line[valueStart] == ' ' )
                {
                    ++valueStart;
                }
                if ( valueStart < line.size() && line[valueStart] >= '0' && line[valueStart] <= '9' )
                    listOffender += sw::FileUtil::getFileNamePart( path ) + " ";
            }
            lineStart = lineEnd + 1;
        }
    }
    SW_EXPECT_TRUE_MSG( listOffender.empty(), ( "루트 상수 상한을 숫자로 따로 적은 백엔드: " + listOffender ).c_str() );
}

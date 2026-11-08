#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingValidatorIssue.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

// ShaderBindingValidatorIssue — 계약 표, 백엔드마다의 자리 규칙, 충돌, assert 슬롯, 정점 입력, 구조 버퍼 원소 배치. 리플렉션은 손으로 만든다(`g_Test*` 는 계약에 없는 이름이다).

using namespace sw;

namespace
{
    struct ShaderBindingValidatorTestUtil
    {
        static constexpr const utf8*        kViolationLogID = "ShaderBindingValidator.ContractViolated";
        static constexpr ShaderTargetFormat kArrFormat[]    = { ShaderTargetFormat::DXBC_D3D11, ShaderTargetFormat::DXIL_D3D12, ShaderTargetFormat::SPIRV_Vulkan, ShaderTargetFormat::SPIRV_OpenGL };

        static void addResource( ShaderReflectionData& reflection, const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint, uint32 bindCount = 0 )
        {
            ShaderReflectedBinding resource{};
            resource._name          = pName;
            resource._type          = pType;
            resource._registerSpace = space;
            resource._bindPoint     = bindPoint;
            resource._bindCount     = bindCount;
            reflection._listResource.push_back( resource );
        }

        /** @brief 리소스 하나만 있는 리플렉션의 위반 수입니다. */
        static uint32 validateOne( ShaderTargetFormat format, const utf8* pName, const utf8* pType, uint32 space, uint32 bindPoint, uint32 bindCount = 0 )
        {
            ShaderReflectionData reflection;
            addResource( reflection, pName, pType, space, bindPoint, bindCount );
            return ShaderBindingValidatorIssue::validate( reflection, format, "Test" );
        }

        static void addVertexInput( ShaderReflectionData& reflection, const utf8* pSemantic, uint32 semanticIndex, uint32 location )
        {
            ShaderVertexInputInfo input{};
            input._semantic      = pSemantic;
            input._semanticIndex = semanticIndex;
            input._location      = location;
            reflection._listVertexInput.push_back( input );
        }

        static void addElementMember( ShaderBufferInfo& element, const utf8* pName, const utf8* pType, uint32 offset )
        {
            ShaderVariableInfo variable{};
            variable._name   = pName;
            variable._type   = pType;
            variable._offset = offset;
            element._listVariable.push_back( variable );
        }

        /** @brief 원소 하나의 위반 수입니다. 규칙이 백엔드와 상관없으므로 모든 포맷에서 같아야 합니다. */
        static void expectElementViolations( const ShaderBufferInfo& element, uint32 expected )
        {
            ShaderReflectionData reflection;
            reflection._listStructuredElement.push_back( element );
            for ( const ShaderTargetFormat format : kArrFormat )
                SW_EXPECT_EQUAL( expected, ShaderBindingValidatorIssue::validate( reflection, format, "Test" ) );
        }

        static const utf8* labelOf( ShaderBindingKind kind )
        {
            switch ( kind )
            {
                case ShaderBindingKind::ConstantBuffer:
                    return "ConstantBuffer";
                case ShaderBindingKind::Texture:
                    return "Texture";
                case ShaderBindingKind::Sampler:
                    return "Sampler";
                case ShaderBindingKind::StructuredBuffer:
                    return "StructuredBuffer";
                case ShaderBindingKind::RwStructuredBuffer:
                    return "RWStructuredBuffer";
                case ShaderBindingKind::RwTexture:
                    return "RWTexture";
                case ShaderBindingKind::Unknown:
                    return "OtherResource";
            }
            return "OtherResource";
        }

        static const ShaderReservedLocation& locationOf( const ShaderReservedBinding& reserved, ShaderTargetFormat format )
        {
            switch ( format )
            {
                case ShaderTargetFormat::DXIL_D3D12:
                    return reserved._dx12;
                case ShaderTargetFormat::SPIRV_Vulkan:
                case ShaderTargetFormat::WGSL_WebGPU: // WGSL 은 Vulkan SPIR-V 에서 옮긴다(Tint) — 자리도 같다
                    return reserved._vulkan;
                case ShaderTargetFormat::SPIRV_OpenGL:
                    return reserved._opengl;
                case ShaderTargetFormat::DXBC_D3D11:
                case ShaderTargetFormat::Count:
                    break;
            }
            return reserved._dx11;
        }
    };
} // namespace

/**
 * @brief [ShaderBindingValidatorTest] 계약 표의 이름은 비어 있지 않고 겹치지 않는다 — 검사가 이름의 첫 항목만 보므로 겹치면 뒤 항목이 조용히 무시된다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ReservedNamesAreUnique )
{
    const vector<ShaderReservedBinding>& listReserved = ShaderBindingValidatorIssue::getReservedBindings();
    SW_REQUIRE_TRUE( listReserved.empty() == false );
    for ( size_t indexA = 0; indexA < listReserved.size(); ++indexA )
    {
        SW_EXPECT_TRUE( listReserved[indexA]._name.empty() == false );
        for ( size_t indexB = indexA + 1; indexB < listReserved.size(); ++indexB )
            SW_EXPECT_FALSE( listReserved[indexA]._name == listReserved[indexB]._name );
    }
}

/**
 * @brief [ShaderBindingValidatorTest] 계약 표의 모든 항목은 계약 자리에 놓으면 그 백엔드의 자리 규칙까지 모두 통과한다(표와 규칙이 서로 어긋나지 않는다)
 */
SW_TEST_CASE( ShaderBindingValidatorTest, EveryReservedEntryPassesAtItsLocation )
{
    using Util = ShaderBindingValidatorTestUtil;
    for ( const ShaderTargetFormat format : Util::kArrFormat )
    {
        for ( const ShaderReservedBinding& reserved : ShaderBindingValidatorIssue::getReservedBindings() )
        {
            const ShaderReservedLocation& location = Util::locationOf( reserved, format );
            if ( location._bDeclared == false )
                continue;
            const uint32 issueCount = Util::validateOne( format, reserved._name.c_str(), Util::labelOf( reserved._kind ), location._space, location._bind );
            SW_EXPECT_TRUE_MSG( issueCount == 0, reserved._name.c_str() );
        }
    }
}

/**
 * @brief [ShaderBindingValidatorTest] 슬롯 리소스의 계약 값: 상수 버퍼 · 구조 버퍼의 자리와 Vulkan 시프트, DX12 에 없는 엔진 텍스처 칸
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ReservedTableHoldsSlotContract )
{
    const ShaderReservedBinding* pPass{ nullptr };
    const ShaderReservedBinding* pMaterials{ nullptr };
    const ShaderReservedBinding* pEngineTexture0{ nullptr };
    const ShaderReservedBinding* pRootConstants{ nullptr };
    for ( const ShaderReservedBinding& reserved : ShaderBindingValidatorIssue::getReservedBindings() )
    {
        const string_view name( reserved._name.c_str(), reserved._name.size() );
        if ( name == shaderslot::cbname::kPass )
            pPass = &reserved;
        else if ( name == shaderslot::resname::kMaterials )
            pMaterials = &reserved;
        else if ( name == "g_SwEngineTex0" )
            pEngineTexture0 = &reserved;
        else if ( name == shaderslot::cbname::kRootConstants )
            pRootConstants = &reserved;
    }
    SW_REQUIRE_NOT_NULL( pPass );
    SW_REQUIRE_NOT_NULL( pMaterials );
    SW_REQUIRE_NOT_NULL( pEngineTexture0 );
    SW_REQUIRE_NOT_NULL( pRootConstants );

    SW_EXPECT_EQUAL( ShaderBindingKind::ConstantBuffer, pPass->_kind );
    SW_EXPECT_EQUAL( shaderslot::kPassConstantBuffer, pPass->_dx12._bind );
    SW_EXPECT_EQUAL( shaderslot::vk::kBShift + shaderslot::kPassConstantBuffer, pPass->_vulkan._bind );

    SW_EXPECT_EQUAL( ShaderBindingKind::StructuredBuffer, pMaterials->_kind );
    SW_EXPECT_EQUAL( shaderslot::kMaterialBuffer, pMaterials->_dx11._bind );
    SW_EXPECT_EQUAL( shaderslot::vk::kTShift + shaderslot::kMaterialBuffer, pMaterials->_vulkan._bind );

    SW_EXPECT_EQUAL( shaderslot::kEngineTexture0, pEngineTexture0->_dx11._bind );
    SW_EXPECT_EQUAL( 0u, pEngineTexture0->_opengl._bind ); // GL 유닛은 칸 번호다
    SW_EXPECT_FALSE( pEngineTexture0->_dx12._bDeclared );
    SW_EXPECT_FALSE( pEngineTexture0->_vulkan._bDeclared );

    SW_EXPECT_EQUAL( shaderslot::kRootConstantSpace, pRootConstants->_dx12._space );
    SW_EXPECT_FALSE( pRootConstants->_vulkan._bDeclared ); // 푸시 상수라 자리가 없다
}

/**
 * @brief [ShaderBindingValidatorTest] 예약 리소스의 어긋남 하나마다 위반 하나다: 위치 · 종류 · 그 백엔드에 없는 선언. 반환값과 목록이 같고 목록에 리소스 이름이 든다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ReservedMismatchesReportOneIssueEach )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderReflectionData wrongRegister;
    Util::addResource( wrongRegister, shaderslot::cbname::kPass, "ConstantBuffer", 0, shaderslot::kPassConstantBuffer + 1 );
    vector<ShaderBindingValidatorIssue> listIssue;
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( wrongRegister, ShaderTargetFormat::DXBC_D3D11, "Test", &listIssue ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIssue.size() );
    SW_EXPECT_TRUE( listIssue[0]._resource == shaderslot::cbname::kPass );
    SW_EXPECT_TRUE( listIssue[0]._message.empty() == false );

    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::DXBC_D3D11, shaderslot::cbname::kPass, "Texture", 0, shaderslot::kPassConstantBuffer ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::DXIL_D3D12, "g_SwEngineTex0", "Texture", 0, 0 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, shaderslot::cbname::kRootConstants, "ConstantBuffer", 0, 0 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::DXBC_D3D11, shaderslot::resname::kSamplers, "Sampler", 0, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] SPIR-V 는 RW 구조버퍼를 StorageBuffer 로, 샘플러 짝 텍스처를 한 종류로 보고하므로 어긋남이 아니다. DX 에서는 같은 차이가 위반이다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, KindMatchToleratesSpirvReporting )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, shaderslot::resname::kCullIndirectArgs, "StorageBuffer", 0, shaderslot::vk::kUShift ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_OpenGL, shaderslot::resname::kCullIndirectArgs, "StorageBuffer", 0, shaderslot::gl::kUAVBinding0 ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, shaderslot::resname::kBindlessTextures, "Sampler", shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkTextureBinding ) );

    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::DXIL_D3D12, shaderslot::resname::kCullIndirectArgs, "StorageBuffer", 0, 0 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::DXIL_D3D12, "g_SwSampler0", "Texture", 0, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] 리소스 목록이 위치의 1차 출처다: 같은 (이름, 종류)의 CB 목록 항목은 합쳐지고 그 자리는 보지 않는다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, ResourceListWinsOverConstantBufferList )
{
    using Util = ShaderBindingValidatorTestUtil;
    ShaderReflectionData reflection;
    Util::addResource( reflection, shaderslot::cbname::kPass, "ConstantBuffer", 0, shaderslot::kPassConstantBuffer );
    ShaderBufferInfo buffer{};
    buffer._name      = shaderslot::cbname::kPass;
    buffer._bindPoint = 2; // 리플렉터가 못 채운 값
    reflection._listConstantBuffer.push_back( buffer );

    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( reflection, ShaderTargetFormat::DXIL_D3D12, "Test" ) );
}

/**
 * @brief [ShaderBindingValidatorTest] 종류를 모르는 리소스는 DX12 · Vulkan 의 자리 규칙과 충돌 검사를 건너뛴다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, UnknownKindSkipsSlotRules )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::DXIL_D3D12, "g_TestStrange", "OtherResource", 9, 99 ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, "g_TestStrange", "OtherResource", 9, 99 ) );

    ShaderReflectionData twoUnknown;
    Util::addResource( twoUnknown, "g_TestA", "OtherResource", 0, 1 );
    Util::addResource( twoUnknown, "g_TestB", "OtherResource", 0, 1 );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( twoUnknown, ShaderTargetFormat::DXBC_D3D11, "Test" ) );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( twoUnknown, ShaderTargetFormat::SPIRV_OpenGL, "Test" ) );
}

/**
 * @brief [ShaderBindingValidatorTest] DX12 space0 의 경계: 레지스터 종류마다 슬롯 수 - 1 까지 되고 슬롯 수부터 위반이다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, Dx12SlotLimitBoundaries )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::DXIL_D3D12;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestCb", "ConstantBuffer", 0, shaderslot::kConstantBufferSlotCount - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestCb", "ConstantBuffer", 0, shaderslot::kConstantBufferSlotCount ) );

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestTex", "Texture", 0, shaderslot::kSRVSlotCount - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", 0, shaderslot::kSRVSlotCount ) );

    // u 의 마지막 칸은 메시지 링 자리다
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, shaderslot::resname::kGPUMessageRing, "UAV", 0, shaderslot::kComputeUAVSlotCount - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestRw", "UAV", 0, shaderslot::kComputeUAVSlotCount ) );

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestSampler", "Sampler", 0, shaderslot::kStaticSamplerCount - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestSampler", "Sampler", 0, shaderslot::kStaticSamplerCount ) );
}

/**
 * @brief [ShaderBindingValidatorTest] DX12 space1 은 첫 레지스터의 무제한 텍스처 배열 전용이다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, Dx12BindlessSpaceRules )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::DXIL_D3D12;
    const uint32             space  = shaderslot::bindless::kTextureSpace;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestTex", "Texture", space, 0, 0 ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestRw", "RWTexture", space, 0, 0 ) );

    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", space, 1, 0 ) ); // t0 이 아니다
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", space, 0, 4 ) ); // 크기가 정해진 배열이다
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestBuf", "StructuredBuffer", space, 0, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] DX12 space2 는 루트 상수(b0) 전용이고, 그 밖의 space 는 루트 시그니처에 없다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, Dx12RootConstantAndOtherSpaces )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::DXIL_D3D12;
    const uint32             space  = shaderslot::kRootConstantSpace;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestRoot", "ConstantBuffer", space, shaderslot::kRootConstantRegister ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestRoot", "ConstantBuffer", space, shaderslot::kRootConstantRegister + 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", space, shaderslot::kRootConstantRegister ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", 3, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] Vulkan 세트 0: binding 이 b · t · u 범위의 양 끝까지 그 종류를 받고, 범위 밖이나 다른 종류는 위반이다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, VulkanSetZeroRanges )
{
    using Util   = ShaderBindingValidatorTestUtil;
    namespace vk = shaderslot::vk;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::SPIRV_Vulkan;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestCb", "ConstantBuffer", 0, vk::kBShift ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestCb", "ConstantBuffer", 0, vk::kBShift + vk::kRangeSize - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestCb", "ConstantBuffer", 0, vk::kTShift ) );

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestBuf", "StorageBuffer", 0, vk::kTShift ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestBuf", "StorageBuffer", 0, vk::kUShift ) ); // SPIR-V 는 읽기 · 쓰기를 가르지 않는다
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestBuf", "StorageBuffer", 0, vk::kUShift + vk::kRangeSize - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestBuf", "StorageBuffer", 0, vk::kBShift ) );

    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", 0, vk::kTShift ) ); // 텍스처는 세트 1 의 배열뿐이다
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestBuf", "StorageBuffer", 0, vk::kSlotBindingCount ) );
}

/**
 * @brief [ShaderBindingValidatorTest] Vulkan 세트 1: binding 마다 받는 종류가 정해져 있고, 그 밖의 세트는 파이프라인 레이아웃에 없다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, VulkanBindlessSetRules )
{
    using Util         = ShaderBindingValidatorTestUtil;
    namespace bindless = shaderslot::bindless;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::SPIRV_Vulkan;
    const uint32             set    = bindless::kVkTextureSet;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestTex", "TextureOrSampler", set, bindless::kVkTextureBinding ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestSampler", "Sampler", set, bindless::kVkTextureBinding ) ); // 결합 샘플러
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestSampler", "Sampler", set, bindless::kVkSamplerBinding ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestSampler", "Sampler", set, bindless::kVkShadowSamplerBinding ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestRw", "RWTexture", set, bindless::kVkRwTextureBinding ) );

    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", set, bindless::kVkSamplerBinding ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestRw", "RWTexture", set, bindless::kVkTextureBinding ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", set, bindless::kVkRwTextureBinding ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", set, bindless::kVkRwTextureBinding + 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", set + 1, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] OpenGL 은 set 을 버리므로 0 이 아닌 set 은 위반이다. 다른 백엔드의 자리 규칙은 보지 않는다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, OpenGlRejectsNonZeroSet )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::SPIRV_OpenGL;

    SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestTex", "Texture", 0, 99 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestTex", "Texture", 3, 0 ) );
}

/**
 * @brief [ShaderBindingValidatorTest] 쓰기 슬롯 u4 는 메시지 링 전용이고 링은 u4 에만 놓인다 — DX 백엔드
 */
SW_TEST_CASE( ShaderBindingValidatorTest, MessageSlotOnDirectX )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const uint32 messageSlot = shaderslot::kGPUMessageUAV;
    for ( const ShaderTargetFormat format : { ShaderTargetFormat::DXBC_D3D11, ShaderTargetFormat::DXIL_D3D12 } )
    {
        SW_EXPECT_EQUAL( 0u, Util::validateOne( format, shaderslot::resname::kGPUMessageRing, "UAV", 0, messageSlot ) );
        SW_EXPECT_EQUAL( 1u, Util::validateOne( format, shaderslot::resname::kGPUMessageRing, "UAV", 0, 0 ) );
        SW_EXPECT_EQUAL( 1u, Util::validateOne( format, "g_TestRw", "UAV", 0, messageSlot ) );
        SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestRw", "UAV", 0, messageSlot - 1 ) );
        SW_EXPECT_EQUAL( 0u, Util::validateOne( format, "g_TestBuf", "StructuredBuffer", 0, messageSlot ) ); // t4 는 다른 이름공간이다
    }
}

/**
 * @brief [ShaderBindingValidatorTest] SPIR-V 도 같은 규칙이다 — RW 구조버퍼가 StorageBuffer 로 보고되어도(결함: 종류로 걸러 Vulkan · GL 에서 이 검사가 돌지 않았다)
 */
SW_TEST_CASE( ShaderBindingValidatorTest, MessageSlotOnSpirv )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    const uint32 vulkanSlot = shaderslot::vk::kUShift + shaderslot::kGPUMessageUAV;
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, shaderslot::resname::kGPUMessageRing, "StorageBuffer", 0, vulkanSlot ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, shaderslot::resname::kGPUMessageRing, "StorageBuffer", 0, vulkanSlot - 1 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::SPIRV_Vulkan, "g_TestBuf", "StorageBuffer", 0, vulkanSlot ) );

    const uint32 openGlSlot = shaderslot::gl::kUAVBinding0 + shaderslot::kGPUMessageUAV;
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_OpenGL, shaderslot::resname::kGPUMessageRing, "StorageBuffer", 0, openGlSlot ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::SPIRV_OpenGL, shaderslot::resname::kGPUMessageRing, "StorageBuffer", 0, shaderslot::gl::kUAVBinding0 ) );
    SW_EXPECT_EQUAL( 1u, Util::validateOne( ShaderTargetFormat::SPIRV_OpenGL, "g_TestBuf", "StorageBuffer", 0, openGlSlot ) );
    SW_EXPECT_EQUAL( 0u, Util::validateOne( ShaderTargetFormat::SPIRV_OpenGL, "g_TestTex", "Texture", 0, openGlSlot ) ); // 머티리얼 텍스처 유닛과 번호만 같다
}

/**
 * @brief [ShaderBindingValidatorTest] DX 충돌은 (레지스터 종류, space, 번호)가 모두 같을 때다. 같은 (이름, 종류)는 한 항목으로 합쳐 충돌이 아니다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, DirectXCollisionNamespace )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderReflectionData sameRegister;
    Util::addResource( sameRegister, "g_TestA", "Texture", 0, 3 );
    Util::addResource( sameRegister, "g_TestB", "StructuredBuffer", 0, 3 ); // 둘 다 t3
    vector<ShaderBindingValidatorIssue> listIssue;
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( sameRegister, ShaderTargetFormat::DXBC_D3D11, "Test", &listIssue ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIssue.size() );
    SW_EXPECT_TRUE( listIssue[0]._resource == "g_TestA" );

    ShaderReflectionData otherClass;
    Util::addResource( otherClass, "g_TestA", "Texture", 0, 3 );
    Util::addResource( otherClass, "g_TestB", "UAV", 0, 3 );
    Util::addResource( otherClass, "g_TestC", "Sampler", 0, 3 );
    Util::addResource( otherClass, "g_TestD", "ConstantBuffer", 0, 3 );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( otherClass, ShaderTargetFormat::DXBC_D3D11, "Test" ) );

    ShaderReflectionData otherSpace;
    Util::addResource( otherSpace, "g_TestA", "Texture", 0, 3 );
    Util::addResource( otherSpace, "g_TestB", "Texture", 1, 3 );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( otherSpace, ShaderTargetFormat::DXBC_D3D11, "Test" ) );

    ShaderReflectionData sameName;
    Util::addResource( sameName, "g_TestA", "Texture", 0, 3 );
    Util::addResource( sameName, "g_TestA", "Texture", 0, 3 );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( sameName, ShaderTargetFormat::DXBC_D3D11, "Test" ) );
}

/**
 * @brief [ShaderBindingValidatorTest] Vulkan 충돌은 (set, binding)이고, 범위 규칙을 지키는 두 항목도 자리가 같으면 위반이다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, VulkanCollisionNamespace )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderReflectionData same;
    Util::addResource( same, "g_TestA", "ConstantBuffer", 0, shaderslot::vk::kBShift );
    Util::addResource( same, "g_TestB", "ConstantBuffer", 0, shaderslot::vk::kBShift );
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( same, ShaderTargetFormat::SPIRV_Vulkan, "Test" ) );

    ShaderReflectionData otherSet;
    Util::addResource( otherSet, "g_TestA", "ConstantBuffer", 0, shaderslot::vk::kBShift );
    Util::addResource( otherSet, "g_TestB", "TextureOrSampler", shaderslot::bindless::kVkTextureSet, shaderslot::vk::kBShift );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( otherSet, ShaderTargetFormat::SPIRV_Vulkan, "Test" ) );
}

/**
 * @brief [ShaderBindingValidatorTest] GL 충돌은 (이름공간, binding)이고 set 은 보지 않는다: 텍스처와 샘플러는 같은 유닛이고, UBO · SSBO · 이미지는 번호가 같아도 겹치지 않는다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, OpenGlCollisionNamespace )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );
    const ShaderTargetFormat format = ShaderTargetFormat::SPIRV_OpenGL;

    ShaderReflectionData unit;
    Util::addResource( unit, "g_TestA", "Texture", 0, 2 );
    Util::addResource( unit, "g_TestB", "Sampler", 0, 2 );
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( unit, format, "Test" ) );

    ShaderReflectionData otherNamespace;
    Util::addResource( otherNamespace, "g_TestA", "Texture", 0, 2 );
    Util::addResource( otherNamespace, "g_TestB", "ConstantBuffer", 0, 2 );
    Util::addResource( otherNamespace, "g_TestC", "StorageBuffer", 0, 2 );
    Util::addResource( otherNamespace, "g_TestD", "RWTexture", 0, 2 );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( otherNamespace, format, "Test" ) );

    ShaderReflectionData setIgnored;
    Util::addResource( setIgnored, "g_TestA", "ConstantBuffer", 0, 0 );
    Util::addResource( setIgnored, "g_TestB", "ConstantBuffer", 10, 0 );
    SW_EXPECT_EQUAL( 2u, ShaderBindingValidatorIssue::validate( setIgnored, format, "Test" ) ); // 0 이 아닌 set 하나 + 충돌 하나
}

/**
 * @brief [ShaderBindingValidatorTest] 정점 입력: 표의 시맨틱 · location 이면 통과하고, Vulkan · GL 만 location 을 본다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, VertexInputLocations )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderReflectionData matching;
    for ( const RHIVertexAttribute& attribute : constant::render::arrPackedMeshVertexAttribute )
        Util::addVertexInput( matching, attribute._pSemanticName, 0, attribute._location );
    for ( const ShaderTargetFormat format : Util::kArrFormat )
        SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( matching, format, "Test" ) );

    const RHIVertexAttribute& first = constant::render::arrPackedMeshVertexAttribute[0];
    ShaderReflectionData      shifted;
    Util::addVertexInput( shifted, first._pSemanticName, 0, first._location + 1 );
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( shifted, ShaderTargetFormat::SPIRV_Vulkan, "Test" ) );
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( shifted, ShaderTargetFormat::SPIRV_OpenGL, "Test" ) );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( shifted, ShaderTargetFormat::DXBC_D3D11, "Test" ) );
    SW_EXPECT_EQUAL( 0u, ShaderBindingValidatorIssue::validate( shifted, ShaderTargetFormat::DXIL_D3D12, "Test" ) );
}

/**
 * @brief [ShaderBindingValidatorTest] 표에 없는 시맨틱 · 0 이 아닌 시맨틱 인덱스는 어느 백엔드든 위반이고, 리소스 칸에 시맨틱과 인덱스가 적힌다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, VertexInputUnknownSemantic )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderReflectionData unknownSemantic;
    Util::addVertexInput( unknownSemantic, "NOT_A_SEMANTIC", 0, 0 );
    ShaderReflectionData secondIndex;
    Util::addVertexInput( secondIndex, constant::render::arrPackedMeshVertexAttribute[0]._pSemanticName, 1, 0 );
    for ( const ShaderTargetFormat format : Util::kArrFormat )
    {
        SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( unknownSemantic, format, "Test" ) );
        SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( secondIndex, format, "Test" ) );
    }

    vector<ShaderBindingValidatorIssue> listIssue;
    SW_EXPECT_EQUAL( 1u, ShaderBindingValidatorIssue::validate( unknownSemantic, ShaderTargetFormat::DXIL_D3D12, "Test", &listIssue ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listIssue.size() );
    SW_EXPECT_TRUE( listIssue[0]._resource == "NOT_A_SEMANTIC0" );
}

/**
 * @brief [ShaderBindingValidatorTest] 구조 버퍼 원소 배치가 맞으면 통과한다: 스칼라 4, 2 성분 8, 3 · 4 성분 · 행렬 16 경계, 크기는 가장 큰 정렬의 배수. float3 뒤의 스칼라도 된다
 */
SW_TEST_CASE( ShaderBindingValidatorTest, StructuredElementValidLayouts )
{
    using Util = ShaderBindingValidatorTestUtil;

    ShaderBufferInfo mixed{};
    mixed._name      = "g_TestElement";
    mixed._totalSize = 48;
    Util::addElementMember( mixed, "scalar", "Float", 0 );
    Util::addElementMember( mixed, "pair", "Uint2", 8 );
    Util::addElementMember( mixed, "vector3", "Float3", 16 );
    Util::addElementMember( mixed, "tail", "Uint", 28 );
    Util::addElementMember( mixed, "vector4", "Float4", 32 );
    Util::expectElementViolations( mixed, 0 );

    ShaderBufferInfo matrix{};
    matrix._name      = "g_TestElement";
    matrix._totalSize = 64;
    Util::addElementMember( matrix, "world", "Float4x4", 0 );
    Util::expectElementViolations( matrix, 0 );

    ShaderBufferInfo scalars{};
    scalars._name      = "g_TestElement";
    scalars._totalSize = 12; // 스칼라뿐이면 4 의 배수면 된다
    Util::addElementMember( scalars, "a", "Uint", 0 );
    Util::addElementMember( scalars, "b", "Bool", 4 );
    Util::addElementMember( scalars, "c", "Float", 8 );
    Util::expectElementViolations( scalars, 0 );
}

/**
 * @brief [ShaderBindingValidatorTest] 경계를 어긴 멤버마다, 그리고 크기가 정렬의 배수가 아닐 때 위반이다(DX 의 촘촘한 배치와 GL std430 이 달라지는 모양)
 */
SW_TEST_CASE( ShaderBindingValidatorTest, StructuredElementInvalidLayouts )
{
    using Util = ShaderBindingValidatorTestUtil;
    SW_EXPECT_LOG( Util::kViolationLogID );

    ShaderBufferInfo misalignedVector3{};
    misalignedVector3._name      = "g_TestElement";
    misalignedVector3._totalSize = 16;
    Util::addElementMember( misalignedVector3, "scalar", "Float", 0 );
    Util::addElementMember( misalignedVector3, "vector3", "Float3", 4 );
    Util::expectElementViolations( misalignedVector3, 1 );

    ShaderBufferInfo misalignedPair{};
    misalignedPair._name      = "g_TestElement";
    misalignedPair._totalSize = 16;
    Util::addElementMember( misalignedPair, "pair", "Uint2", 4 );
    Util::expectElementViolations( misalignedPair, 1 );

    ShaderBufferInfo misalignedMatrix{};
    misalignedMatrix._name      = "g_TestElement";
    misalignedMatrix._totalSize = 80;
    Util::addElementMember( misalignedMatrix, "world", "Float4x4", 8 );
    Util::expectElementViolations( misalignedMatrix, 1 );

    ShaderBufferInfo sizeNotMultiple{};
    sizeNotMultiple._name      = "g_TestElement";
    sizeNotMultiple._totalSize = 20;
    Util::addElementMember( sizeNotMultiple, "vector4", "Float4", 0 );
    Util::addElementMember( sizeNotMultiple, "scalar", "Float", 16 );
    Util::expectElementViolations( sizeNotMultiple, 1 );

    ShaderBufferInfo both{};
    both._name      = "g_TestElement";
    both._totalSize = 52;
    Util::addElementMember( both, "scalar", "Float", 0 );
    Util::addElementMember( both, "vector4", "Float4", 4 );
    Util::expectElementViolations( both, 2 );
}

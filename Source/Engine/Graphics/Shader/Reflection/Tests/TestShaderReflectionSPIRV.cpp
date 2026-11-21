#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/SPIRVConstants.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionUtil.h"

#include <cstring>

// ShaderReflectionSPIRVTest — SPIR-V 리플렉션: 손으로 지은 모듈에서 cbuffer 멤버 이름 · 오프셋 · 타입(정사각 아닌 행렬 포함), 깨진 길이 · 고리 진 타입에서도 멈추지 않기(nogpu).

using namespace sw;

namespace
{
    /** @brief SPIR-V 워드를 명령어 단위로 쌓습니다(헤더 다섯 워드로 시작한다). */
    struct SPIRVBuilder
    {
        vector<uint32> _listWord{ spirv::kMagic, 0x00010000u, 0u, 100u, 0u };

        void add( uint32 opcode, std::initializer_list<uint32> listOperand )
        {
            _listWord.push_back( ( static_cast<uint32>( listOperand.size() + 1 ) << 16 ) | opcode );
            _listWord.insert( _listWord.end(), listOperand.begin(), listOperand.end() );
        }

        /** @brief 이름 명령어(OpName · OpMemberName)입니다 — 글은 널 끝 · 4 바이트 채움이다. */
        void addName( uint32 opcode, std::initializer_list<uint32> listOperand, const utf8* pName )
        {
            const size_t   length = StringUtil::strlen( pName ) + 1;
            vector<uint32> listText( ( length + 3 ) / 4, 0u );
            std::memcpy( listText.data(), pName, length );
            _listWord.push_back( ( static_cast<uint32>( listOperand.size() + listText.size() + 1 ) << 16 ) | opcode );
            _listWord.insert( _listWord.end(), listOperand.begin(), listOperand.end() );
            _listWord.insert( _listWord.end(), listText.begin(), listText.end() );
        }

        vector<uint8> toBytes() const
        {
            vector<uint8> bytes( _listWord.size() * 4 );
            std::memcpy( bytes.data(), _listWord.data(), bytes.size() );
            return bytes;
        }
    };

    /** @brief `cbuffer Params { float4x3 world; float3 tint; }`(binding 2, set 1)을 담은 모듈입니다. 행렬은 DXC 처럼 float3 열 4 개다. */
    SPIRVBuilder makeConstantBufferModule()
    {
        enum : uint32
        {
            kFloat = 1,
            kFloat3,
            kMatrix,
            kBlock,
            kPointer,
            kVariable
        };
        SPIRVBuilder builder;
        builder.addName( spirv::kOpName, { kVariable }, "Params" );
        builder.addName( spirv::kOpMemberName, { kBlock, 0 }, "world" );
        builder.addName( spirv::kOpMemberName, { kBlock, 1 }, "tint" );
        builder.add( spirv::kOpDecorate, { kVariable, spirv::kDecorationBinding, 2 } );
        builder.add( spirv::kOpDecorate, { kVariable, spirv::kDecorationDescriptorSet, 1 } );
        builder.add( spirv::kOpMemberDecorate, { kBlock, 0, spirv::kDecorationOffset, 0 } );
        builder.add( spirv::kOpMemberDecorate, { kBlock, 1, spirv::kDecorationOffset, 64 } );
        builder.add( spirv::kOpTypeFloat, { kFloat, 32 } );
        builder.add( spirv::kOpTypeVector, { kFloat3, kFloat, 3 } );
        builder.add( spirv::kOpTypeMatrix, { kMatrix, kFloat3, 4 } );
        builder.add( spirv::kOpTypeStruct, { kBlock, kMatrix, kFloat3 } );
        builder.add( spirv::kOpTypePointer, { kPointer, spirv::kStorageClassUniform, kBlock } );
        builder.add( spirv::kOpVariable, { kPointer, kVariable, spirv::kStorageClassUniform } );
        return builder;
    }
} // namespace

/**
 * @brief [ShaderReflectionSPIRVTest] cbuffer 의 이름 · 자리 · 멤버 오프셋 · 타입을 읽는다 — 정사각이 아닌 행렬은 DX 리플렉터와 같은 `Float{행}x{열}` 이다
 * @details 예전에는 열 수를 두 번 써 HLSL `float4x3`(DXC 가 float3 열 4 개로 낸다)을 `Float4x4` 로 불렀다.
 */
SW_TEST_CASE( ShaderReflectionSPIRVTest, ReadsConstantBufferLayout )
{
    const expected<ShaderReflectionData> reflected = ShaderReflectionUtil::reflectSPIRV( makeConstantBufferModule().toBytes() );
    SW_REQUIRE_TRUE( reflected.has_value() );
    const ShaderReflectionData& data = *reflected;
    SW_REQUIRE_EQUAL( size_t( 1 ), data._listConstantBuffer.size() );
    const ShaderBufferInfo& buffer = data._listConstantBuffer[0];
    SW_EXPECT_EQUAL( string( "Params" ), buffer._name );
    SW_EXPECT_EQUAL( 1u, buffer._registerSpace );
    SW_EXPECT_EQUAL( 2u, buffer._bindPoint );
    SW_REQUIRE_EQUAL( size_t( 2 ), buffer._listVariable.size() );
    SW_EXPECT_EQUAL( string( "world" ), buffer._listVariable[0]._name );
    SW_EXPECT_EQUAL( string( "Float4x3" ), buffer._listVariable[0]._type );
    SW_EXPECT_EQUAL( string( "tint" ), buffer._listVariable[1]._name );
    SW_EXPECT_EQUAL( string( "Float3" ), buffer._listVariable[1]._type );
    SW_EXPECT_EQUAL( 64u, buffer._listVariable[1]._offset );
    SW_EXPECT_EQUAL( 0u, buffer._totalSize % 256u ); // 256 바이트로 맞춘다
    SW_REQUIRE_EQUAL( size_t( 1 ), data._listResource.size() );
    SW_EXPECT_TRUE( data._listResource[0]._kind == ShaderBindingKind::ConstantBuffer );
}

/** @brief [ShaderReflectionSPIRVTest] 길이가 모듈 끝을 넘는 명령어 · 고리 진 타입(벡터의 성분이 자기 자신)에서도 멈추지 않고 읽은 데까지 답한다 */
SW_TEST_CASE( ShaderReflectionSPIRVTest, MalformedModulesDoNotHang )
{
    SPIRVBuilder cyclic = makeConstantBufferModule();
    cyclic.add( spirv::kOpTypeVector, { 3, 3, 2 } ); // 열 타입을 자기 성분을 가리키는 벡터로 덮는다
    const expected<ShaderReflectionData> cyclicData = ShaderReflectionUtil::reflectSPIRV( cyclic.toBytes() );
    SW_REQUIRE_TRUE( cyclicData.has_value() );
    SW_EXPECT_EQUAL( size_t( 1 ), cyclicData->_listConstantBuffer.size() ); // 재귀가 스택을 넘기지 않는다

    SPIRVBuilder truncated = makeConstantBufferModule();
    truncated._listWord.push_back( ( 50u << 16 ) | spirv::kOpTypeStruct ); // 모듈 끝을 넘는 길이
    const expected<ShaderReflectionData> truncatedData = ShaderReflectionUtil::reflectSPIRV( truncated.toBytes() );
    SW_REQUIRE_TRUE( truncatedData.has_value() );
    SW_EXPECT_EQUAL( size_t( 1 ), truncatedData->_listConstantBuffer.size() ); // 앞까지 읽은 표로 답한다

    // 읽을 수 없는 모듈은 빈 결과가 아니라 오류다 — 빈 레이아웃으로 PSO 가 서면 슬롯이 조용히 빠진다.
    vector<uint8> badMagic = makeConstantBufferModule().toBytes();
    badMagic[0]            = 0;
    SW_EXPECT_FALSE( ShaderReflectionUtil::reflectSPIRV( badMagic ).has_value() );
    SW_EXPECT_FALSE( ShaderReflectionUtil::reflectSPIRV( vector<uint8>( 18, 0 ) ).has_value() );
}

/**
 * @brief [ShaderReflectionSPIRVTest] 빈 바이트코드 · 모르는 형식은 `reflect` 가 오류로 돌려준다
 * @details 변이: 옛 빈 결과 반환으로 되돌리면 `has_value()` 가 참이라 진다.
 */
SW_TEST_CASE( ShaderReflectionSPIRVTest, ReflectFailsInsteadOfReturningEmpty )
{
    SW_EXPECT_FALSE( ShaderReflectionData::reflect( vector<uint8>{}, ShaderTargetFormat::SPIRV_Vulkan ).has_value() );
    SW_EXPECT_FALSE( ShaderReflectionData::reflect( vector<uint8>( 64, 0 ), ShaderTargetFormat::WGSL_WebGPU ).has_value() );
    SW_EXPECT_FALSE( ShaderReflectionData::reflect( vector<uint8>( 64, 0 ), ShaderTargetFormat::Count ).has_value() );
    SW_EXPECT_TRUE( ShaderReflectionData::reflect( makeConstantBufferModule().toBytes(), ShaderTargetFormat::SPIRV_Vulkan ).has_value() );
}

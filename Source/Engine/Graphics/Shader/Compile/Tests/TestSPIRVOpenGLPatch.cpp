#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Shader/Compile/SPIRVOpenGLPatch.h"
#include "Engine/Graphics/Shader/Reflection/SPIRVConstants.h"

using namespace sw;

// GL 용 SPIR-V 패치(SPIRVOpenGLPatch) — 손으로 짠 작은 모듈로 본다. 디바이스 · DXC 없음.

namespace
{
    struct SPIRVOpenGLPatchTestInternal
    {
        static void append( vector<uint32>& inoutWord, uint32 opcode, std::initializer_list<uint32> listOperand )
        {
            inoutWord.push_back( ( static_cast<uint32>( listOperand.size() + 1u ) << 16 ) | opcode );
            inoutWord.insert( inoutWord.end(), listOperand.begin(), listOperand.end() );
        }

        /** @brief 구조체 %5 { int, int } — 멤버 1 에만 이름이 있다(DXC 의 `type.StructuredBuffer.T` 처럼 이름 빠진 멤버가 있다). */
        static vector<uint8> makeModule()
        {
            vector<uint32> listWord{ spirv::kMagic, 0x00010000u, 0u, 10u, 0u };
            append( listWord, spirv::kOpCapability, { 1u } );
            append( listWord, spirv::kOpMemoryModel, { 0u, 1u } );
            append( listWord, spirv::kOpName, { 5u, 0x00000053u } );           // "S"
            append( listWord, spirv::kOpMemberName, { 5u, 1u, 0x00000062u } ); // 멤버 1 "b"
            append( listWord, spirv::kOpDecorate, { 5u, 2u } );                // Block
            append( listWord, spirv::kOpTypeInt, { 2u, 32u, 0u } );
            append( listWord, spirv::kOpTypeStruct, { 5u, 2u, 2u } );
            const uint8* pByte = reinterpret_cast<const uint8*>( listWord.data() );
            return vector<uint8>( pByte, pByte + listWord.size() * 4u );
        }
    };
} // namespace

/**
 * @brief [SPIRVOpenGLPatchTest] 이름 없는 구조체 멤버에만 `_m<번호>` 이름이 붙고, 그 명령이 첫 장식 앞(디버그 이름 절 끝)에 들어가는지 검증
 */
SW_TEST_CASE( SPIRVOpenGLPatchTest, NamesOnlyUnnamedMembersBeforeAnnotations )
{
    vector<uint8> bytecode = SPIRVOpenGLPatchTestInternal::makeModule();
    SPIRVOpenGLPatch::nameStructMembers( bytecode );

    const uint32*  pWord     = reinterpret_cast<const uint32*>( bytecode.data() );
    const size_t   wordCount = bytecode.size() / 4u;
    vector<uint32> listOpcode;
    uint32         addedMember = 99u;
    uint32         addedName   = 0u;
    for ( size_t offset = 5; offset < wordCount; offset += pWord[offset] >> 16 )
    {
        const uint32 opcode = pWord[offset] & 0xFFFFu;
        listOpcode.push_back( opcode );
        if ( opcode == spirv::kOpMemberName && pWord[offset + 2] == 0u )
        {
            addedMember = pWord[offset + 2];
            addedName   = pWord[offset + 3];
        }
    }
    // 능력 · 메모리 모델 · 이름 · 멤버 1 이름 · 새 멤버 0 이름 · 장식 · 타입 둘 — 멤버 1 은 다시 이름을 받지 않는다.
    const vector<uint32> listExpected{ spirv::kOpCapability, spirv::kOpMemoryModel, spirv::kOpName, spirv::kOpMemberName,
                                       spirv::kOpMemberName, spirv::kOpDecorate, spirv::kOpTypeInt, spirv::kOpTypeStruct };
    SW_EXPECT_TRUE( listOpcode == listExpected );
    SW_EXPECT_EQUAL( 0u, addedMember );
    SW_EXPECT_EQUAL( 0x00306D5Fu, addedName ); // "_m0" + 널

    // 두 번 걸어도 늘지 않는다(이미 이름이 있다).
    const size_t patchedSize = bytecode.size();
    SPIRVOpenGLPatch::nameStructMembers( bytecode );
    SW_EXPECT_EQUAL( patchedSize, bytecode.size() );
}

/**
 * @brief [SPIRVOpenGLPatchTest] SPIR-V 가 아닌 바이트는 그대로 두는지 검증
 */
SW_TEST_CASE( SPIRVOpenGLPatchTest, LeavesNonSPIRVUntouched )
{
    vector<uint8>       bytes( 24u, 0xABu );
    const vector<uint8> original = bytes;
    SPIRVOpenGLPatch::apply( bytes );
    SW_EXPECT_TRUE( bytes == original );
}

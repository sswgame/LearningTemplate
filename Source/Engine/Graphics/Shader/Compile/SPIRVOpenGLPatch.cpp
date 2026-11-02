#include "pch.h"

#include "Engine/Graphics/Shader/Compile/SPIRVOpenGLPatch.h"

#include "Core/Container/unordered_set.h"

#include "Engine/Graphics/Shader/Reflection/SPIRVConstants.h"

namespace sw
{
    namespace
    {
        struct SPIRVOpenGLPatchInternal
        {
            /** @brief 디버그 이름 절까지(능력 · 확장 · 메모리 모델 · 진입점 · 실행 모드 · 소스 · 이름)의 명령이면 true 입니다 — 그 뒤가 장식 · 타입 절이다. */
            static bool isPreambleOpcode( uint32 opcode )
            {
                constexpr uint32 arrPreamble[] = { spirv::kOpSourceContinued, spirv::kOpSource, spirv::kOpSourceExtension, spirv::kOpName, spirv::kOpMemberName,
                                                   spirv::kOpString, spirv::kOpExtension, spirv::kOpExtInstImport, spirv::kOpMemoryModel, spirv::kOpEntryPoint,
                                                   spirv::kOpExecutionMode, spirv::kOpCapability, spirv::kOpModuleProcessed, spirv::kOpExecutionModeId };
                for ( const uint32 preamble : arrPreamble )
                {
                    if ( opcode == preamble )
                        return true;
                }
                return false;
            }

            /** @brief 매직이 맞고 단어로 나뉘는 모듈이면 단어 수, 아니면 0 입니다. */
            static size_t countWords( const vector<uint8>& bytecode )
            {
                const bool bShaped = bytecode.size() >= 20 && ( bytecode.size() % 4 ) == 0;
                return bShaped && reinterpret_cast<const uint32*>( bytecode.data() )[0] == spirv::kMagic ? bytecode.size() / 4 : 0;
            }
        };
    } // namespace

    void SPIRVOpenGLPatch::apply( vector<uint8>& ioBytecode )
    {
        patchBuiltins( ioBytecode );
        nameStructMembers( ioBytecode );
    }

    void SPIRVOpenGLPatch::patchBuiltins( vector<uint8>& ioBytecode )
    {
        const size_t wordCount = SPIRVOpenGLPatchInternal::countWords( ioBytecode );
        uint32*      pWord     = reinterpret_cast<uint32*>( ioBytecode.data() );
        size_t       offset    = 5;
        while ( offset < wordCount )
        {
            const uint32 opcode = pWord[offset] & 0xFFFFu;
            const uint32 length = pWord[offset] >> 16;
            if ( length == 0 || offset + length > wordCount )
                break;
            uint32*    pValue             = nullptr;
            const bool bMemberBuiltIn     = opcode == spirv::kOpMemberDecorate && length >= 5 && pWord[offset + 3] == spirv::kDecorationBuiltIn;
            const bool bBuiltInDecoration = opcode == spirv::kOpDecorate && length >= 4 && pWord[offset + 2] == spirv::kDecorationBuiltIn;
            if ( bBuiltInDecoration )
                pValue = &pWord[offset + 3];
            else if ( bMemberBuiltIn )
                pValue = &pWord[offset + 4];
            if ( pValue != nullptr )
            {
                if ( *pValue == spirv::kBuiltInInstanceIndex )
                    *pValue = spirv::kBuiltInInstanceID;
                else if ( *pValue == spirv::kBuiltInVertexIndex )
                    *pValue = spirv::kBuiltInVertexID;
            }
            offset += length;
        }
    }

    void SPIRVOpenGLPatch::nameStructMembers( vector<uint8>& ioBytecode )
    {
        const size_t                      wordCount = SPIRVOpenGLPatchInternal::countWords( ioBytecode );
        const uint32*                     pWord     = reinterpret_cast<const uint32*>( ioBytecode.data() );
        vector<std::pair<uint32, uint32>> listStruct; // (구조체 번호, 멤버 수)
        unordered_set<uint64>             setNamed;   // (구조체 번호 << 32) | 멤버
        size_t                            insertAt = 0;
        size_t                            offset   = 5;
        while ( offset < wordCount )
        {
            const uint32 opcode = pWord[offset] & 0xFFFFu;
            const uint32 length = pWord[offset] >> 16;
            if ( length == 0 || offset + length > wordCount )
                return; // 깨진 모듈은 건드리지 않는다
            if ( insertAt == 0 && SPIRVOpenGLPatchInternal::isPreambleOpcode( opcode ) == false )
                insertAt = offset;
            if ( opcode == spirv::kOpTypeStruct && length >= 2 )
                listStruct.emplace_back( pWord[offset + 1], length - 2u );
            if ( opcode == spirv::kOpMemberName && length >= 4 )
                setNamed.insert( ( static_cast<uint64>( pWord[offset + 1] ) << 32 ) | pWord[offset + 2] );
            offset += length;
        }
        vector<uint32> listInsert;
        for ( const auto& [structID, memberCount] : listStruct )
        {
            for ( uint32 member = 0; member < memberCount; ++member )
            {
                if ( setNamed.count( ( static_cast<uint64>( structID ) << 32 ) | member ) != 0 )
                    continue;
                // 문자열은 널로 끝나고 단어 경계까지 0 으로 채운다(SPIR-V 리터럴 문자열).
                const string   name        = "_m" + to_string( member );
                const uint32   stringWords = static_cast<uint32>( name.size() / 4u + 1u );
                const uint32   length      = 3u + stringWords;
                vector<uint32> listString( stringWords, 0u );
                std::memcpy( listString.data(), name.data(), name.size() );
                listInsert.push_back( ( length << 16 ) | spirv::kOpMemberName );
                listInsert.push_back( structID );
                listInsert.push_back( member );
                listInsert.insert( listInsert.end(), listString.begin(), listString.end() );
            }
        }
        if ( listInsert.empty() || insertAt == 0 )
            return;
        const uint8* pInsert = reinterpret_cast<const uint8*>( listInsert.data() );
        ioBytecode.insert( ioBytecode.begin() + static_cast<std::ptrdiff_t>( insertAt * 4u ), pInsert, pInsert + listInsert.size() * 4u );
    }
} // namespace sw

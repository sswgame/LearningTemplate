#include "pch.h"

#include "Core/Container/unordered_set.h"

#include "Engine/Graphics/Shader/Reflection/ShaderReflectionUtil.h"
#include "Engine/Graphics/Shader/Reflection/SpirvConstants.h"

namespace sw
{
    namespace
    {
        struct ShaderReflectionSpirvInternal
        {

            struct SpirvType
            {
                enum class Kind : uint8
                {
                    Unknown,
                    Bool,
                    Int,
                    Uint,
                    Float,
                    Vector,
                    Matrix,
                    Struct,
                    Pointer,
                    Image,
                    Sampler,
                    SampledImage,
                    Array,       ///< OpTypeArray. _subTypeId 원소, _count 길이(상수 id 를 풀어 둔 값), _arrayStride
                    RuntimeArray ///< OpTypeRuntimeArray. 무제한 배열(bindless `T name[]`)
                };

                Kind           _kind  = Kind::Unknown;
                uint32         _width = 32;
                uint32         _count{ 0 };
                uint32         _subTypeId{ 0 };
                uint32         _storageClass{ 0 };
                vector<uint32> _listMemberTypeId;
                uint32         _arrayStride{ 0 };
            };

            static string resolveSpirvTypeName( uint32 typeId, const unordered_map<uint32, SpirvType>& mapType, uint32& outSize )
            {
                auto it = mapType.find( typeId );
                if ( it == mapType.end() )
                {
                    outSize = 4;
                    return "Float";
                }

                const SpirvType& spirvType = it->second;
                switch ( spirvType._kind )
                {
                    case SpirvType::Kind::Bool:
                    {
                        outSize = 4;
                        return "Bool";
                    }
                    case SpirvType::Kind::Int:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Int64" : ( spirvType._width == 16 ? "Int16" : "Int" );
                    }
                    case SpirvType::Kind::Uint:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Uint64" : ( spirvType._width == 16 ? "Uint16" : "Uint" );
                    }
                    case SpirvType::Kind::Float:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Double" : "Float";
                    }
                    case SpirvType::Kind::Vector:
                    {
                        uint32 subSize = 4;
                        string subName = resolveSpirvTypeName( spirvType._subTypeId, mapType, subSize );
                        outSize        = subSize * spirvType._count;
                        return subName + to_string( spirvType._count );
                    }
                    case SpirvType::Kind::Matrix:
                    {
                        uint32 columnSize = 16;
                        resolveSpirvTypeName( spirvType._subTypeId, mapType, columnSize );
                        outSize = columnSize * spirvType._count;
                        if ( spirvType._count == 4 )
                            return "Float4x4";
                        return "Float" + to_string( spirvType._count ) + "x" + to_string( spirvType._count );
                    }
                    case SpirvType::Kind::Array:
                    {
                        // 배열 크기 = (원소 수 - 1) * stride + 원소 크기(마지막 원소 뒤 패딩 없음. DX 리플렉션과 같은 규칙).
                        uint32       elemSize = 4;
                        const string elemName = resolveSpirvTypeName( spirvType._subTypeId, mapType, elemSize );
                        const uint32 stride   = spirvType._arrayStride > 0 ? spirvType._arrayStride : elemSize;
                        outSize               = spirvType._count > 0 ? ( spirvType._count - 1 ) * stride + elemSize : elemSize;
                        return elemName;
                    }
                    case SpirvType::Kind::Unknown:
                    case SpirvType::Kind::Struct:
                    case SpirvType::Kind::Pointer:
                    case SpirvType::Kind::Image:
                    case SpirvType::Kind::Sampler:
                    case SpirvType::Kind::SampledImage:
                    case SpirvType::Kind::RuntimeArray:
                    {
                        outSize = 4;
                        return "Float";
                    }
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderReflection" );

    ShaderReflectionData ShaderReflectionUtil::reflectSpirv( const vector<uint8>& bytecode )
    {
        if ( bytecode.size() < 20 || ( bytecode.size() % 4 ) != 0 )
        {
            SW_LOG_WARNING( "SPIR-V bytecode size invalid." );
            return {};
        }

        const uint32* pWords    = reinterpret_cast<const uint32*>( bytecode.data() );
        const size_t  wordCount = bytecode.size() / 4;
        if ( pWords[0] != spirv::kMagic )
        {
            SW_LOG_WARNING( "Not a SPIR-V module (bad magic)." );
            return {};
        }

        ShaderReflectionData data{};

        unordered_map<uint32, string>                                   mapName;
        unordered_map<uint32, unordered_map<uint32, string>>            mapMemberName;
        unordered_map<uint32, uint32>                                   mapBinding;
        unordered_map<uint32, uint32>                                   mapDescriptorSet;
        unordered_map<uint32, uint32>                                   mapLocation; ///< OpDecorate Location. 정점 입력 변수
        uint32                                                          executionModel{ invalid_index::kUint32 };
        unordered_map<uint32, uint32>                                   mapArrayStride;
        unordered_map<uint32, uint32>                                   mapConstantValue;      ///< OpConstant(32비트 정수). 배열 길이 해석용
        unordered_set<uint32>                                           uniqueBufferBlockType; ///< BufferBlock 데코레이션이 붙은 구조체 타입 id
        unordered_map<uint32, unordered_map<uint32, uint32>>            mapMemberOffset;
        unordered_map<uint32, ShaderReflectionSpirvInternal::SpirvType> mapType;

        struct VariableInfo
        {
            uint32 _storageClass{ 0 };
            uint32 _typeId{ 0 };
        };
        unordered_map<uint32, VariableInfo> mapVariable;

        size_t offset = 5;
        while ( offset < wordCount )
        {
            const uint32 first = pWords[offset];
            // SPIR-V 명령어 헤더: 상위 16비트 = WordCount, 하위 16비트 = Opcode (스펙 2.3 "Physical Layout").
            const uint32 instructionWordCount = first >> 16;
            const uint32 opcode               = first & 0xFFFFu;
            if ( instructionWordCount == 0 || offset + instructionWordCount > wordCount )
                break;

            if ( opcode == spirv::kOpEntryPoint && instructionWordCount >= 3 )
            {
                executionModel = pWords[offset + 1];
            }
            else if ( opcode == spirv::kOpName && instructionWordCount >= 3 )
            {
                const uint32 target = pWords[offset + 1];
                const utf8*  pStr   = reinterpret_cast<const utf8*>( &pWords[offset + 2] );
                const size_t maxLen = static_cast<size_t>( instructionWordCount - 2 ) * 4;
                mapName[target]     = string( pStr, strnlen( pStr, maxLen ) );
            }
            else if ( opcode == spirv::kOpMemberName && instructionWordCount >= 4 )
            {
                const uint32 target                = pWords[offset + 1];
                const uint32 memberIndex           = pWords[offset + 2];
                const utf8*  pStr                  = reinterpret_cast<const utf8*>( &pWords[offset + 3] );
                const size_t maxLen                = static_cast<size_t>( instructionWordCount - 3 ) * 4;
                mapMemberName[target][memberIndex] = string( pStr, strnlen( pStr, maxLen ) );
            }
            else if ( opcode == spirv::kOpDecorate && instructionWordCount >= 3 )
            {
                const uint32 target     = pWords[offset + 1];
                const uint32 decoration = pWords[offset + 2];
                if ( decoration == spirv::kDecorationBinding && instructionWordCount >= 4 )
                    mapBinding[target] = pWords[offset + 3];
                else if ( decoration == spirv::kDecorationDescriptorSet && instructionWordCount >= 4 )
                    mapDescriptorSet[target] = pWords[offset + 3];
                else if ( decoration == spirv::kDecorationArrayStride && instructionWordCount >= 4 )
                    mapArrayStride[target] = pWords[offset + 3];
                else if ( decoration == spirv::kDecorationBufferBlock )
                    uniqueBufferBlockType.insert( target );
                else if ( decoration == spirv::kDecorationLocation && instructionWordCount >= 4 )
                    mapLocation[target] = pWords[offset + 3];
            }
            else if ( opcode == spirv::kOpMemberDecorate && instructionWordCount >= 5 )
            {
                const uint32 target      = pWords[offset + 1];
                const uint32 memberIndex = pWords[offset + 2];
                const uint32 decoration  = pWords[offset + 3];
                if ( decoration == spirv::kDecorationOffset )
                    mapMemberOffset[target][memberIndex] = pWords[offset + 4];
            }
            else if ( opcode == spirv::kOpTypeBool && instructionWordCount >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Bool, 32, 1, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeInt && instructionWordCount >= 4 )
            {
                const uint32 id         = pWords[offset + 1];
                const uint32 width      = pWords[offset + 2];
                const uint32 signedness = pWords[offset + 3];
                mapType[id]             = ShaderReflectionSpirvInternal::SpirvType{ signedness ? ShaderReflectionSpirvInternal::SpirvType::Kind::Int : ShaderReflectionSpirvInternal::SpirvType::Kind::Uint, width, 1, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeFloat && instructionWordCount >= 3 )
            {
                const uint32 id    = pWords[offset + 1];
                const uint32 width = pWords[offset + 2];
                mapType[id]        = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Float, width, 1, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeVector && instructionWordCount >= 4 )
            {
                const uint32 id       = pWords[offset + 1];
                const uint32 compType = pWords[offset + 2];
                const uint32 count    = pWords[offset + 3];
                mapType[id]           = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Vector, 32, count, compType, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeMatrix && instructionWordCount >= 4 )
            {
                const uint32 id         = pWords[offset + 1];
                const uint32 columnType = pWords[offset + 2];
                const uint32 count      = pWords[offset + 3];
                mapType[id]             = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Matrix, 32, count, columnType, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeImage && instructionWordCount >= 2 )
            {
                // OpTypeImage: result, sampled type, Dim, Depth, Arrayed, MS, Sampled(1 = 샘플, 2 = 스토리지), Format.
                // Sampled 를 _count 에 담아 두면 리소스 분류가 RWTexture(스토리지 이미지)를 가릴 수 있다.
                const uint32 sampled        = instructionWordCount >= 8 ? pWords[offset + 7] : 0;
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Image, 0, sampled, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeSampler && instructionWordCount >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Sampler, 0, 0, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeSampledImage && instructionWordCount >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::SampledImage, 0, 0, 0, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeArray && instructionWordCount >= 4 )
            {
                const uint32 id       = pWords[offset + 1];
                const uint32 elemType = pWords[offset + 2];
                const uint32 lengthId = pWords[offset + 3];
                const auto   lenIt    = mapConstantValue.find( lengthId );
                const uint32 length   = ( lenIt != mapConstantValue.end() ) ? lenIt->second : 0;
                mapType[id]           = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Array, 0, length, elemType, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeRuntimeArray && instructionWordCount >= 3 )
            {
                const uint32 id       = pWords[offset + 1];
                const uint32 elemType = pWords[offset + 2];
                mapType[id]           = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::RuntimeArray, 0, 0, elemType, 0, {}, 0 };
            }
            else if ( opcode == spirv::kOpTypeStruct && instructionWordCount >= 2 )
            {
                const uint32                             id = pWords[offset + 1];
                ShaderReflectionSpirvInternal::SpirvType st{ ShaderReflectionSpirvInternal::SpirvType::Kind::Struct, 0, 0, 0, 0, {}, 0 };
                for ( uint32 wordIndex = 2; wordIndex < instructionWordCount; ++wordIndex )
                {
                    st._listMemberTypeId.push_back( pWords[offset + wordIndex] );
                }
                mapType[id] = std::move( st );
            }
            else if ( opcode == spirv::kOpTypePointer && instructionWordCount >= 4 )
            {
                const uint32 id           = pWords[offset + 1];
                const uint32 storageClass = pWords[offset + 2];
                const uint32 subType      = pWords[offset + 3];
                mapType[id]               = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Pointer, 0, 0, subType, storageClass, {}, 0 };
            }
            else if ( opcode == spirv::kOpConstant && instructionWordCount >= 4 )
            {
                // 32비트 정수 상수만(배열 길이). 타입 확인은 생략한다. 길이 id 로 조회할 때만 쓴다.
                mapConstantValue[pWords[offset + 2]] = pWords[offset + 3];
            }
            else if ( opcode == spirv::kOpVariable && instructionWordCount >= 4 )
            {
                const uint32 typeId       = pWords[offset + 1];
                const uint32 resultId     = pWords[offset + 2];
                const uint32 storageClass = pWords[offset + 3];
                mapVariable[resultId]     = VariableInfo{ storageClass, typeId };
            }

            offset += instructionWordCount;
        }

        // ArrayStride 데코레이션은 타입 정의 앞에 온다. 지금 붙인다.
        for ( const auto& [typeId, stride] : mapArrayStride )
        {
            auto it = mapType.find( typeId );
            if ( it != mapType.end() )
                it->second._arrayStride = stride;
        }

        // 변수의 포인터 → 배열(무제한/고정) → 구조체 순으로 벗겨 "블록 구조체" 타입 id 를 반환한다.
        // bindless `ConstantBuffer<T> name[]` 는 Uniform 포인터 → OpTypeRuntimeArray → Block 구조체다.
        // outIsArray 는 무제한/고정 배열이었으면 true 다(bindCount 0 으로 보고한다).
        auto resolveBlockStruct = [&]( uint32 pointerTypeId, bool& outIsArray ) -> uint32
        {
            outIsArray = false;
            auto ptrIt = mapType.find( pointerTypeId );
            if ( ptrIt == mapType.end() || ptrIt->second._kind != ShaderReflectionSpirvInternal::SpirvType::Kind::Pointer )
                return 0;
            uint32 typeId = ptrIt->second._subTypeId;
            for ( uint32 depth = 0; depth < 2; ++depth )
            {
                auto it = mapType.find( typeId );
                if ( it == mapType.end() )
                    return 0;
                if ( it->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::RuntimeArray ||
                     it->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Array )
                {
                    outIsArray = true;
                    typeId     = it->second._subTypeId;
                    continue;
                }
                break;
            }
            return typeId;
        };

        /** 구조체 멤버들을 CB 멤버 표에 넣습니다(baseOffset 은 감싼 구조체 안에서의 시작 오프셋입니다). */
        auto appendStructMembers = [&]( uint32 structTypeId, uint32 baseOffset, ShaderBufferInfo& outBuffer )
        {
            auto structTypeIt = mapType.find( structTypeId );
            if ( structTypeIt == mapType.end() || structTypeIt->second._kind != ShaderReflectionSpirvInternal::SpirvType::Kind::Struct )
                return;
            const auto& memberNameMap   = mapMemberName[structTypeId];
            const auto& memberOffsetMap = mapMemberOffset[structTypeId];

            uint32 memberIdx{ 0 };
            for ( uint32 memberTypeId : structTypeIt->second._listMemberTypeId )
            {
                ShaderVariableInfo varInfo{};
                auto               mNameIt = memberNameMap.find( memberIdx );
                if ( mNameIt != memberNameMap.end() )
                    varInfo._name = mNameIt->second;
                else
                    varInfo._name = string( "member_" ) + to_string( memberIdx );

                auto mOffIt     = memberOffsetMap.find( memberIdx );
                varInfo._offset = baseOffset + ( ( mOffIt != memberOffsetMap.end() ) ? mOffIt->second : 0 );
                varInfo._type   = ShaderReflectionSpirvInternal::resolveSpirvTypeName( memberTypeId, mapType, varInfo._size );

                outBuffer._listVariable.push_back( varInfo );
                outBuffer._totalSize = MathUtil::max( outBuffer._totalSize, varInfo._offset + varInfo._size );
                ++memberIdx;
            }
        };

        for ( const auto& [id, var] : mapVariable )
        {
            const auto nameIt    = mapName.find( id );
            const auto bindingIt = mapBinding.find( id );
            const auto setIt     = mapDescriptorSet.find( id );
            if ( bindingIt == mapBinding.end() )
                continue; // 푸시 상수 · 입출력 변수. 바인딩 자리가 없다

            string name;
            if ( nameIt != mapName.end() )
                name = nameIt->second;
            else
                name = string( "Resource_" ) + to_string( id );

            const uint32 space     = ( setIt != mapDescriptorSet.end() ) ? setIt->second : 0;
            const uint32 bindPoint = bindingIt->second;

            bool         bIsArray{ false };
            const uint32 blockTypeId = resolveBlockStruct( var._typeId, bIsArray );

            // SSBO 는 SPIR-V 버전에 따라 **두 가지**로 표현된다. StorageBuffer 저장 클래스(1.4+ / Vulkan 1.1+
            // 타깃)이거나, 구식으로는 Uniform 저장 클래스에 구조체 타입이 BufferBlock 으로 데코레이션된다.
            // 둘 다 봐야 한다. 앞쪽만 보면 GL 용 SPIR-V 의 StructuredBuffer 가 상수버퍼로 분류돼, 바인더가 그것을 CB 슬롯으로
            // 걸고 bindStructuredBuffer 는 부르지 않는다(GL 에서 인스턴스 행렬이 모두 0 으로 읽혀 메시가 하나도 안 그려진다).
            bool bIsStorageBuffer = ( var._storageClass == spirv::kStorageClassStorageBuffer );
            if ( bIsStorageBuffer == false && var._storageClass == spirv::kStorageClassUniform &&
                 blockTypeId != 0 && uniqueBufferBlockType.find( blockTypeId ) != uniqueBufferBlockType.end() )
                bIsStorageBuffer = true;

            // 진짜 cbuffer(Uniform storage class)만 "CB 레이아웃(멤버 오프셋 포함)" 으로 채운다.
            // StructuredBuffer/RWStructuredBuffer(StorageBuffer storage class)는 구조체 원소 타입을 똑같은
            // 방식으로 반영하지만 실제 cbuffer 가 아니다. 여기 포함시키면 ShaderBindingLayout 이
            // ConstantBuffer 종류의 "가짜 CB" 슬롯(멤버 이름이 원소 구조체 필드와 겹침)을 만들어
            // 엔진 CB 버퍼에 엉뚱한 오프셋으로 값을 덮어쓸 수 있다. 아래 리소스 루프가 StructuredBuffer 로
            // 올바르게 분류해 따로 처리한다.
            if ( var._storageClass == spirv::kStorageClassUniform && bIsStorageBuffer == false )
            {
                ShaderBufferInfo buf{};
                buf._name          = name;
                buf._registerSpace = space;
                buf._bindPoint     = bindPoint;
                buf._totalSize     = 0;

                auto blockIt = mapType.find( blockTypeId );
                if ( blockIt != mapType.end() && blockIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Struct )
                {
                    // 엔진 cbuffer 는 맨 필드지만, `cbuffer { name_t x; }` 처럼 구조체 하나로 감싼 cbuffer 는 블록 멤버가
                    // 구조체 하나다. 엔진과 머티리얼 패커는 필드 이름으로 찾으므로 한 겹 벗긴다(DX 리플렉터와 같은 규칙).
                    const auto& listMember = blockIt->second._listMemberTypeId;
                    uint32      innerId{ 0 };
                    if ( listMember.size() == 1 )
                    {
                        auto innerIt = mapType.find( listMember[0] );
                        if ( innerIt != mapType.end() && innerIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Struct )
                            innerId = listMember[0];
                    }
                    if ( innerId != 0 )
                    {
                        const auto&  offsetMap  = mapMemberOffset[blockTypeId];
                        const auto   offIt      = offsetMap.find( 0u ); // 키가 uint32 다. 부호 있는 리터럴이면 이종 키 오버로드로 샌다
                        const uint32 baseOffset = ( offIt != offsetMap.end() ) ? offIt->second : 0;
                        appendStructMembers( innerId, baseOffset, buf );
                    }
                    else
                    {
                        appendStructMembers( blockTypeId, 0, buf );
                    }
                    buf._totalSize = MathUtil::align( buf._totalSize, 256u );
                }

                data._listConstantBuffer.push_back( std::move( buf ) );
            }

            // StructuredBuffer<T> 의 원소 레이아웃: 블록 { T arr[] } 의 멤버 0 이 (런타임)배열이고 원소가 구조체면
            // 그 필드들을 원소 레이아웃으로 낸다. stride 는 배열 타입의 ArrayStride 다(std430: 예 float4+float+uint = 32).
            if ( bIsStorageBuffer && blockTypeId != 0 )
            {
                auto blockIt = mapType.find( blockTypeId );
                if ( blockIt != mapType.end() && blockIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Struct &&
                     blockIt->second._listMemberTypeId.size() == 1 )
                {
                    auto arrIt = mapType.find( blockIt->second._listMemberTypeId[0] );
                    if ( arrIt != mapType.end() &&
                         ( arrIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::RuntimeArray ||
                           arrIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Array ) )
                    {
                        auto elemIt = mapType.find( arrIt->second._subTypeId );
                        if ( elemIt != mapType.end() && elemIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Struct )
                        {
                            ShaderBufferInfo element{};
                            element._name          = name;
                            element._registerSpace = space;
                            element._bindPoint     = bindPoint;
                            appendStructMembers( arrIt->second._subTypeId, 0, element );
                            element._totalSize = arrIt->second._arrayStride > 0 ? arrIt->second._arrayStride : element._totalSize;
                            if ( element._listVariable.empty() == false )
                                data._listStructuredElement.push_back( std::move( element ) );
                        }
                    }
                }
            }

            ShaderReflectedBinding res{};
            res._name          = name;
            res._registerSpace = space;
            res._bindPoint     = bindPoint;
            res._bindCount     = bIsArray ? 0u : 1u; ///< 무제한 배열([])은 0. DX 리플렉션의 BindCount 와 같은 뜻
            if ( bIsStorageBuffer )
                res._type = "StorageBuffer";
            else if ( var._storageClass == spirv::kStorageClassUniform )
                res._type = "ConstantBuffer";
            else if ( var._storageClass == spirv::kStorageClassUniformConstant )
            {
                // 포인터의 원소 타입으로 샘플러만 가른다. 배열(bindless)은 원소를 따라가서 본다.
                res._type      = "TextureOrSampler";
                auto pointeeIt = mapType.find( blockTypeId );
                if ( pointeeIt != mapType.end() && pointeeIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Sampler )
                    res._type = "Sampler";
                else if ( pointeeIt != mapType.end() && pointeeIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Image && pointeeIt->second._count == 2 )
                    res._type = "RWTexture"; // 스토리지 이미지(RWTexture2D)
            }
            else
                res._type = "OtherResource";
            data._listResource.push_back( std::move( res ) );
        }

        // 정점 입력 — Input 저장 클래스에 Location 이 붙은 변수. DXC 는 `in.var.<SEMANTIC>` 로 이름을 남기고
        // 시스템 값(SV_VertexID 등)은 BuiltIn 이라 Location 이 없다.
        if ( executionModel == spirv::kExecutionModelVertex )
        {
            for ( const auto& [id, var] : mapVariable )
            {
                if ( var._storageClass != spirv::kStorageClassInput )
                    continue;
                auto locationIt = mapLocation.find( id );
                if ( locationIt == mapLocation.end() )
                    continue;
                string name;
                auto   nameIt = mapName.find( id );
                if ( nameIt != mapName.end() )
                    name = nameIt->second;
                constexpr string_view kPrefix = "in.var.";
                if ( name.size() >= kPrefix.size() && string_view( name ).substr( 0, kPrefix.size() ) == kPrefix )
                    name = name.substr( kPrefix.size() );

                ShaderVertexInputInfo input{};
                input._location   = locationIt->second;
                size_t digitBegin = name.size();
                while ( digitBegin > 0 && name[digitBegin - 1] >= '0' && name[digitBegin - 1] <= '9' )
                {
                    --digitBegin;
                }
                input._semantic = name.substr( 0, digitBegin );
                for ( size_t digitIndex = digitBegin; digitIndex < name.size(); ++digitIndex )
                {
                    input._semanticIndex = input._semanticIndex * 10 + static_cast<uint32>( name[digitIndex] - '0' );
                }
                data._listVertexInput.push_back( std::move( input ) );
            }
            std::sort( data._listVertexInput.begin(), data._listVertexInput.end(),
                       []( const ShaderVertexInputInfo& lhs, const ShaderVertexInputInfo& rhs )
            { return lhs._location < rhs._location; } );
        }

        SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %# VertexInputs: %#",
                      data._listConstantBuffer.size(), data._listResource.size(), data._listVertexInput.size() );
        return data;
    }

} // namespace sw

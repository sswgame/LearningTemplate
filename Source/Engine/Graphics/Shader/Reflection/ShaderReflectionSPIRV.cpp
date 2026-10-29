#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Graphics/Shader/Reflection/SPIRVConstants.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionUtil.h"

namespace sw
{
    namespace
    {
        struct ShaderReflectionSPIRVInternal
        {

            struct SPIRVType
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
                    Array,       ///< OpTypeArray. _subTypeID 원소, _count 길이(상수 id 를 풀어 둔 값), _arrayStride
                    RuntimeArray ///< OpTypeRuntimeArray. 무제한 배열(bindless `T name[]`)
                };

                Kind           _kind  = Kind::Unknown;
                uint32         _width = 32;
                uint32         _count{ 0 };
                uint32         _subTypeID{ 0 };
                uint32         _storageClass{ 0 };
                vector<uint32> _listMemberTypeID;
                uint32         _arrayStride{ 0 };
            };

            /** @brief OpVariable 하나입니다. */
            struct SPIRVVariable
            {
                uint32 _storageClass{ 0 };
                uint32 _typeID{ 0 };
            };

            /** @brief 명령어를 한 번 훑어 모은 이름 · 데코레이션 · 타입 · 변수 표입니다. 리플렉션 단계는 이것만 읽습니다. */
            struct SPIRVModule
            {
                unordered_map<uint32, string>                        _mapName;
                unordered_map<uint32, unordered_map<uint32, string>> _mapMemberName;
                unordered_map<uint32, uint32>                        _mapBinding;
                unordered_map<uint32, uint32>                        _mapDescriptorSet;
                unordered_map<uint32, uint32>                        _mapLocation; ///< OpDecorate Location. 정점 입력 변수
                uint32                                               _executionModel{ invalid_index::kUint32 };
                unordered_map<uint32, uint32>                        _mapArrayStride;
                unordered_map<uint32, uint32>                        _mapConstantValue;      ///< OpConstant(32비트 정수). 배열 길이 해석용
                unordered_set<uint32>                                _uniqueBufferBlockType; ///< BufferBlock 데코레이션이 붙은 구조체 타입 id
                unordered_map<uint32, unordered_map<uint32, uint32>> _mapMemberOffset;
                unordered_map<uint32, SPIRVType>                     _mapType;
                unordered_map<uint32, SPIRVVariable>                 _mapVariable;
            };

            static string resolveSPIRVTypeName( uint32 typeID, const unordered_map<uint32, SPIRVType>& mapType, uint32& outSize )
            {
                auto it = mapType.find( typeID );
                if ( it == mapType.end() )
                {
                    outSize = 4;
                    return "Float";
                }

                const SPIRVType& spirvType = it->second;
                switch ( spirvType._kind )
                {
                    case SPIRVType::Kind::Bool:
                    {
                        outSize = 4;
                        return "Bool";
                    }
                    case SPIRVType::Kind::Int:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Int64" : ( spirvType._width == 16 ? "Int16" : "Int" );
                    }
                    case SPIRVType::Kind::Uint:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Uint64" : ( spirvType._width == 16 ? "Uint16" : "Uint" );
                    }
                    case SPIRVType::Kind::Float:
                    {
                        outSize = spirvType._width / 8;
                        return ( spirvType._width == 64 ) ? "Double" : "Float";
                    }
                    case SPIRVType::Kind::Vector:
                    {
                        uint32 subSize = 4;
                        string subName = resolveSPIRVTypeName( spirvType._subTypeID, mapType, subSize );
                        outSize        = subSize * spirvType._count;
                        return subName + to_string( spirvType._count );
                    }
                    case SPIRVType::Kind::Matrix:
                    {
                        uint32 columnSize = 16;
                        resolveSPIRVTypeName( spirvType._subTypeID, mapType, columnSize );
                        outSize = columnSize * spirvType._count;
                        if ( spirvType._count == 4 )
                            return "Float4x4";
                        return formatToString( "Float%#x%#", spirvType._count, spirvType._count );
                    }
                    case SPIRVType::Kind::Array:
                    {
                        // 배열 크기 = (원소 수 - 1) * stride + 원소 크기(마지막 원소 뒤 패딩 없음. DX 리플렉션과 같은 규칙).
                        uint32       elemSize = 4;
                        const string elemName = resolveSPIRVTypeName( spirvType._subTypeID, mapType, elemSize );
                        const uint32 stride   = spirvType._arrayStride > 0 ? spirvType._arrayStride : elemSize;
                        outSize               = spirvType._count > 0 ? ( spirvType._count - 1 ) * stride + elemSize : elemSize;
                        return elemName;
                    }
                    case SPIRVType::Kind::Unknown:
                    case SPIRVType::Kind::Struct:
                    case SPIRVType::Kind::Pointer:
                    case SPIRVType::Kind::Image:
                    case SPIRVType::Kind::Sampler:
                    case SPIRVType::Kind::SampledImage:
                    case SPIRVType::Kind::RuntimeArray:
                    {
                        outSize = 4;
                        return "Float";
                    }
                }
            }

            /** @brief 이름 · 데코레이션 명령어 하나를 표에 넣습니다. 그런 명령어가 아니면 false 입니다. */
            [[nodiscard]] static bool parseAnnotation( const uint32* pInst, uint32 wordCount, uint32 opcode, SPIRVModule& module )
            {
                if ( opcode == spirv::kOpEntryPoint && wordCount >= 3 )
                {
                    module._executionModel = pInst[1];
                }
                else if ( opcode == spirv::kOpName && wordCount >= 3 )
                {
                    const utf8*  pStr         = reinterpret_cast<const utf8*>( &pInst[2] );
                    const size_t maxLen       = static_cast<size_t>( wordCount - 2 ) * 4;
                    module._mapName[pInst[1]] = string( pStr, strnlen( pStr, maxLen ) );
                }
                else if ( opcode == spirv::kOpMemberName && wordCount >= 4 )
                {
                    const utf8*  pStr                         = reinterpret_cast<const utf8*>( &pInst[3] );
                    const size_t maxLen                       = static_cast<size_t>( wordCount - 3 ) * 4;
                    module._mapMemberName[pInst[1]][pInst[2]] = string( pStr, strnlen( pStr, maxLen ) );
                }
                else if ( opcode == spirv::kOpDecorate && wordCount >= 3 )
                {
                    const uint32 target     = pInst[1];
                    const uint32 decoration = pInst[2];
                    if ( decoration == spirv::kDecorationBinding && wordCount >= 4 )
                        module._mapBinding[target] = pInst[3];
                    else if ( decoration == spirv::kDecorationDescriptorSet && wordCount >= 4 )
                        module._mapDescriptorSet[target] = pInst[3];
                    else if ( decoration == spirv::kDecorationArrayStride && wordCount >= 4 )
                        module._mapArrayStride[target] = pInst[3];
                    else if ( decoration == spirv::kDecorationBufferBlock )
                        module._uniqueBufferBlockType.insert( target );
                    else if ( decoration == spirv::kDecorationLocation && wordCount >= 4 )
                        module._mapLocation[target] = pInst[3];
                }
                else if ( opcode == spirv::kOpMemberDecorate && wordCount >= 5 )
                {
                    if ( pInst[3] == spirv::kDecorationOffset )
                        module._mapMemberOffset[pInst[1]][pInst[2]] = pInst[4];
                }
                else
                {
                    return false;
                }
                return true;
            }

            /** @brief 타입 · 상수 · 변수 명령어 하나를 표에 넣습니다. 모르는 명령어는 건너뜁니다. */
            static void parseTypeOrValue( const uint32* pInst, uint32 wordCount, uint32 opcode, SPIRVModule& module )
            {
                using Kind                                = SPIRVType::Kind;
                unordered_map<uint32, SPIRVType>& mapType = module._mapType;
                if ( opcode == spirv::kOpTypeBool && wordCount >= 2 )
                {
                    mapType[pInst[1]] = SPIRVType{ Kind::Bool, 32, 1, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeInt && wordCount >= 4 )
                {
                    // 결과, 폭, 부호(0 이면 부호 없음).
                    mapType[pInst[1]] = SPIRVType{ pInst[3] != 0 ? Kind::Int : Kind::Uint, pInst[2], 1, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeFloat && wordCount >= 3 )
                {
                    mapType[pInst[1]] = SPIRVType{ Kind::Float, pInst[2], 1, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeVector && wordCount >= 4 )
                {
                    // 결과, 성분 타입, 성분 수.
                    mapType[pInst[1]] = SPIRVType{ Kind::Vector, 32, pInst[3], pInst[2], 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeMatrix && wordCount >= 4 )
                {
                    // 결과, 열 타입, 열 수.
                    mapType[pInst[1]] = SPIRVType{ Kind::Matrix, 32, pInst[3], pInst[2], 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeImage && wordCount >= 2 )
                {
                    // OpTypeImage: result, sampled type, Dim, Depth, Arrayed, MS, Sampled(1 = 샘플, 2 = 스토리지), Format.
                    // Sampled 를 _count 에 담아 두면 리소스 분류가 RWTexture(스토리지 이미지)를 가릴 수 있다.
                    const uint32 sampled = wordCount >= 8 ? pInst[7] : 0;
                    mapType[pInst[1]]    = SPIRVType{ Kind::Image, 0, sampled, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeSampler && wordCount >= 2 )
                {
                    mapType[pInst[1]] = SPIRVType{ Kind::Sampler, 0, 0, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeSampledImage && wordCount >= 2 )
                {
                    mapType[pInst[1]] = SPIRVType{ Kind::SampledImage, 0, 0, 0, 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeArray && wordCount >= 4 )
                {
                    // 결과, 원소 타입, 길이 상수 id.
                    const auto   lenIt  = module._mapConstantValue.find( pInst[3] );
                    const uint32 length = ( lenIt != module._mapConstantValue.end() ) ? lenIt->second : 0;
                    mapType[pInst[1]]   = SPIRVType{ Kind::Array, 0, length, pInst[2], 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeRuntimeArray && wordCount >= 3 )
                {
                    mapType[pInst[1]] = SPIRVType{ Kind::RuntimeArray, 0, 0, pInst[2], 0, {}, 0 };
                }
                else if ( opcode == spirv::kOpTypeStruct && wordCount >= 2 )
                {
                    SPIRVType st{ Kind::Struct, 0, 0, 0, 0, {}, 0 };
                    for ( uint32 wordIndex = 2; wordIndex < wordCount; ++wordIndex )
                    {
                        st._listMemberTypeID.push_back( pInst[wordIndex] );
                    }
                    mapType[pInst[1]] = std::move( st );
                }
                else if ( opcode == spirv::kOpTypePointer && wordCount >= 4 )
                {
                    // 결과, 저장 클래스, 가리키는 타입.
                    mapType[pInst[1]] = SPIRVType{ Kind::Pointer, 0, 0, pInst[3], pInst[2], {}, 0 };
                }
                else if ( opcode == spirv::kOpConstant && wordCount >= 4 )
                {
                    // 32비트 정수 상수만(배열 길이). 타입 확인은 생략한다. 길이 id 로 조회할 때만 쓴다.
                    module._mapConstantValue[pInst[2]] = pInst[3];
                }
                else if ( opcode == spirv::kOpVariable && wordCount >= 4 )
                {
                    // 결과 타입, 결과 id, 저장 클래스.
                    module._mapVariable[pInst[2]] = SPIRVVariable{ pInst[3], pInst[1] };
                }
            }

            /** @brief 헤더(5 워드) 뒤의 명령어를 모두 훑어 표를 채웁니다. 길이가 깨진 명령어에서 멈춥니다. */
            static void parseModule( const uint32* pWords, size_t wordCount, SPIRVModule& outModule )
            {
                size_t offset = 5;
                while ( offset < wordCount )
                {
                    const uint32 first = pWords[offset];
                    // SPIR-V 명령어 헤더: 상위 16비트 = WordCount, 하위 16비트 = Opcode (스펙 2.3 "Physical Layout").
                    const uint32 instructionWordCount = first >> 16;
                    const uint32 opcode               = first & 0xFFFFu;
                    if ( instructionWordCount == 0 || offset + instructionWordCount > wordCount )
                        break;
                    if ( parseAnnotation( pWords + offset, instructionWordCount, opcode, outModule ) == false )
                        parseTypeOrValue( pWords + offset, instructionWordCount, opcode, outModule );
                    offset += instructionWordCount;
                }

                // ArrayStride 데코레이션은 타입 정의 앞에 온다. 지금 붙인다.
                for ( const auto& [typeID, stride] : outModule._mapArrayStride )
                {
                    auto it = outModule._mapType.find( typeID );
                    if ( it != outModule._mapType.end() )
                        it->second._arrayStride = stride;
                }
            }

            /**
             * @brief 변수의 포인터 → 배열(무제한/고정) → 구조체 순으로 벗겨 "블록 구조체" 타입 id 를 반환합니다.
             * @details bindless `ConstantBuffer<T> name[]` 는 Uniform 포인터 → OpTypeRuntimeArray → Block 구조체다.
             *          @p outIsArray 는 무제한/고정 배열이었으면 true 다(bindCount 0 으로 보고한다).
             */
            static uint32 resolveBlockStruct( const SPIRVModule& module, uint32 pointerTypeID, bool& outIsArray )
            {
                outIsArray = false;
                auto ptrIt = module._mapType.find( pointerTypeID );
                if ( ptrIt == module._mapType.end() || ptrIt->second._kind != SPIRVType::Kind::Pointer )
                    return 0;
                uint32 typeID = ptrIt->second._subTypeID;
                for ( uint32 depth = 0; depth < 2; ++depth )
                {
                    auto it = module._mapType.find( typeID );
                    if ( it == module._mapType.end() )
                        return 0;
                    if ( it->second._kind == SPIRVType::Kind::RuntimeArray || it->second._kind == SPIRVType::Kind::Array )
                    {
                        outIsArray = true;
                        typeID     = it->second._subTypeID;
                        continue;
                    }
                    break;
                }
                return typeID;
            }

            /** @brief 그 id 가 구조체 타입이면 그 타입입니다. 아니면 nullptr 입니다. */
            static const SPIRVType* findStructType( const SPIRVModule& module, uint32 typeID )
            {
                auto it = module._mapType.find( typeID );
                return ( it != module._mapType.end() && it->second._kind == SPIRVType::Kind::Struct ) ? &it->second : nullptr;
            }

            /** @brief 구조체 멤버들을 CB 멤버 표에 넣습니다(@p baseOffset 은 감싼 구조체 안에서의 시작 오프셋입니다). */
            static void appendStructMembers( SPIRVModule& module, uint32 structTypeID, uint32 baseOffset, ShaderBufferInfo& outBuffer )
            {
                const SPIRVType* pStruct = findStructType( module, structTypeID );
                if ( pStruct == nullptr )
                    return;
                const auto& memberNameMap   = module._mapMemberName[structTypeID];
                const auto& memberOffsetMap = module._mapMemberOffset[structTypeID];

                uint32 memberIdx{ 0 };
                for ( uint32 memberTypeID : pStruct->_listMemberTypeID )
                {
                    ShaderVariableInfo varInfo{};
                    auto               mNameIt = memberNameMap.find( memberIdx );
                    if ( mNameIt != memberNameMap.end() )
                        varInfo._name = mNameIt->second;
                    else
                        varInfo._name = string( "member_" ) + to_string( memberIdx );

                    auto mOffIt     = memberOffsetMap.find( memberIdx );
                    varInfo._offset = baseOffset + ( ( mOffIt != memberOffsetMap.end() ) ? mOffIt->second : 0 );
                    varInfo._type   = resolveSPIRVTypeName( memberTypeID, module._mapType, varInfo._size );

                    outBuffer._listVariable.push_back( varInfo );
                    outBuffer._totalSize = MathUtil::max( outBuffer._totalSize, varInfo._offset + varInfo._size );
                    ++memberIdx;
                }
            }

            /** @brief 바인딩 한 자리의 이름 · 공간 · 번호입니다. */
            struct BindingSlot
            {
                string _name;
                uint32 _space{ 0 };
                uint32 _bindPoint{ 0 };
            };

            /** @brief 진짜 cbuffer(Uniform 저장 클래스) 하나를 CB 레이아웃(멤버 오프셋 포함)으로 냅니다. */
            static void appendConstantBuffer( SPIRVModule& module, const BindingSlot& slot, uint32 blockTypeID, ShaderReflectionData& outData )
            {
                ShaderBufferInfo buf{};
                buf._name          = slot._name;
                buf._registerSpace = slot._space;
                buf._bindPoint     = slot._bindPoint;
                buf._totalSize     = 0;

                const SPIRVType* pBlock = findStructType( module, blockTypeID );
                if ( pBlock != nullptr )
                {
                    // 엔진 cbuffer 는 맨 필드지만, `cbuffer { name_t x; }` 처럼 구조체 하나로 감싼 cbuffer 는 블록 멤버가
                    // 구조체 하나다. 엔진과 머티리얼 패커는 필드 이름으로 찾으므로 한 겹 벗긴다(DX 리플렉터와 같은 규칙).
                    const auto&  listMember = pBlock->_listMemberTypeID;
                    const uint32 innerID    = ( listMember.size() == 1 && findStructType( module, listMember[0] ) != nullptr ) ? listMember[0] : 0;
                    if ( innerID != 0 )
                    {
                        const auto&  offsetMap  = module._mapMemberOffset[blockTypeID];
                        const auto   offIt      = offsetMap.find( 0u ); // 키가 uint32 다. 부호 있는 리터럴이면 이종 키 오버로드로 샌다
                        const uint32 baseOffset = ( offIt != offsetMap.end() ) ? offIt->second : 0;
                        appendStructMembers( module, innerID, baseOffset, buf );
                    }
                    else
                    {
                        appendStructMembers( module, blockTypeID, 0, buf );
                    }
                    buf._totalSize = MathUtil::align( buf._totalSize, 256u );
                }

                outData._listConstantBuffer.push_back( std::move( buf ) );
            }

            /**
             * @brief StructuredBuffer<T> 의 원소 레이아웃을 냅니다.
             * @details 블록 { T arr[] } 의 멤버 0 이 (런타임)배열이고 원소가 구조체면 그 필드들을 원소 레이아웃으로 낸다.
             *          stride 는 배열 타입의 ArrayStride 다(std430: 예 float4+float+uint = 32).
             */
            static void appendStructuredElement( SPIRVModule& module, const BindingSlot& slot, uint32 blockTypeID, ShaderReflectionData& outData )
            {
                const SPIRVType* pBlock = findStructType( module, blockTypeID );
                if ( pBlock == nullptr || pBlock->_listMemberTypeID.size() != 1 )
                    return;
                auto       arrIt  = module._mapType.find( pBlock->_listMemberTypeID[0] );
                const bool bArray = arrIt != module._mapType.end() &&
                                    ( arrIt->second._kind == SPIRVType::Kind::RuntimeArray || arrIt->second._kind == SPIRVType::Kind::Array );
                if ( bArray == false || findStructType( module, arrIt->second._subTypeID ) == nullptr )
                    return;
                ShaderBufferInfo element{};
                element._name          = slot._name;
                element._registerSpace = slot._space;
                element._bindPoint     = slot._bindPoint;
                appendStructMembers( module, arrIt->second._subTypeID, 0, element );
                element._totalSize = arrIt->second._arrayStride > 0 ? arrIt->second._arrayStride : element._totalSize;
                if ( element._listVariable.empty() == false )
                    outData._listStructuredElement.push_back( std::move( element ) );
            }

            /** @brief 바인딩 목록에 낼 리소스 종류 이름입니다. */
            static const utf8* classifyResource( const SPIRVModule& module, const SPIRVVariable& var, uint32 blockTypeID, bool bIsStorageBuffer )
            {
                if ( bIsStorageBuffer )
                    return "StorageBuffer";
                if ( var._storageClass == spirv::kStorageClassUniform )
                    return "ConstantBuffer";
                if ( var._storageClass != spirv::kStorageClassUniformConstant )
                    return "OtherResource";
                // 포인터의 원소 타입으로 샘플러만 가른다. 배열(bindless)은 원소를 따라가서 본다.
                auto pointeeIt = module._mapType.find( blockTypeID );
                if ( pointeeIt == module._mapType.end() )
                    return "TextureOrSampler";
                if ( pointeeIt->second._kind == SPIRVType::Kind::Sampler )
                    return "Sampler";
                if ( pointeeIt->second._kind == SPIRVType::Kind::Image && pointeeIt->second._count == 2 )
                    return "RWTexture"; // 스토리지 이미지(RWTexture2D)
                return "TextureOrSampler";
            }

            /** @brief 바인딩 자리가 있는 변수 하나를 CB 레이아웃 · 구조화 원소 · 리소스 목록에 냅니다. */
            static void reflectBoundVariable( SPIRVModule& module, uint32 id, const SPIRVVariable& var, uint32 bindPoint, ShaderReflectionData& outData )
            {
                const auto  nameIt = module._mapName.find( id );
                const auto  setIt  = module._mapDescriptorSet.find( id );
                BindingSlot slot{};
                slot._name      = nameIt != module._mapName.end() ? nameIt->second : string( "Resource_" ) + to_string( id );
                slot._space     = ( setIt != module._mapDescriptorSet.end() ) ? setIt->second : 0;
                slot._bindPoint = bindPoint;

                bool         bIsArray{ false };
                const uint32 blockTypeID = resolveBlockStruct( module, var._typeID, bIsArray );

                // SSBO 는 SPIR-V 버전에 따라 **두 가지**로 표현된다. StorageBuffer 저장 클래스(1.4+ / Vulkan 1.1+
                // 타깃)이거나, 구식으로는 Uniform 저장 클래스에 구조체 타입이 BufferBlock 으로 데코레이션된다.
                // 둘 다 봐야 한다. 앞쪽만 보면 GL 용 SPIR-V 의 StructuredBuffer 가 상수버퍼로 분류돼, 바인더가 그것을 CB 슬롯으로
                // 걸고 bindStructuredBuffer 는 부르지 않는다(GL 에서 인스턴스 행렬이 모두 0 으로 읽혀 메시가 하나도 안 그려진다).
                const bool bBufferBlock = var._storageClass == spirv::kStorageClassUniform && blockTypeID != 0 &&
                                          module._uniqueBufferBlockType.find( blockTypeID ) != module._uniqueBufferBlockType.end();
                const bool bIsStorageBuffer = var._storageClass == spirv::kStorageClassStorageBuffer || bBufferBlock;

                // 진짜 cbuffer(Uniform storage class)만 "CB 레이아웃(멤버 오프셋 포함)" 으로 채운다.
                // StructuredBuffer/RWStructuredBuffer(StorageBuffer storage class)는 구조체 원소 타입을 똑같은
                // 방식으로 반영하지만 실제 cbuffer 가 아니다. 여기 포함시키면 ShaderBindingLayout 이
                // ConstantBuffer 종류의 "가짜 CB" 슬롯(멤버 이름이 원소 구조체 필드와 겹침)을 만들어
                // 엔진 CB 버퍼에 엉뚱한 오프셋으로 값을 덮어쓸 수 있다. 아래가 StructuredBuffer 로 따로 분류한다.
                if ( var._storageClass == spirv::kStorageClassUniform && bIsStorageBuffer == false )
                    appendConstantBuffer( module, slot, blockTypeID, outData );
                if ( bIsStorageBuffer && blockTypeID != 0 )
                    appendStructuredElement( module, slot, blockTypeID, outData );

                ShaderReflectedBinding res{};
                res._name          = slot._name;
                res._registerSpace = slot._space;
                res._bindPoint     = slot._bindPoint;
                res._bindCount     = bIsArray ? 0u : 1u; ///< 무제한 배열([])은 0. DX 리플렉션의 BindCount 와 같은 뜻
                res._type          = classifyResource( module, var, blockTypeID, bIsStorageBuffer );
                outData._listResource.push_back( std::move( res ) );
            }

            /**
             * @brief 정점 입력을 냅니다 — Input 저장 클래스에 Location 이 붙은 변수입니다.
             * @details DXC 는 `in.var.<SEMANTIC>` 로 이름을 남기고 시스템 값(SV_VertexID 등)은 BuiltIn 이라 Location 이 없다.
             */
            static void appendVertexInputs( const SPIRVModule& module, ShaderReflectionData& outData )
            {
                for ( const auto& [id, var] : module._mapVariable )
                {
                    if ( var._storageClass != spirv::kStorageClassInput )
                        continue;
                    auto locationIt = module._mapLocation.find( id );
                    if ( locationIt == module._mapLocation.end() )
                        continue;
                    string name;
                    auto   nameIt = module._mapName.find( id );
                    if ( nameIt != module._mapName.end() )
                        name = nameIt->second;
                    constexpr string_view kPrefix = "in.var.";
                    if ( StringUtil::startsWith( name, kPrefix ) )
                        name = name.substr( kPrefix.size() );

                    ShaderVertexInputInfo input{};
                    input._location   = locationIt->second;
                    size_t digitBegin = name.size();
                    while ( digitBegin > 0 && StringUtil::isDigitChar( name[digitBegin - 1] ) )
                    {
                        --digitBegin;
                    }
                    input._semantic = name.substr( 0, digitBegin );
                    for ( size_t digitIndex = digitBegin; digitIndex < name.size(); ++digitIndex )
                    {
                        input._semanticIndex = input._semanticIndex * 10 + static_cast<uint32>( name[digitIndex] - '0' );
                    }
                    outData._listVertexInput.push_back( std::move( input ) );
                }
                VectorUtil::sort( outData._listVertexInput, []( const ShaderVertexInputInfo& lhs, const ShaderVertexInputInfo& rhs )
                { return lhs._location < rhs._location; } );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderReflection" );

    ShaderReflectionData ShaderReflectionUtil::reflectSPIRV( const vector<uint8>& bytecode )
    {
        using Internal = ShaderReflectionSPIRVInternal;
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

        Internal::SPIRVModule module;
        Internal::parseModule( pWords, wordCount, module );

        ShaderReflectionData data{};
        for ( const auto& [id, var] : module._mapVariable )
        {
            const auto bindingIt = module._mapBinding.find( id );
            if ( bindingIt == module._mapBinding.end() )
                continue; // 푸시 상수 · 입출력 변수. 바인딩 자리가 없다
            Internal::reflectBoundVariable( module, id, var, bindingIt->second, data );
        }
        if ( module._executionModel == spirv::kExecutionModelVertex )
            Internal::appendVertexInputs( module, data );

        SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %# VertexInputs: %#",
                      data._listConstantBuffer.size(), data._listResource.size(), data._listVertexInput.size() );
        return data;
    }

} // namespace sw

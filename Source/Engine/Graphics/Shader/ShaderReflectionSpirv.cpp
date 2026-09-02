#include "pch.h"

#include "Engine/Graphics/Shader/ShaderReflectionUtil.h"

namespace sw
{
    namespace
    {
        struct ShaderReflectionSpirvInternal
        {
            static constexpr uint32 kSpirvMagic         = 0x07230203u;
            static constexpr uint32 kOpName             = 5u;
            static constexpr uint32 kOpMemberName       = 6u;
            static constexpr uint32 kOpDecorate         = 71u;
            static constexpr uint32 kOpMemberDecorate   = 72u;
            static constexpr uint32 kOpTypeBool         = 20u;
            static constexpr uint32 kOpTypeInt          = 21u;
            static constexpr uint32 kOpTypeFloat        = 22u;
            static constexpr uint32 kOpTypeVector       = 23u;
            static constexpr uint32 kOpTypeMatrix       = 24u;
            static constexpr uint32 kOpTypeImage        = 25u;
            static constexpr uint32 kOpTypeSampler      = 26u;
            static constexpr uint32 kOpTypeSampledImage = 27u;
            static constexpr uint32 kOpTypeStruct       = 30u;
            static constexpr uint32 kOpTypePointer      = 32u;
            static constexpr uint32 kOpVariable         = 59u;

            static constexpr uint32 kDecorationBinding       = 33u;
            static constexpr uint32 kDecorationDescriptorSet = 34u;
            static constexpr uint32 kDecorationOffset        = 35u;

            static constexpr uint32 kStorageClassUniformConstant = 0u;
            static constexpr uint32 kStorageClassUniform         = 2u;
            static constexpr uint32 kStorageClassStorageBuffer   = 12u;

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
                    SampledImage
                };

                Kind           _kind  = Kind::Unknown;
                uint32         _width = 32;
                uint32         _count{ 0 };
                uint32         _subTypeId{ 0 };
                uint32         _storageClass{ 0 };
                vector<uint32> _listMemberTypeId;
            };

            static string resolveSpirvTypeName( uint32 typeId, const unordered_map<uint32, SpirvType>& mapType, uint32& outSize )
            {
                auto it = mapType.find( typeId );
                if ( it == mapType.end() )
                {
                    outSize = 4;
                    return "Float";
                }

                const SpirvType& t = it->second;
                switch ( t._kind )
                {
                    case SpirvType::Kind::Bool:
                        outSize = 4;
                        return "Bool";
                    case SpirvType::Kind::Int:
                        outSize = t._width / 8;
                        return ( t._width == 64 ) ? "Int64" : ( t._width == 16 ? "Int16" : "Int" );
                    case SpirvType::Kind::Uint:
                        outSize = t._width / 8;
                        return ( t._width == 64 ) ? "Uint64" : ( t._width == 16 ? "Uint16" : "Uint" );
                    case SpirvType::Kind::Float:
                        outSize = t._width / 8;
                        return ( t._width == 64 ) ? "Double" : "Float";
                    case SpirvType::Kind::Vector:
                    {
                        uint32 subSize = 4;
                        string subName = resolveSpirvTypeName( t._subTypeId, mapType, subSize );
                        outSize        = subSize * t._count;
                        return subName + to_string( t._count );
                    }
                    case SpirvType::Kind::Matrix:
                    {
                        uint32 colSize = 16;
                        resolveSpirvTypeName( t._subTypeId, mapType, colSize );
                        outSize = colSize * t._count;
                        if ( t._count == 4 )
                            return "Float4x4";
                        return "Float" + to_string( t._count ) + "x" + to_string( t._count );
                    }
                    case SpirvType::Kind::Unknown:
                    case SpirvType::Kind::Struct:
                    case SpirvType::Kind::Pointer:
                    case SpirvType::Kind::Image:
                    case SpirvType::Kind::Sampler:
                    case SpirvType::Kind::SampledImage:
                    default:
                        outSize = 4;
                        return "Float";
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
        if ( pWords[0] != ShaderReflectionSpirvInternal::kSpirvMagic )
        {
            SW_LOG_WARNING( "Not a SPIR-V module (bad magic)." );
            return {};
        }

        ShaderReflectionData data{};

        unordered_map<uint32, string>                                   mapName;
        unordered_map<uint32, unordered_map<uint32, string>>            mapMemberName;
        unordered_map<uint32, uint32>                                   mapBinding;
        unordered_map<uint32, uint32>                                   mapDescriptorSet;
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
            const uint32 first      = pWords[offset];
            const uint32 instrWords = first & 0xFFFFu;
            const uint32 opcode     = first >> 16;
            if ( instrWords == 0 || offset + instrWords > wordCount )
                break;

            if ( opcode == ShaderReflectionSpirvInternal::kOpName && instrWords >= 3 )
            {
                const uint32 target = pWords[offset + 1];
                const utf8*  pStr   = reinterpret_cast<const utf8*>( &pWords[offset + 2] );
                const size_t maxLen = ( instrWords - 2 ) * 4;
                mapName[target]     = string( pStr, strnlen( pStr, maxLen ) );
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpMemberName && instrWords >= 4 )
            {
                const uint32 target                = pWords[offset + 1];
                const uint32 memberIndex           = pWords[offset + 2];
                const utf8*  pStr                  = reinterpret_cast<const utf8*>( &pWords[offset + 3] );
                const size_t maxLen                = ( instrWords - 3 ) * 4;
                mapMemberName[target][memberIndex] = string( pStr, strnlen( pStr, maxLen ) );
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpDecorate && instrWords >= 3 )
            {
                const uint32 target     = pWords[offset + 1];
                const uint32 decoration = pWords[offset + 2];
                if ( decoration == ShaderReflectionSpirvInternal::kDecorationBinding && instrWords >= 4 )
                    mapBinding[target] = pWords[offset + 3];
                else if ( decoration == ShaderReflectionSpirvInternal::kDecorationDescriptorSet && instrWords >= 4 )
                    mapDescriptorSet[target] = pWords[offset + 3];
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpMemberDecorate && instrWords >= 5 )
            {
                const uint32 target      = pWords[offset + 1];
                const uint32 memberIndex = pWords[offset + 2];
                const uint32 decoration  = pWords[offset + 3];
                if ( decoration == ShaderReflectionSpirvInternal::kDecorationOffset )
                    mapMemberOffset[target][memberIndex] = pWords[offset + 4];
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeBool && instrWords >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Bool, 32, 1, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeInt && instrWords >= 4 )
            {
                const uint32 id         = pWords[offset + 1];
                const uint32 width      = pWords[offset + 2];
                const uint32 signedness = pWords[offset + 3];
                mapType[id]             = ShaderReflectionSpirvInternal::SpirvType{ signedness ? ShaderReflectionSpirvInternal::SpirvType::Kind::Int : ShaderReflectionSpirvInternal::SpirvType::Kind::Uint, width, 1, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeFloat && instrWords >= 3 )
            {
                const uint32 id    = pWords[offset + 1];
                const uint32 width = pWords[offset + 2];
                mapType[id]        = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Float, width, 1, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeVector && instrWords >= 4 )
            {
                const uint32 id       = pWords[offset + 1];
                const uint32 compType = pWords[offset + 2];
                const uint32 count    = pWords[offset + 3];
                mapType[id]           = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Vector, 32, count, compType, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeMatrix && instrWords >= 4 )
            {
                const uint32 id      = pWords[offset + 1];
                const uint32 colType = pWords[offset + 2];
                const uint32 count   = pWords[offset + 3];
                mapType[id]          = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Matrix, 32, count, colType, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeImage && instrWords >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Image, 0, 0, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeSampler && instrWords >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Sampler, 0, 0, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeSampledImage && instrWords >= 2 )
            {
                mapType[pWords[offset + 1]] = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::SampledImage, 0, 0, 0, 0, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypeStruct && instrWords >= 2 )
            {
                const uint32                             id = pWords[offset + 1];
                ShaderReflectionSpirvInternal::SpirvType st{ ShaderReflectionSpirvInternal::SpirvType::Kind::Struct, 0, 0, 0, 0, {} };
                for ( uint32 wordIndex = 2; wordIndex < instrWords; ++wordIndex )
                    st._listMemberTypeId.push_back( pWords[offset + wordIndex] );
                mapType[id] = std::move( st );
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpTypePointer && instrWords >= 4 )
            {
                const uint32 id           = pWords[offset + 1];
                const uint32 storageClass = pWords[offset + 2];
                const uint32 subType      = pWords[offset + 3];
                mapType[id]               = ShaderReflectionSpirvInternal::SpirvType{ ShaderReflectionSpirvInternal::SpirvType::Kind::Pointer, 0, 0, subType, storageClass, {} };
            }
            else if ( opcode == ShaderReflectionSpirvInternal::kOpVariable && instrWords >= 4 )
            {
                const uint32 typeId       = pWords[offset + 1];
                const uint32 resultId     = pWords[offset + 2];
                const uint32 storageClass = pWords[offset + 3];
                mapVariable[resultId]     = VariableInfo{ storageClass, typeId };
            }

            offset += instrWords;
        }

        for ( const auto& [id, var] : mapVariable )
        {
            const auto nameIt    = mapName.find( id );
            const auto bindingIt = mapBinding.find( id );
            const auto setIt     = mapDescriptorSet.find( id );
            if ( bindingIt == mapBinding.end() )
                continue;

            string name;
            if ( nameIt != mapName.end() )
                name = nameIt->second;
            else
                name = string( "Resource_" ) + to_string( id );

            const uint32 space     = ( setIt != mapDescriptorSet.end() ) ? setIt->second : 0;
            const uint32 bindPoint = bindingIt->second;

            if ( var._storageClass == ShaderReflectionSpirvInternal::kStorageClassUniform || var._storageClass == ShaderReflectionSpirvInternal::kStorageClassStorageBuffer )
            {
                ShaderBufferInfo buf{};
                buf._name          = name;
                buf._registerSpace = space;
                buf._bindPoint     = bindPoint;
                buf._totalSize     = 0;

                // Pointer -> Struct 타입 분석 및 내부 멤버 변수 추출
                auto ptrTypeIt = mapType.find( var._typeId );
                if ( ptrTypeIt != mapType.end() && ptrTypeIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Pointer )
                {
                    const uint32 structTypeId = ptrTypeIt->second._subTypeId;
                    auto         structTypeIt = mapType.find( structTypeId );
                    if ( structTypeIt != mapType.end() && structTypeIt->second._kind == ShaderReflectionSpirvInternal::SpirvType::Kind::Struct )
                    {
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
                            varInfo._offset = ( mOffIt != memberOffsetMap.end() ) ? mOffIt->second : 0;
                            varInfo._type   = ShaderReflectionSpirvInternal::resolveSpirvTypeName( memberTypeId, mapType, varInfo._size );

                            buf._listVariable.push_back( varInfo );
                            buf._totalSize = MathUtil::max( buf._totalSize, varInfo._offset + varInfo._size );
                            ++memberIdx;
                        }
                        buf._totalSize = MathUtil::align( buf._totalSize, 256u );
                    }
                }

                data._listConstantBuffer.push_back( std::move( buf ) );
            }

            ShaderResourceBinding res{};
            res._name          = name;
            res._registerSpace = space;
            res._bindPoint     = bindPoint;
            res._bindCount     = 1;
            if ( var._storageClass == ShaderReflectionSpirvInternal::kStorageClassUniform )
                res._type = "ConstantBuffer";
            else if ( var._storageClass == ShaderReflectionSpirvInternal::kStorageClassStorageBuffer )
                res._type = "StorageBuffer";
            else if ( var._storageClass == ShaderReflectionSpirvInternal::kStorageClassUniformConstant )
                res._type = "TextureOrSampler";
            else
                res._type = "OtherResource";
            data._listResource.push_back( std::move( res ) );
        }

        SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
                      data._listConstantBuffer.size(), data._listResource.size() );
        return data;
    }

} // namespace sw

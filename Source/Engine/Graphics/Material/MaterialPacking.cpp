#include "pch.h"

#include "Engine/Graphics/Material/MaterialPacking.h"

#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionData.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct MaterialPackingInternal
        {
            struct PropertyTypeDesc
            {
                const utf8*          _pName;
                MaterialPropertyType _type;
                uint32               _size; ///< 셰이더 · CB 타입으로 쓸 때의 패킹 크기(0 = CB 에 안 들어감)
            };

            inline static const PropertyTypeDesc s_PropertyTypes[] = {
                {         "Float",          MaterialPropertyType::Float,  4},
                {        "Float2",         MaterialPropertyType::Float2,  8},
                {        "Float3",         MaterialPropertyType::Float3, 12},
                {        "Float4",         MaterialPropertyType::Float4, 16},
                {      "Float4x4",       MaterialPropertyType::Float4x4, 64},
                {          "Uint",           MaterialPropertyType::Uint,  4},
                {         "Uint2",          MaterialPropertyType::Uint2,  8},
                {         "Uint3",          MaterialPropertyType::Uint3, 12},
                {         "Uint4",          MaterialPropertyType::Uint4, 16},
                {           "Int",            MaterialPropertyType::Int,  4},
                {          "Int2",           MaterialPropertyType::Int2,  8},
                {          "Int3",           MaterialPropertyType::Int3, 12},
                {          "Int4",           MaterialPropertyType::Int4, 16},
                {          "Bool",           MaterialPropertyType::Bool,  4},
                {         "Range",          MaterialPropertyType::Range,  4},
                {         "Color",          MaterialPropertyType::Color, 16},
                {          "Enum",           MaterialPropertyType::Enum,  4},
                {       "BitFlag",        MaterialPropertyType::BitFlag,  4},
                {   "ChannelMask",    MaterialPropertyType::ChannelMask,  4},
                {     "Texture2D",      MaterialPropertyType::Texture2D,  4},
                {   "TextureCube",    MaterialPropertyType::TextureCube,  4},
                {     "Texture3D",      MaterialPropertyType::Texture3D,  4},
                {"Texture2DArray", MaterialPropertyType::Texture2DArray,  4},
                {       "Keyword",        MaterialPropertyType::Keyword,  0},
                // 별칭(Unity · Unreal 이름)
                {        "Scalar",          MaterialPropertyType::Float,  4},
                {        "Vector",         MaterialPropertyType::Float4, 16},
                {       "Vector2",         MaterialPropertyType::Float2,  8},
                {       "Vector3",         MaterialPropertyType::Float3, 12},
                {       "Vector4",         MaterialPropertyType::Float4, 16},
                {        "Matrix",       MaterialPropertyType::Float4x4, 64},
                {       "Integer",            MaterialPropertyType::Int,  4},
                {        "Toggle",           MaterialPropertyType::Bool,  4},
                {  "StaticSwitch",        MaterialPropertyType::Keyword,  0},
                {       "Cubemap",    MaterialPropertyType::TextureCube,  4},
                {        "Volume",      MaterialPropertyType::Texture3D,  4},
            };

            static bool equalsName( string_view value, const utf8* pExpected )
            {
                return StringUtil::equals( value, pExpected );
            }

            /** @brief `_enumType` 이 가리키는 리플렉션 enum 입니다. 타입 이름은 찾기만 한다(에셋 글을 전역 이름 표에 넣지 않는다). */
            static const EnumInfo* findReflectedEnum( const MaterialProperty& prop )
            {
                if ( prop._enumType.empty() )
                    return nullptr;
                const hashed_string typeName = hashed_string::findInterned( prop._enumType );
                return typeName.empty() ? nullptr : engine::getSubsystem<TypeRegistry>().findEnum( typeName );
            }

            /**
             * @brief 이름 · 숫자 토큰 하나를 값으로 읽습니다. 모르는 토큰이면 false 입니다.
             * @details 파일의 `_enumEntries` 이름(철자 그대로)이 먼저다. 리플렉션 enum 이 있으면 `EnumInfo::tryParseText`(이름 · ValueAlias ·
             *          알려진 값의 숫자)이고, 없으면 숫자 리터럴을 받는다.
             */
            [[nodiscard]] static bool tryParseEnumToken( const MaterialProperty& prop, const EnumInfo* pInfo, string_view token, int64& outValue )
            {
                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    if ( equalsName( token, enumEntry._name.c_str() ) )
                    {
                        outValue = enumEntry._value;
                        return true;
                    }
                }
                if ( pInfo != nullptr )
                    return pInfo->tryParseText( token, outValue );
                return StringUtil::parseInt64( token, outValue, 0 );
            }

            /**
             * @brief Enum · BitFlag 값 글을 읽습니다. 모르는 이름 · 토큰이나 표식 값(`Invalid` · `Count`)이 하나라도 있으면 false 입니다.
             * @details 비트플래그는 `|` · `,` 로 나눈 토큰마다 읽어 합친다. 빈 글은 0 이다(값을 적지 않은 프로퍼티). 실패하면 부르는 쪽이
             *          경고하고 쓰지 않는다 — 버퍼의 앞 값이 남는다.
             */
            [[nodiscard]] static bool tryParseEnumOrFlags( const MaterialProperty& prop, string_view value, bool bitFlagMode, int64& outValue )
            {
                const string_view trimmed = StringUtil::trim( value );
                if ( trimmed.empty() )
                {
                    outValue = 0;
                    return true;
                }

                const EnumInfo* pInfo = findReflectedEnum( prop );
                int64           result{ 0 };
                if ( bitFlagMode || prop._type == MaterialPropertyType::BitFlag )
                {
                    string_splitter splitter( trimmed, { "|", "," } );
                    for ( string_view part : splitter.getSplitList() )
                    {
                        const string_view token = StringUtil::trim( part );
                        if ( token.empty() )
                            continue;
                        int64 tokenValue{ 0 };
                        if ( tryParseEnumToken( prop, pInfo, token, tokenValue ) == false )
                            return false;
                        result |= tokenValue;
                    }
                }
                else if ( tryParseEnumToken( prop, pInfo, trimmed, result ) == false )
                {
                    return false;
                }

                if ( pInfo != nullptr && pInfo->isValidValue( result ) == false )
                    return false;
                outValue = result;
                return true;
            }

            /**
             * @brief ChannelMask 글(`RGBA` 글자 조합 · 0..15 숫자)을 비트 마스크로 읽습니다. 빈 글은 네 채널 모두입니다(값을 적지 않은 프로퍼티).
             * @details 모르는 글자나 넷을 넘는 비트는 false 입니다 — 오타가 조용히 네 채널이 되지 않는다(Bool · Enum · Range 와 같은 규칙).
             */
            [[nodiscard]] static bool tryParseChannelMask( string_view value, uint32& outMask )
            {
                constexpr uint32  kAllChannels = 0xFu;
                const string_view trimmedValue = StringUtil::trim( value );
                if ( trimmedValue.empty() )
                {
                    outMask = kAllChannels;
                    return true;
                }

                // 숫자
                uint64 numericValue{ 0 };
                if ( StringUtil::parseUint64( trimmedValue, numericValue, 0 ) )
                {
                    if ( numericValue > kAllChannels )
                        return false;
                    outMask = static_cast<uint32>( numericValue );
                    return true;
                }

                uint32 mask{ 0 };
                for ( utf8 charByte : trimmedValue )
                {
                    switch ( StringUtil::toUpperChar( charByte ) )
                    {
                        case 'R':
                        {
                            mask |= 1u;
                            break;
                        }
                        case 'G':
                        {
                            mask |= 2u;
                            break;
                        }
                        case 'B':
                        {
                            mask |= 4u;
                            break;
                        }
                        case 'A':
                        {
                            mask |= 8u;
                            break;
                        }
                        default:
                        {
                            return false;
                        }
                    }
                }
                outMask = mask;
                return true;
            }

            /**
             * @brief 확보된 칸(`packSize`) 안에 들어갈 때만 씁니다.
             * @details `writeNumericValue` 뿐 아니라 switch 안에서 **직접 `Memory::copy` 하는 형제 경로들**도 이것을 거쳐야 합니다.
             *          칸 크기는 셰이더 리플렉션이 정하고(`ShaderVariableInfo::_size`) 쓰는 크기는
             *          `_shaderType`(리플렉션 전에는 `defaultShaderTypeFor`)이 정하므로 **둘이 어긋날 수 있습니다.**
             *          예를 들어 셰이더가 `uint`(4바이트)로 선언한 자리에 `_shaderType` 이
             *          `Float4` 인 `ChannelMask` 를 쓰면 4바이트 칸에 16바이트를 씁니다.
             *          조용히 넘치는 대신 쓰지 않고 false 를 반환합니다(부르는 쪽이 경고합니다).
             */
            [[nodiscard]] static bool writeBoundedValue( void* pDst, size_t packSize, const void* pSrc, size_t byteCount )
            {
                const bool bInvalidBoundedWrite = pDst == nullptr || pSrc == nullptr || byteCount > packSize;
                if ( bInvalidBoundedWrite )
                    return false;
                Memory::copy( pDst, pSrc, byteCount );
                return true;
            }

            /** @brief 숫자 글을 셰이더 타입의 칸들로 씁니다. 색인지는 @p cpuType 이 정한다 — 색의 셰이더 타입은 `Float4` 라(`defaultShaderTypeFor` · 리플렉션 · XML) 셰이더 타입으로는 가릴 수 없다. */
            [[nodiscard]] static bool writeNumericValue( void* pDst, size_t packSize, MaterialPropertyType cpuType, MaterialPropertyType shaderType, string_view value )
            {
                const uint32 need                  = MaterialPacking::packedSizeOf( shaderType );
                const bool   bInvalidPackArguments = need == 0 || packSize < need || pDst == nullptr;
                if ( bInvalidPackArguments )
                    return false;

                const bool       bFloatPackedType = shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Float2 || shaderType == MaterialPropertyType::Float3 || shaderType == MaterialPropertyType::Float4 || shaderType == MaterialPropertyType::Float4x4 || shaderType == MaterialPropertyType::Range || shaderType == MaterialPropertyType::Color;
                const bool       bUintPackedType  = shaderType == MaterialPropertyType::Uint || shaderType == MaterialPropertyType::Uint2 || shaderType == MaterialPropertyType::Uint3 || shaderType == MaterialPropertyType::Uint4;
                const bool       bIntPackedType   = shaderType == MaterialPropertyType::Int || shaderType == MaterialPropertyType::Int2 || shaderType == MaterialPropertyType::Int3 || shaderType == MaterialPropertyType::Int4;
                constexpr uint32 kMaxWords        = 16; // Float4x4
                const uint32     count            = need / 4;
                const bool       bKnownType       = bFloatPackedType || bUintPackedType || bIntPackedType;
                if ( bKnownType == false || count > kMaxWords )
                    return false;

                // 칸을 모두 읽은 뒤 한 번에 쓴다 — 못 읽은 칸이 있으면 0 으로 채우지 않고 버퍼를 건드리지 않은 채 실패한다. 글이 모자란 칸은 기본값(색의 알파는 1)이다.
                string_splitter splitter( value, { " ", "	", "," } );
                const auto&     tokens = splitter.getSplitList();
                uint32          arrWord[kMaxWords]{};
                for ( uint32 wordIndex = 0; wordIndex < count; ++wordIndex )
                {
                    if ( wordIndex >= tokens.size() )
                    {
                        const float32 alpha = 1.0f;
                        if ( wordIndex == 3 && cpuType == MaterialPropertyType::Color )
                            Memory::copy( &arrWord[wordIndex], &alpha, sizeof( alpha ) );
                        continue;
                    }
                    bool bRead{ false };
                    if ( bFloatPackedType )
                    {
                        float32 component{ 0.0f };
                        bRead = StringUtil::parseFloat( tokens[wordIndex], component );
                        Memory::copy( &arrWord[wordIndex], &component, sizeof( component ) );
                    }
                    else if ( bUintPackedType )
                    {
                        uint64 component{ 0 };
                        bRead              = StringUtil::parseUint64( tokens[wordIndex], component, 10 ) && component <= MathUtil::kMaxUInt32; // 넘치는 값이 잘려 다른 값이 되지 않는다
                        arrWord[wordIndex] = static_cast<uint32>( component );
                    }
                    else
                    {
                        int32 component{ 0 };
                        bRead = StringUtil::parseInt( tokens[wordIndex], component, 10 );
                        Memory::copy( &arrWord[wordIndex], &component, sizeof( component ) );
                    }
                    if ( bRead == false )
                    {
                        SW_LOG_ERROR_ID( "Material.VectorNumberUnreadable", "Material value '%#' has an unreadable number '%#'", value, tokens[wordIndex] );
                        return false;
                    }
                }
                Memory::copy( pDst, arrWord, need );
                return true;
            }
            // `packPropertyIntoBuffer` 의 종류별 쓰기 — 셰이더 타입에 맞는 바이트를 pDst 의 packSize 안에 쓴다.

            /** @brief Bool — 셰이더가 float(Range 포함)면 1.0 · 0.0, 아니면 1 · 0 입니다. 읽지 못한 글이면 false 입니다. */
            [[nodiscard]] static bool packBool( const MaterialProperty& prop, string_view value, MaterialPropertyType shaderType, uint8* pDst, uint32 packSize )
            {
                bool bValue{ false };
                if ( MaterialPacking::parseBoolToken( value, prop._name, "Material.BooleanValueUnreadable", bValue ) == false )
                    return false;
                const uint32 boolVal = bValue ? 1u : 0u;
                if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Range )
                {
                    const float32 floatVal = boolVal != 0 ? 1.0f : 0.0f;
                    return writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
                }
                return writeBoundedValue( pDst, packSize, &boolVal, sizeof( boolVal ) );
            }

            /** @brief Enum · BitFlag — 열거자 이름(BitFlag 면 `|` 조합)을 값으로 풀어 셰이더 타입(Int · Float · Uint)으로 씁니다. 모르는 이름이면 false 입니다. */
            [[nodiscard]] static bool packEnum( const MaterialProperty& prop, string_view value, MaterialPropertyType shaderType, bool bFlags, uint8* pDst, uint32 packSize )
            {
                int64 enumVal{ 0 };
                if ( tryParseEnumOrFlags( prop, value, bFlags, enumVal ) == false )
                {
                    SW_LOG_WARNING_ID( "Material.UnknownEnumValue", "Material parameter '%#' has an unknown enum value '%#' - value kept", prop._name, value );
                    return false;
                }
                if ( bFlags == false && shaderType == MaterialPropertyType::Int )
                {
                    const int32 intVal = static_cast<int32>( enumVal );
                    return writeBoundedValue( pDst, packSize, &intVal, sizeof( intVal ) );
                }
                if ( bFlags == false && shaderType == MaterialPropertyType::Float )
                {
                    const float32 floatVal = static_cast<float32>( enumVal );
                    return writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
                }
                const uint32 uEnumVal = static_cast<uint32>( enumVal );
                return writeBoundedValue( pDst, packSize, &uEnumVal, sizeof( uEnumVal ) );
            }

            /** @brief ChannelMask — 셰이더가 float4 면 채널마다 1.0 · 0.0, 아니면 비트 마스크입니다. 읽지 못한 글이면 false 입니다. */
            [[nodiscard]] static bool packChannelMask( const MaterialProperty& prop, string_view value, MaterialPropertyType shaderType, uint8* pDst, uint32 packSize )
            {
                uint32 mask{ 0 };
                if ( tryParseChannelMask( value, mask ) == false )
                {
                    SW_LOG_ERROR_ID( "Material.ChannelMaskUnreadable", "Material parameter '%#' has an unreadable channel mask '%#'", prop._name, value );
                    return false;
                }
                if ( shaderType == MaterialPropertyType::Float4 )
                {
                    float32 arrComp[4] = {
                        ( mask & 1 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 2 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 4 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 8 ) != 0 ? 1.0f : 0.0f,
                    };
                    return writeBoundedValue( pDst, packSize, arrComp, sizeof( arrComp ) );
                }
                return writeBoundedValue( pDst, packSize, &mask, sizeof( mask ) );
            }

            /**
             * @brief 텍스처 — 붙은 텍스처의 bindless 인덱스입니다(_value 에 숫자를 적어 덮어쓰는 것도 허용한다).
             * @details 붙은 텍스처가 없으면 0 이 아니라 kInvalidIndex 를 넣는다. 0 은 Vulkan 에서 첫 번째 텍스처라는 **유효한** 인덱스다(DX12 는 0 을
             *          null 텍스처로 비워 둔다 — D3D12RHIDevice::kNullTextureBindlessIndex). 셰이더의 Bindless::sampleIndex 는 kInvalidIndex 를 "텍스처 없음" 으로 처리한다.
             */
            [[nodiscard]] static bool packTextureIndex( const MaterialProperty& prop, string_view value, uint8* pDst, uint32 packSize )
            {
                uint32 textureIndex = prop._textureIndex;
                if ( textureIndex == kInvalidDescriptorIndex && value.empty() == false )
                {
                    uint64 numericVal{ 0 };
                    if ( StringUtil::parseUint64( value, numericVal, 0 ) && numericVal <= MathUtil::kMaxUInt32 ) // 넘치는 값이 잘려 유효한 인덱스가 되지 않는다
                        textureIndex = static_cast<uint32>( numericVal );
                }
                return writeBoundedValue( pDst, packSize, &textureIndex, sizeof( textureIndex ) );
            }

            /** @brief Range — float 로 읽어 [min, max] 로 자릅니다(min < max 일 때). 못 읽는 글(빈 글 포함)이면 0 으로 쓰지 않고 false 입니다. */
            [[nodiscard]] static bool packRange( const MaterialProperty& prop, string_view value, uint8* pDst, uint32 packSize )
            {
                float32 floatVal{ 0.0f };
                if ( StringUtil::parseFloat( value, floatVal ) == false )
                {
                    SW_LOG_ERROR_ID( "Material.ParameterNumberUnreadable", "Material parameter '%#' has an unreadable number '%#'", prop._name, value );
                    return false;
                }
                if ( prop._min < prop._max )
                    floatVal = MathUtil::clamp( floatVal, prop._min, prop._max );
                return writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
            }

            /** @brief 프로퍼티의 셰이더 타입입니다. 비어 있으면(리플렉션 전) 종류의 기본입니다. */
            static MaterialPropertyType resolveShaderType( const MaterialProperty& prop )
            {
                return prop._shaderType != MaterialPropertyType::Unknown ? prop._shaderType : MaterialPacking::defaultShaderTypeFor( prop._type );
            }

            /** @brief 칸 크기입니다. 프로퍼티 크기가 먼저이고, 없으면 셰이더 타입의 크기(모르면 4)입니다. */
            static uint32 resolvePackSize( const MaterialProperty& prop, MaterialPropertyType shaderType )
            {
                if ( prop._size != 0 )
                    return prop._size;
                const uint32 packSize = MaterialPacking::packedSizeOf( shaderType );
                return packSize != 0 ? packSize : 4u;
            }

            /** @brief 글을 float 칸으로 읽는 셰이더 타입이면 true 입니다(`writeNumericValue` 의 float 갈래). */
            static bool isFloatShaderType( MaterialPropertyType shaderType )
            {
                return shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Float2 || shaderType == MaterialPropertyType::Float3 || shaderType == MaterialPropertyType::Float4 || shaderType == MaterialPropertyType::Float4x4 || shaderType == MaterialPropertyType::Range || shaderType == MaterialPropertyType::Color;
            }

            /** @brief 글을 그대로 `writeNumericValue` 로 넘기는 종류면 true 입니다(불리언 · 열거 · 채널 · 텍스처 · Range 는 글을 저마다 읽는다). */
            static bool isNumericPropertyType( MaterialPropertyType type )
            {
                switch ( type )
                {
                    case MaterialPropertyType::Color:
                    case MaterialPropertyType::Float:
                    case MaterialPropertyType::Float2:
                    case MaterialPropertyType::Float3:
                    case MaterialPropertyType::Float4:
                    case MaterialPropertyType::Float4x4:
                    case MaterialPropertyType::Uint:
                    case MaterialPropertyType::Uint2:
                    case MaterialPropertyType::Uint3:
                    case MaterialPropertyType::Uint4:
                    case MaterialPropertyType::Int:
                    case MaterialPropertyType::Int2:
                    case MaterialPropertyType::Int3:
                    case MaterialPropertyType::Int4:
                        return true;
                    default:
                        return false;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MaterialPropertyType MaterialPacking::stringToType( string_view str, uint32& outSize )
    {
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( MaterialPackingInternal::equalsName( str, desc._pName ) )
            {
                outSize = desc._size;
                return desc._type;
            }
        }
        outSize = 0;
        return MaterialPropertyType::Unknown;
    }

    const utf8* MaterialPacking::typeToString( MaterialPropertyType type )
    {
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( desc._type == type )
                return desc._pName;
        }
        return "Unknown";
    }

    uint32 MaterialPacking::packedSizeOf( MaterialPropertyType type )
    {
        uint32 size{ 0 };
        MaterialPacking::stringToType( MaterialPacking::typeToString( type ), size );
        // 알려진 enum 값이면 표에서 처음 맞는 항목의 크기를 쓴다
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( desc._type == type )
                return desc._size;
        }
        return size;
    }

    bool MaterialPacking::isTextureType( MaterialPropertyType type )
    {
        return type == MaterialPropertyType::Texture2D || type == MaterialPropertyType::TextureCube || type == MaterialPropertyType::Texture3D || type == MaterialPropertyType::Texture2DArray;
    }

    bool MaterialPacking::isNonBufferType( MaterialPropertyType type )
    {
        return type == MaterialPropertyType::Keyword;
    }

    MaterialPropertyType MaterialPacking::defaultShaderTypeFor( MaterialPropertyType cpuType )
    {
        switch ( cpuType )
        {
            case MaterialPropertyType::Bool:
            case MaterialPropertyType::Enum:
            case MaterialPropertyType::BitFlag:
            case MaterialPropertyType::ChannelMask:
            case MaterialPropertyType::Texture2D:
            case MaterialPropertyType::TextureCube:
            case MaterialPropertyType::Texture3D:
            case MaterialPropertyType::Texture2DArray:
                return MaterialPropertyType::Uint;
            case MaterialPropertyType::Range:
                return MaterialPropertyType::Float;
            case MaterialPropertyType::Color:
                return MaterialPropertyType::Float4;
            case MaterialPropertyType::Keyword:
            case MaterialPropertyType::Unknown:
                return MaterialPropertyType::Unknown;
            case MaterialPropertyType::Float:
            case MaterialPropertyType::Float2:
            case MaterialPropertyType::Float3:
            case MaterialPropertyType::Float4:
            case MaterialPropertyType::Float4x4:
            case MaterialPropertyType::Uint:
            case MaterialPropertyType::Uint2:
            case MaterialPropertyType::Uint3:
            case MaterialPropertyType::Uint4:
            case MaterialPropertyType::Int:
            case MaterialPropertyType::Int2:
            case MaterialPropertyType::Int3:
            case MaterialPropertyType::Int4:
                return cpuType;
        }
        return cpuType;
    }

    MaterialPropertyType MaterialPacking::shaderTypeFromReflectionName( string_view typeName, uint32 byteSize )
    {
        if ( typeName.empty() == false )
        {
            uint32                     ignored{ 0 };
            const MaterialPropertyType reflectedType       = MaterialPacking::stringToType( typeName, ignored );
            const bool                 bPlainReflectedType = reflectedType != MaterialPropertyType::Unknown && MaterialPacking::isNonBufferType( reflectedType ) == false && MaterialPacking::isTextureType( reflectedType ) == false && reflectedType != MaterialPropertyType::Enum && reflectedType != MaterialPropertyType::BitFlag && reflectedType != MaterialPropertyType::Range && reflectedType != MaterialPropertyType::Color && reflectedType != MaterialPropertyType::ChannelMask && reflectedType != MaterialPropertyType::Bool;
            if ( bPlainReflectedType )
                return reflectedType;
            // HLSL 의 bool 은 보통 "Bool" 로 보고된다
            if ( MaterialPackingInternal::equalsName( typeName, "Bool" ) )
                return MaterialPropertyType::Uint;
        }
        if ( byteSize == 4 )
            return MaterialPropertyType::Float;
        if ( byteSize == 8 )
            return MaterialPropertyType::Float2;
        if ( byteSize == 12 )
            return MaterialPropertyType::Float3;
        if ( byteSize == 16 )
            return MaterialPropertyType::Float4;
        if ( byteSize == 64 )
            return MaterialPropertyType::Float4x4;
        return MaterialPropertyType::Unknown;
    }

    uint32 MaterialPacking::alignOffset( uint32 offset, uint32 typeSize )
    {
        uint32 align = 4;
        if ( 4 < typeSize && typeSize <= 16 )
            align = 16;
        if ( typeSize == 64 )
            align = 16;
        return MathUtil::align( offset, align );
    }

    bool MaterialPacking::isPaddingMember( string_view memberName )
    {
        return StringUtil::startsWith( memberName, "_swPad" );
    }

    bool MaterialPacking::isTextureIndexMember( string_view memberName, string_view reflectedType )
    {
        // 리플렉션 이름을 직접 본다 — `shaderTypeFromReflectionName` 은 Bool 도 Uint 로 묶으므로 `bool useNormalMap` 이 텍스처 인덱스가 된다.
        return MaterialPackingInternal::equalsName( reflectedType, "Uint" ) && StringUtil::endsWith( memberName, string_view{ kTextureIndexMemberSuffix } );
    }

    vector<uint8> MaterialPacking::makeFallbackElement( const vector<ShaderVariableInfo>& listMember, uint32 stride )
    {
        vector<uint8> bytes( stride, 0 );
        const uint32  noTexture = kInvalidDescriptorIndex;
        const uint32  indexSize = static_cast<uint32>( sizeof( noTexture ) );
        for ( const ShaderVariableInfo& member : listMember )
        {
            const bool bFits = member._offset + indexSize <= stride;
            if ( bFits && isTextureIndexMember( member._name, member._type ) )
                Memory::copy( bytes.data() + member._offset, &noTexture, indexSize );
        }
        return bytes;
    }

    bool MaterialPacking::parseBoolToken( string_view token, string_view name, const utf8* pID, bool& outValue )
    {
        bool value{ false };
        if ( StringUtil::trim( token ).empty() || StringUtil::tryParseBool( token, value ) == false )
        {
            SW_LOG_WRITE( sw::LogLevel::Error, pID, "Material value '%#' has an unreadable boolean '%#'", name, token );
            return false;
        }
        outValue = value;
        return true;
    }

    bool MaterialPacking::packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer )
    {
        if ( MaterialPacking::isNonBufferType( prop._type ) )
        {
            prop._size = 0;
            return true;
        }

        prop._shaderType = MaterialPackingInternal::resolveShaderType( prop );
        prop._size       = MaterialPackingInternal::resolvePackSize( prop, prop._shaderType );
        return packValueIntoBuffer( prop, prop._value, buffer );
    }

    bool MaterialPacking::packValueIntoBuffer( const MaterialProperty& prop, string_view value, vector<uint8>& buffer )
    {
        if ( MaterialPacking::isNonBufferType( prop._type ) )
            return true;

        const MaterialPropertyType shaderType = MaterialPackingInternal::resolveShaderType( prop );
        const uint32               packSize   = MaterialPackingInternal::resolvePackSize( prop, shaderType );

        if ( buffer.size() < prop._offset + packSize )
            buffer.resize( prop._offset + packSize, 0 );

        uint8* pDst = buffer.data() + prop._offset;

        // 종류마다 쓰기 함수 하나인 짧은 디스패처다(긴 경우는 MaterialPackingInternal::pack* 에 있다).
        using Pack = MaterialPackingInternal;
        switch ( prop._type )
        {
            case MaterialPropertyType::Bool:
                return Pack::packBool( prop, value, shaderType, pDst, packSize );
            case MaterialPropertyType::Enum:
                return Pack::packEnum( prop, value, shaderType, false, pDst, packSize );
            case MaterialPropertyType::BitFlag:
                return Pack::packEnum( prop, value, shaderType, true, pDst, packSize );
            case MaterialPropertyType::ChannelMask:
                return Pack::packChannelMask( prop, value, shaderType, pDst, packSize );
            case MaterialPropertyType::Texture2D:
            case MaterialPropertyType::TextureCube:
            case MaterialPropertyType::Texture3D:
            case MaterialPropertyType::Texture2DArray:
                return Pack::packTextureIndex( prop, value, pDst, packSize );
            case MaterialPropertyType::Range:
                return Pack::packRange( prop, value, pDst, packSize );
            case MaterialPropertyType::Color:
            case MaterialPropertyType::Float:
            case MaterialPropertyType::Float2:
            case MaterialPropertyType::Float3:
            case MaterialPropertyType::Float4:
            case MaterialPropertyType::Float4x4:
            case MaterialPropertyType::Uint:
            case MaterialPropertyType::Uint2:
            case MaterialPropertyType::Uint3:
            case MaterialPropertyType::Uint4:
            case MaterialPropertyType::Int:
            case MaterialPropertyType::Int2:
            case MaterialPropertyType::Int3:
            case MaterialPropertyType::Int4:
                return MaterialPackingInternal::writeNumericValue( pDst, packSize, prop._type, shaderType, value );
            case MaterialPropertyType::Keyword:
            case MaterialPropertyType::Unknown:
                return false;
        }
        return MaterialPackingInternal::writeNumericValue( pDst, packSize, prop._type, shaderType, value );
    }

    bool MaterialPacking::packFloatsIntoBuffer( const MaterialProperty& prop, const float32* pValue, uint32 count, vector<uint8>& buffer )
    {
        const MaterialPropertyType shaderType = MaterialPackingInternal::resolveShaderType( prop );
        bool                       bFinite{ true };
        for ( uint32 index = 0; index < count; ++index )
        {
            bFinite = bFinite && std::isfinite( pValue[index] );
        }
        const bool bPlainFloats = MaterialPackingInternal::isNumericPropertyType( prop._type ) && MaterialPackingInternal::isFloatShaderType( shaderType );
        const bool bRange       = prop._type == MaterialPropertyType::Range && count == 1;
        // 글로 읽어야 뜻이 같은 경우(불리언 · 열거 · 정수 칸, 글이 거절하는 NaN · 무한)는 글로 간다. 최단 왕복 글이라 float 칸은 어느 길이든 같은 비트다.
        if ( bFinite == false || ( bPlainFloats == false && bRange == false ) )
            return packValueIntoBuffer( prop, formatFloats( pValue, count ), buffer );

        const uint32 packSize = MaterialPackingInternal::resolvePackSize( prop, shaderType );
        if ( buffer.size() < prop._offset + packSize )
            buffer.resize( prop._offset + packSize, 0 );
        uint8* pDst = buffer.data() + prop._offset;
        if ( bRange )
        {
            const float32 floatVal = prop._min < prop._max ? MathUtil::clamp( pValue[0], prop._min, prop._max ) : pValue[0];
            return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
        }

        const uint32 need = MaterialPacking::packedSizeOf( shaderType );
        if ( need == 0 || packSize < need )
            return false;
        // 모자란 칸의 규칙은 `writeNumericValue` 와 같다(0, 색의 알파는 1).
        float32      arrWord[16]{};
        const uint32 wordCount = need / 4;
        for ( uint32 index = 0; index < wordCount && index < count; ++index )
        {
            arrWord[index] = pValue[index];
        }
        if ( count <= 3 && wordCount > 3 && prop._type == MaterialPropertyType::Color )
            arrWord[3] = 1.0f;
        Memory::copy( pDst, arrWord, need );
        return true;
    }

    string MaterialPacking::formatFloats( const float32* pValue, uint32 count )
    {
        StringBuilder<constant::buffer::kMax64> text;
        for ( uint32 index = 0; index < count; ++index )
        {
            text.appendFormat( index == 0 ? "%#" : " %#", pValue[index] );
        }
        return string{ text.c_str(), text.size() };
    }
} // namespace sw

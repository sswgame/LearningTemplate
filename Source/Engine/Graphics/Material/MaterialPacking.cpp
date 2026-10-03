#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
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

            static bool iequals( string_view value, const utf8* pExpected )
            {
                return StringUtil::equals( value, pExpected, true );
            }

            /** @brief `_enumType` 이 가리키는 리플렉션 enum 입니다. 타입 이름은 찾기만 한다(에셋 글을 전역 이름 표에 넣지 않는다). */
            static const EnumInfo* findReflectedEnum( const MaterialProperty& prop )
            {
                if ( prop._enumType.empty() )
                    return nullptr;
                const hashed_string typeName = hashed_string::findInterned( prop._enumType );
                return typeName.empty() ? nullptr : engine::getTypeRegistry().findEnum( typeName );
            }

            /**
             * @brief 이름 · 숫자 토큰 하나를 값으로 읽습니다. 모르는 토큰이면 false 입니다.
             * @details 파일의 `_enumEntries` 이름(대소문자 무시)이 먼저다. 리플렉션 enum 이 있으면 `EnumInfo::tryParseText`(이름 · ValueAlias ·
             *          알려진 값의 숫자)이고, 없으면 숫자 리터럴을 받는다.
             */
            static bool tryParseEnumToken( const MaterialProperty& prop, const EnumInfo* pInfo, string_view token, int64& outValue )
            {
                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    if ( iequals( token, enumEntry._name.c_str() ) )
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
            static bool tryParseEnumOrFlags( const MaterialProperty& prop, string_view value, bool bitFlagMode, int64& outValue )
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

            static uint32 parseChannelMask( string_view value )
            {
                const string valueNt( value );
                const string trimmedValue{ StringUtil::trim( valueNt ) };
                if ( trimmedValue.empty() )
                    return 0xFu;

                // 숫자
                uint64 numericValue{ 0 };
                if ( StringUtil::parseUint64( trimmedValue, numericValue, 0 ) )
                    return static_cast<uint32>( numericValue );

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
                            break;
                    }
                }
                return mask != 0 ? mask : 0xFu;
            }

            /**
             * @brief 확보된 칸(`packSize`) 안에 들어갈 때만 씁니다.
             * @details 아래 `writeNumericValue` 는 처음부터 `packSize < need` 를 보고 있었는데,
             *          switch 안에서 **직접 `Memory::copy` 하는 형제 경로들**은 그 검사를 건너뛰었습니다.
             *          칸 크기는 셰이더 리플렉션이 정하고(`ShaderVariableInfo::_size`) 쓰는 크기는
             *          머티리얼 XML 의 `shaderType` 이 정하므로 **둘이 어긋날 수 있습니다.**
             *          예를 들어 셰이더가 `uint`(4바이트)로 선언한 자리에 XML 이
             *          `ChannelMask` + `shaderType="Float4"` 를 적으면 4바이트 칸에 16바이트를 씁니다.
             *          손으로 지은 머티리얼 XML 이 이 저장소를 여러 번 물었으므로, 조용히 넘치는
             *          대신 쓰지 않고 false 를 반환합니다(부르는 쪽이 경고합니다).
             */
            static bool writeBoundedValue( void* pDst, size_t packSize, const void* pSrc, size_t byteCount )
            {
                if ( pDst == nullptr || pSrc == nullptr || byteCount > packSize )
                    return false;
                Memory::copy( pDst, pSrc, byteCount );
                return true;
            }

            static bool writeNumericValue( void* pDst, size_t packSize, MaterialPropertyType shaderType, string_view value )
            {
                const uint32 need = MaterialUtil::packedSizeOf( shaderType );
                if ( need == 0 || packSize < need || pDst == nullptr )
                    return false;

                string_splitter splitter( value, { " ", "	", "," } );
                const auto&     tokens = splitter.getSplitList();
                if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Float2 || shaderType == MaterialPropertyType::Float3 || shaderType == MaterialPropertyType::Float4 || shaderType == MaterialPropertyType::Float4x4 || shaderType == MaterialPropertyType::Range || shaderType == MaterialPropertyType::Color )
                {
                    float32* pPtr  = reinterpret_cast<float32*>( pDst );
                    uint32   count = need / 4;
                    for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
                    {
                        // 글이 모자란 칸은 기본값(색의 알파는 1)이다. 있는데 못 읽은 칸은 알리고 0 이다(예전에는 조용히 0).
                        float32 component = ( propIndex == 3 && shaderType == MaterialPropertyType::Color ) ? 1.0f : 0.0f;
                        if ( propIndex < tokens.size() )
                        {
                            component = 0.0f;
                            if ( StringUtil::parseFloat( tokens[propIndex], component ) == false )
                                SW_LOG_WARNING( "Material value '%#' has an unreadable number '%#' - packed as 0", value, tokens[propIndex] );
                        }
                        pPtr[propIndex] = component;
                    }
                    return true;
                }
                if ( shaderType == MaterialPropertyType::Uint || shaderType == MaterialPropertyType::Uint2 || shaderType == MaterialPropertyType::Uint3 || shaderType == MaterialPropertyType::Uint4 )
                {
                    uint32* pPtr  = reinterpret_cast<uint32*>( pDst );
                    uint32  count = need / 4;
                    for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
                    {
                        uint64 component{ 0 };
                        if ( propIndex < tokens.size() && StringUtil::parseUint64( tokens[propIndex], component, 10 ) == false )
                            SW_LOG_WARNING( "Material value '%#' has an unreadable integer '%#' - packed as 0", value, tokens[propIndex] );
                        pPtr[propIndex] = static_cast<uint32>( component );
                    }
                    return true;
                }
                if ( shaderType == MaterialPropertyType::Int || shaderType == MaterialPropertyType::Int2 || shaderType == MaterialPropertyType::Int3 || shaderType == MaterialPropertyType::Int4 )
                {
                    int32* pPtr  = reinterpret_cast<int32*>( pDst );
                    uint32 count = need / 4;
                    for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
                    {
                        int32 component{ 0 };
                        if ( propIndex < tokens.size() && StringUtil::parseInt( tokens[propIndex], component, 10 ) == false )
                            SW_LOG_WARNING( "Material value '%#' has an unreadable integer '%#' - packed as 0", value, tokens[propIndex] );
                        pPtr[propIndex] = component;
                    }
                    return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    MaterialPropertyType MaterialUtil::stringToType( string_view str, uint32& outSize )
    {
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( MaterialPackingInternal::iequals( str, desc._pName ) )
            {
                outSize = desc._size;
                return desc._type;
            }
        }
        outSize = 0;
        return MaterialPropertyType::Unknown;
    }

    const utf8* MaterialUtil::typeToString( MaterialPropertyType type )
    {
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( desc._type == type )
                return desc._pName;
        }
        return "Unknown";
    }

    uint32 MaterialUtil::packedSizeOf( MaterialPropertyType type )
    {
        uint32 size{ 0 };
        MaterialUtil::stringToType( MaterialUtil::typeToString( type ), size );
        // 알려진 enum 값이면 표에서 처음 맞는 항목의 크기를 쓴다
        for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
        {
            if ( desc._type == type )
                return desc._size;
        }
        return size;
    }

    bool MaterialUtil::isTextureType( MaterialPropertyType type )
    {
        return type == MaterialPropertyType::Texture2D || type == MaterialPropertyType::TextureCube || type == MaterialPropertyType::Texture3D || type == MaterialPropertyType::Texture2DArray;
    }

    bool MaterialUtil::isNonBufferType( MaterialPropertyType type )
    {
        return type == MaterialPropertyType::Keyword;
    }

    MaterialPropertyType MaterialUtil::defaultShaderTypeFor( MaterialPropertyType cpuType )
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
            default:
                break;
        }
        return cpuType;
    }

    MaterialPropertyType MaterialUtil::shaderTypeFromReflectionName( string_view typeName, uint32 byteSize )
    {
        if ( typeName.empty() == false )
        {
            uint32                     ignored{ 0 };
            const MaterialPropertyType reflectedType = MaterialUtil::stringToType( typeName, ignored );
            if ( reflectedType != MaterialPropertyType::Unknown && MaterialUtil::isNonBufferType( reflectedType ) == false && MaterialUtil::isTextureType( reflectedType ) == false && reflectedType != MaterialPropertyType::Enum && reflectedType != MaterialPropertyType::BitFlag && reflectedType != MaterialPropertyType::Range && reflectedType != MaterialPropertyType::Color && reflectedType != MaterialPropertyType::ChannelMask && reflectedType != MaterialPropertyType::Bool )
                return reflectedType;
            // HLSL 의 bool 은 보통 "Bool" 로 보고된다
            if ( MaterialPackingInternal::iequals( typeName, "Bool" ) )
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

    uint32 MaterialUtil::alignOffset( uint32 offset, uint32 typeSize )
    {
        uint32 align = 4;
        if ( 4 < typeSize && typeSize <= 16 )
            align = 16;
        if ( typeSize == 64 )
            align = 16;
        return MathUtil::align( offset, align );
    }

    bool MaterialUtil::parseBoolToken( string_view token, string_view name, bool fallback )
    {
        if ( StringUtil::trim( token ).empty() )
            return fallback;
        bool value{ fallback };
        if ( StringUtil::tryParseBool( token, value ) == false )
            SW_LOG_WARNING( "Material value '%#' has an unreadable boolean '%#' - using %#", name, token, fallback ? "true" : "false" );
        return value;
    }

    bool MaterialUtil::packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer )
    {
        if ( MaterialUtil::isNonBufferType( prop._type ) )
        {
            prop._size = 0;
            return true;
        }

        MaterialPropertyType shaderType = prop._shaderType;
        if ( shaderType == MaterialPropertyType::Unknown )
            shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );
        prop._shaderType = shaderType;

        uint32 packSize = MaterialUtil::packedSizeOf( shaderType );
        if ( packSize == 0 )
            packSize = 4;
        if ( prop._size == 0 )
            prop._size = packSize;
        else
            packSize = prop._size;

        if ( buffer.size() < prop._offset + packSize )
            buffer.resize( prop._offset + packSize, 0 );

        uint8* pDst = buffer.data() + prop._offset;

        switch ( prop._type )
        {
            case MaterialPropertyType::Bool:
            {
                const uint32 boolVal = MaterialUtil::parseBoolToken( prop._value, prop._name, false ) ? 1u : 0u;
                if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Range )
                {
                    const float32 floatVal = boolVal != 0 ? 1.0f : 0.0f;
                    return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
                }
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &boolVal, sizeof( boolVal ) );
            }
            case MaterialPropertyType::Enum:
            {
                int64 enumVal{ 0 };
                if ( MaterialPackingInternal::tryParseEnumOrFlags( prop, prop._value, false, enumVal ) == false )
                {
                    SW_LOG_WARNING( "Material parameter '%#' has an unknown enum value '%#' - value kept", prop._name, prop._value );
                    return false;
                }
                const uint32 uEnumVal = static_cast<uint32>( enumVal );
                if ( shaderType == MaterialPropertyType::Int )
                {
                    const int32 intVal = static_cast<int32>( enumVal );
                    return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &intVal, sizeof( intVal ) );
                }
                if ( shaderType == MaterialPropertyType::Float )
                {
                    const float32 floatVal = static_cast<float32>( enumVal );
                    return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
                }
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &uEnumVal, sizeof( uEnumVal ) );
            }
            case MaterialPropertyType::BitFlag:
            {
                int64 flagsVal{ 0 };
                if ( MaterialPackingInternal::tryParseEnumOrFlags( prop, prop._value, true, flagsVal ) == false )
                {
                    SW_LOG_WARNING( "Material parameter '%#' has an unknown enum value '%#' - value kept", prop._name, prop._value );
                    return false;
                }
                const uint32 uEnumVal = static_cast<uint32>( flagsVal );
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &uEnumVal, sizeof( uEnumVal ) );
            }
            case MaterialPropertyType::ChannelMask:
            {
                const uint32 mask = MaterialPackingInternal::parseChannelMask( prop._value );
                if ( shaderType == MaterialPropertyType::Float4 )
                {
                    float32 arrComp[4] = {
                        ( mask & 1 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 2 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 4 ) != 0 ? 1.0f : 0.0f,
                        ( mask & 8 ) != 0 ? 1.0f : 0.0f,
                    };
                    return MaterialPackingInternal::writeBoundedValue( pDst, packSize, arrComp, sizeof( arrComp ) );
                }
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &mask, sizeof( mask ) );
            }
            case MaterialPropertyType::Texture2D:
            case MaterialPropertyType::TextureCube:
            case MaterialPropertyType::Texture3D:
            case MaterialPropertyType::Texture2DArray:
            {
                uint32 textureIndex = prop._textureIndex;
                if ( textureIndex == kInvalidDescriptorIndex && prop._value.empty() == false )
                {
                    // _value 에 숫자를 적어 덮어쓰는 것도 허용한다
                    uint64 numericVal{ 0 };
                    if ( StringUtil::parseUint64( prop._value, numericVal, 0 ) )
                        textureIndex = static_cast<uint32>( numericVal );
                }
                // 붙은 텍스처가 없으면 0 이 아니라 kInvalidIndex 를 넣는다. 0 은 "첫 번째 슬롯"
                // 이라는 **유효한** 디스크립터 인덱스라서, 셰이더가 그 자리에 있던 상수버퍼를
                // Texture2D 로 읽어 DX12 에서 GPU 페이지 폴트(DEVICE_HUNG)가 났다. 셰이더의
                // swSampleIndex 는 kInvalidIndex 를 "텍스처 없음" 으로 이미 처리한다.
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &textureIndex, sizeof( textureIndex ) );
            }
            case MaterialPropertyType::Range:
            {
                float32 floatVal{ 0.0f };
                if ( StringUtil::parseFloat( prop._value, floatVal ) == false )
                    SW_LOG_WARNING( "Material parameter '%#' has an unreadable number '%#' - using 0", prop._name, prop._value );
                if ( prop._min < prop._max )
                    floatVal = MathUtil::clamp( floatVal, prop._min, prop._max );
                return MaterialPackingInternal::writeBoundedValue( pDst, packSize, &floatVal, sizeof( floatVal ) );
            }
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
                return MaterialPackingInternal::writeNumericValue( pDst, packSize, shaderType, prop._value );
            case MaterialPropertyType::Keyword:
            case MaterialPropertyType::Unknown:
                return false;
            default:
                break;
        }
        return MaterialPackingInternal::writeNumericValue( pDst, packSize, shaderType, prop._value );
    }
} // namespace sw

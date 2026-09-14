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
                uint32               _size; ///< Packed size when used as shader/CB type (0 = non-CB)
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
                // Aliases (Unity / Unreal naming)
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

            static int64 resolveNamedValue( const MaterialProperty& prop, string_view token, bool bitFlagMode )
            {
                const string tokenNt( token );
                const string name = StringUtil::trim( tokenNt.c_str() );
                if ( name.empty() )
                    return 0;

                // Numeric literal
                {
                    int64 parsedValue{ 0 };
                    if ( StringUtil::parseInt64( name, parsedValue, 0 ) )
                        return parsedValue;
                }

                for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
                {
                    if ( iequals( enumEntry._name, name.c_str() ) )
                        return enumEntry._value;
                }

                if ( prop._enumType.empty() == false )
                {
                    const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( prop._enumType.c_str() ) );
                    if ( pInfo != nullptr )
                    {
                        if ( bitFlagMode || pInfo->_bIsBitFlag )
                            return pInfo->stringFlagsToValue( name );
                        hashed_string key( name.c_str() );
                        const auto    it = pInfo->_mapNameToValue.find( key );
                        if ( it != pInfo->_mapNameToValue.end() )
                            return it->second;
                    }
                }
                return 0;
            }

            static int64 parseEnumOrFlags( const MaterialProperty& prop, string_view value, bool bitFlagMode )
            {
                if ( bitFlagMode || prop._type == MaterialPropertyType::BitFlag )
                {
                    if ( prop._enumType.empty() == false )
                    {
                        const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( prop._enumType.c_str() ) );
                        if ( pInfo != nullptr && pInfo->_bIsBitFlag )
                            return pInfo->stringFlagsToValue( value );
                    }
                    int64           result{ 0 };
                    string_splitter splitter( value, { "|", "," } );
                    for ( string_view part : splitter.getSplitList() )
                    {
                        result |= resolveNamedValue( prop, string( part ), true );
                    }
                    return result;
                }
                return resolveNamedValue( prop, value, false );
            }

            static uint32 parseChannelMask( string_view value )
            {
                const string valueNt( value );
                const string trimmedValue = StringUtil::trim( valueNt.c_str() );
                if ( trimmedValue.empty() )
                    return 0xFu;

                // Numeric
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
                        float32 component{ 0.0f };
                        if ( propIndex < tokens.size() )
                            StringUtil::parseFloat( tokens[propIndex], component );
                        else
                            component = ( propIndex == 3 && shaderType == MaterialPropertyType::Color ) ? 1.0f : 0.0f;
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
                        if ( propIndex < tokens.size() )
                            StringUtil::parseUint64( tokens[propIndex], component, 10 );
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
                        if ( propIndex < tokens.size() )
                            StringUtil::parseInt( tokens[propIndex], component, 10 );
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
        // Prefer first matching entry size for known enum values
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
            // Bool from HLSL often reported as Bool
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

    bool MaterialUtil::parseBoolToken( string_view token )
    {
        return StringUtil::parseBool( token, false );
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
                const uint32 boolVal = MaterialUtil::parseBoolToken( prop._value ) ? 1u : 0u;
                if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Range )
                {
                    const float32 floatVal = boolVal != 0 ? 1.0f : 0.0f;
                    Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
                }
                else
                    Memory::copy( pDst, &boolVal, sizeof( boolVal ) );
                return true;
            }
            case MaterialPropertyType::Enum:
            {
                const int64  enumVal  = MaterialPackingInternal::parseEnumOrFlags( prop, prop._value, false );
                const uint32 uEnumVal = static_cast<uint32>( enumVal );
                if ( shaderType == MaterialPropertyType::Int )
                {
                    const int32 intVal = static_cast<int32>( enumVal );
                    Memory::copy( pDst, &intVal, sizeof( intVal ) );
                }
                else if ( shaderType == MaterialPropertyType::Float )
                {
                    const float32 floatVal = static_cast<float32>( enumVal );
                    Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
                }
                else
                    Memory::copy( pDst, &uEnumVal, sizeof( uEnumVal ) );
                return true;
            }
            case MaterialPropertyType::BitFlag:
            {
                const uint32 uEnumVal = static_cast<uint32>( MaterialPackingInternal::parseEnumOrFlags( prop, prop._value, true ) );
                Memory::copy( pDst, &uEnumVal, sizeof( uEnumVal ) );
                return true;
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
                    Memory::copy( pDst, arrComp, sizeof( arrComp ) );
                }
                else
                    Memory::copy( pDst, &mask, sizeof( mask ) );
                return true;
            }
            case MaterialPropertyType::Texture2D:
            case MaterialPropertyType::TextureCube:
            case MaterialPropertyType::Texture3D:
            case MaterialPropertyType::Texture2DArray:
            {
                uint32 textureIndex = prop._textureIndex;
                if ( textureIndex == kInvalidDescriptorIndex && prop._value.empty() == false )
                {
                    // Allow numeric override in _value
                    uint64 numericVal{ 0 };
                    if ( StringUtil::parseUint64( prop._value, numericVal, 0 ) )
                        textureIndex = static_cast<uint32>( numericVal );
                }
                // 붙은 텍스처가 없으면 0 이 아니라 SW_INVALID_INDEX 를 넣는다. 0 은 "첫 번째 슬롯"
                // 이라는 **유효한** 디스크립터 인덱스라서, 셰이더가 그 자리에 있던 상수버퍼를
                // Texture2D 로 읽어 DX12 에서 GPU 페이지 폴트(DEVICE_HUNG)가 났다. 셰이더의
                // SW_SampleIndex 는 SW_INVALID_INDEX 를 "텍스처 없음" 으로 이미 처리한다.
                Memory::copy( pDst, &textureIndex, sizeof( textureIndex ) );
                return true;
            }
            case MaterialPropertyType::Range:
            {
                float32 floatVal{ 0.0f };
                StringUtil::parseFloat( prop._value, floatVal );
                if ( prop._min < prop._max )
                    floatVal = MathUtil::clamp( floatVal, prop._min, prop._max );
                if ( shaderType == MaterialPropertyType::Float || packSize >= 4 )
                    Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
                return true;
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

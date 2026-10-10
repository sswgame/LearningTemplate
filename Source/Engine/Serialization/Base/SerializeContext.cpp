#include "pch.h"

#include "Engine/Serialization/Base/SerializeContext.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/TagID.h"

#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Base/SerializeReflectAny.h"

namespace sw
{
    namespace
    {
        struct SerializeContextInternal
        {
            static string_view unquote( string_view strView )
            {
                string_view trimmed = StringUtil::trim( strView );
                if ( trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"' )
                {
                    trimmed.remove_prefix( 1 );
                    trimmed.remove_suffix( 1 );
                }
                return trimmed;
            }

            template <typename T>
            static void regBuiltinBin( SerializeContext& context, const utf8* pName )
            {
                if constexpr ( std::is_same_v<T, string> || std::is_same_v<T, hashed_string> ||
                               std::is_same_v<T, atomic<bool>> || std::is_same_v<T, TagID> )
                    return;
                auto writeFn = []( const void* pPtr, vector<uint8>& listBuf )
                {
                    const uint8* pByte = reinterpret_cast<const uint8*>( pPtr );
                    listBuf.insert( listBuf.end(), pByte, pByte + sizeof( T ) );
                };
                auto readFn = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
                {
                    if ( offset + sizeof( T ) > size )
                        return false;
                    // bool 은 바이트를 그대로 옮기지 않는다. 0 · 1 이 아닌 바이트(망가진 파일)를 memcpy 하면 값이 정의되지 않는 bool 이 되고,
                    // 컴파일러는 그것을 2 로 쓰거나 `b` 와 `b == true` 를 다르게 본다.
                    if constexpr ( std::is_same_v<T, bool> )
                        *static_cast<bool*>( pPtr ) = pData[offset] != 0;
                    else
                        Memory::copy( pPtr, pData + offset, sizeof( T ) );
                    offset += sizeof( T );
                    return true;
                };
                context.registerBinaryHandler( hashed_string( pName ), writeFn, readFn );
            }

            /**
             * @brief 필드 타입의 범위를 넘는 정수 글자를 거절합니다. 값은 그대로 둡니다(대개 멤버 초기값) — 읽는 쪽이 orphan 으로 남기거나 실패로 알립니다.
             * @details 64 비트로 읽은 뒤 잘라 넣으면 "300" 이 uint8 44, "4000000000" 이 int32 음수가 된다. 쓰는 쪽은 늘 범위 안의 값을 적으므로
             *          왕복은 그대로다(모르는 enum 이름을 다루는 `SerializerUtil::parseTextValue` 와 같은 규칙).
             */
            static bool rejectOutOfRange( string_view token, size_t byteSize )
            {
                SW_LOG_WARNING( "'%#' is out of range for a %#-byte integer field - the field keeps its current value", token,
                                static_cast<uint32>( byteSize ) );
                return false;
            }

            template <typename T>
            [[nodiscard]] static bool parseScalarValue( string_view token, T& outValue )
            {
                const string_view trimmed = StringUtil::trim( token );
                if ( trimmed.empty() )
                    return false;

                if constexpr ( std::is_floating_point_v<T> )
                {
                    if constexpr ( sizeof( T ) == sizeof( float32 ) )
                    {
                        float32 val{ 0.0f };
                        if ( StringUtil::parseFloat( trimmed, val ) == false )
                            return false;
                        outValue = static_cast<T>( val );
                    }
                    else
                    {
                        float64 val{ 0.0 };
                        if ( StringUtil::parseDouble( trimmed, val ) == false )
                            return false;
                        outValue = static_cast<T>( val );
                    }
                }
                else if constexpr ( std::is_unsigned_v<T> )
                {
                    uint64 val{ 0 };
                    if ( StringUtil::parseUint64( trimmed, val, 10 ) == false )
                        return false;
                    if ( val > static_cast<uint64>( std::numeric_limits<T>::max() ) )
                        return rejectOutOfRange( trimmed, sizeof( T ) );
                    outValue = static_cast<T>( val );
                }
                else
                {
                    int64 val{ 0 };
                    if ( StringUtil::parseInt64( trimmed, val, 10 ) == false )
                        return false;
                    if ( val < static_cast<int64>( std::numeric_limits<T>::min() ) || val > static_cast<int64>( std::numeric_limits<T>::max() ) )
                        return rejectOutOfRange( trimmed, sizeof( T ) );
                    outValue = static_cast<T>( val );
                }
                return true;
            }

            template <typename T>
            static void registerNumericTextHandler( SerializeContext& context, const hashed_string& typeName )
            {
                context.registerTextHandler(
                    typeName,
                    []( const void* pPtr )
                { return sw::to_string( *static_cast<const T*>( pPtr ) ); },
                    []( void* pPtr, string_view strView ) -> bool
                {
                    return parseScalarValue<T>( strView, *static_cast<T*>( pPtr ) );
                } );
            }

            template <typename TVec, typename TElem, int32 kCount>
            static void registerVectorTextHandler( SerializeContext& context, const hashed_string& typeName )
            {
                context.registerTextHandler(
                    typeName,
                    []( const void* pPtr )
                {
                    const auto*                           pElem = reinterpret_cast<const TElem*>( pPtr );
                    StringBuilder<constant::kMaxBuffer64> sb;
                    for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
                    {
                        if ( axisIndex > 0 )
                            sb.append( ',' );
                        sb.append( pElem[axisIndex] );
                    }
                    return string{ sb.c_str(), sb.size() };
                },
                    []( void* pPtr, string_view strView )
                {
                    auto*  pOut = reinterpret_cast<TElem*>( pPtr );
                    size_t start{ 0 };
                    for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
                    {
                        const size_t      sep   = strView.find( ',', start );
                        const string_view token = strView.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
                        if ( parseScalarValue<TElem>( token, pOut[axisIndex] ) == false )
                            return false;
                        if ( sep == string_view::npos )
                        {
                            if ( axisIndex != kCount - 1 )
                                return false;
                            break;
                        }
                        start = sep + 1;
                    }
                    return true;
                } );
            }

            static void registerFloatVectorTextHandlers( SerializeContext& context )
            {
                registerVectorTextHandler<float2, float32, 2>( context, hashed_string( PredefinedNameType::NameType_float2 ) );
                registerVectorTextHandler<float3, float32, 3>( context, hashed_string( PredefinedNameType::NameType_float3 ) );
                registerVectorTextHandler<float4, float32, 4>( context, hashed_string( PredefinedNameType::NameType_float4 ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SerializeContext::registerBinaryHandler( hashed_string typeName, BinaryWriteFn writeFn, BinaryReadFn readFn )
    {
        _mapBinaryWriter.insert_or_assign( typeName, std::move( writeFn ) );
        _mapBinaryReader.insert_or_assign( typeName, std::move( readFn ) );
    }

    void SerializeContext::registerTextHandler( hashed_string typeName, TextWriteFn writeFn, TextReadFn readFn )
    {
        _mapTextWriter.insert_or_assign( typeName, std::move( writeFn ) );
        _mapTextReader.insert_or_assign( typeName, std::move( readFn ) );
    }

    const SerializeContext::BinaryWriteFn* SerializeContext::findBinaryWriter( hashed_string typeName ) const
    {
        auto it = _mapBinaryWriter.find( typeName );
        if ( it != _mapBinaryWriter.end() )
            return &it->second;
        return ( _pHandlerFallback != nullptr ) ? _pHandlerFallback->findBinaryWriter( typeName ) : nullptr;
    }

    const SerializeContext::BinaryReadFn* SerializeContext::findBinaryReader( hashed_string typeName ) const
    {
        auto it = _mapBinaryReader.find( typeName );
        if ( it != _mapBinaryReader.end() )
            return &it->second;
        return ( _pHandlerFallback != nullptr ) ? _pHandlerFallback->findBinaryReader( typeName ) : nullptr;
    }

    const SerializeContext::TextWriteFn* SerializeContext::findTextWriter( hashed_string typeName ) const
    {
        auto it = _mapTextWriter.find( typeName );
        if ( it != _mapTextWriter.end() )
            return &it->second;
        return ( _pHandlerFallback != nullptr ) ? _pHandlerFallback->findTextWriter( typeName ) : nullptr;
    }

    const SerializeContext::TextReadFn* SerializeContext::findTextReader( hashed_string typeName ) const
    {
        auto it = _mapTextReader.find( typeName );
        if ( it != _mapTextReader.end() )
            return &it->second;
        return ( _pHandlerFallback != nullptr ) ? _pHandlerFallback->findTextReader( typeName ) : nullptr;
    }

    SerializeContext SerializeContext::deriveFromDefault()
    {
        const SerializeContext& defaultContext = getDefault();

        SerializeContext derived;
        derived._pHandlerFallback = &defaultContext;
        // 표만 빌리고 **설정은 물려받는다**(`= getDefault()` 와 같은 동작).
        derived.setIgnoreCaseKeys( defaultContext.ignoresCaseKeys() );
        derived.setAllowUnknownProperties( defaultContext.allowsUnknownProperties() );
        return derived;
    }

    const SerializeContext& SerializeContext::getDefault()
    {
        static SerializeContext s_defaultContext = []()
        {
            SerializeContext context;

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) SerializeContextInternal::regBuiltinBin<CppType>( context, #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER

            BinaryWriteFn strWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
            {
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( *static_cast<const string*>( pPtr ) );
            };

            BinaryReadFn strReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
            {
                BinaryStreamReader reader{ pData, size };
                if ( reader.skip( offset ) == false )
                    return false;
                if ( reader.readString( *static_cast<string*>( pPtr ) ) == false )
                    return false;
                offset = reader.getOffset();
                return true;
            };

            context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_string ), strWriteBin, strReadBin );

            BinaryWriteFn hashedStrWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
            {
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( static_cast<const hashed_string*>( pPtr )->c_str() );
            };

            BinaryReadFn hashedStrReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
            {
                BinaryStreamReader reader{ pData, size };
                if ( reader.skip( offset ) == false )
                    return false;
                string_view text;
                if ( reader.readStringView( text ) == false )
                    return false;
                *static_cast<hashed_string*>( pPtr ) = hashed_string( text.data(), static_cast<uint32>( text.size() ) );
                offset                               = reader.getOffset();
                return true;
            };

            context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), hashedStrWriteBin, hashedStrReadBin );

            BinaryWriteFn atomicBoolWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
            {
                const uint8 flag = static_cast<const atomic<bool>*>( pPtr )->load() ? 1 : 0;
                listBuf.push_back( flag );
            };
            BinaryReadFn atomicBoolReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
            {
                if ( offset + sizeof( uint8 ) > size )
                    return false;
                static_cast<atomic<bool>*>( pPtr )->store( pData[offset] != 0 );
                offset += sizeof( uint8 );
                return true;
            };
            context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), atomicBoolWriteBin, atomicBoolReadBin );

            BinaryWriteFn tagIDWriteBin = []( const void* pPtr, vector<uint8>& listBuf )
            {
                const TagID&       tag  = *static_cast<const TagID*>( pPtr );
                const utf8*        pStr = tag._pString != nullptr ? tag._pString : "";
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( pStr );
            };
            BinaryReadFn tagIDReadBin = []( void* pPtr, const uint8* pData, size_t size, size_t& offset ) -> bool
            {
                BinaryStreamReader reader{ pData, size };
                if ( reader.skip( offset ) == false )
                    return false;
                string_view text;
                if ( reader.readStringView( text ) == false )
                    return false;
                if ( text.empty() )
                    *static_cast<TagID*>( pPtr ) = TagID{};
                else
                    *static_cast<TagID*>( pPtr ) = TagID::request( text );
                offset = reader.getOffset();
                return true;
            };
            context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_TagID ), tagIDWriteBin, tagIDReadBin );

#define SW_BUILTIN_TEXT_none( Canon, CppType )
#define SW_BUILTIN_TEXT_stoi( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoll( Canon, CppType )  SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoul( Canon, CppType )  SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoull( Canon, CppType ) SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stof( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stod( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) SW_BUILTIN_TEXT_##TextConv( Canon, CppType )
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
#undef SW_BUILTIN_TEXT_none
#undef SW_BUILTIN_TEXT_stoi
#undef SW_BUILTIN_TEXT_stoll
#undef SW_BUILTIN_TEXT_stoul
#undef SW_BUILTIN_TEXT_stoull
#undef SW_BUILTIN_TEXT_stof
#undef SW_BUILTIN_TEXT_stod

            auto boolWrite = []( const void* pPtr )
            { return *static_cast<const bool*>( pPtr ) ? "true" : "false"; };
            // 불리언 글이 아니면 실패다(값은 그대로) — "ture" 가 조용히 false 가 되지 않게. 읽는 쪽이 고아 · 실패로 알린다.
            auto boolRead = []( void* pPtr, string_view strView )
            {
                return StringUtil::tryParseBool( strView, *static_cast<bool*>( pPtr ) );
            };
            context.registerTextHandler( hashed_string( PredefinedNameType::NameType_bool ), boolWrite, boolRead );

            auto atomicBoolWrite = []( const void* pPtr )
            { return static_cast<const atomic<bool>*>( pPtr )->load() ? "true" : "false"; };
            auto atomicBoolRead = []( void* pPtr, string_view strView )
            {
                bool bValue = false;
                if ( StringUtil::tryParseBool( strView, bValue ) == false )
                    return false;
                static_cast<atomic<bool>*>( pPtr )->store( bValue );
                return true;
            };
            context.registerTextHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), atomicBoolWrite, atomicBoolRead );

            auto tagIDWrite = []( const void* pPtr ) -> string
            {
                const TagID& tag = *static_cast<const TagID*>( pPtr );
                return tag._pString != nullptr ? string( tag._pString ) : string{};
            };
            auto tagIDRead = []( void* pPtr, string_view strView ) -> bool
            {
                string_view text = StringUtil::trim( strView );
                if ( StringUtil::startsWith( text, "str:" ) )
                    text.remove_prefix( 4 );
                if ( text.empty() )
                {
                    *static_cast<TagID*>( pPtr ) = TagID{};
                    return true;
                }
                *static_cast<TagID*>( pPtr ) = TagID::request( text );
                return true;
            };
            context.registerTextHandler( hashed_string( PredefinedNameType::NameType_TagID ), tagIDWrite, tagIDRead );

            TextWriteFn strWriteTxt = []( const void* pPtr )
            { return string( static_cast<const string*>( pPtr )->c_str() ); };
            TextReadFn strReadTxt = []( void* pPtr, string_view strView )
            {
                *static_cast<string*>( pPtr ) = string( SerializeContextInternal::unquote( strView ) );
                return true;
            };

            context.registerTextHandler( hashed_string( PredefinedNameType::NameType_string ), strWriteTxt, strReadTxt );

            TextWriteFn hashedStrWriteTxt = []( const void* pPtr )
            { return string( static_cast<const hashed_string*>( pPtr )->c_str() ); };
            TextReadFn hashedStrReadTxt = []( void* pPtr, string_view strView )
            {
                const string_view sv                 = SerializeContextInternal::unquote( strView );
                *static_cast<hashed_string*>( pPtr ) = hashed_string( sv.data(), static_cast<uint32>( sv.size() ) );
                return true;
            };

            context.registerTextHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), hashedStrWriteTxt, hashedStrReadTxt );

            auto packedWrite = []( const void* pPtr ) -> string
            {
                const uint64 packed = static_cast<const SlotHandle*>( pPtr )->packed();
                return sw::to_string( packed );
            };
            auto packedRead = []( void* pPtr, string_view strView ) -> bool
            {
                uint64 packed{ 0 };
                if ( StringUtil::parseUint64( StringUtil::trim( strView ), packed, 10 ) == false )
                    return false;
                *static_cast<SlotHandle*>( pPtr ) = SlotHandle::fromPacked( packed );
                return true;
            };
            context.registerTextHandler( hashed_string( "SlotHandle" ), packedWrite, packedRead );

            context.registerTextHandler(
                hashed_string( "ComponentHandle" ),
                []( const void* pPtr )
            {
                const auto&                           handle = *static_cast<const ComponentHandle*>( pPtr );
                StringBuilder<constant::kMaxBuffer64> sb;
                sb.append( handle.objectID() ).append( ':' ).append( handle.componentID() );
                return string{ sb.c_str(), sb.size() };
            },
                []( void* pPtr, string_view strView )
            {
                const string_view trimmed = StringUtil::trim( strView );
                const size_t      sep     = trimmed.find( ':' );
                if ( sep == string_view::npos )
                    return false;
                uint64 objectID{ 0 };
                uint64 componentID{ 0 };
                if ( StringUtil::parseUint64( trimmed.substr( 0, sep ), objectID, 10 ) == false ||
                     StringUtil::parseUint64( trimmed.substr( sep + 1 ), componentID, 10 ) == false )
                    return false;
                *static_cast<ComponentHandle*>( pPtr ) =
                    ComponentHandle::makeOwned( objectID, componentID );
                return true;
            } );

            context.registerTextHandler(
                hashed_string( "GameObjectHandle" ),
                []( const void* pPtr )
            { return sw::to_string( static_cast<const GameObjectHandle*>( pPtr )->objectID() ); },
                []( void* pPtr, string_view strView )
            {
                uint64 objectID{ 0 };
                if ( StringUtil::parseUint64( StringUtil::trim( strView ), objectID, 10 ) == false )
                    return false;
                *static_cast<GameObjectHandle*>( pPtr ) = GameObjectHandle::make( objectID );
                return true;
            } );

            SerializeContextInternal::registerFloatVectorTextHandlers( context );
            registerReflectAnyHandlers( context );
            return context;
        }();

        return s_defaultContext;
    }

} // namespace sw

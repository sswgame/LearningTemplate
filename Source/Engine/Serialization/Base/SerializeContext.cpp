#include "pch.h"

#include "Engine/Serialization/Base/SerializeContext.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/PackedTypes.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/TagID.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectAny.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/BinaryStream.h"
#include "Engine/Serialization/Base/DataPath.h"
#include "Engine/Serialization/Base/SerializeReflectAny.h"

namespace sw
{
    namespace
    {
        struct SerializeContextInternal
        {
            /** @brief 양끝의 큰따옴표 한 쌍을 벗깁니다. 공백은 건드리지 않습니다(글 칸은 끝 공백도 값이라 왕복이 고정점이어야 한다). */
            static string_view stripQuotes( string_view strView )
            {
                const bool bQuotedText = strView.size() >= 2 && strView.front() == '"' && strView.back() == '"';
                if ( bQuotedText )
                {
                    strView.remove_prefix( 1 );
                    strView.remove_suffix( 1 );
                }
                return strView;
            }

            /** @brief 공백을 자르고 큰따옴표를 벗깁니다. 이름 칸(`hashed_string`)용입니다 - 이름의 앞뒤 공백은 값이 아니다. */
            static string_view unquote( string_view strView )
            {
                return stripQuotes( StringUtil::trim( strView ) );
            }

            template <typename T>
            static void writeBuiltinBinary( const void* pPtr, vector<uint8>& listBuf )
            {
                const uint8* pByte = reinterpret_cast<const uint8*>( pPtr );
                listBuf.insert( listBuf.end(), pByte, pByte + sizeof( T ) );
            }

            template <typename T>
            [[nodiscard]] static bool readBuiltinBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
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
            }

            static void writeStringBinary( const void* pPtr, vector<uint8>& listBuf )
            {
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( *static_cast<const string*>( pPtr ) );
            }

            [[nodiscard]] static bool readStringBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
            {
                BinaryStreamReader reader{ pData, size };
                if ( reader.skip( offset ) == false )
                    return false;
                if ( reader.readString( *static_cast<string*>( pPtr ) ) == false )
                    return false;
                offset = reader.getOffset();
                return true;
            }

            static void writeHashedStringBinary( const void* pPtr, vector<uint8>& listBuf )
            {
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( static_cast<const hashed_string*>( pPtr )->c_str() );
            }

            [[nodiscard]] static bool readHashedStringBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
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
            }

            static void writeAtomicBoolBinary( const void* pPtr, vector<uint8>& listBuf )
            {
                const uint8 flag = static_cast<const atomic<bool>*>( pPtr )->load() ? 1 : 0;
                listBuf.push_back( flag );
            }

            [[nodiscard]] static bool readAtomicBoolBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
            {
                if ( offset + sizeof( uint8 ) > size )
                    return false;
                static_cast<atomic<bool>*>( pPtr )->store( pData[offset] != 0 );
                offset += sizeof( uint8 );
                return true;
            }

            static void writeTagIDBinary( const void* pPtr, vector<uint8>& listBuf )
            {
                const TagID&       tag  = *static_cast<const TagID*>( pPtr );
                const utf8*        pStr = tag._pString != nullptr ? tag._pString : "";
                BinaryStreamWriter writer{ listBuf };
                writer.writeString( pStr );
            }

            [[nodiscard]] static bool readTagIDBinary( void* pPtr, const uint8* pData, size_t size, size_t& offset )
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
            }

            static string writeBoolText( const void* pPtr ) { return *static_cast<const bool*>( pPtr ) ? "true" : "false"; }

            // 불리언 글이 아니면 실패다(값은 그대로) — "ture" 가 조용히 false 가 되지 않게. 읽는 쪽이 고아 · 실패로 알린다.
            [[nodiscard]] static bool readBoolText( void* pPtr, string_view strView ) { return StringUtil::tryParseBool( strView, *static_cast<bool*>( pPtr ) ); }

            static string writeAtomicBoolText( const void* pPtr ) { return static_cast<const atomic<bool>*>( pPtr )->load() ? "true" : "false"; }

            [[nodiscard]] static bool readAtomicBoolText( void* pPtr, string_view strView )
            {
                bool bValue = false;
                if ( StringUtil::tryParseBool( strView, bValue ) == false )
                    return false;
                static_cast<atomic<bool>*>( pPtr )->store( bValue );
                return true;
            }

            static string writeTagIDText( const void* pPtr )
            {
                const TagID& tag = *static_cast<const TagID*>( pPtr );
                return tag._pString != nullptr ? string( tag._pString ) : string{};
            }

            [[nodiscard]] static bool readTagIDText( void* pPtr, string_view strView )
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
            }

            static string writeStringText( const void* pPtr ) { return string( static_cast<const string*>( pPtr )->c_str() ); }

            [[nodiscard]] static bool readStringText( void* pPtr, string_view strView )
            {
                *static_cast<string*>( pPtr ) = string( stripQuotes( strView ) );
                return true;
            }

            static string writeHashedStringText( const void* pPtr ) { return string( static_cast<const hashed_string*>( pPtr )->c_str() ); }

            [[nodiscard]] static bool readHashedStringText( void* pPtr, string_view strView )
            {
                const string_view sv                 = unquote( strView );
                *static_cast<hashed_string*>( pPtr ) = hashed_string( sv.data(), static_cast<uint32>( sv.size() ) );
                return true;
            }

            static string writeSlotHandleText( const void* pPtr )
            {
                const uint64 packed = static_cast<const SlotHandle*>( pPtr )->packed();
                return sw::to_string( packed );
            }

            [[nodiscard]] static bool readSlotHandleText( void* pPtr, string_view strView )
            {
                uint64 packed{ 0 };
                if ( StringUtil::parseUint64( StringUtil::trim( strView ), packed, 10 ) == false )
                    return false;
                *static_cast<SlotHandle*>( pPtr ) = SlotHandle::fromPacked( packed );
                return true;
            }

            template <typename T>
            static void regBuiltinBin( SerializeContext& context, const utf8* pName )
            {
                constexpr bool bSkippedBuiltInType = std::is_same_v<T, string> || std::is_same_v<T, hashed_string> ||
                                                     std::is_same_v<T, atomic<bool>> || std::is_same_v<T, TagID>;
                if constexpr ( bSkippedBuiltInType )
                    return;
                context.registerBinaryHandler( hashed_string( pName ), SW_DELEGATE_FUNCTION( SerializeContext::BinaryWriteFn, &writeBuiltinBinary<T> ),
                                               SW_DELEGATE_FUNCTION( SerializeContext::BinaryReadFn, &readBuiltinBinary<T> ) );
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

            /**
             * @brief 원소 @p kCount 개를 쉼표로 이은 글로 씁니다(`1,0.5,0`). 벡터 · 행렬 · 압축 타입의 글 처리기가 한 형식을 씁니다.
             * @details 행렬(16 칸)은 64 바이트에 담기지 않는다 — 칸 수에 맞는 버퍼를 고른다.
             */
            template <typename TElem, int32 kCount>
            static string writeNumberList( const TElem* pElem )
            {
                StringBuilder<( kCount > 4 ) ? constant::kMaxBuffer512 : constant::kMaxBuffer64> sb;
                for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
                {
                    if ( axisIndex > 0 )
                        sb.append( ',' );
                    sb.append( pElem[axisIndex] );
                }
                return string{ sb.c_str(), sb.size() };
            }

            /** @brief 쉼표로 이은 글에서 원소 정확히 @p kCount 개를 읽습니다. 모자라거나 남거나 숫자가 아니면 false 이고 @p pOut 은 그대로입니다(다 읽은 뒤에만 씁니다). */
            template <typename TElem, int32 kCount>
            [[nodiscard]] static bool readNumberList( string_view strView, TElem* pOut )
            {
                TElem  arrParsed[kCount]{};
                size_t start{ 0 };
                for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
                {
                    const size_t      sep   = strView.find( ',', start );
                    const string_view token = strView.substr( start, sep == string_view::npos ? string_view::npos : sep - start );
                    if ( parseScalarValue<TElem>( token, arrParsed[axisIndex] ) == false )
                        return false;
                    const bool isLast = axisIndex == kCount - 1;
                    // 마지막 칸 뒤에 쉼표가 더 있으면(칸이 남으면) 거절하고, 마지막 전에 쉼표가 없으면(칸이 모자라면) 거절한다.
                    if ( ( sep == string_view::npos ) != isLast )
                        return false;
                    start = sep + 1;
                }
                for ( int32 axisIndex = 0; axisIndex < kCount; ++axisIndex )
                {
                    pOut[axisIndex] = arrParsed[axisIndex];
                }
                return true;
            }

            template <typename TVec, typename TElem, int32 kCount>
            static void registerVectorTextHandler( SerializeContext& context, const hashed_string& typeName )
            {
                context.registerTextHandler(
                    typeName,
                    []( const void* pPtr )
                { return writeNumberList<TElem, kCount>( reinterpret_cast<const TElem*>( pPtr ) ); },
                    []( void* pPtr, string_view strView )
                { return readNumberList<TElem, kCount>( strView, reinterpret_cast<TElem*>( pPtr ) ); } );
            }

            /**
             * @brief 압축 타입(`ReflectBuiltins.xxx` 의 `packed` 줄)의 글 처리기를 둡니다. 형식은 성분 실수의 쉼표 목록(`0.5,1,0.25`)입니다.
             * @details 쓸 때는 `toFloats` 로 푼 값이 왕복에 안전한 자릿수로 나가고, 읽을 때는 범위를 넘는 성분을 거절합니다(정수 필드가 범위를 넘는 글을 거절하는 것과
             *          같다 — `unorm8` 에 "5" 가 말없이 1 이 되지 않는다). 거절하면 값은 그대로입니다. 한 번 푼 값은 다시 같은 비트가 됩니다(`PackedValueTraits.h` 의 예외 둘 제외).
             *          무한대 · NaN 글은 실수 읽기(`StringUtil::parseFloat`)가 유한한 값만 받으므로 `half` 의 무한대는 글로 왕복하지 못합니다(이진은 비트 그대로).
             */
            template <typename T>
            static void registerPackedTextHandler( SerializeContext& context, const utf8* pTypeName )
            {
                context.registerTextHandler(
                    hashed_string( pTypeName ),
                    []( const void* pPtr )
                {
                    const PackedFloats floats = PackedValueTraits<T>::toFloats( *static_cast<const T*>( pPtr ) );
                    return writeNumberList<float32, static_cast<int32>( PackedValueTraits<T>::kComponentCount )>( floats._arrValue );
                },
                    []( void* pPtr, string_view strView )
                {
                    constexpr int32 kCount = static_cast<int32>( PackedValueTraits<T>::kComponentCount );
                    PackedFloats    floats{};
                    if ( readNumberList<float32, kCount>( strView, floats._arrValue ) == false )
                        return false;
                    if ( isPackedFloatsInRange<T>( floats ) == false )
                    {
                        SW_LOG_WARNING( "'%#' is out of range for a packed value - the field keeps its current value", StringUtil::trim( strView ) );
                        return false;
                    }
                    *static_cast<T*>( pPtr ) = PackedValueTraits<T>::fromFloats( floats );
                    return true;
                } );
            }

            /** @brief 실수 칸을 쉼표로 잇는 글 처리기를 둡니다. 쿼터니언은 (x, y, z, w), 행렬은 행 우선 16 개입니다(스켈레톤 파일이 쓴다). */
            static void registerFloatVectorTextHandlers( SerializeContext& context )
            {
                static_assert( sizeof( quaternion ) == 4 * sizeof( float32 ) && sizeof( float4x4 ) == 16 * sizeof( float32 ), "칸이 빈틈 없이 이어져야 배열로 읽는다" );
                registerVectorTextHandler<float2, float32, 2>( context, hashed_string( PredefinedNameType::NameType_float2 ) );
                registerVectorTextHandler<float3, float32, 3>( context, hashed_string( PredefinedNameType::NameType_float3 ) );
                registerVectorTextHandler<float4, float32, 4>( context, hashed_string( PredefinedNameType::NameType_float4 ) );
                registerVectorTextHandler<quaternion, float32, 4>( context, hashed_string( PredefinedNameType::NameType_quaternion ) );
                registerVectorTextHandler<float4x4, float32, 16>( context, hashed_string( PredefinedNameType::NameType_float4x4 ) );
            }

            /** @brief 핸들(컴포넌트 · 게임 오브젝트)과 비트 목록의 텍스트 핸들러를 등록합니다. */
            static void registerHandleTextHandlers( SerializeContext& context )
            {
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

                // 비트 목록은 '0' · '1' 글 하나로 값째 쓰고 읽는다(맨 왼쪽이 가장 높은 비트, `DynamicBitset::to_string`). 원소 주소가 없으므로 컨테이너 래퍼를 쓰지 않는다.
                // 다른 글자가 섞이면 실패다(값은 그대로, `DynamicBitset::parse`).
                context.registerTextHandler(
                    hashed_string( "DynamicBitset" ),
                    []( const void* pPtr )
                { return static_cast<const DynamicBitset*>( pPtr )->to_string(); },
                    []( void* pPtr, string_view strView )
                { return DynamicBitset::parse( SerializeContextInternal::stripQuotes( StringUtil::trim( strView ) ), *static_cast<DynamicBitset*>( pPtr ) ); } );
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

    const TypeRegistry& SerializeContext::getTypeRegistry() const
    {
        return ( _pTypeRegistry != nullptr ) ? *_pTypeRegistry : engine::getSubsystem<TypeRegistry>();
    }

    SerializeContext SerializeContext::deriveFromDefault( SerializePurpose purpose )
    {
        const SerializeContext& defaultContext = getDefault( purpose );

        SerializeContext derived{ purpose };
        derived._pHandlerFallback = &defaultContext;
        // 표만 빌리고 **설정은 물려받는다**(`= getDefault()` 와 같은 동작).
        derived.setSkipUnknownProperties( defaultContext.skipsUnknownProperties() );
        derived.setSkipUnreadableProperties( defaultContext.skipsUnreadableProperties() );
        return derived;
    }

    SerializeContext SerializeContext::makeDefault( SerializePurpose purpose )
    {
        SerializeContext context{ purpose };

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, TextConv, Ns, ... ) SerializeContextInternal::regBuiltinBin<CppType>( context, #Canon );
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"

#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER

        using Internal = SerializeContextInternal;
        context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_string ), SW_DELEGATE_FUNCTION( BinaryWriteFn, &Internal::writeStringBinary ),
                                       SW_DELEGATE_FUNCTION( BinaryReadFn, &Internal::readStringBinary ) );
        context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), SW_DELEGATE_FUNCTION( BinaryWriteFn, &Internal::writeHashedStringBinary ),
                                       SW_DELEGATE_FUNCTION( BinaryReadFn, &Internal::readHashedStringBinary ) );
        context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), SW_DELEGATE_FUNCTION( BinaryWriteFn, &Internal::writeAtomicBoolBinary ),
                                       SW_DELEGATE_FUNCTION( BinaryReadFn, &Internal::readAtomicBoolBinary ) );
        context.registerBinaryHandler( hashed_string( PredefinedNameType::NameType_TagID ), SW_DELEGATE_FUNCTION( BinaryWriteFn, &Internal::writeTagIDBinary ),
                                       SW_DELEGATE_FUNCTION( BinaryReadFn, &Internal::readTagIDBinary ) );

#define SW_BUILTIN_TEXT_none( Canon, CppType )
#define SW_BUILTIN_TEXT_stoi( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoll( Canon, CppType )  SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoul( Canon, CppType )  SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stoull( Canon, CppType ) SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stof( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_stod( Canon, CppType )   SerializeContextInternal::registerNumericTextHandler<CppType>( context, hashed_string( PredefinedNameType::NameType_##Canon ) );
#define SW_BUILTIN_TEXT_packed( Canon, CppType ) SerializeContextInternal::registerPackedTextHandler<CppType>( context, #Canon );

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
#undef SW_BUILTIN_TEXT_packed

        context.registerTextHandler( hashed_string( PredefinedNameType::NameType_bool ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeBoolText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readBoolText ) );
        context.registerTextHandler( hashed_string( PredefinedNameType::NameType_atomic_bool ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeAtomicBoolText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readAtomicBoolText ) );
        context.registerTextHandler( hashed_string( PredefinedNameType::NameType_TagID ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeTagIDText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readTagIDText ) );
        context.registerTextHandler( hashed_string( PredefinedNameType::NameType_string ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeStringText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readStringText ) );
        context.registerTextHandler( hashed_string( PredefinedNameType::NameType_hashed_string ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeHashedStringText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readHashedStringText ) );
        context.registerTextHandler( hashed_string( "SlotHandle" ), SW_DELEGATE_FUNCTION( TextWriteFn, &Internal::writeSlotHandleText ),
                                     SW_DELEGATE_FUNCTION( TextReadFn, &Internal::readSlotHandleText ) );

        DataPath::registerSerializeHandler( context );
        SerializeContextInternal::registerHandleTextHandlers( context );

        SerializeContextInternal::registerFloatVectorTextHandlers( context );
        registerReflectAnyHandlers( context );
        return context;
    }

    const SerializeContext& SerializeContext::getDefault( SerializePurpose purpose )
    {
        // 목적마다 표 한 벌이다. 빌려 쓰는 문맥(`deriveFromDefault`)이 목적만 바꿔 다른 표를 가리키지 않게, 표와 목적을 함께 둔다.
        // 칸 순서가 열거 값 순서다 — 값을 더하면 아래 단언이 멈춘다.
        static_assert( static_cast<uint8>( SerializePurpose::SaveGame ) == 2, "목적마다 칸 하나" );
        static const SerializeContext s_arrContext[] = { makeDefault( SerializePurpose::Authoring ), makeDefault( SerializePurpose::Snapshot ),
                                                         makeDefault( SerializePurpose::SaveGame ) };
        return s_arrContext[static_cast<uint8>( purpose )];
    }

} // namespace sw

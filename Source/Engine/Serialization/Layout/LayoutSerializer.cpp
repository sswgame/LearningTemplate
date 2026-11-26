#include "pch.h"

#include "Engine/Serialization/Layout/LayoutSerializer.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Layout/LayoutFieldDesc.h"

namespace sw
{
    namespace
    {
        struct LayoutSerializerInternal
        {
            /** @brief 의존 확인이 따라 내려가는 깊이 상한입니다. 서로를 담는 타입이 끝없이 돌지 않게 합니다. */
            static constexpr int32 kMaxDependencyDepth = 32;

            /** @brief 정수를 낮은 바이트부터 섞습니다. 호스트 바이트 순서와 무관합니다. */
            static uint64 appendHashInteger( uint64 hash, uint64 value ) noexcept
            {
                for ( int32 index = 0; index < 8; ++index )
                {
                    const uint8 byte = static_cast<uint8>( value & 0xFFu );
                    hash             = HashUtil::appendBytes64( hash, &byte, 1 );
                    value >>= 8;
                }
                return hash;
            }

            /**
             * @brief 의존 스키마가 모두 적격인지 따라 내려가 봅니다. 아니면 그 이유(대상은 `필드 (타입): 깊은 쪽 까닭` 경로)입니다.
             * @param listVisited 지금 확인 중인 스키마들입니다. 서로를 담는 타입이 다시 나오면 이미 확인 중이므로 통과합니다.
             */
            static expected<void> areDependenciesValid( const LayoutSchema& schema, vector<const LayoutSchema*>& listVisited )
            {
                if ( schema._dependencyState.load( std::memory_order_acquire ) == LayoutSchemaDependencyState::Valid )
                    return {};
                if ( VectorUtil::contains( listVisited, &schema ) )
                    return {};
                if ( static_cast<int32>( listVisited.size() ) >= kMaxDependencyDepth )
                    return unexpected( Error{ ErrorKind::Unsupported, "layout schema dependencies nest too deeply" } );
                listVisited.push_back( &schema );

                TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
                for ( uint32 index = 0; index < schema._dependencyCount; ++index )
                {
                    const LayoutSchema::Dependency& dependency = schema._pDependency[index];
                    const TypeInfo*                 pType      = registry.findType( dependency._fqn );
                    const LayoutSchema*             pNested    = pType != nullptr ? pType->_pLayoutSchema : nullptr;
                    string_view                     nestedReason;
                    expected<void>                  nested{};
                    if ( pType == nullptr )
                        nestedReason = "type is not registered";
                    else if ( pNested == nullptr )
                        nestedReason = "type has no generated layout schema";
                    else if ( pNested->isEligible() == false )
                        nestedReason = pNested->_pIneligibleReason;
                    else if ( ( nested = areDependenciesValid( *pNested, listVisited ) ).has_value() )
                        continue;

                    listVisited.pop_back();
                    Error error{ ErrorKind::Unsupported, "a field's type is not layout schema eligible", dependency._pFieldName };
                    error.appendDetail( " (" ).appendDetail( dependency._fqn.c_str() ).appendDetail( "): " );
                    if ( nested.has_value() )
                        error.appendDetail( nestedReason );
                    else
                        error.appendDetail( nested.error().detail() );
                    return unexpected( error );
                }
                listVisited.pop_back();
                return {};
            }

            static bool isQuantizedRangeValid( const float32 minValue, const float32 maxValue, const float32 step ) noexcept
            {
                const bool bFiniteRange = std::isfinite( minValue ) && std::isfinite( maxValue );
                return bFiniteRange && minValue < maxValue && step > 0.0f;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    LayoutSchema::Dependency::Dependency( const utf8* pFieldName, const utf8* pTypeFqn )
        : _pFieldName{ pFieldName }
        , _fqn{ pTypeFqn }
        , _cache{}
    {
    }

} // namespace sw

namespace sw
{
    bool LayoutFieldCodec::isRangeEncodable( const int64 minValue, const int64 maxValue ) noexcept
    {
        if ( minValue > maxValue )
            return false;
        // 차이는 부호 없는 수로 셉니다. 양 끝이 int64 끝에 가까우면 부호 있는 뺄셈이 넘칩니다.
        const uint64 width = static_cast<uint64>( maxValue ) - static_cast<uint64>( minValue );
        return width <= static_cast<uint64>( std::numeric_limits<uint32>::max() );
    }

    void LayoutFieldCodec::writeRangedOffset( BitWriter& writer, const uint64 offset, const int64 minValue, const int64 maxValue )
    {
        writer.writeBits( static_cast<uint32>( offset ), BitMath::computeBitsRequired( minValue, maxValue ) );
    }

    uint64 LayoutFieldCodec::readRangedOffset( BitReader& reader, const int64 minValue, const int64 maxValue )
    {
        return reader.readBits( BitMath::computeBitsRequired( minValue, maxValue ) );
    }

    bool LayoutFieldCodec::writeBoundedFloat32( BitWriter& writer, const float32 value, const float64 minValue, const float64 maxValue )
    {
        const float64 widened = static_cast<float64>( value );
        if ( ( minValue <= widened && widened <= maxValue ) == false )
            return false;
        writer.writeFloat( value );
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readBoundedFloat32( BitReader& reader, const float64 minValue, const float64 maxValue, float32& outValue )
    {
        const float32 value   = reader.readFloatBits(); // 범위 비교가 NaN 을 거짓으로 걸러 낸다
        const float64 widened = static_cast<float64>( value );
        if ( ( minValue <= widened && widened <= maxValue ) == false )
            return LayoutReadResult::OutOfRange;
        outValue = value;
        return LayoutReadResult::Ok;
    }

    void LayoutFieldCodec::writeFloat64( BitWriter& writer, const float64 value )
    {
        writeRawBits( writer, BitUtil::doubleToBits( value ), 64 );
    }

    float64 LayoutFieldCodec::readFloat64( BitReader& reader )
    {
        return BitUtil::bitsToDouble( readRawBits( reader, 64 ) );
    }

    bool LayoutFieldCodec::writeBoundedFloat64( BitWriter& writer, const float64 value, const float64 minValue, const float64 maxValue )
    {
        if ( ( minValue <= value && value <= maxValue ) == false )
            return false;
        writeFloat64( writer, value );
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readBoundedFloat64( BitReader& reader, const float64 minValue, const float64 maxValue, float64& outValue )
    {
        const float64 value = readFloat64( reader );
        if ( ( minValue <= value && value <= maxValue ) == false )
            return LayoutReadResult::OutOfRange;
        outValue = value;
        return LayoutReadResult::Ok;
    }

    bool LayoutFieldCodec::computeQuantizedIndexCount( const float32 minValue, const float32 maxValue, const float32 step, uint32& outIndexCount )
    {
        if ( LayoutSerializerInternal::isQuantizedRangeValid( minValue, maxValue, step ) == false )
            return false;
        // 칸 수가 32 비트를 넘으면 정수 변환이 넘칩니다. 칸 수 계산(반올림)은 `BitWriter::writeQuantizedFloat` 와 같지만 범위 밖 값의 규칙은 다릅니다.
        // 여기서는 유한한 범위 밖 값을 경계로 자릅니다(`Step` 실수는 경계로 자른다). 범위는 선언의 `Min` · `Max` 라
        // 스키마 해시에 들어가고, 자른 값도 같은 해시의 받는 쪽이 같은 뜻으로 읽습니다. 손 코덱의 `BitWriter` 는 범위를 호출부가 손으로 정하므로 범위 밖을 거절합니다.
        const float64 indexCount = ( static_cast<float64>( maxValue ) - static_cast<float64>( minValue ) ) / static_cast<float64>( step );
        if ( indexCount >= static_cast<float64>( std::numeric_limits<uint32>::max() ) )
            return false;
        outIndexCount = static_cast<uint32>( ( maxValue - minValue ) / step + 0.5f );
        return true;
    }

    bool LayoutFieldCodec::writeQuantizedFloat32( BitWriter& writer, const float32 value, const float32 minValue, const float32 maxValue, const float32 step )
    {
        uint32 indexCount = 0;
        if ( std::isfinite( value ) == false || computeQuantizedIndexCount( minValue, maxValue, step, indexCount ) == false )
            return false;
        const float32 clamped = MathUtil::clamp( value, minValue, maxValue );
        const uint32  index   = MathUtil::min( indexCount, static_cast<uint32>( ( clamped - minValue ) / step + 0.5f ) );
        writer.writeBits( index, BitMath::computeBitsRequired( 0, static_cast<int64>( indexCount ) ) );
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readQuantizedFloat32( BitReader& reader, const float32 minValue, const float32 maxValue, const float32 step, float32& outValue )
    {
        uint32 indexCount = 0;
        if ( computeQuantizedIndexCount( minValue, maxValue, step, indexCount ) == false )
            return LayoutReadResult::OutOfRange;
        const uint32 index = reader.readBits( BitMath::computeBitsRequired( 0, static_cast<int64>( indexCount ) ) );
        if ( index > indexCount )
            return LayoutReadResult::OutOfRange;
        // 마지막 칸은 간격의 반올림 오차로 최댓값을 넘을 수 있습니다. 그 칸의 값은 최댓값입니다(쓰기가 경계로 자른 값과 같은 칸입니다).
        outValue = MathUtil::min( maxValue, minValue + static_cast<float32>( index ) * step );
        return LayoutReadResult::Ok;
    }

    bool LayoutFieldCodec::isBitEqual( const float32 a, const float32 b ) noexcept
    {
        return BitUtil::floatToBits( a ) == BitUtil::floatToBits( b );
    }

    bool LayoutFieldCodec::isBitEqual( const float64 a, const float64 b ) noexcept
    {
        return BitUtil::doubleToBits( a ) == BitUtil::doubleToBits( b );
    }

    bool LayoutFieldCodec::writeCount( BitWriter& writer, const size_t count, const uint32 maxCount )
    {
        if ( count > static_cast<size_t>( maxCount ) )
            return false;
        writer.writeBits( static_cast<uint32>( count ), BitMath::computeBitsRequired( 0, static_cast<int64>( maxCount ) ) );
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readCount( BitReader& reader, const uint32 maxCount, uint32& outCount )
    {
        const uint32 count = reader.readBits( BitMath::computeBitsRequired( 0, static_cast<int64>( maxCount ) ) );
        if ( count > maxCount )
            return LayoutReadResult::OutOfRange;
        outCount = count;
        return LayoutReadResult::Ok;
    }

    bool LayoutFieldCodec::writeString( BitWriter& writer, const string& value, const uint32 maxCount )
    {
        if ( writeCount( writer, value.size(), maxCount ) == false )
            return false;
        for ( const utf8 character : value )
        {
            writer.writeBits( static_cast<uint8>( character ), 8 );
        }
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readString( BitReader& reader, const uint32 maxCount, string& outValue )
    {
        uint32                 count  = 0;
        const LayoutReadResult result = readCount( reader, maxCount, count );
        if ( result != LayoutReadResult::Ok )
            return result;
        outValue.resize( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            outValue[index] = static_cast<utf8>( reader.readBits( 8 ) );
        }
        // 글자 필드의 입구이다. 잘린 읽기는 뒤의 넘침 검사가 Truncated 로 돌려주므로 여기서 내용을 판정하지 않는다.
        // 받은 글은 저장되어 다른 사람의 화면 · 로그로 나간다. 깨진 UTF-8 · NUL 을 키트마다 막지 않고 여기서 한 번 막는다(바이너리는 `vector<uint8>` 로 나른다).
        const bool bTextOk = reader.hasOverflowed() || ( StringUtil::isValidUTF8( string_view( outValue ) ) && outValue.find( '\0' ) == string::npos );
        return bTextOk ? LayoutReadResult::Ok : LayoutReadResult::InvalidText;
    }

    bool LayoutFieldCodec::writeBitset( BitWriter& writer, const DynamicBitset& value, const uint32 maxCount )
    {
        if ( writeCount( writer, value.size(), maxCount ) == false )
            return false;
        for ( uint32 bitIndex = 0; bitIndex < value.size(); ++bitIndex )
        {
            writer.writeBool( value.test( bitIndex ) );
        }
        return true;
    }

    LayoutReadResult LayoutFieldCodec::readBitset( BitReader& reader, const uint32 maxCount, DynamicBitset& outValue )
    {
        uint32                 count  = 0;
        const LayoutReadResult result = readCount( reader, maxCount, count );
        if ( result != LayoutReadResult::Ok )
            return result;
        DynamicBitset value( count );
        for ( uint32 bitIndex = 0; bitIndex < count; ++bitIndex )
        {
            value.set( bitIndex, reader.readBool() );
        }
        outValue = std::move( value );
        return LayoutReadResult::Ok;
    }

    void LayoutFieldCodec::writeRawBits( BitWriter& writer, const uint64 value, const int32 bitCount )
    {
        const int32 lowBits = MathUtil::min( bitCount, BitMath::kMaxBitsPerCall );
        writer.writeBits( static_cast<uint32>( value ), lowBits );
        if ( bitCount > lowBits )
            writer.writeBits( static_cast<uint32>( value >> BitMath::kMaxBitsPerCall ), bitCount - lowBits );
    }

    uint64 LayoutFieldCodec::readRawBits( BitReader& reader, const int32 bitCount )
    {
        const int32 lowBits = MathUtil::min( bitCount, BitMath::kMaxBitsPerCall );
        uint64      value   = reader.readBits( lowBits );
        if ( bitCount > lowBits )
            value |= static_cast<uint64>( reader.readBits( bitCount - lowBits ) ) << BitMath::kMaxBitsPerCall;
        return value;
    }
} // namespace sw

namespace sw
{
    namespace
    {
        /** @brief 범위 이름이 볼 수 있는 객체 하나입니다. 최상위가 깊이 0 이고, 펼친 중첩 타입(`Inline`)마다 하나씩 쌓입니다. 호출 스택에만 삽니다. */
        struct LayoutScope
        {
            const void*        _pObject;
            const LayoutScope* _pParent;
            uint32             _depth;
        };

        template <typename T>
        struct LayoutScalarTag
        {
            using Type = T;
        };

        /**
         * @brief 서술자 표(`LayoutFieldDesc`)를 해석해 쓰고 · 읽고 · 비교합니다. 예전에 생성기가 타입마다 낸 직선 코드와 같은 차례 · 같은 `LayoutFieldCodec` 호출이다 —
         *        같은 객체는 같은 바이트, 같은 바이트는 같은 판정(경계 · 개수 · 열거자 · 글자)이다(`LayoutSerializerTest` 의 손 인코딩 대조).
         */
        struct LayoutInterpreter
        {
            /** @brief 정수 꼴마다 @p visitor 를 그 타입 꼬리표로 부릅니다. 실수 꼴은 생성기가 정수 자리에 내지 않으므로 멈춥니다. */
            template <typename Visitor>
            static auto dispatchInteger( const LayoutScalarType type, Visitor&& visitor )
            {
                switch ( type )
                {
                    case LayoutScalarType::Int8:
                        return visitor( LayoutScalarTag<int8>{} );
                    case LayoutScalarType::Int16:
                        return visitor( LayoutScalarTag<int16>{} );
                    case LayoutScalarType::Int32:
                        return visitor( LayoutScalarTag<int32>{} );
                    case LayoutScalarType::Int64:
                        return visitor( LayoutScalarTag<int64>{} );
                    case LayoutScalarType::UInt8:
                        return visitor( LayoutScalarTag<uint8>{} );
                    case LayoutScalarType::UInt16:
                        return visitor( LayoutScalarTag<uint16>{} );
                    case LayoutScalarType::UInt32:
                        return visitor( LayoutScalarTag<uint32>{} );
                    case LayoutScalarType::UInt64:
                        return visitor( LayoutScalarTag<uint64>{} );
                    case LayoutScalarType::Float32:
                    case LayoutScalarType::Float64:
                        break;
                }
                reportFatalError( "layout field descriptor has a floating-point type in an integer slot (%#)", static_cast<int32>( type ) );
            }

            template <typename Target, typename T>
            static Target loadSigned( const void* const pField, const bool bNegate )
            {
                const T value = *static_cast<const T*>( pField );
                if ( bNegate )
                    return static_cast<Target>( -value );
                return static_cast<Target>( value );
            }

            /**
             * @brief 범위 한쪽 끝을 @p Target 으로 읽습니다. 필드 이름이면 그 필드 타입의 값에 부호를 바꾸고(C++ 단항 `-` 승격 그대로) @p Target 으로 바로 바꿉니다 —
             *        예전 생성 식 `static_cast<Target>( ( -object._x ) )` 와 같은 값이다(float32 자리를 float64 를 거쳐 바꾸면 두 번 반올림된다).
             */
            template <typename Target>
            static Target loadBound( const LayoutBound& bound, const LayoutScope& scope, const Target missing )
            {
                if ( bound._kind == LayoutBound::Kind::None )
                    return missing;
                if ( bound._kind == LayoutBound::Kind::Literal )
                {
                    if constexpr ( std::is_same_v<Target, int64> )
                        return bound._integer;
                    else
                        return static_cast<Target>( bound._float );
                }
                const LayoutScope* pScope = &scope;
                while ( pScope->_depth != bound._scopeDepth )
                {
                    pScope = pScope->_pParent;
                }
                const void* const pField = static_cast<const uint8*>( pScope->_pObject ) + bound._fieldOffset;
                if ( bound._fieldType == LayoutScalarType::Float32 )
                    return loadSigned<Target, float32>( pField, bound._bNegate );
                if ( bound._fieldType == LayoutScalarType::Float64 )
                    return loadSigned<Target, float64>( pField, bound._bNegate );
                return dispatchInteger( bound._fieldType, [&]( auto tag ) -> Target
                { return loadSigned<Target, typename decltype( tag )::Type>( pField, bound._bNegate ); } );
            }

            static bool isEnumValue( const LayoutFieldDesc& desc, const int64 value ) noexcept
            {
                for ( uint32 index = 0; index < desc._enumValueCount; ++index )
                {
                    if ( desc._pEnumValue[index] == value )
                        return true;
                }
                return false;
            }

            static bool isFloatBounded( const LayoutFieldDesc& desc ) noexcept { return desc._min.isPresent() || desc._max.isPresent(); }

            /** @brief 의존 스키마입니다. 진입점이 `findSchema` 로 확인했으므로 늘 있다 — 없으면(확인한 뒤 모듈이 내려감) 대신 쓸 스키마 없이 멈춘다. */
            static const LayoutSchema& requireDependency( const LayoutSchema& schema, const uint32 dependencyIndex )
            {
                if ( dependencyIndex == LayoutFieldDesc::kSelfDependency )
                    return schema;
                const LayoutSchema::Dependency& dependency = schema._pDependency[dependencyIndex];
                const TypeInfo*                 pType      = dependency._cache.find( dependency._fqn );
                const LayoutSchema*             pNested    = pType != nullptr ? pType->_pLayoutSchema : nullptr;
                if ( pNested == nullptr || pNested->isEligible() == false )
                    reportFatalError( "'%#' has no layout schema: nested schema is missing although its owner was checked (module unloaded?)", dependency._fqn.c_str() );
                return *pNested;
            }

            // ------------------------------------------------------------------------------
            // 1) 쓰기
            // ------------------------------------------------------------------------------
            static bool writeObject( const LayoutSchema& schema, BitWriter& writer, const void* pObject )
            {
                if ( schema._pBaseCast != nullptr && writeObject( requireDependency( schema, 0 ), writer, schema._pBaseCast( pObject ) ) == false )
                    return false;
                const LayoutScope scope{ pObject, nullptr, 0 };
                return writeChildren( schema, 0, schema._fieldCount, writer, pObject, scope );
            }

            static bool writeChildren( const LayoutSchema& schema, const uint32 first, const uint32 count, BitWriter& writer, const void* pBase, const LayoutScope& scope )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    if ( writeField( schema, schema._pField[index], writer, pBase, scope ) == false )
                        return false;
                }
                return true;
            }

            static bool writeElements( const LayoutSchema& schema, const LayoutFieldDesc& desc, BitWriter& writer, const void* pSequence, const LayoutScope& scope )
            {
                const LayoutSequenceOps& ops     = *desc._pSequenceOps;
                const size_t             count   = ops._pSize( pSequence );
                const uint8* const       pData   = static_cast<const uint8*>( ops._pData( pSequence ) );
                const LayoutFieldDesc&   element = schema._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( writeField( schema, element, writer, pData + index * ops._elementSize, scope ) == false )
                        return false;
                }
                return true;
            }

            static bool writeField( const LayoutSchema& schema, const LayoutFieldDesc& desc, BitWriter& writer, const void* pBase, const LayoutScope& scope )
            {
                const void* const pValue = static_cast<const uint8*>( pBase ) + desc._offset;
                switch ( desc._kind )
                {
                    case LayoutFieldKind::Bool:
                    {
                        writer.writeBool( *static_cast<const bool*>( pValue ) );
                        return true;
                    }
                    case LayoutFieldKind::BitFieldBool:
                    {
                        writer.writeBool( desc._pGetBit( pBase ) );
                        return true;
                    }
                    case LayoutFieldKind::Integer:
                    {
                        const int64 minValue = loadBound<int64>( desc._min, scope, LayoutFieldCodec::kNoLowerBound );
                        const int64 maxValue = loadBound<int64>( desc._max, scope, LayoutFieldCodec::kNoUpperBound );
                        const bool  bRanged  = desc._min.isPresent() && desc._max.isPresent();
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T       = typename decltype( tag )::Type;
                            const T value = *static_cast<const T*>( pValue );
                            if ( bRanged )
                                return LayoutFieldCodec::writeRangedInteger( writer, value, minValue, maxValue );
                            return LayoutFieldCodec::writeVarInteger( writer, value, minValue, maxValue );
                        } );
                    }
                    case LayoutFieldKind::Float32:
                    {
                        const float32 value = *static_cast<const float32*>( pValue );
                        if ( desc._step > 0.0f )
                        {
                            const float32 minValue = loadBound<float32>( desc._min, scope, -std::numeric_limits<float32>::infinity() );
                            const float32 maxValue = loadBound<float32>( desc._max, scope, std::numeric_limits<float32>::infinity() );
                            return LayoutFieldCodec::writeQuantizedFloat32( writer, value, minValue, maxValue, desc._step );
                        }
                        if ( isFloatBounded( desc ) )
                        {
                            return LayoutFieldCodec::writeBoundedFloat32( writer, value, loadBound<float64>( desc._min, scope, LayoutFieldCodec::kNoLowerFloatBound ),
                                                                          loadBound<float64>( desc._max, scope, LayoutFieldCodec::kNoUpperFloatBound ) );
                        }
                        writer.writeFloat( value );
                        return true;
                    }
                    case LayoutFieldKind::Float64:
                    {
                        const float64 value = *static_cast<const float64*>( pValue );
                        if ( isFloatBounded( desc ) )
                        {
                            return LayoutFieldCodec::writeBoundedFloat64( writer, value, loadBound<float64>( desc._min, scope, LayoutFieldCodec::kNoLowerFloatBound ),
                                                                          loadBound<float64>( desc._max, scope, LayoutFieldCodec::kNoUpperFloatBound ) );
                        }
                        LayoutFieldCodec::writeFloat64( writer, value );
                        return true;
                    }
                    case LayoutFieldKind::Enum:
                    {
                        const int64 value = dispatchInteger( desc._numberType, [&]( auto tag ) -> int64
                        { return static_cast<int64>( *static_cast<const typename decltype( tag )::Type*>( pValue ) ); } );
                        if ( isEnumValue( desc, value ) == false )
                            return false;
                        return LayoutFieldCodec::writeRangedInteger( writer, value, desc._min._integer, desc._max._integer );
                    }
                    case LayoutFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T        = typename decltype( tag )::Type;
                            using Unsigned = std::make_unsigned_t<T>;
                            const T value  = *static_cast<const T*>( pValue );
                            if ( ( static_cast<uint64>( value ) & ~desc._enumMask ) != 0 )
                                return false;
                            LayoutFieldCodec::writeRawBits( writer, static_cast<uint64>( static_cast<Unsigned>( value ) ), static_cast<int32>( sizeof( Unsigned ) * 8 ) );
                            return true;
                        } );
                    }
                    case LayoutFieldKind::String:
                        return LayoutFieldCodec::writeString( writer, *static_cast<const string*>( pValue ), desc._maxCount );
                    case LayoutFieldKind::Bitset:
                        return LayoutFieldCodec::writeBitset( writer, *static_cast<const DynamicBitset*>( pValue ), desc._maxCount );
                    case LayoutFieldKind::Vector:
                    {
                        if ( LayoutFieldCodec::writeCount( writer, desc._pSequenceOps->_pSize( pValue ), desc._maxCount ) == false )
                            return false;
                        return writeElements( schema, desc, writer, pValue, scope );
                    }
                    case LayoutFieldKind::StaticArray:
                        return writeElements( schema, desc, writer, pValue, scope );
                    case LayoutFieldKind::Math:
                        return writeChildren( schema, desc._firstChild, desc._childCount, writer, pValue, scope );
                    case LayoutFieldKind::Inline:
                    {
                        const LayoutScope inner{ pValue, &scope, scope._depth + 1 };
                        return writeChildren( schema, desc._firstChild, desc._childCount, writer, pValue, inner );
                    }
                    case LayoutFieldKind::Dependency:
                        return writeObject( requireDependency( schema, desc._dependencyIndex ), writer, pValue );
                }
                return false;
            }

            // ------------------------------------------------------------------------------
            // 2) 읽기 — 읽는 바이트는 네트워크 · 세이브에서 온다. 경계 · 개수 · 열거자 · 글자 검사는 `LayoutFieldCodec` 이 하고 여기서 하나도 건너뛰지 않는다.
            // ------------------------------------------------------------------------------
            static LayoutReadResult readObject( const LayoutSchema& schema, BitReader& reader, void* pObject )
            {
                LayoutReadResult result = LayoutReadResult::Ok;
                if ( schema._pBaseCast != nullptr )
                {
                    result = readObject( requireDependency( schema, 0 ), reader, const_cast<void*>( schema._pBaseCast( pObject ) ) );
                    if ( result != LayoutReadResult::Ok )
                        return result;
                }
                const LayoutScope scope{ pObject, nullptr, 0 };
                result = readChildren( schema, 0, schema._fieldCount, reader, pObject, scope );
                if ( result != LayoutReadResult::Ok )
                    return result;
                if ( schema._pReadValidate != nullptr )
                {
                    ValidationContext context;
                    schema._pReadValidate( pObject, context );
                    if ( context.hasError() )
                        return LayoutReadResult::ValidateFailed;
                }
                return LayoutReadResult::Ok;
            }

            static LayoutReadResult readChildren( const LayoutSchema& schema, const uint32 first, const uint32 count, BitReader& reader, void* pBase, const LayoutScope& scope )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    const LayoutReadResult result = readField( schema, schema._pField[index], reader, pBase, scope );
                    if ( result != LayoutReadResult::Ok )
                        return result;
                }
                return LayoutReadResult::Ok;
            }

            static LayoutReadResult readElements( const LayoutSchema& schema, const LayoutFieldDesc& desc, BitReader& reader, void* pSequence, const LayoutScope& scope )
            {
                const LayoutSequenceOps& ops     = *desc._pSequenceOps;
                const size_t             count   = ops._pSize( pSequence );
                uint8* const             pData   = static_cast<uint8*>( ops._pWritableData( pSequence ) );
                const LayoutFieldDesc&   element = schema._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    const LayoutReadResult result = readField( schema, element, reader, pData + index * ops._elementSize, scope );
                    if ( result != LayoutReadResult::Ok )
                        return result;
                }
                return LayoutReadResult::Ok;
            }

            static LayoutReadResult readField( const LayoutSchema& schema, const LayoutFieldDesc& desc, BitReader& reader, void* pBase, const LayoutScope& scope )
            {
                void* const pValue = static_cast<uint8*>( pBase ) + desc._offset;
                switch ( desc._kind )
                {
                    case LayoutFieldKind::Bool:
                    {
                        *static_cast<bool*>( pValue ) = reader.readBool();
                        return LayoutReadResult::Ok;
                    }
                    case LayoutFieldKind::BitFieldBool:
                    {
                        desc._pSetBit( pBase, reader.readBool() );
                        return LayoutReadResult::Ok;
                    }
                    case LayoutFieldKind::Integer:
                    {
                        const int64 minValue = loadBound<int64>( desc._min, scope, LayoutFieldCodec::kNoLowerBound );
                        const int64 maxValue = loadBound<int64>( desc._max, scope, LayoutFieldCodec::kNoUpperBound );
                        const bool  bRanged  = desc._min.isPresent() && desc._max.isPresent();
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> LayoutReadResult
                        {
                            using T  = typename decltype( tag )::Type;
                            T& value = *static_cast<T*>( pValue );
                            if ( bRanged )
                                return LayoutFieldCodec::readRangedInteger( reader, minValue, maxValue, value );
                            return LayoutFieldCodec::readVarInteger( reader, minValue, maxValue, value );
                        } );
                    }
                    case LayoutFieldKind::Float32:
                    {
                        float32& value = *static_cast<float32*>( pValue );
                        if ( desc._step > 0.0f )
                        {
                            const float32 minValue = loadBound<float32>( desc._min, scope, -std::numeric_limits<float32>::infinity() );
                            const float32 maxValue = loadBound<float32>( desc._max, scope, std::numeric_limits<float32>::infinity() );
                            return LayoutFieldCodec::readQuantizedFloat32( reader, minValue, maxValue, desc._step, value );
                        }
                        if ( isFloatBounded( desc ) )
                        {
                            return LayoutFieldCodec::readBoundedFloat32( reader, loadBound<float64>( desc._min, scope, LayoutFieldCodec::kNoLowerFloatBound ),
                                                                         loadBound<float64>( desc._max, scope, LayoutFieldCodec::kNoUpperFloatBound ), value );
                        }
                        // 범위 없는 실수는 비트 그대로 왕복한다(NaN 포함 — `isBitEqual` 과 같은 뜻). 유한 검사는 범위를 적은 필드의 몫이다.
                        value = reader.readFloatBits();
                        return LayoutReadResult::Ok;
                    }
                    case LayoutFieldKind::Float64:
                    {
                        float64& value = *static_cast<float64*>( pValue );
                        if ( isFloatBounded( desc ) )
                        {
                            return LayoutFieldCodec::readBoundedFloat64( reader, loadBound<float64>( desc._min, scope, LayoutFieldCodec::kNoLowerFloatBound ),
                                                                         loadBound<float64>( desc._max, scope, LayoutFieldCodec::kNoUpperFloatBound ), value );
                        }
                        value = LayoutFieldCodec::readFloat64( reader );
                        return LayoutReadResult::Ok;
                    }
                    case LayoutFieldKind::Enum:
                    {
                        int64                  value  = 0;
                        const LayoutReadResult result = LayoutFieldCodec::readRangedInteger( reader, desc._min._integer, desc._max._integer, value );
                        if ( result != LayoutReadResult::Ok )
                            return result;
                        if ( isEnumValue( desc, value ) == false )
                            return LayoutReadResult::OutOfRange;
                        // 읽은 값은 [최소, 최대] 열거자 안이라 기반 타입에 그대로 들어간다.
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> LayoutReadResult
                        {
                            using T                    = typename decltype( tag )::Type;
                            *static_cast<T*>( pValue ) = static_cast<T>( value );
                            return LayoutReadResult::Ok;
                        } );
                    }
                    case LayoutFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> LayoutReadResult
                        {
                            using T            = typename decltype( tag )::Type;
                            using Unsigned     = std::make_unsigned_t<T>;
                            const uint64 raw   = LayoutFieldCodec::readRawBits( reader, static_cast<int32>( sizeof( Unsigned ) * 8 ) );
                            const T      value = static_cast<T>( static_cast<Unsigned>( raw ) );
                            // 비트 플래그는 켜진 비트의 조합이라 범위가 없다. 등록된 비트 밖은 거절한다.
                            if ( ( static_cast<uint64>( value ) & ~desc._enumMask ) != 0 )
                                return LayoutReadResult::OutOfRange;
                            *static_cast<T*>( pValue ) = value;
                            return LayoutReadResult::Ok;
                        } );
                    }
                    case LayoutFieldKind::String:
                        return LayoutFieldCodec::readString( reader, desc._maxCount, *static_cast<string*>( pValue ) );
                    case LayoutFieldKind::Bitset:
                        return LayoutFieldCodec::readBitset( reader, desc._maxCount, *static_cast<DynamicBitset*>( pValue ) );
                    case LayoutFieldKind::Vector:
                    {
                        uint32                 count  = 0;
                        const LayoutReadResult result = LayoutFieldCodec::readCount( reader, desc._maxCount, count );
                        if ( result != LayoutReadResult::Ok )
                            return result;
                        desc._pSequenceOps->_pResize( pValue, count );
                        return readElements( schema, desc, reader, pValue, scope );
                    }
                    case LayoutFieldKind::StaticArray:
                        return readElements( schema, desc, reader, pValue, scope );
                    case LayoutFieldKind::Math:
                        return readChildren( schema, desc._firstChild, desc._childCount, reader, pValue, scope );
                    case LayoutFieldKind::Inline:
                    {
                        const LayoutScope inner{ pValue, &scope, scope._depth + 1 };
                        return readChildren( schema, desc._firstChild, desc._childCount, reader, pValue, inner );
                    }
                    case LayoutFieldKind::Dependency:
                        return readObject( requireDependency( schema, desc._dependencyIndex ), reader, pValue );
                }
                return LayoutReadResult::OutOfRange;
            }

            // ------------------------------------------------------------------------------
            // 3) 비교 — 스키마와 같은 필드 집합을 선언 순서로. 실수는 비트 패턴이다.
            // ------------------------------------------------------------------------------
            static bool equalsObject( const LayoutSchema& schema, const void* pA, const void* pB )
            {
                if ( schema._pBaseCast != nullptr && equalsObject( requireDependency( schema, 0 ), schema._pBaseCast( pA ), schema._pBaseCast( pB ) ) == false )
                    return false;
                return equalsChildren( schema, 0, schema._fieldCount, pA, pB );
            }

            static bool equalsChildren( const LayoutSchema& schema, const uint32 first, const uint32 count, const void* pBaseA, const void* pBaseB )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    if ( equalsField( schema, schema._pField[index], pBaseA, pBaseB ) == false )
                        return false;
                }
                return true;
            }

            static bool equalsElements( const LayoutSchema& schema, const LayoutFieldDesc& desc, const void* pSequenceA, const void* pSequenceB )
            {
                const LayoutSequenceOps& ops     = *desc._pSequenceOps;
                const size_t             count   = ops._pSize( pSequenceA );
                const uint8* const       pDataA  = static_cast<const uint8*>( ops._pData( pSequenceA ) );
                const uint8* const       pDataB  = static_cast<const uint8*>( ops._pData( pSequenceB ) );
                const LayoutFieldDesc&   element = schema._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( equalsField( schema, element, pDataA + index * ops._elementSize, pDataB + index * ops._elementSize ) == false )
                        return false;
                }
                return true;
            }

            static bool equalsField( const LayoutSchema& schema, const LayoutFieldDesc& desc, const void* pBaseA, const void* pBaseB )
            {
                const void* const pA = static_cast<const uint8*>( pBaseA ) + desc._offset;
                const void* const pB = static_cast<const uint8*>( pBaseB ) + desc._offset;
                switch ( desc._kind )
                {
                    case LayoutFieldKind::Bool:
                        return *static_cast<const bool*>( pA ) == *static_cast<const bool*>( pB );
                    case LayoutFieldKind::BitFieldBool:
                        return desc._pGetBit( pBaseA ) == desc._pGetBit( pBaseB );
                    case LayoutFieldKind::Integer:
                    case LayoutFieldKind::Enum:
                    case LayoutFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T = typename decltype( tag )::Type;
                            return *static_cast<const T*>( pA ) == *static_cast<const T*>( pB );
                        } );
                    }
                    case LayoutFieldKind::Float32:
                        return LayoutFieldCodec::isBitEqual( *static_cast<const float32*>( pA ), *static_cast<const float32*>( pB ) );
                    case LayoutFieldKind::Float64:
                        return LayoutFieldCodec::isBitEqual( *static_cast<const float64*>( pA ), *static_cast<const float64*>( pB ) );
                    case LayoutFieldKind::String:
                        return *static_cast<const string*>( pA ) == *static_cast<const string*>( pB );
                    case LayoutFieldKind::Bitset:
                        return *static_cast<const DynamicBitset*>( pA ) == *static_cast<const DynamicBitset*>( pB );
                    case LayoutFieldKind::Vector:
                    {
                        if ( desc._pSequenceOps->_pSize( pA ) != desc._pSequenceOps->_pSize( pB ) )
                            return false;
                        return equalsElements( schema, desc, pA, pB );
                    }
                    case LayoutFieldKind::StaticArray:
                        return equalsElements( schema, desc, pA, pB );
                    case LayoutFieldKind::Math:
                    case LayoutFieldKind::Inline:
                        return equalsChildren( schema, desc._firstChild, desc._childCount, pA, pB );
                    case LayoutFieldKind::Dependency:
                        return equalsObject( requireDependency( schema, desc._dependencyIndex ), pA, pB );
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<const LayoutSchema*> LayoutSerializer::findSchema( const TypeInfo& type )
    {
        const LayoutSchema* pCodec = type._pLayoutSchema;
        if ( pCodec == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "type has no generated layout schema", type._name.c_str() } );
        if ( expected<void> valid = validateSchema( *pCodec ); valid.has_value() == false )
            return unexpected( valid.error() );
        return pCodec;
    }

    expected<void> LayoutSerializer::validateSchema( const LayoutSchema& schema )
    {
        if ( schema.isEligible() == false )
            return unexpected( Error{ ErrorKind::Unsupported, "type is not layout schema eligible", schema._pIneligibleReason } );
        if ( schema._dependencyState.load( std::memory_order_acquire ) == LayoutSchemaDependencyState::Valid )
            return {};

        vector<const LayoutSchema*> listVisited;
        if ( expected<void> dependencies = LayoutSerializerInternal::areDependenciesValid( schema, listVisited ); dependencies.has_value() == false )
            return unexpected( dependencies.error() );
        schema._dependencyState.store( LayoutSchemaDependencyState::Valid, std::memory_order_release );
        return {};
    }

    uint64 LayoutSerializer::computeSchemaHash( const uint32 version, const string_view canonical, const std::initializer_list<uint64> listNestedHash )
    {
        uint64 hash = HashUtil::kFNVOffset64;
        hash        = LayoutSerializerInternal::appendHashInteger( hash, version );
        hash        = HashUtil::appendBytes64( hash, canonical.data(), canonical.size() );
        for ( const uint64 nestedHash : listNestedHash )
        {
            hash = LayoutSerializerInternal::appendHashInteger( hash, nestedHash );
        }
        return hash;
    }

    uint64 LayoutSerializer::getSchemaHash( const LayoutSchema& schema )
    {
        const uint64 cached = schema._schemaHash.load( std::memory_order_acquire );
        if ( cached != 0 )
            return cached;
        // 다른 타입의 해시는 다른 헤더 · 모듈일 수 있으므로 실행 시간에 그 스키마에서 얻어 의존 차례로 섞는다(`computeSchemaHash` 와 같은 식).
        uint64 hash = computeSchemaHash( kVersion, schema._pCanonical, {} );
        for ( uint32 index = 0; index < schema._dependencyCount; ++index )
        {
            hash = LayoutSerializerInternal::appendHashInteger( hash, getSchemaHash( LayoutInterpreter::requireDependency( schema, index ) ) );
        }
        schema._schemaHash.store( hash, std::memory_order_release );
        return hash;
    }

    void LayoutSerializer::stopForMissingSchema( const hashed_string& fqn, string_view reason, string_view detail )
    {
        // 대체 값(0 해시, 빈 결과)을 돌려주면 그 값이 핸드셰이크 해시나 롤백 상태에 섞입니다. 고칠 곳은 선언이므로 멈춥니다.
        reportFatalError( "'%#' has no layout schema: %# (%#)", fqn.c_str(), reason, detail );
    }

    bool LayoutSerializer::writeWithSchema( const LayoutSchema& schema, const void* pObject, BitWriter& writer )
    {
        LayoutFieldCodec::writeRawBits( writer, getSchemaHash( schema ), 64 );
        return LayoutInterpreter::writeObject( schema, writer, pObject );
    }

    LayoutReadResult LayoutSerializer::readWithSchema( const LayoutSchema& schema, const hashed_string& typeFqn, BitReader& reader, void* pObject )
    {
        const uint64 schemaHash = LayoutFieldCodec::readRawBits( reader, 64 );
        if ( reader.hasOverflowed() )
            return LayoutReadResult::Truncated;
        if ( schemaHash != getSchemaHash( schema ) )
        {
            // 마이그레이션 표: 키는 (대상 타입, 기록된 해시), 값은 옛 비트를 지금 객체로 읽는 함수다. 없으면 거절한다 — 옛 비트를 지금 배치로 읽는 폴백은 없다.
            const LayoutSchemaMigration* pMigration = LayoutSchemaMigration::find( typeFqn, schemaHash );
            if ( pMigration == nullptr )
                return LayoutReadResult::SchemaMismatch;
            const LayoutReadResult result = pMigration->_pFunction( reader, pObject );
            if ( result == LayoutReadResult::Ok && reader.hasOverflowed() )
                return LayoutReadResult::Truncated;
            return result;
        }
        return readBodyWithSchema( schema, reader, pObject );
    }

    namespace
    {
        /** @brief 등록된 스키마 결합형 마이그레이션(등록 순서)입니다. Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다(`FormatUpgradeStep` 과 같다). */
        SW_DEFINE_REGISTRATION_LIST( getLayoutSchemaMigrations, const LayoutSchemaMigration, "LayoutSchemaMigrations", RegistrationOrder::Insertion, true )
    } // namespace

    bool LayoutSchemaMigration::registerMigration( const LayoutSchemaMigration* pMigration )
    {
        if ( pMigration == nullptr || StringUtil::isNullOrEmpty( pMigration->_pTypeFqn ) || pMigration->_pFunction == nullptr )
            return false;
        // 같은 (타입, 해시) 두 번 등록은 이름 목록이 이름 중복으로 거절한다 — 여기서 `find` 로 미리 보지 않는다(등록은 정적 초기화라 이름 풀 앞이다, hashed_string 금지)
        StringBuilder<constant::buffer::kMax256> name;
        name.appendFormat( "%#:%#", pMigration->_pTypeFqn, pMigration->_fromSchemaHash );
        return getLayoutSchemaMigrations().addLogged( pMigration, "Layout schema migration", name.view() ) == RegistrationResult::Added;
    }

    void LayoutSchemaMigration::unregisterMigration( const LayoutSchemaMigration* pMigration )
    {
        (void)getLayoutSchemaMigrations().remove( pMigration ); // 등록되어 있지 않은(거절된) 항목을 빼는 것은 할 일이 없다
    }

    const LayoutSchemaMigration* LayoutSchemaMigration::find( const hashed_string& typeFqn, const uint64 fromSchemaHash )
    {
        const RegistrationList<const LayoutSchemaMigration>& list = getLayoutSchemaMigrations();
        for ( uint32 index = 0; index < list.getCount(); ++index )
        {
            const LayoutSchemaMigration* pMigration = list.getAt( index );
            if ( pMigration->_fromSchemaHash == fromSchemaHash && string_view{ pMigration->_pTypeFqn } == string_view{ typeFqn.c_str() } )
                return pMigration;
        }
        return nullptr;
    }

    bool LayoutSerializer::writeBodyWithSchema( const LayoutSchema& schema, const void* pObject, BitWriter& writer )
    {
        return LayoutInterpreter::writeObject( schema, writer, pObject );
    }

    LayoutReadResult LayoutSerializer::readPropertiesWithSchema( const LayoutSchema& schema, BitReader& reader, void* pObject )
    {
        const LayoutReadResult result = LayoutInterpreter::readObject( schema, reader, pObject );
        // 모자란 스트림은 0 을 읽으므로 그 전에 범위 · 열거자 검사가 먼저 걸릴 수 있습니다. 원인은 잘림입니다.
        if ( reader.hasOverflowed() )
            return LayoutReadResult::Truncated;
        return result;
    }

    bool LayoutSerializer::equalsFieldWithSchema( const LayoutSchema& schema, const uint32 fieldIndex, const void* pA, const void* pB )
    {
        return LayoutInterpreter::equalsChildren( schema, fieldIndex, 1, pA, pB );
    }

    LayoutReadResult LayoutSerializer::readBodyWithSchema( const LayoutSchema& schema, BitReader& reader, void* pObject )
    {
        const LayoutReadResult result = readPropertiesWithSchema( schema, reader, pObject );
        if ( result != LayoutReadResult::Ok )
            return result;

        const int32 bitsRemaining = reader.getBitsRemaining();
        if ( bitsRemaining >= 8 )
            return LayoutReadResult::TrailingData;
        if ( bitsRemaining > 0 && reader.readBits( bitsRemaining ) != 0 )
            return LayoutReadResult::TrailingData;
        return LayoutReadResult::Ok;
    }

    bool LayoutSerializer::equalsWithSchema( const LayoutSchema& schema, const void* pA, const void* pB )
    {
        return LayoutInterpreter::equalsObject( schema, pA, pB );
    }
} // namespace sw

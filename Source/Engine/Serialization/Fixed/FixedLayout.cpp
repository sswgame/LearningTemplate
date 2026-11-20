#include "pch.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/Reflection.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Fixed/FixedFieldDesc.h"

namespace sw
{
    namespace
    {
        struct FixedLayoutInternal
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
             * @brief 의존 코덱이 모두 적격인지 따라 내려가 봅니다. 아니면 그 이유(대상은 `필드 (타입): 깊은 쪽 까닭` 경로)입니다.
             * @param listVisited 지금 확인 중인 코덱들입니다. 서로를 담는 타입이 다시 나오면 이미 확인 중이므로 통과합니다.
             */
            static expected<void> areDependenciesValid( const FixedCodec& codec, vector<const FixedCodec*>& listVisited )
            {
                if ( codec._dependencyState.load( std::memory_order_acquire ) == FixedDependencyState::Valid )
                    return {};
                if ( VectorUtil::contains( listVisited, &codec ) )
                    return {};
                if ( static_cast<int32>( listVisited.size() ) >= kMaxDependencyDepth )
                    return unexpected( Error{ ErrorKind::Unsupported, "fixed layout dependencies nest too deeply" } );
                listVisited.push_back( &codec );

                TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
                for ( uint32 index = 0; index < codec._dependencyCount; ++index )
                {
                    const FixedCodec::Dependency& dependency = codec._pDependency[index];
                    const TypeInfo*               pType      = registry.findType( dependency._fqn );
                    const FixedCodec*             pNested    = pType != nullptr ? pType->_pFixedCodec : nullptr;
                    string_view                   nestedReason;
                    expected<void>                nested{};
                    if ( pType == nullptr )
                        nestedReason = "type is not registered";
                    else if ( pNested == nullptr )
                        nestedReason = "type has no generated fixed layout codec";
                    else if ( pNested->isEligible() == false )
                        nestedReason = pNested->_pIneligibleReason;
                    else if ( ( nested = areDependenciesValid( *pNested, listVisited ) ).has_value() )
                        continue;

                    listVisited.pop_back();
                    Error error{ ErrorKind::Unsupported, "a field's type is not fixed layout eligible", dependency._pFieldName };
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
    FixedCodec::Dependency::Dependency( const utf8* pFieldName, const utf8* pTypeFqn )
        : _pFieldName{ pFieldName }
        , _fqn{ pTypeFqn }
        , _cache{}
    {
    }

    FixedCodec::FixedCodec( const FixedFieldDesc* pField, const uint32 fieldCount, const utf8* pCanonical, const Dependency* pDependency, const uint32 dependencyCount,
                            const BaseCastFunction pBaseCast, const ReflectValidateFunction pValidate ) noexcept
        : _pField{ pField }
        , _pCanonical{ pCanonical }
        , _pIneligibleReason{ nullptr }
        , _pDependency{ pDependency }
        , _pBaseCast{ pBaseCast }
        , _pValidate{ pValidate }
        , _fieldCount{ fieldCount }
        , _dependencyCount{ dependencyCount }
        , _dependencyState{ dependencyCount == 0 ? FixedDependencyState::Valid : FixedDependencyState::Unknown }
        , _schemaHash{ 0 }
        , _pReplicated{ nullptr }
    {
    }

    FixedCodec::FixedCodec( const utf8* pIneligibleReason ) noexcept
        : _pField{ nullptr }
        , _pCanonical{ nullptr }
        , _pIneligibleReason{ pIneligibleReason }
        , _pDependency{ nullptr }
        , _pBaseCast{ nullptr }
        , _pValidate{ nullptr }
        , _fieldCount{ 0 }
        , _dependencyCount{ 0 }
        , _dependencyState{ FixedDependencyState::Missing }
        , _schemaHash{ 0 }
        , _pReplicated{ nullptr }
    {
    }
} // namespace sw

namespace sw
{
    bool FixedCodecUtil::isRangeEncodable( const int64 minValue, const int64 maxValue ) noexcept
    {
        if ( minValue > maxValue )
            return false;
        // 차이는 부호 없는 수로 셉니다. 양 끝이 int64 끝에 가까우면 부호 있는 뺄셈이 넘칩니다.
        const uint64 width = static_cast<uint64>( maxValue ) - static_cast<uint64>( minValue );
        return width <= static_cast<uint64>( std::numeric_limits<uint32>::max() );
    }

    void FixedCodecUtil::writeRangedOffset( BitWriter& writer, const uint64 offset, const int64 minValue, const int64 maxValue )
    {
        writer.writeBits( static_cast<uint32>( offset ), BitMath::computeBitsRequired( minValue, maxValue ) );
    }

    uint64 FixedCodecUtil::readRangedOffset( BitReader& reader, const int64 minValue, const int64 maxValue )
    {
        return reader.readBits( BitMath::computeBitsRequired( minValue, maxValue ) );
    }

    bool FixedCodecUtil::writeBoundedFloat32( BitWriter& writer, const float32 value, const float64 minValue, const float64 maxValue )
    {
        const float64 widened = static_cast<float64>( value );
        if ( ( minValue <= widened && widened <= maxValue ) == false )
            return false;
        writer.writeFloat( value );
        return true;
    }

    FixedReadResult FixedCodecUtil::readBoundedFloat32( BitReader& reader, const float64 minValue, const float64 maxValue, float32& outValue )
    {
        const float32 value   = reader.readFloatBits(); // 범위 비교가 NaN 을 거짓으로 걸러 낸다
        const float64 widened = static_cast<float64>( value );
        if ( ( minValue <= widened && widened <= maxValue ) == false )
            return FixedReadResult::OutOfRange;
        outValue = value;
        return FixedReadResult::Ok;
    }

    void FixedCodecUtil::writeFloat64( BitWriter& writer, const float64 value )
    {
        writeRawBits( writer, BitUtil::doubleToBits( value ), 64 );
    }

    float64 FixedCodecUtil::readFloat64( BitReader& reader )
    {
        return BitUtil::bitsToDouble( readRawBits( reader, 64 ) );
    }

    bool FixedCodecUtil::writeBoundedFloat64( BitWriter& writer, const float64 value, const float64 minValue, const float64 maxValue )
    {
        if ( ( minValue <= value && value <= maxValue ) == false )
            return false;
        writeFloat64( writer, value );
        return true;
    }

    FixedReadResult FixedCodecUtil::readBoundedFloat64( BitReader& reader, const float64 minValue, const float64 maxValue, float64& outValue )
    {
        const float64 value = readFloat64( reader );
        if ( ( minValue <= value && value <= maxValue ) == false )
            return FixedReadResult::OutOfRange;
        outValue = value;
        return FixedReadResult::Ok;
    }

    bool FixedCodecUtil::computeQuantizedIndexCount( const float32 minValue, const float32 maxValue, const float32 step, uint32& outIndexCount )
    {
        if ( FixedLayoutInternal::isQuantizedRangeValid( minValue, maxValue, step ) == false )
            return false;
        // 칸 수가 32 비트를 넘으면 정수 변환이 넘칩니다. 칸 수 계산(반올림)은 `BitWriter::writeQuantizedFloat` 와 같지만 범위 밖 값의 규칙은 다릅니다.
        // 여기서는 유한한 범위 밖 값을 경계로 자릅니다(`Step` 실수는 경계로 자른다는 설계, `Source/Engine/Serialization/Fixed/README.md` 3.4). 범위는 선언의 `Min` · `Max` 라
        // 스키마 해시에 들어가고, 자른 값도 같은 해시의 받는 쪽이 같은 뜻으로 읽습니다. 손 코덱의 `BitWriter` 는 범위를 호출부가 손으로 정하므로 범위 밖을 거절합니다.
        const float64 indexCount = ( static_cast<float64>( maxValue ) - static_cast<float64>( minValue ) ) / static_cast<float64>( step );
        if ( indexCount >= static_cast<float64>( std::numeric_limits<uint32>::max() ) )
            return false;
        outIndexCount = static_cast<uint32>( ( maxValue - minValue ) / step + 0.5f );
        return true;
    }

    bool FixedCodecUtil::writeQuantizedFloat32( BitWriter& writer, const float32 value, const float32 minValue, const float32 maxValue, const float32 step )
    {
        uint32 indexCount = 0;
        if ( std::isfinite( value ) == false || computeQuantizedIndexCount( minValue, maxValue, step, indexCount ) == false )
            return false;
        const float32 clamped = MathUtil::clamp( value, minValue, maxValue );
        const uint32  index   = MathUtil::min( indexCount, static_cast<uint32>( ( clamped - minValue ) / step + 0.5f ) );
        writer.writeBits( index, BitMath::computeBitsRequired( 0, static_cast<int64>( indexCount ) ) );
        return true;
    }

    FixedReadResult FixedCodecUtil::readQuantizedFloat32( BitReader& reader, const float32 minValue, const float32 maxValue, const float32 step, float32& outValue )
    {
        uint32 indexCount = 0;
        if ( computeQuantizedIndexCount( minValue, maxValue, step, indexCount ) == false )
            return FixedReadResult::OutOfRange;
        const uint32 index = reader.readBits( BitMath::computeBitsRequired( 0, static_cast<int64>( indexCount ) ) );
        if ( index > indexCount )
            return FixedReadResult::OutOfRange;
        // 마지막 칸은 간격의 반올림 오차로 최댓값을 넘을 수 있습니다. 그 칸의 값은 최댓값입니다(쓰기가 경계로 자른 값과 같은 칸입니다).
        outValue = MathUtil::min( maxValue, minValue + static_cast<float32>( index ) * step );
        return FixedReadResult::Ok;
    }

    bool FixedCodecUtil::isBitEqual( const float32 a, const float32 b ) noexcept
    {
        return BitUtil::floatToBits( a ) == BitUtil::floatToBits( b );
    }

    bool FixedCodecUtil::isBitEqual( const float64 a, const float64 b ) noexcept
    {
        return BitUtil::doubleToBits( a ) == BitUtil::doubleToBits( b );
    }

    bool FixedCodecUtil::writeCount( BitWriter& writer, const size_t count, const uint32 maxCount )
    {
        if ( count > static_cast<size_t>( maxCount ) )
            return false;
        writer.writeBits( static_cast<uint32>( count ), BitMath::computeBitsRequired( 0, static_cast<int64>( maxCount ) ) );
        return true;
    }

    FixedReadResult FixedCodecUtil::readCount( BitReader& reader, const uint32 maxCount, uint32& outCount )
    {
        const uint32 count = reader.readBits( BitMath::computeBitsRequired( 0, static_cast<int64>( maxCount ) ) );
        if ( count > maxCount )
            return FixedReadResult::OutOfRange;
        outCount = count;
        return FixedReadResult::Ok;
    }

    bool FixedCodecUtil::writeString( BitWriter& writer, const string& value, const uint32 maxCount )
    {
        if ( writeCount( writer, value.size(), maxCount ) == false )
            return false;
        for ( const utf8 character : value )
        {
            writer.writeBits( static_cast<uint8>( character ), 8 );
        }
        return true;
    }

    FixedReadResult FixedCodecUtil::readString( BitReader& reader, const uint32 maxCount, string& outValue )
    {
        uint32                count  = 0;
        const FixedReadResult result = readCount( reader, maxCount, count );
        if ( result != FixedReadResult::Ok )
            return result;
        outValue.resize( count );
        for ( uint32 index = 0; index < count; ++index )
        {
            outValue[index] = static_cast<utf8>( reader.readBits( 8 ) );
        }
        // 글자 필드의 입구이다. 잘린 읽기는 뒤의 넘침 검사가 Truncated 로 돌려주므로 여기서 내용을 판정하지 않는다.
        // 받은 글은 저장되어 다른 사람의 화면 · 로그로 나간다. 깨진 UTF-8 · NUL 을 키트마다 막지 않고 여기서 한 번 막는다(바이너리는 `vector<uint8>` 로 나른다).
        const bool bTextOk = reader.hasOverflowed() || ( StringUtil::isValidUTF8( string_view( outValue ) ) && outValue.find( '\0' ) == string::npos );
        return bTextOk ? FixedReadResult::Ok : FixedReadResult::InvalidText;
    }

    bool FixedCodecUtil::writeBitset( BitWriter& writer, const DynamicBitset& value, const uint32 maxCount )
    {
        if ( writeCount( writer, value.size(), maxCount ) == false )
            return false;
        for ( uint32 bitIndex = 0; bitIndex < value.size(); ++bitIndex )
        {
            writer.writeBool( value.test( bitIndex ) );
        }
        return true;
    }

    FixedReadResult FixedCodecUtil::readBitset( BitReader& reader, const uint32 maxCount, DynamicBitset& outValue )
    {
        uint32                count  = 0;
        const FixedReadResult result = readCount( reader, maxCount, count );
        if ( result != FixedReadResult::Ok )
            return result;
        DynamicBitset value( count );
        for ( uint32 bitIndex = 0; bitIndex < count; ++bitIndex )
        {
            value.set( bitIndex, reader.readBool() );
        }
        outValue = std::move( value );
        return FixedReadResult::Ok;
    }

    void FixedCodecUtil::writeRawBits( BitWriter& writer, const uint64 value, const int32 bitCount )
    {
        const int32 lowBits = MathUtil::min( bitCount, BitMath::kMaxBitsPerCall );
        writer.writeBits( static_cast<uint32>( value ), lowBits );
        if ( bitCount > lowBits )
            writer.writeBits( static_cast<uint32>( value >> BitMath::kMaxBitsPerCall ), bitCount - lowBits );
    }

    uint64 FixedCodecUtil::readRawBits( BitReader& reader, const int32 bitCount )
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
        struct FixedScope
        {
            const void*       _pObject;
            const FixedScope* _pParent;
            uint32            _depth;
        };

        template <typename T>
        struct FixedNumberTag
        {
            using Type = T;
        };

        /**
         * @brief 서술자 표(`FixedFieldDesc`)를 해석해 쓰고 · 읽고 · 비교합니다. 예전에 생성기가 타입마다 낸 직선 코드와 같은 차례 · 같은 `FixedCodecUtil` 호출이다 —
         *        같은 객체는 같은 바이트, 같은 바이트는 같은 판정(경계 · 개수 · 열거자 · 글자)이다(`FixedLayoutTest` 의 손 인코딩 대조).
         */
        struct FixedFieldCodec
        {
            /** @brief 정수 꼴마다 @p visitor 를 그 타입 꼬리표로 부릅니다. 실수 꼴은 생성기가 정수 자리에 내지 않으므로 멈춥니다. */
            template <typename Visitor>
            static auto dispatchInteger( const FixedNumberType type, Visitor&& visitor )
            {
                switch ( type )
                {
                    case FixedNumberType::Int8:
                        return visitor( FixedNumberTag<int8>{} );
                    case FixedNumberType::Int16:
                        return visitor( FixedNumberTag<int16>{} );
                    case FixedNumberType::Int32:
                        return visitor( FixedNumberTag<int32>{} );
                    case FixedNumberType::Int64:
                        return visitor( FixedNumberTag<int64>{} );
                    case FixedNumberType::UInt8:
                        return visitor( FixedNumberTag<uint8>{} );
                    case FixedNumberType::UInt16:
                        return visitor( FixedNumberTag<uint16>{} );
                    case FixedNumberType::UInt32:
                        return visitor( FixedNumberTag<uint32>{} );
                    case FixedNumberType::UInt64:
                        return visitor( FixedNumberTag<uint64>{} );
                    case FixedNumberType::Float32:
                    case FixedNumberType::Float64:
                        break;
                }
                SW_FATAL( "fixed layout descriptor has a floating-point type in an integer slot (%#)", static_cast<int32>( type ) );
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
            static Target loadBound( const FixedBound& bound, const FixedScope& scope, const Target missing )
            {
                if ( bound._kind == FixedBound::Kind::None )
                    return missing;
                if ( bound._kind == FixedBound::Kind::Literal )
                {
                    if constexpr ( std::is_same_v<Target, int64> )
                        return bound._integer;
                    else
                        return static_cast<Target>( bound._float );
                }
                const FixedScope* pScope = &scope;
                while ( pScope->_depth != bound._scopeDepth )
                {
                    pScope = pScope->_pParent;
                }
                const void* const pField = static_cast<const uint8*>( pScope->_pObject ) + bound._fieldOffset;
                if ( bound._fieldType == FixedNumberType::Float32 )
                    return loadSigned<Target, float32>( pField, bound._bNegate );
                if ( bound._fieldType == FixedNumberType::Float64 )
                    return loadSigned<Target, float64>( pField, bound._bNegate );
                return dispatchInteger( bound._fieldType, [&]( auto tag ) -> Target
                { return loadSigned<Target, typename decltype( tag )::Type>( pField, bound._bNegate ); } );
            }

            static bool isEnumValue( const FixedFieldDesc& desc, const int64 value ) noexcept
            {
                for ( uint32 index = 0; index < desc._enumValueCount; ++index )
                {
                    if ( desc._pEnumValue[index] == value )
                        return true;
                }
                return false;
            }

            static bool isFloatBounded( const FixedFieldDesc& desc ) noexcept { return desc._min.isPresent() || desc._max.isPresent(); }

            /** @brief 의존 코덱입니다. 진입점이 `findCodec` 으로 확인했으므로 늘 있다 — 없으면(확인한 뒤 모듈이 내려감) 대신 쓸 코덱 없이 멈춘다. */
            static const FixedCodec& requireDependency( const FixedCodec& codec, const uint32 dependencyIndex )
            {
                if ( dependencyIndex == FixedFieldDesc::kSelfDependency )
                    return codec;
                const FixedCodec::Dependency& dependency = codec._pDependency[dependencyIndex];
                const TypeInfo*               pType      = dependency._cache.find( dependency._fqn );
                const FixedCodec*             pNested    = pType != nullptr ? pType->_pFixedCodec : nullptr;
                if ( pNested == nullptr || pNested->isEligible() == false )
                    SW_FATAL( "'%#' has no fixed layout codec: nested codec is missing although its owner was checked (module unloaded?)", dependency._fqn.c_str() );
                return *pNested;
            }

            // ------------------------------------------------------------------------------
            // 1) 쓰기
            // ------------------------------------------------------------------------------
            static bool writeObject( const FixedCodec& codec, BitWriter& writer, const void* pObject )
            {
                if ( codec._pBaseCast != nullptr && writeObject( requireDependency( codec, 0 ), writer, codec._pBaseCast( pObject ) ) == false )
                    return false;
                const FixedScope scope{ pObject, nullptr, 0 };
                return writeChildren( codec, 0, codec._fieldCount, writer, pObject, scope );
            }

            static bool writeChildren( const FixedCodec& codec, const uint32 first, const uint32 count, BitWriter& writer, const void* pBase, const FixedScope& scope )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    if ( writeField( codec, codec._pField[index], writer, pBase, scope ) == false )
                        return false;
                }
                return true;
            }

            static bool writeElements( const FixedCodec& codec, const FixedFieldDesc& desc, BitWriter& writer, const void* pSequence, const FixedScope& scope )
            {
                const FixedSequenceOps& ops     = *desc._pSequenceOps;
                const size_t            count   = ops._pSize( pSequence );
                const uint8* const      pData   = static_cast<const uint8*>( ops._pData( pSequence ) );
                const FixedFieldDesc&   element = codec._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( writeField( codec, element, writer, pData + index * ops._elementSize, scope ) == false )
                        return false;
                }
                return true;
            }

            static bool writeField( const FixedCodec& codec, const FixedFieldDesc& desc, BitWriter& writer, const void* pBase, const FixedScope& scope )
            {
                const void* const pValue = static_cast<const uint8*>( pBase ) + desc._offset;
                switch ( desc._kind )
                {
                    case FixedFieldKind::Bool:
                    {
                        writer.writeBool( *static_cast<const bool*>( pValue ) );
                        return true;
                    }
                    case FixedFieldKind::BitFieldBool:
                    {
                        writer.writeBool( desc._pGetBit( pBase ) );
                        return true;
                    }
                    case FixedFieldKind::Integer:
                    {
                        const int64 minValue = loadBound<int64>( desc._min, scope, FixedCodecUtil::kNoLowerBound );
                        const int64 maxValue = loadBound<int64>( desc._max, scope, FixedCodecUtil::kNoUpperBound );
                        const bool  bRanged  = desc._min.isPresent() && desc._max.isPresent();
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T       = typename decltype( tag )::Type;
                            const T value = *static_cast<const T*>( pValue );
                            if ( bRanged )
                                return FixedCodecUtil::writeRangedInteger( writer, value, minValue, maxValue );
                            return FixedCodecUtil::writeVarInteger( writer, value, minValue, maxValue );
                        } );
                    }
                    case FixedFieldKind::Float32:
                    {
                        const float32 value = *static_cast<const float32*>( pValue );
                        if ( desc._step > 0.0f )
                        {
                            const float32 minValue = loadBound<float32>( desc._min, scope, -std::numeric_limits<float32>::infinity() );
                            const float32 maxValue = loadBound<float32>( desc._max, scope, std::numeric_limits<float32>::infinity() );
                            return FixedCodecUtil::writeQuantizedFloat32( writer, value, minValue, maxValue, desc._step );
                        }
                        if ( isFloatBounded( desc ) )
                        {
                            return FixedCodecUtil::writeBoundedFloat32( writer, value, loadBound<float64>( desc._min, scope, FixedCodecUtil::kNoLowerFloatBound ),
                                                                        loadBound<float64>( desc._max, scope, FixedCodecUtil::kNoUpperFloatBound ) );
                        }
                        writer.writeFloat( value );
                        return true;
                    }
                    case FixedFieldKind::Float64:
                    {
                        const float64 value = *static_cast<const float64*>( pValue );
                        if ( isFloatBounded( desc ) )
                        {
                            return FixedCodecUtil::writeBoundedFloat64( writer, value, loadBound<float64>( desc._min, scope, FixedCodecUtil::kNoLowerFloatBound ),
                                                                        loadBound<float64>( desc._max, scope, FixedCodecUtil::kNoUpperFloatBound ) );
                        }
                        FixedCodecUtil::writeFloat64( writer, value );
                        return true;
                    }
                    case FixedFieldKind::Enum:
                    {
                        const int64 value = dispatchInteger( desc._numberType, [&]( auto tag ) -> int64
                        { return static_cast<int64>( *static_cast<const typename decltype( tag )::Type*>( pValue ) ); } );
                        if ( isEnumValue( desc, value ) == false )
                            return false;
                        return FixedCodecUtil::writeRangedInteger( writer, value, desc._min._integer, desc._max._integer );
                    }
                    case FixedFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T        = typename decltype( tag )::Type;
                            using Unsigned = std::make_unsigned_t<T>;
                            const T value  = *static_cast<const T*>( pValue );
                            if ( ( static_cast<uint64>( value ) & ~desc._enumMask ) != 0 )
                                return false;
                            FixedCodecUtil::writeRawBits( writer, static_cast<uint64>( static_cast<Unsigned>( value ) ), static_cast<int32>( sizeof( Unsigned ) * 8 ) );
                            return true;
                        } );
                    }
                    case FixedFieldKind::String:
                        return FixedCodecUtil::writeString( writer, *static_cast<const string*>( pValue ), desc._maxCount );
                    case FixedFieldKind::Bitset:
                        return FixedCodecUtil::writeBitset( writer, *static_cast<const DynamicBitset*>( pValue ), desc._maxCount );
                    case FixedFieldKind::Vector:
                    {
                        if ( FixedCodecUtil::writeCount( writer, desc._pSequenceOps->_pSize( pValue ), desc._maxCount ) == false )
                            return false;
                        return writeElements( codec, desc, writer, pValue, scope );
                    }
                    case FixedFieldKind::FixedArray:
                        return writeElements( codec, desc, writer, pValue, scope );
                    case FixedFieldKind::Math:
                        return writeChildren( codec, desc._firstChild, desc._childCount, writer, pValue, scope );
                    case FixedFieldKind::Inline:
                    {
                        const FixedScope inner{ pValue, &scope, scope._depth + 1 };
                        return writeChildren( codec, desc._firstChild, desc._childCount, writer, pValue, inner );
                    }
                    case FixedFieldKind::Codec:
                        return writeObject( requireDependency( codec, desc._dependencyIndex ), writer, pValue );
                }
                return false;
            }

            // ------------------------------------------------------------------------------
            // 2) 읽기 — 읽는 바이트는 네트워크 · 세이브에서 온다. 경계 · 개수 · 열거자 · 글자 검사는 `FixedCodecUtil` 이 하고 여기서 하나도 건너뛰지 않는다.
            // ------------------------------------------------------------------------------
            static FixedReadResult readObject( const FixedCodec& codec, BitReader& reader, void* pObject )
            {
                FixedReadResult result = FixedReadResult::Ok;
                if ( codec._pBaseCast != nullptr )
                {
                    result = readObject( requireDependency( codec, 0 ), reader, const_cast<void*>( codec._pBaseCast( pObject ) ) );
                    if ( result != FixedReadResult::Ok )
                        return result;
                }
                const FixedScope scope{ pObject, nullptr, 0 };
                result = readChildren( codec, 0, codec._fieldCount, reader, pObject, scope );
                if ( result != FixedReadResult::Ok )
                    return result;
                if ( codec._pValidate != nullptr )
                {
                    ValidationContext context;
                    codec._pValidate( pObject, context );
                    if ( context.hasError() )
                        return FixedReadResult::ValidateFailed;
                }
                return FixedReadResult::Ok;
            }

            static FixedReadResult readChildren( const FixedCodec& codec, const uint32 first, const uint32 count, BitReader& reader, void* pBase, const FixedScope& scope )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    const FixedReadResult result = readField( codec, codec._pField[index], reader, pBase, scope );
                    if ( result != FixedReadResult::Ok )
                        return result;
                }
                return FixedReadResult::Ok;
            }

            static FixedReadResult readElements( const FixedCodec& codec, const FixedFieldDesc& desc, BitReader& reader, void* pSequence, const FixedScope& scope )
            {
                const FixedSequenceOps& ops     = *desc._pSequenceOps;
                const size_t            count   = ops._pSize( pSequence );
                uint8* const            pData   = static_cast<uint8*>( ops._pWritableData( pSequence ) );
                const FixedFieldDesc&   element = codec._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    const FixedReadResult result = readField( codec, element, reader, pData + index * ops._elementSize, scope );
                    if ( result != FixedReadResult::Ok )
                        return result;
                }
                return FixedReadResult::Ok;
            }

            static FixedReadResult readField( const FixedCodec& codec, const FixedFieldDesc& desc, BitReader& reader, void* pBase, const FixedScope& scope )
            {
                void* const pValue = static_cast<uint8*>( pBase ) + desc._offset;
                switch ( desc._kind )
                {
                    case FixedFieldKind::Bool:
                    {
                        *static_cast<bool*>( pValue ) = reader.readBool();
                        return FixedReadResult::Ok;
                    }
                    case FixedFieldKind::BitFieldBool:
                    {
                        desc._pSetBit( pBase, reader.readBool() );
                        return FixedReadResult::Ok;
                    }
                    case FixedFieldKind::Integer:
                    {
                        const int64 minValue = loadBound<int64>( desc._min, scope, FixedCodecUtil::kNoLowerBound );
                        const int64 maxValue = loadBound<int64>( desc._max, scope, FixedCodecUtil::kNoUpperBound );
                        const bool  bRanged  = desc._min.isPresent() && desc._max.isPresent();
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> FixedReadResult
                        {
                            using T  = typename decltype( tag )::Type;
                            T& value = *static_cast<T*>( pValue );
                            if ( bRanged )
                                return FixedCodecUtil::readRangedInteger( reader, minValue, maxValue, value );
                            return FixedCodecUtil::readVarInteger( reader, minValue, maxValue, value );
                        } );
                    }
                    case FixedFieldKind::Float32:
                    {
                        float32& value = *static_cast<float32*>( pValue );
                        if ( desc._step > 0.0f )
                        {
                            const float32 minValue = loadBound<float32>( desc._min, scope, -std::numeric_limits<float32>::infinity() );
                            const float32 maxValue = loadBound<float32>( desc._max, scope, std::numeric_limits<float32>::infinity() );
                            return FixedCodecUtil::readQuantizedFloat32( reader, minValue, maxValue, desc._step, value );
                        }
                        if ( isFloatBounded( desc ) )
                        {
                            return FixedCodecUtil::readBoundedFloat32( reader, loadBound<float64>( desc._min, scope, FixedCodecUtil::kNoLowerFloatBound ),
                                                                       loadBound<float64>( desc._max, scope, FixedCodecUtil::kNoUpperFloatBound ), value );
                        }
                        // 범위 없는 실수는 비트 그대로 왕복한다(NaN 포함 — `isBitEqual` 과 같은 뜻). 유한 검사는 범위를 적은 필드의 몫이다.
                        value = reader.readFloatBits();
                        return FixedReadResult::Ok;
                    }
                    case FixedFieldKind::Float64:
                    {
                        float64& value = *static_cast<float64*>( pValue );
                        if ( isFloatBounded( desc ) )
                        {
                            return FixedCodecUtil::readBoundedFloat64( reader, loadBound<float64>( desc._min, scope, FixedCodecUtil::kNoLowerFloatBound ),
                                                                       loadBound<float64>( desc._max, scope, FixedCodecUtil::kNoUpperFloatBound ), value );
                        }
                        value = FixedCodecUtil::readFloat64( reader );
                        return FixedReadResult::Ok;
                    }
                    case FixedFieldKind::Enum:
                    {
                        int64                 value  = 0;
                        const FixedReadResult result = FixedCodecUtil::readRangedInteger( reader, desc._min._integer, desc._max._integer, value );
                        if ( result != FixedReadResult::Ok )
                            return result;
                        if ( isEnumValue( desc, value ) == false )
                            return FixedReadResult::OutOfRange;
                        // 읽은 값은 [최소, 최대] 열거자 안이라 기반 타입에 그대로 들어간다.
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> FixedReadResult
                        {
                            using T                    = typename decltype( tag )::Type;
                            *static_cast<T*>( pValue ) = static_cast<T>( value );
                            return FixedReadResult::Ok;
                        } );
                    }
                    case FixedFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> FixedReadResult
                        {
                            using T            = typename decltype( tag )::Type;
                            using Unsigned     = std::make_unsigned_t<T>;
                            const uint64 raw   = FixedCodecUtil::readRawBits( reader, static_cast<int32>( sizeof( Unsigned ) * 8 ) );
                            const T      value = static_cast<T>( static_cast<Unsigned>( raw ) );
                            // 비트 플래그는 켜진 비트의 조합이라 범위가 없다. 등록된 비트 밖은 거절한다.
                            if ( ( static_cast<uint64>( value ) & ~desc._enumMask ) != 0 )
                                return FixedReadResult::OutOfRange;
                            *static_cast<T*>( pValue ) = value;
                            return FixedReadResult::Ok;
                        } );
                    }
                    case FixedFieldKind::String:
                        return FixedCodecUtil::readString( reader, desc._maxCount, *static_cast<string*>( pValue ) );
                    case FixedFieldKind::Bitset:
                        return FixedCodecUtil::readBitset( reader, desc._maxCount, *static_cast<DynamicBitset*>( pValue ) );
                    case FixedFieldKind::Vector:
                    {
                        uint32                count  = 0;
                        const FixedReadResult result = FixedCodecUtil::readCount( reader, desc._maxCount, count );
                        if ( result != FixedReadResult::Ok )
                            return result;
                        desc._pSequenceOps->_pResize( pValue, count );
                        return readElements( codec, desc, reader, pValue, scope );
                    }
                    case FixedFieldKind::FixedArray:
                        return readElements( codec, desc, reader, pValue, scope );
                    case FixedFieldKind::Math:
                        return readChildren( codec, desc._firstChild, desc._childCount, reader, pValue, scope );
                    case FixedFieldKind::Inline:
                    {
                        const FixedScope inner{ pValue, &scope, scope._depth + 1 };
                        return readChildren( codec, desc._firstChild, desc._childCount, reader, pValue, inner );
                    }
                    case FixedFieldKind::Codec:
                        return readObject( requireDependency( codec, desc._dependencyIndex ), reader, pValue );
                }
                return FixedReadResult::OutOfRange;
            }

            // ------------------------------------------------------------------------------
            // 3) 비교 — 코덱과 같은 필드 집합을 선언 순서로. 실수는 비트 패턴이다.
            // ------------------------------------------------------------------------------
            static bool equalsObject( const FixedCodec& codec, const void* pA, const void* pB )
            {
                if ( codec._pBaseCast != nullptr && equalsObject( requireDependency( codec, 0 ), codec._pBaseCast( pA ), codec._pBaseCast( pB ) ) == false )
                    return false;
                return equalsChildren( codec, 0, codec._fieldCount, pA, pB );
            }

            static bool equalsChildren( const FixedCodec& codec, const uint32 first, const uint32 count, const void* pBaseA, const void* pBaseB )
            {
                for ( uint32 index = first; index < first + count; ++index )
                {
                    if ( equalsField( codec, codec._pField[index], pBaseA, pBaseB ) == false )
                        return false;
                }
                return true;
            }

            static bool equalsElements( const FixedCodec& codec, const FixedFieldDesc& desc, const void* pSequenceA, const void* pSequenceB )
            {
                const FixedSequenceOps& ops     = *desc._pSequenceOps;
                const size_t            count   = ops._pSize( pSequenceA );
                const uint8* const      pDataA  = static_cast<const uint8*>( ops._pData( pSequenceA ) );
                const uint8* const      pDataB  = static_cast<const uint8*>( ops._pData( pSequenceB ) );
                const FixedFieldDesc&   element = codec._pField[desc._firstChild];
                for ( size_t index = 0; index < count; ++index )
                {
                    if ( equalsField( codec, element, pDataA + index * ops._elementSize, pDataB + index * ops._elementSize ) == false )
                        return false;
                }
                return true;
            }

            static bool equalsField( const FixedCodec& codec, const FixedFieldDesc& desc, const void* pBaseA, const void* pBaseB )
            {
                const void* const pA = static_cast<const uint8*>( pBaseA ) + desc._offset;
                const void* const pB = static_cast<const uint8*>( pBaseB ) + desc._offset;
                switch ( desc._kind )
                {
                    case FixedFieldKind::Bool:
                        return *static_cast<const bool*>( pA ) == *static_cast<const bool*>( pB );
                    case FixedFieldKind::BitFieldBool:
                        return desc._pGetBit( pBaseA ) == desc._pGetBit( pBaseB );
                    case FixedFieldKind::Integer:
                    case FixedFieldKind::Enum:
                    case FixedFieldKind::FlagEnum:
                    {
                        return dispatchInteger( desc._numberType, [&]( auto tag ) -> bool
                        {
                            using T = typename decltype( tag )::Type;
                            return *static_cast<const T*>( pA ) == *static_cast<const T*>( pB );
                        } );
                    }
                    case FixedFieldKind::Float32:
                        return FixedCodecUtil::isBitEqual( *static_cast<const float32*>( pA ), *static_cast<const float32*>( pB ) );
                    case FixedFieldKind::Float64:
                        return FixedCodecUtil::isBitEqual( *static_cast<const float64*>( pA ), *static_cast<const float64*>( pB ) );
                    case FixedFieldKind::String:
                        return *static_cast<const string*>( pA ) == *static_cast<const string*>( pB );
                    case FixedFieldKind::Bitset:
                        return *static_cast<const DynamicBitset*>( pA ) == *static_cast<const DynamicBitset*>( pB );
                    case FixedFieldKind::Vector:
                    {
                        if ( desc._pSequenceOps->_pSize( pA ) != desc._pSequenceOps->_pSize( pB ) )
                            return false;
                        return equalsElements( codec, desc, pA, pB );
                    }
                    case FixedFieldKind::FixedArray:
                        return equalsElements( codec, desc, pA, pB );
                    case FixedFieldKind::Math:
                    case FixedFieldKind::Inline:
                        return equalsChildren( codec, desc._firstChild, desc._childCount, pA, pB );
                    case FixedFieldKind::Codec:
                        return equalsObject( requireDependency( codec, desc._dependencyIndex ), pA, pB );
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<const FixedCodec*> FixedLayout::findCodec( const TypeInfo& type )
    {
        const FixedCodec* pCodec = type._pFixedCodec;
        if ( pCodec == nullptr )
            return unexpected( Error{ ErrorKind::NotFound, "type has no generated fixed layout codec", type._name.c_str() } );
        if ( expected<void> valid = validateCodec( *pCodec ); valid.has_value() == false )
            return unexpected( valid.error() );
        return pCodec;
    }

    expected<void> FixedLayout::validateCodec( const FixedCodec& codec )
    {
        if ( codec.isEligible() == false )
            return unexpected( Error{ ErrorKind::Unsupported, "type is not fixed layout eligible", codec._pIneligibleReason } );
        if ( codec._dependencyState.load( std::memory_order_acquire ) == FixedDependencyState::Valid )
            return {};

        vector<const FixedCodec*> listVisited;
        if ( expected<void> dependencies = FixedLayoutInternal::areDependenciesValid( codec, listVisited ); dependencies.has_value() == false )
            return unexpected( dependencies.error() );
        codec._dependencyState.store( FixedDependencyState::Valid, std::memory_order_release );
        return {};
    }

    uint64 FixedLayout::computeSchemaHash( const uint32 version, const string_view canonical, const std::initializer_list<uint64> listNestedHash )
    {
        uint64 hash = HashUtil::kFNVOffset64;
        hash        = FixedLayoutInternal::appendHashInteger( hash, version );
        hash        = HashUtil::appendBytes64( hash, canonical.data(), canonical.size() );
        for ( const uint64 nestedHash : listNestedHash )
        {
            hash = FixedLayoutInternal::appendHashInteger( hash, nestedHash );
        }
        return hash;
    }

    uint64 FixedLayout::getSchemaHash( const FixedCodec& codec )
    {
        const uint64 cached = codec._schemaHash.load( std::memory_order_acquire );
        if ( cached != 0 )
            return cached;
        // 다른 타입의 해시는 다른 헤더 · 모듈일 수 있으므로 실행 시간에 그 코덱에서 얻어 의존 차례로 섞는다(`computeSchemaHash` 와 같은 식).
        uint64 hash = computeSchemaHash( kVersion, codec._pCanonical, {} );
        for ( uint32 index = 0; index < codec._dependencyCount; ++index )
        {
            hash = FixedLayoutInternal::appendHashInteger( hash, getSchemaHash( FixedFieldCodec::requireDependency( codec, index ) ) );
        }
        codec._schemaHash.store( hash, std::memory_order_release );
        return hash;
    }

    const utf8* FixedLayout::toString( const FixedReadResult result ) noexcept
    {
        switch ( result )
        {
            case FixedReadResult::Ok:
                return "Ok";
            case FixedReadResult::SchemaMismatch:
                return "SchemaMismatch";
            case FixedReadResult::Truncated:
                return "Truncated";
            case FixedReadResult::OutOfRange:
                return "OutOfRange";
            case FixedReadResult::ValidateFailed:
                return "ValidateFailed";
            case FixedReadResult::TrailingData:
                return "TrailingData";
            case FixedReadResult::InvalidText:
                return "InvalidText";
        }
        return "Unknown";
    }

    void FixedLayout::stopForMissingCodec( const hashed_string& fqn, string_view reason, string_view detail )
    {
        // 대체 값(0 해시, 빈 결과)을 돌려주면 그 값이 핸드셰이크 해시나 롤백 상태에 섞입니다. 고칠 곳은 선언이므로 멈춥니다.
        SW_FATAL( "'%#' has no fixed layout codec: %# (%#)", fqn.c_str(), reason, detail );
    }

    bool FixedLayout::writeWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer )
    {
        FixedCodecUtil::writeRawBits( writer, getSchemaHash( codec ), 64 );
        return FixedFieldCodec::writeObject( codec, writer, pObject );
    }

    FixedReadResult FixedLayout::readWithCodec( const FixedCodec& codec, const hashed_string& typeFqn, BitReader& reader, void* pObject )
    {
        const uint64 schemaHash = FixedCodecUtil::readRawBits( reader, 64 );
        if ( reader.hasOverflowed() )
            return FixedReadResult::Truncated;
        if ( schemaHash != getSchemaHash( codec ) )
        {
            // 변환 표: 키는 (대상 타입, 기록된 해시), 값은 옛 비트를 지금 객체로 읽는 함수다. 없으면 거절한다 — 옛 비트를 지금 배치로 읽는 폴백은 없다.
            const FixedLayoutConversion* pConversion = FixedLayoutConversion::find( typeFqn, schemaHash );
            if ( pConversion == nullptr )
                return FixedReadResult::SchemaMismatch;
            const FixedReadResult result = pConversion->_pFunction( reader, pObject );
            if ( result == FixedReadResult::Ok && reader.hasOverflowed() )
                return FixedReadResult::Truncated;
            return result;
        }
        return readBodyWithCodec( codec, reader, pObject );
    }

    namespace
    {
        /** @brief 등록된 고정 배치형 변환(등록 순서)입니다. Engine 이미지의 함수 정적이라 게임 모듈이 바뀌어도 남는다(`FormatUpgradeStep` 과 같다). */
        SW_DEFINE_REGISTRATION_LIST( getFixedLayoutConversions, const FixedLayoutConversion, "FixedLayoutConversions", RegistrationOrder::Insertion, true )
    } // namespace

    bool FixedLayoutConversion::registerConversion( const FixedLayoutConversion* pConversion )
    {
        if ( pConversion == nullptr || StringUtil::isNullOrEmpty( pConversion->_pTypeFqn ) || pConversion->_pFunction == nullptr )
            return false;
        // 같은 (타입, 해시) 두 번 등록은 이름 목록이 이름 중복으로 거절한다 — 여기서 `find` 로 미리 보지 않는다(등록은 정적 초기화라 이름 풀 앞이다, hashed_string 금지)
        string name = pConversion->_pTypeFqn;
        name += ':';
        name += sw::to_string( pConversion->_fromSchemaHash );
        return getFixedLayoutConversions().addLogged( pConversion, "Fixed layout conversion", name ) == RegistrationResult::Added;
    }

    void FixedLayoutConversion::unregisterConversion( const FixedLayoutConversion* pConversion )
    {
        (void)getFixedLayoutConversions().remove( pConversion ); // 등록되어 있지 않은(거절된) 항목을 빼는 것은 할 일이 없다
    }

    const FixedLayoutConversion* FixedLayoutConversion::find( const hashed_string& typeFqn, const uint64 fromSchemaHash )
    {
        const RegistrationList<const FixedLayoutConversion>& list = getFixedLayoutConversions();
        for ( uint32 index = 0; index < list.getCount(); ++index )
        {
            const FixedLayoutConversion* pConversion = list.getAt( index );
            if ( pConversion->_fromSchemaHash == fromSchemaHash && string_view{ pConversion->_pTypeFqn } == string_view{ typeFqn.c_str() } )
                return pConversion;
        }
        return nullptr;
    }

    bool FixedLayout::writeBodyWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer )
    {
        return FixedFieldCodec::writeObject( codec, writer, pObject );
    }

    FixedReadResult FixedLayout::readPropertiesWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject )
    {
        const FixedReadResult result = FixedFieldCodec::readObject( codec, reader, pObject );
        // 모자란 스트림은 0 을 읽으므로 그 전에 범위 · 열거자 검사가 먼저 걸릴 수 있습니다. 원인은 잘림입니다.
        if ( reader.hasOverflowed() )
            return FixedReadResult::Truncated;
        return result;
    }

    bool FixedLayout::equalsFieldWithCodec( const FixedCodec& codec, const uint32 fieldIndex, const void* pA, const void* pB )
    {
        return FixedFieldCodec::equalsChildren( codec, fieldIndex, 1, pA, pB );
    }

    FixedReadResult FixedLayout::readBodyWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject )
    {
        const FixedReadResult result = readPropertiesWithCodec( codec, reader, pObject );
        if ( result != FixedReadResult::Ok )
            return result;

        const int32 bitsRemaining = reader.getBitsRemaining();
        if ( bitsRemaining >= 8 )
            return FixedReadResult::TrailingData;
        if ( bitsRemaining > 0 && reader.readBits( bitsRemaining ) != 0 )
            return FixedReadResult::TrailingData;
        return FixedReadResult::Ok;
    }

    bool FixedLayout::equalsWithCodec( const FixedCodec& codec, const void* pA, const void* pB )
    {
        return FixedFieldCodec::equalsObject( codec, pA, pB );
    }
} // namespace sw

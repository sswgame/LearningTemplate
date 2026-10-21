#include "pch.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "Core/Common/BitUtil.h"
#include "Core/Common/HashUtil.h"
#include "Core/Container/DynamicBitset.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"

SW_LOG_CALLER( "FixedLayout" );
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
             * @brief 의존 코덱이 모두 적격인지 따라 내려가 봅니다. 아니면 @p outReason 에 경로와 이유를 적습니다.
             * @param listVisited 지금 확인 중인 코덱들입니다. 서로를 담는 타입이 다시 나오면 이미 확인 중이므로 통과합니다.
             */
            static bool areDependenciesValid( const FixedCodec& codec, vector<const FixedCodec*>& listVisited, string& outReason )
            {
                if ( codec._dependencyState.load( std::memory_order_acquire ) == FixedDependencyState::Valid )
                    return true;
                if ( VectorUtil::contains( listVisited, &codec ) )
                    return true;
                if ( static_cast<int32>( listVisited.size() ) >= kMaxDependencyDepth )
                {
                    outReason = "fixed layout dependencies nest too deeply";
                    return false;
                }
                listVisited.push_back( &codec );

                TypeRegistry& registry = engine::getSubsystem<TypeRegistry>();
                for ( uint32 index = 0; index < codec._dependencyCount; ++index )
                {
                    const FixedCodec::Dependency& dependency = codec._pDependency[index];
                    const TypeInfo*               pType      = registry.findType( hashed_string( dependency._pTypeFqn ) );
                    const FixedCodec*             pNested    = pType != nullptr ? pType->_pFixedCodec : nullptr;
                    string                        nestedReason;
                    if ( pType == nullptr )
                        nestedReason = "type is not registered";
                    else if ( pNested == nullptr )
                        nestedReason = "type has no generated fixed layout codec";
                    else if ( pNested->isEligible() == false )
                        nestedReason = pNested->_pIneligibleReason;
                    else if ( areDependenciesValid( *pNested, listVisited, nestedReason ) )
                        continue;

                    outReason = string( "field '" ) + dependency._pFieldName + "' of type '" + dependency._pTypeFqn + "': " + nestedReason;
                    listVisited.pop_back();
                    return false;
                }
                listVisited.pop_back();
                return true;
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
    FixedCodec::FixedCodec( WriteFunction pWrite, ReadFunction pRead, EqualsFunction pEquals, SchemaHashFunction pComputeSchemaHash, const utf8* pCanonical,
                            const Dependency* pDependency, uint32 dependencyCount ) noexcept
        : _pWrite{ pWrite }
        , _pRead{ pRead }
        , _pEquals{ pEquals }
        , _pComputeSchemaHash{ pComputeSchemaHash }
        , _pCanonical{ pCanonical }
        , _pIneligibleReason{ nullptr }
        , _pDependency{ pDependency }
        , _dependencyCount{ dependencyCount }
        , _dependencyState{ dependencyCount == 0 ? FixedDependencyState::Valid : FixedDependencyState::Unknown }
    {
    }

    FixedCodec::FixedCodec( const utf8* pIneligibleReason ) noexcept
        : _pWrite{ nullptr }
        , _pRead{ nullptr }
        , _pEquals{ nullptr }
        , _pComputeSchemaHash{ nullptr }
        , _pCanonical{ nullptr }
        , _pIneligibleReason{ pIneligibleReason }
        , _pDependency{ nullptr }
        , _dependencyCount{ 0 }
        , _dependencyState{ FixedDependencyState::Missing }
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
        const float32 value   = reader.readFloat();
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
        // 여기서는 유한한 범위 밖 값을 경계로 자릅니다(`Step` 실수는 경계로 자른다는 설계, `docs/plans/ReflectionFormats.md` 3.4). 범위는 선언의 `Min` · `Max` 라
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
        return FixedReadResult::Ok;
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
    const FixedCodec* FixedLayout::findCodec( const TypeInfo& type, string& outReason )
    {
        const FixedCodec* pCodec = type._pFixedCodec;
        if ( pCodec == nullptr )
        {
            outReason = "type has no generated fixed layout codec";
            return nullptr;
        }
        if ( pCodec->isEligible() == false )
        {
            outReason = pCodec->_pIneligibleReason;
            return nullptr;
        }
        if ( pCodec->_dependencyState.load( std::memory_order_acquire ) == FixedDependencyState::Valid )
            return pCodec;

        vector<const FixedCodec*> listVisited;
        if ( FixedLayoutInternal::areDependenciesValid( *pCodec, listVisited, outReason ) == false )
            return nullptr;
        pCodec->_dependencyState.store( FixedDependencyState::Valid, std::memory_order_release );
        return pCodec;
    }

    const FixedCodec* FixedLayout::findNestedCodec( const TypeLookupCache& cache, const hashed_string& fqn )
    {
        const TypeInfo*   pType  = cache.find( fqn );
        const FixedCodec* pCodec = pType != nullptr ? pType->_pFixedCodec : nullptr;
        if ( pCodec == nullptr || pCodec->isEligible() == false )
            return nullptr;
        return pCodec;
    }

    const FixedCodec& FixedLayout::requireNestedCodec( const TypeLookupCache& cache, const hashed_string& fqn )
    {
        const FixedCodec* pCodec = findNestedCodec( cache, fqn );
        if ( pCodec == nullptr )
            stopForMissingCodec( fqn, "nested codec is missing although its owner was checked (module unloaded?)" );
        return *pCodec;
    }

    uint64 FixedLayout::computeSchemaHash( const uint32 version, const string_view canonical, const std::initializer_list<uint64> listNestedHash )
    {
        uint64 hash = HashUtil::kFnvOffset64;
        hash        = FixedLayoutInternal::appendHashInteger( hash, version );
        hash        = HashUtil::appendBytes64( hash, canonical.data(), canonical.size() );
        for ( const uint64 nestedHash : listNestedHash )
        {
            hash = FixedLayoutInternal::appendHashInteger( hash, nestedHash );
        }
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
        }
        return "Unknown";
    }

    void FixedLayout::stopForMissingCodec( const hashed_string& fqn, const string& reason )
    {
        // 대체 값(0 해시, 빈 결과)을 돌려주면 그 값이 핸드셰이크 해시나 롤백 상태에 섞입니다. 고칠 곳은 선언이므로 멈춥니다.
        SW_LOG_ERROR( "'%#' has no fixed layout codec: %#", fqn.c_str(), reason );
        std::abort();
    }

    bool FixedLayout::writeWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer )
    {
        FixedCodecUtil::writeRawBits( writer, codec._pComputeSchemaHash(), 64 );
        return codec._pWrite( writer, pObject );
    }

    FixedReadResult FixedLayout::readWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject )
    {
        const uint64 schemaHash = FixedCodecUtil::readRawBits( reader, 64 );
        if ( reader.hasOverflowed() )
            return FixedReadResult::Truncated;
        if ( schemaHash != codec._pComputeSchemaHash() )
        {
            // 변환 레지스트리를 볼 자리입니다. 키는 (기록된 해시, 대상 타입)이고 값은 옛 바이트를 새 객체로 바꾸는 함수입니다(ReflectionFormats.md 3.14).
            // 변환할 옛 B 데이터가 없어 레지스트리가 아직 없으므로 거절합니다.
            return FixedReadResult::SchemaMismatch;
        }
        return readBodyWithCodec( codec, reader, pObject );
    }

    bool FixedLayout::writeBodyWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer )
    {
        return codec._pWrite( writer, pObject );
    }

    FixedReadResult FixedLayout::readBodyWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject )
    {
        const FixedReadResult result = codec._pRead( reader, pObject );
        // 모자란 스트림은 0 을 읽으므로 그 전에 범위 · 열거자 검사가 먼저 걸릴 수 있습니다. 원인은 잘림입니다.
        if ( reader.hasOverflowed() )
            return FixedReadResult::Truncated;
        if ( result != FixedReadResult::Ok )
            return result;

        const int32 bitsRemaining = reader.getBitsRemaining();
        if ( bitsRemaining >= 8 )
            return FixedReadResult::TrailingData;
        if ( bitsRemaining > 0 && reader.readBits( bitsRemaining ) != 0 )
            return FixedReadResult::TrailingData;
        return FixedReadResult::Ok;
    }
} // namespace sw

/**
 * @file FixedLayout.h
 * @brief 고정 배치형(B) 직렬화입니다. 같은 빌드끼리만 읽는 바이트를 코드 생성기가 타입마다 만든 함수로 쓰고, 읽고, 비교합니다.
 * @details 계획과 근거는 `docs/plans/ReflectionFormats.md` 3절과 6절에 있습니다.
 *
 *          **사용자는 B 를 선언하지 않습니다.** 생성기가 `REFLECT()` 타입마다 코덱을 만들어 `TypeInfo::_pFixedCodec` 에 등록합니다.
 *          필드 타입이 인코딩 표 밖이면(포인터, `MaxCount` 없는 가변 길이, 핸들 등) 코덱 대신 그 이유를 등록합니다.
 *          코덱이 필요한 등록 API 는 기동 때 `findCodec` 으로 확인하고, 없으면 이유와 함께 멈춥니다.
 *
 *          스트림은 `[스키마 해시 64 비트][최상위 타입의 필드들]` 입니다. 필드는 선언 순서대로 쓰고 태그와 크기를 붙이지 않습니다.
 *          해시는 생성기가 만든 정규 문자열(필드 이름, 순서, 타입, 범위 키, 열거자)과 `kVersion` 에서 나옵니다.
 *          그래서 필드 순서만 바꿔도 다른 빌드가 쓴 바이트는 `SchemaMismatch` 로 거절됩니다. **필드 순서가 와이어 형식입니다.**
 *          롤백과 스냅샷처럼 같은 빌드끼리만 오가는 데이터에 씁니다. 세이브와 에셋은 자기 기술형(`BinarySerializer`)에 둡니다.
 *
 *          읽기는 원자적입니다. 대상을 복사한 임시 객체에 읽고, 끝까지 성공했을 때만 옮깁니다. 실패하면 대상은 그대로입니다.
 *          코덱에 없는 필드(Transient, 포인터)는 복사본에서 이어집니다.
 *
 *          코덱이 없는 타입을 `write` · `read` · `equals` · `getSchemaHash` 에 넘기면 이유를 로그에 남기고 프로세스를 멈춥니다(모든 구성).
 *          값 하나(0 해시, 빈 결과)를 대신 돌려주면 핸드셰이크 해시에 그 값이 섞이므로 대체 값을 두지 않습니다.
 */
#pragma once
#include "Core/Network/BitStream.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    class DynamicBitset;
    class NetMessageChannel;

    /** @brief 읽기 결과입니다. `Ok` 가 아니면 대상 객체는 바뀌지 않았습니다. 결과를 버리면 컴파일이 멈춥니다. */
    enum class [[nodiscard]] FixedReadResult : uint8
    {
        Ok,
        SchemaMismatch, ///< 해시가 다릅니다. 필드, 순서, 범위 키가 다른 선언(다른 빌드)이 쓴 바이트입니다
        Truncated,      ///< 스트림이 모자랍니다
        OutOfRange,     ///< 범위 밖 정수 · 개수 · 양자화 칸, 등록되지 않은 열거자, 허용 범위 밖 실수입니다
        ValidateFailed, ///< `REFLECT( Validate = fn )` 가 오류를 적었습니다
        TrailingData,   ///< 필드를 다 읽고도 바이트 경계 패딩보다 많은 비트가 남았습니다
    };
} // namespace sw

namespace sw
{
    /** @brief 코덱이 의존하는 다른 타입의 코덱을 확인한 결과입니다. */
    enum class FixedDependencyState : uint8
    {
        Unknown, ///< 아직 확인하지 않았습니다
        Valid,   ///< 모두 적격입니다
        Missing, ///< 하나라도 없거나 적격이 아닙니다(적격이 아닌 타입 자신도 이 상태입니다)
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 생성기가 타입마다 만드는 코덱 테이블입니다. `TypeInfo::_pFixedCodec` 가 가리킵니다.
     * @details 적격이 아닌 타입은 함수가 모두 비어 있고 `_pIneligibleReason` 에 이유가 있습니다. 함수는 `FixedLayout` 만 부릅니다.
     *          적격 여부와 의존 타입 확인을 `FixedLayout::findCodec` 한 곳에 두어, 확인 없이 부르는 길을 만들지 않습니다.
     *          모듈 코드(생성된 함수)를 가리키므로 모듈을 해제하면 `TypeInfo` 가 이 포인터를 비웁니다.
     */
    struct SW_API FixedCodec
    {
        using WriteFunction      = bool ( * )( BitWriter& writer, const void* pObject );
        using ReadFunction       = FixedReadResult ( * )( BitReader& reader, void* pObject );
        using EqualsFunction     = bool ( * )( const void* pA, const void* pB );
        using SchemaHashFunction = uint64 ( * )();

        /** @brief 이 코덱이 실행 시간에 찾아 부르는 다른 타입의 코덱입니다(기반 클래스, 펼치지 못한 중첩 타입). */
        struct Dependency
        {
            const utf8* _pFieldName; ///< 오류 메시지에 씁니다. 기반 클래스면 "(base)" 입니다
            const utf8* _pTypeFqn;
        };

        WriteFunction      _pWrite;
        ReadFunction       _pRead;
        EqualsFunction     _pEquals;
        SchemaHashFunction _pComputeSchemaHash;
        const utf8*        _pCanonical;        ///< 이 타입 몫의 정규 문자열입니다(`FixedSchemas.gen.txt` 와 같은 글)
        const utf8*        _pIneligibleReason; ///< 적격이 아니면 이유이고, 적격이면 nullptr 입니다
        const Dependency*  _pDependency;
        uint32             _dependencyCount;
        /** @brief 의존 코덱을 확인한 결과입니다. 첫 사용이 채우고, 그 뒤로는 원자 읽기 한 번입니다. */
        mutable atomic<FixedDependencyState> _dependencyState;

        /** @brief 적격 타입의 코덱입니다. */
        FixedCodec( WriteFunction pWrite, ReadFunction pRead, EqualsFunction pEquals, SchemaHashFunction pComputeSchemaHash, const utf8* pCanonical,
                    const Dependency* pDependency, uint32 dependencyCount ) noexcept;
        /** @brief 적격이 아닌 타입입니다. @p pIneligibleReason 은 필드 이름과 이유입니다. */
        explicit FixedCodec( const utf8* pIneligibleReason ) noexcept;

        bool isEligible() const noexcept { return _pIneligibleReason == nullptr; }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 생성 코드가 필드마다 부르는 인코딩 도우미입니다. 사용자 코드는 `FixedLayout` 을 씁니다.
     * @details 범위 밖 정수, `MaxCount` 초과, 등록되지 않은 열거자, 허용 범위 밖 실수는 쓰기 실패입니다. `BitWriter::writeInt` 처럼 잘라 쓰지 않습니다.
     *          `Step` 을 적은 실수만 손실 인코딩이므로 쓸 때 경계로 자릅니다. 읽을 때는 무엇도 자르지 않고 범위를 벗어나면 OutOfRange 입니다.
     */
    struct SW_API FixedCodecUtil
    {
        /** @brief 정수 경계를 적지 않은 쪽입니다. */
        static constexpr int64 kNoLowerBound = std::numeric_limits<int64>::min();
        static constexpr int64 kNoUpperBound = std::numeric_limits<int64>::max();
        /** @brief 실수 경계를 적지 않은 쪽입니다. */
        static constexpr float64 kNoLowerFloatBound = -std::numeric_limits<float64>::infinity();
        static constexpr float64 kNoUpperFloatBound = std::numeric_limits<float64>::infinity();

        // ------------------------------------------------------------------------------
        // 1) 정수
        // ------------------------------------------------------------------------------
        /** @brief @p value 가 [@p minValue, @p maxValue] 안인지 봅니다. 부호 없는 64 비트의 큰 값도 맞게 비교합니다. */
        template <typename T>
        static bool isIntegerInRange( const T value, const int64 minValue, const int64 maxValue ) noexcept
        {
            if constexpr ( std::is_signed_v<T> )
            {
                const int64 signedValue = static_cast<int64>( value );
                return minValue <= signedValue && signedValue <= maxValue;
            }
            else
            {
                const uint64 unsignedValue = static_cast<uint64>( value );
                if ( unsignedValue > static_cast<uint64>( kNoUpperBound ) )
                    return maxValue == kNoUpperBound;
                const int64 signedValue = static_cast<int64>( unsignedValue );
                return minValue <= signedValue && signedValue <= maxValue;
            }
        }

        /** @brief [@p minValue, @p maxValue] 범위 정수를 범위 비트 수로 씁니다. 범위 밖이면 쓰지 않고 false 입니다. */
        template <typename T>
        [[nodiscard]] static bool writeRangedInteger( BitWriter& writer, const T value, const int64 minValue, const int64 maxValue )
        {
            if ( isRangeEncodable( minValue, maxValue ) == false || isIntegerInRange( value, minValue, maxValue ) == false )
                return false;
            writeRangedOffset( writer, static_cast<uint64>( static_cast<int64>( value ) - minValue ), minValue, maxValue );
            return true;
        }

        /** @brief `writeRangedInteger` 로 쓴 값을 읽습니다. 범위나 @p T 를 넘으면 OutOfRange 입니다. */
        template <typename T>
        static FixedReadResult readRangedInteger( BitReader& reader, const int64 minValue, const int64 maxValue, T& outValue )
        {
            if ( isRangeEncodable( minValue, maxValue ) == false )
                return FixedReadResult::OutOfRange;
            const uint64 offset = readRangedOffset( reader, minValue, maxValue );
            if ( offset > static_cast<uint64>( maxValue - minValue ) )
                return FixedReadResult::OutOfRange;
            const int64 value = minValue + static_cast<int64>( offset );
            if ( isRepresentable<T>( value ) == false )
                return FixedReadResult::OutOfRange;
            outValue = static_cast<T>( value );
            return FixedReadResult::Ok;
        }

        /** @brief 범위가 없거나 한쪽만 있는 정수를 가변 길이로 씁니다. 부호가 있으면 지그재그입니다. 경계 밖이면 false 입니다. */
        template <typename T>
        [[nodiscard]] static bool writeVarInteger( BitWriter& writer, const T value, const int64 minValue, const int64 maxValue )
        {
            if ( isIntegerInRange( value, minValue, maxValue ) == false )
                return false;
            if constexpr ( std::is_signed_v<T> )
                writer.writeVarInt( static_cast<int64>( value ) );
            else
                writer.writeVarUint( static_cast<uint64>( value ) );
            return true;
        }

        /** @brief `writeVarInteger` 로 쓴 값을 읽습니다. @p T 나 경계를 넘으면 OutOfRange 입니다. */
        template <typename T>
        static FixedReadResult readVarInteger( BitReader& reader, const int64 minValue, const int64 maxValue, T& outValue )
        {
            T value{};
            if constexpr ( std::is_signed_v<T> )
            {
                const int64 raw = reader.readVarInt();
                if ( isRepresentable<T>( raw ) == false )
                    return FixedReadResult::OutOfRange;
                value = static_cast<T>( raw );
            }
            else
            {
                const uint64 raw = reader.readVarUint();
                if ( raw > static_cast<uint64>( std::numeric_limits<T>::max() ) )
                    return FixedReadResult::OutOfRange;
                value = static_cast<T>( raw );
            }
            if ( isIntegerInRange( value, minValue, maxValue ) == false )
                return FixedReadResult::OutOfRange;
            outValue = value;
            return FixedReadResult::Ok;
        }

        // ------------------------------------------------------------------------------
        // 2) 실수
        // ------------------------------------------------------------------------------
        /** @brief 32 비트 원본 비트를 씁니다. 경계가 있으면 그 안이어야 합니다(NaN 은 실패입니다). */
        [[nodiscard]] static bool writeBoundedFloat32( BitWriter& writer, float32 value, float64 minValue, float64 maxValue );
        static FixedReadResult    readBoundedFloat32( BitReader& reader, float64 minValue, float64 maxValue, float32& outValue );
        /** @brief 64 비트 원본 비트입니다. */
        static void                  writeFloat64( BitWriter& writer, float64 value );
        [[nodiscard]] static float64 readFloat64( BitReader& reader );
        [[nodiscard]] static bool    writeBoundedFloat64( BitWriter& writer, float64 value, float64 minValue, float64 maxValue );
        static FixedReadResult       readBoundedFloat64( BitReader& reader, float64 minValue, float64 maxValue, float64& outValue );
        /**
         * @brief `Step` 양자화입니다. 범위 밖 값은 경계로 자릅니다. `Step` 을 적은 필드만 이 손실이 있습니다.
         * @details 칸 번호 [0, 칸 수] 를 범위 비트 수로 씁니다. 칸 수와 칸 값은 `readQuantizedFloat32` 와 같은 함수로 구해 두 쪽이 어긋나지 않습니다.
         * @return NaN · 무한대 값, 비거나 뒤집힌 범위, 0 이하 간격, 32 비트를 넘는 칸 수이면 false
         */
        [[nodiscard]] static bool writeQuantizedFloat32( BitWriter& writer, float32 value, float32 minValue, float32 maxValue, float32 step );
        /** @brief 칸 번호가 칸 수를 넘으면 자르지 않고 OutOfRange 입니다. 범위가 쓸 수 없는 모양이어도 OutOfRange 입니다. */
        static FixedReadResult readQuantizedFloat32( BitReader& reader, float32 minValue, float32 maxValue, float32 step, float32& outValue );
        /** @brief 비트 패턴 비교입니다. `NaN == NaN` 은 참이고 `-0 == +0` 은 거짓입니다(같은 바이트를 만드는지와 같은 뜻입니다). */
        static bool isBitEqual( float32 a, float32 b ) noexcept;
        static bool isBitEqual( float64 a, float64 b ) noexcept;

        // ------------------------------------------------------------------------------
        // 3) 개수 · 문자열 · 열거형 비트
        // ------------------------------------------------------------------------------
        /** @brief [0, @p maxCount] 개수를 씁니다. 넘으면 잘라 쓰지 않고 false 입니다. */
        [[nodiscard]] static bool writeCount( BitWriter& writer, size_t count, uint32 maxCount );
        static FixedReadResult    readCount( BitReader& reader, uint32 maxCount, uint32& outCount );
        /** @brief 길이(개수 규칙)와 문자마다 8 비트입니다. */
        [[nodiscard]] static bool writeString( BitWriter& writer, const string& value, uint32 maxCount );
        static FixedReadResult    readString( BitReader& reader, uint32 maxCount, string& outValue );
        /** @brief 비트 수(개수 규칙)와 비트마다 1 비트입니다. 원소 주소 없이 값으로 읽고 씁니다(`DynamicBitset`). */
        [[nodiscard]] static bool writeBitset( BitWriter& writer, const DynamicBitset& value, uint32 maxCount );
        static FixedReadResult    readBitset( BitReader& reader, uint32 maxCount, DynamicBitset& outValue );
        /** @brief @p bitCount(최대 64) 비트를 씁니다. */
        static void                 writeRawBits( BitWriter& writer, uint64 value, int32 bitCount );
        [[nodiscard]] static uint64 readRawBits( BitReader& reader, int32 bitCount );

        /** @brief 비트 플래그 열거형(`ENUM( Flags )`)을 기반 타입 전체 폭으로 씁니다. 플래그가 아닌 열거형은 열거자 범위 비트(`writeRangedInteger`)로 씁니다. */
        template <typename E>
        static void writeEnumBits( BitWriter& writer, const E value )
        {
            using Unsigned = std::make_unsigned_t<std::underlying_type_t<E>>;
            writeRawBits( writer, static_cast<uint64>( static_cast<Unsigned>( value ) ), static_cast<int32>( sizeof( Unsigned ) * 8 ) );
        }

        template <typename E>
        [[nodiscard]] static E readEnumBits( BitReader& reader )
        {
            using Unsigned = std::make_unsigned_t<std::underlying_type_t<E>>;
            return static_cast<E>( static_cast<Unsigned>( readRawBits( reader, static_cast<int32>( sizeof( Unsigned ) * 8 ) ) ) );
        }

    private:
        /** @brief 범위 비트 수로 쓸 수 있는 범위인지(뒤집히지 않았고 폭이 32 비트 이하인지) 봅니다. 필드 이름 범위는 실행 시간에 정해지므로 매번 봅니다. */
        static bool                 isRangeEncodable( int64 minValue, int64 maxValue ) noexcept;
        static void                 writeRangedOffset( BitWriter& writer, uint64 offset, int64 minValue, int64 maxValue );
        [[nodiscard]] static uint64 readRangedOffset( BitReader& reader, int64 minValue, int64 maxValue );
        /** @brief 양자화 칸 수(가장 큰 칸 번호)입니다. 쓰기와 읽기가 이 함수 하나로 구합니다. 쓸 수 없는 범위이면 false 입니다. */
        [[nodiscard]] static bool computeQuantizedIndexCount( float32 minValue, float32 maxValue, float32 step, uint32& outIndexCount );

        template <typename T>
        static bool isRepresentable( const int64 value ) noexcept
        {
            if constexpr ( std::is_signed_v<T> )
                return static_cast<int64>( std::numeric_limits<T>::min() ) <= value && value <= static_cast<int64>( std::numeric_limits<T>::max() );
            else
                return 0 <= value && static_cast<uint64>( value ) <= static_cast<uint64>( std::numeric_limits<T>::max() );
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 고정 배치형 직렬화의 진입점입니다. 롤백, 넷 채널, 온라인 메서드 같은 등록 API 가 이것을 부릅니다.
     * @details 타입은 컴파일러 시그니처의 FQN(`typeFqn<T>`)과 함수 지역 캐시로 한 번 찾습니다.
     *          코덱이 없거나 적격이 아니면 이유를 로그에 남기고 프로세스를 멈춥니다. 조용히 다른 형식이나 대체 값으로 바꾸지 않습니다.
     *          등록 API 는 기동 때 `findCodec` 으로 미리 확인해 그 자리에서 이유를 보여 줍니다.
     */
    class SW_API FixedLayout
    {
    public:
        /** @brief 인코딩 규칙(가변 정수 꼴, 양자화 반올림, 스트림 머리)의 버전입니다. 규칙을 바꾸면 이 값을 올려 모든 스키마 해시를 바꿉니다. */
        static constexpr uint32 kVersion = 2; ///< 2: `Count` 없는 열거형을 기반 타입 전체 폭 대신 [최소 열거자, 최대 열거자] 범위 비트로 쓴다.

        // ------------------------------------------------------------------------------
        // 1) 진입점
        // ------------------------------------------------------------------------------
        /** @brief 스키마 해시와 필드를 이어 씁니다. 범위 밖 값이 있으면 false 이고, @p writer 에는 쓰다 만 비트가 남습니다. */
        template <typename T>
        [[nodiscard]] static bool write( const T& object, BitWriter& writer )
        {
            return writeWithCodec( requireCodec<T>(), &object, writer );
        }

        /** @brief @p byteCount 바이트 전체를 한 객체로 읽습니다. `Ok` 일 때만 @p inoutObject 를 바꿉니다. */
        template <typename T>
        [[nodiscard]] static FixedReadResult read( const uint8* pData, const int32 byteCount, T& inoutObject )
        {
            BitReader reader( pData, byteCount );
            return read( reader, inoutObject );
        }

        /** @brief @p reader 에 남은 비트 전체를 한 객체로 읽습니다. 받은 메시지 본문처럼 읽기 객체를 넘겨받는 쪽이 씁니다. `Ok` 일 때만 @p inoutObject 를 바꿉니다. */
        template <typename T>
        [[nodiscard]] static FixedReadResult read( BitReader& reader, T& inoutObject )
        {
            const FixedCodec&     codec     = requireCodec<T>();
            T                     temporary = inoutObject; // 코덱에 없는 필드는 이 복사본에서 이어집니다
            const FixedReadResult result    = readWithCodec( codec, reader, &temporary );
            if ( result == FixedReadResult::Ok )
                inoutObject = std::move( temporary );
            return result;
        }

        /** @brief 코덱과 같은 필드 집합을 선언 순서로 비교합니다. 실수는 비트 패턴으로 봅니다(`FixedCodecUtil::isBitEqual`). */
        template <typename T>
        [[nodiscard]] static bool equals( const T& a, const T& b )
        {
            return requireCodec<T>()._pEquals( &a, &b );
        }

        /** @brief 스트림 머리에 싣는 스키마 해시입니다. */
        template <typename T>
        [[nodiscard]] static uint64 getSchemaHash()
        {
            return requireCodec<T>()._pComputeSchemaHash();
        }

        /** @brief 코덱을 찾습니다. 없거나 의존 타입이 적격이 아니면 nullptr 이고 @p outReason 에 필드와 이유를 적습니다(등록 API 의 기동 확인). */
        template <typename T>
        [[nodiscard]] static const FixedCodec* findCodec( string& outReason )
        {
            static TypeLookupCache s_cache;
            const TypeInfo*        pType = s_cache.find( typeFqn<T>() );
            if ( pType == nullptr )
            {
                outReason = "type is not registered";
                return nullptr;
            }
            return findCodec( *pType, outReason );
        }

        [[nodiscard]] static const FixedCodec* findCodec( const TypeInfo& type, string& outReason );

        // ------------------------------------------------------------------------------
        // 2) 생성 코드가 부르는 것
        // ------------------------------------------------------------------------------
        /** @brief 다른 타입(기반, 펼치지 못한 중첩)의 적격 코덱을 찾습니다. 등록되지 않았거나 적격이 아니면 nullptr 입니다. */
        [[nodiscard]] static const FixedCodec* findNestedCodec( const TypeLookupCache& cache, const hashed_string& fqn );
        /**
         * @brief 생성 코드가 다른 타입의 코덱을 얻습니다. 진입점이 `findCodec` 으로 의존을 확인했으므로 늘 있습니다.
         * @details 없으면(확인한 뒤 모듈이 내려간 경우) 이유를 남기고 프로세스를 멈춥니다. 대신 쓸 코덱을 돌려주지 않습니다.
         */
        static const FixedCodec& requireNestedCodec( const TypeLookupCache& cache, const hashed_string& fqn );
        /** @brief 정규 문자열과 중첩 타입 해시로 스키마 해시(FNV-1a 64)를 계산합니다. @p version 은 보통 `kVersion` 입니다. */
        [[nodiscard]] static uint64 computeSchemaHash( uint32 version, string_view canonical, std::initializer_list<uint64> listNestedHash );

        /** @brief 로그에 쓰는 이름입니다. */
        static const utf8* toString( FixedReadResult result ) noexcept;

    private:
        template <typename T>
        static const FixedCodec& requireCodec()
        {
            string            reason;
            const FixedCodec* pCodec = findCodec<T>( reason );
            if ( pCodec == nullptr )
                stopForMissingCodec( typeFqn<T>(), reason );
            return *pCodec;
        }

        /** @brief 코덱이 없다는 오류를 남기고 프로세스를 멈춥니다. 모든 구성에서 돌아오지 않습니다. */
        [[noreturn]] static void             stopForMissingCodec( const hashed_string& fqn, const string& reason );
        [[nodiscard]] static bool            writeWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer );
        [[nodiscard]] static FixedReadResult readWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject );

        // 스키마 해시 머리 없이 필드만 쓰고 읽는다. 같은 스키마임을 핸드셰이크가 확인한 채널(`NetMessageChannel`) 안에서만 뜻이 있으므로 그 클래스만 부른다 —
        // 다른 길로 머리 없는 바이트를 만들면 다른 빌드의 바이트를 거절할 자리가 사라진다(ReflectionFormats.md 3.10).
        friend class NetMessageChannel;
        template <typename T>
        [[nodiscard]] static bool writeBody( const T& object, BitWriter& writer )
        {
            return writeBodyWithCodec( requireCodec<T>(), &object, writer );
        }
        template <typename T>
        [[nodiscard]] static FixedReadResult readBody( BitReader& reader, T& inoutObject )
        {
            const FixedCodec&     codec     = requireCodec<T>();
            T                     temporary = inoutObject;
            const FixedReadResult result    = readBodyWithCodec( codec, reader, &temporary );
            if ( result == FixedReadResult::Ok )
                inoutObject = std::move( temporary );
            return result;
        }
        [[nodiscard]] static bool            writeBodyWithCodec( const FixedCodec& codec, const void* pObject, BitWriter& writer );
        [[nodiscard]] static FixedReadResult readBodyWithCodec( const FixedCodec& codec, BitReader& reader, void* pObject );
    };
} // namespace sw

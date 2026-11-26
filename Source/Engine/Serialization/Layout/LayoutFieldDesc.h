/**
 * @file LayoutFieldDesc.h
 * @brief 레이아웃 스키마의 필드 서술자 표입니다. 생성기가 타입마다 상수 초기화 표를 내고, 공용 해석기(`LayoutSerializer`)가 그 표로 쓰고 · 읽고 · 비교합니다.
 * @details 언리얼 `FRepLayout`(클래스마다 프로퍼티 표를 지어 해석하는 복제 직렬화)과 유니티 `SerializedObject` 자리입니다.
 *          본문은 해석기 하나이고 타입마다 남는 것은 표와 정규 문자열이다.
 *
 *          - 표는 평평한 배열이다. 묶음(최상위 필드, 펼친 중첩 타입, 수학 타입, 컨테이너 원소)의 자식은 이어진 칸이고 `_firstChild` · `_childCount` 로 가리킨다.
 *          - 오프셋은 바로 위 묶음의 주소 기준이다(최상위는 객체, 원소는 그 원소). 비트필드는 주소가 없어 생성 접근 함수가 바로 위 객체를 받는다.
 *          - 범위가 필드 이름이면(`Min = _halfSize`) 그 필드가 사는 범위 깊이(0 이 최상위, 펼친 중첩 타입마다 하나 더)와 그 안 오프셋 · 수 타입을 싣는다.
 *            해석기가 범위 깊이마다 객체 주소를 쥐고 내려가 읽는다. 앞선 필드만 범위가 될 수 있어(생성기 검사) 읽기에서도 이미 읽은 값이다.
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 메모리에 놓인 수의 꼴입니다. 정수 필드 · 열거형 기반 타입 · 범위 필드가 씁니다. */
    enum class LayoutScalarType : uint8
    {
        Int8,
        Int16,
        Int32,
        Int64,
        UInt8,
        UInt16,
        UInt32,
        UInt64,
        Float32,
        Float64,
    };

    /** @brief @p T 의 수 꼴입니다. 열거형은 기반 타입으로 봅니다. 크기와 부호로 고르므로 `char` · `long` 도 같은 폭의 정수와 같은 값 규칙입니다. */
    template <typename TDecl>
    constexpr LayoutScalarType getLayoutScalarType() noexcept
    {
        using T = std::remove_cv_t<TDecl>;
        if constexpr ( std::is_enum_v<T> )
        {
            return getLayoutScalarType<std::underlying_type_t<T>>();
        }
        else if constexpr ( std::is_same_v<T, float32> )
        {
            return LayoutScalarType::Float32;
        }
        else if constexpr ( std::is_same_v<T, float64> )
        {
            return LayoutScalarType::Float64;
        }
        else
        {
            static_assert( std::is_integral_v<T> && std::is_same_v<T, bool> == false, "layout scalar field must be an integer, float32 or float64" );
            if constexpr ( sizeof( T ) == 1 )
                return std::is_signed_v<T> ? LayoutScalarType::Int8 : LayoutScalarType::UInt8;
            else if constexpr ( sizeof( T ) == 2 )
                return std::is_signed_v<T> ? LayoutScalarType::Int16 : LayoutScalarType::UInt16;
            else if constexpr ( sizeof( T ) == 4 )
                return std::is_signed_v<T> ? LayoutScalarType::Int32 : LayoutScalarType::UInt32;
            else
                return std::is_signed_v<T> ? LayoutScalarType::Int64 : LayoutScalarType::UInt64;
        }
    }
} // namespace sw

namespace sw
{
    /** @brief 범위 한쪽 끝입니다. 없음, 숫자(생성기가 C++ 식으로 낸 값), 앞선 필드 이름 셋 중 하나입니다. */
    struct LayoutBound
    {
        enum class Kind : uint8
        {
            None,
            Literal,
            Field,
        };

        int64            _integer{ 0 };     ///< 정수 자리의 숫자입니다
        float64          _float{ 0.0 };     ///< 실수 자리의 숫자입니다. `Step` 자리면 float32 로 바꾼 값을 넓힌 것이라 되돌려도 같습니다
        size_t           _fieldOffset{ 0 }; ///< 필드 이름이면 그 범위 객체 안 오프셋입니다
        uint32           _scopeDepth{ 0 };  ///< 필드 이름이면 그 필드가 사는 범위 깊이입니다(0 이 최상위)
        Kind             _kind{ Kind::None };
        LayoutScalarType _fieldType{ LayoutScalarType::Int32 };
        /// @brief `Min = -_halfSize` 처럼 부호를 바꿉니다(그 필드 타입의 C++ 단항 `-` 규칙 그대로).
        bool _bNegate{ false }; //

        static constexpr LayoutBound none() noexcept { return LayoutBound{}; }

        static constexpr LayoutBound integer( const int64 value ) noexcept
        {
            LayoutBound bound{};
            bound._kind    = Kind::Literal;
            bound._integer = value;
            return bound;
        }

        static constexpr LayoutBound real( const float64 value ) noexcept
        {
            LayoutBound bound{};
            bound._kind  = Kind::Literal;
            bound._float = value;
            return bound;
        }

        static constexpr LayoutBound field( const uint32 scopeDepth, const size_t offset, const LayoutScalarType type, const bool bNegate ) noexcept
        {
            LayoutBound bound{};
            bound._kind        = Kind::Field;
            bound._scopeDepth  = scopeDepth;
            bound._fieldOffset = offset;
            bound._fieldType   = type;
            bound._bNegate     = bNegate;
            return bound;
        }

        constexpr bool isPresent() const noexcept { return _kind != Kind::None; }
    };
} // namespace sw

namespace sw
{
    /** @brief 컨테이너(`vector` · `array` · `T[N]`)를 타입 없이 다루는 함수 묶음입니다. 컨테이너 타입마다 정적 객체 하나입니다(`LayoutVectorOpsOf` · `LayoutArrayOpsOf`). */
    struct LayoutSequenceOps
    {
        size_t ( *_pSize )( const void* pSequence );
        void ( *_pResize )( void* pSequence, size_t count ); ///< 고정 길이면 nullptr 입니다
        const void* ( *_pData )( const void* pSequence );
        void* ( *_pWritableData )( void* pSequence );
        size_t _elementSize;
    };

    /** @brief 컨테이너 @p C 의 원소 타입입니다. 생성 코드가 원소의 수 꼴 · 크기를 이 타입으로 구합니다. */
    template <typename C>
    using LayoutElementType = std::remove_cv_t<std::remove_reference_t<decltype( *std::begin( std::declval<C&>() ) )>>;

    /** @brief 가변 길이 시퀀스(`vector`)의 함수 묶음입니다. */
    template <typename C>
    struct LayoutVectorOpsOf
    {
        static size_t      size( const void* pSequence ) { return static_cast<const C*>( pSequence )->size(); }
        static void        resize( void* pSequence, const size_t count ) { static_cast<C*>( pSequence )->resize( count ); }
        static const void* data( const void* pSequence ) { return static_cast<const C*>( pSequence )->data(); }
        static void*       getWritableData( void* pSequence ) { return static_cast<C*>( pSequence )->data(); }

        static constexpr LayoutSequenceOps kInstance{ &size, &resize, &data, &getWritableData, sizeof( LayoutElementType<C> ) };
    };

    /** @brief @p C 가 `EnumArray<E, T>` 인지입니다. 길이를 `std::tuple_size` 대신 `kSize` 로 읽습니다. */
    template <typename C>
    struct IsEnumArray : std::false_type
    {
    };
    template <typename E, typename T>
    struct IsEnumArray<EnumArray<E, T>> : std::true_type
    {
    };

    /** @brief 고정 길이 배열(`T[N]` · `array<T, N>` · `EnumArray<E, T>`)의 함수 묶음입니다. 길이는 타입이 정합니다. */
    template <typename C>
    struct LayoutArrayOpsOf
    {
        static constexpr size_t computeCount() noexcept
        {
            if constexpr ( std::is_array_v<C> )
                return std::extent_v<C>;
            else if constexpr ( IsEnumArray<C>::value )
                return C::kSize; // 열거자 수(`EnumArray`)
            else
                return std::tuple_size<C>::value;
        }
        static size_t      size( const void* ) { return computeCount(); }
        static const void* data( const void* pSequence ) { return std::data( *static_cast<const C*>( pSequence ) ); }
        static void*       getWritableData( void* pSequence ) { return std::data( *static_cast<C*>( pSequence ) ); }

        static constexpr LayoutSequenceOps kInstance{ &size, nullptr, &data, &getWritableData, sizeof( LayoutElementType<C> ) };
    };
} // namespace sw

namespace sw
{
    /** @brief 서술자 한 칸이 무엇을 어떻게 인코딩하는지입니다. 생성기의 `LayoutValueKind` 에서 옵니다(같은 인코딩 표). */
    enum class LayoutFieldKind : uint8
    {
        Bool,         ///< `bool` 한 비트입니다
        BitFieldBool, ///< 1 비트 비트필드입니다. 주소가 없어 생성 접근 함수(`_pGetBit` · `_pSetBit`)가 바로 위 객체를 받습니다
        Integer,      ///< 범위 둘이 다 있으면 범위 비트, 아니면 가변 길이입니다(`LayoutFieldCodec`)
        Float32,      ///< 범위가 없으면 원본 비트, 있으면 허용 범위 검사, `Step` 이면 양자화입니다
        Float64,
        Enum,     ///< [최소, 최대] 범위 비트와 등록 열거자 검사입니다
        FlagEnum, ///< 기반 타입 전체 폭과 등록 비트 마스크 검사입니다
        String,
        Bitset,
        Vector,      ///< 개수(`MaxCount`)와 원소들입니다. 원소는 `_firstChild` 한 칸입니다
        StaticArray, ///< 개수 없이 원소 N 개입니다
        Math,        ///< `float3` 같은 수학 타입입니다. 자식이 멤버마다 Float32 입니다
        Inline,      ///< 같은 헤더의 중첩 타입을 펼친 것입니다. 범위 이름의 깊이가 하나 늘어납니다
        Dependency,  ///< 다른 타입(또는 자기 타입)의 스키마입니다. `_dependencyIndex` 가 그 의존입니다
    };

    /**
     * @brief 생성 코드가 타입마다 내는 스키마 결합형 서술자 표(`static constexpr ::sw::LayoutFieldDesc s_arrLayoutField[]`)의 한 칸입니다.
     * @details 생성 코드는 아래 `make*` 함수로만 칸을 짓는다 — 종류마다 쓰는 칸이 달라 차례로 적는 초기화는 한 칸이 밀려도 컴파일된다.
     */
    struct LayoutFieldDesc
    {
        using GetBitFunction = bool ( * )( const void* pOwner );
        using SetBitFunction = void ( * )( void* pOwner, bool bValue );

        /** @brief 자기 타입의 스키마을 가리키는 `_dependencyIndex` 입니다(`vector<Self>`). */
        static constexpr uint32 kSelfDependency = 0xFFFFFFFFu;

        size_t                   _offset{ 0 };
        LayoutBound              _min{};
        LayoutBound              _max{};
        const LayoutSequenceOps* _pSequenceOps{ nullptr };
        const int64*             _pEnumValue{ nullptr }; ///< Enum 의 등록 열거자 값(`Count` 제외)
        GetBitFunction           _pGetBit{ nullptr };
        SetBitFunction           _pSetBit{ nullptr };
        uint64                   _enumMask{ 0 }; ///< FlagEnum 의 등록 비트 합입니다
        float32                  _step{ 0.0f };  ///< 0 보다 크면 `Step` 양자화입니다
        uint32                   _maxCount{ 0 }; ///< String · Bitset · Vector 의 `MaxCount` 입니다
        uint32                   _enumValueCount{ 0 };
        uint32                   _firstChild{ 0 };
        uint32                   _childCount{ 0 };
        uint32                   _dependencyIndex{ 0 };
        LayoutFieldKind          _kind{ LayoutFieldKind::Bool };
        LayoutScalarType         _numberType{ LayoutScalarType::Int32 }; ///< Integer 의 타입, Enum · FlagEnum 의 기반 타입입니다

        static constexpr LayoutFieldDesc makeBool( const size_t offset ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind   = LayoutFieldKind::Bool;
            desc._offset = offset;
            return desc;
        }

        static constexpr LayoutFieldDesc makeBitFieldBool( const GetBitFunction pGetBit, const SetBitFunction pSetBit ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind    = LayoutFieldKind::BitFieldBool;
            desc._pGetBit = pGetBit;
            desc._pSetBit = pSetBit;
            return desc;
        }

        static constexpr LayoutFieldDesc makeInteger( const size_t offset, const LayoutScalarType type, const LayoutBound& minBound, const LayoutBound& maxBound ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind       = LayoutFieldKind::Integer;
            desc._offset     = offset;
            desc._numberType = type;
            desc._min        = minBound;
            desc._max        = maxBound;
            return desc;
        }

        /** @brief @p step 이 0 이면 양자화하지 않습니다. 양자화면 범위 숫자는 float32 로 바꾼 값입니다(생성기가 그 식으로 낸다). */
        static constexpr LayoutFieldDesc makeFloat32( const size_t offset, const LayoutBound& minBound, const LayoutBound& maxBound, const float32 step ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind   = LayoutFieldKind::Float32;
            desc._offset = offset;
            desc._min    = minBound;
            desc._max    = maxBound;
            desc._step   = step;
            return desc;
        }

        static constexpr LayoutFieldDesc makeFloat64( const size_t offset, const LayoutBound& minBound, const LayoutBound& maxBound ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind   = LayoutFieldKind::Float64;
            desc._offset = offset;
            desc._min    = minBound;
            desc._max    = maxBound;
            return desc;
        }

        static constexpr LayoutFieldDesc makeEnum( const size_t offset, const LayoutScalarType underlying, const int64 minValue, const int64 maxValue, const int64* pValue,
                                                   const uint32 valueCount ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind           = LayoutFieldKind::Enum;
            desc._offset         = offset;
            desc._numberType     = underlying;
            desc._min            = LayoutBound::integer( minValue );
            desc._max            = LayoutBound::integer( maxValue );
            desc._pEnumValue     = pValue;
            desc._enumValueCount = valueCount;
            return desc;
        }

        static constexpr LayoutFieldDesc makeFlagEnum( const size_t offset, const LayoutScalarType underlying, const uint64 mask ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind       = LayoutFieldKind::FlagEnum;
            desc._offset     = offset;
            desc._numberType = underlying;
            desc._enumMask   = mask;
            return desc;
        }

        static constexpr LayoutFieldDesc makeString( const size_t offset, const uint32 maxCount ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind     = LayoutFieldKind::String;
            desc._offset   = offset;
            desc._maxCount = maxCount;
            return desc;
        }

        static constexpr LayoutFieldDesc makeBitset( const size_t offset, const uint32 maxCount ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind     = LayoutFieldKind::Bitset;
            desc._offset   = offset;
            desc._maxCount = maxCount;
            return desc;
        }

        static constexpr LayoutFieldDesc makeVector( const size_t offset, const uint32 maxCount, const LayoutSequenceOps* pOps, const uint32 elementIndex ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind         = LayoutFieldKind::Vector;
            desc._offset       = offset;
            desc._maxCount     = maxCount;
            desc._pSequenceOps = pOps;
            desc._firstChild   = elementIndex;
            desc._childCount   = 1;
            return desc;
        }

        static constexpr LayoutFieldDesc makeStaticArray( const size_t offset, const LayoutSequenceOps* pOps, const uint32 elementIndex ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind         = LayoutFieldKind::StaticArray;
            desc._offset       = offset;
            desc._pSequenceOps = pOps;
            desc._firstChild   = elementIndex;
            desc._childCount   = 1;
            return desc;
        }

        static constexpr LayoutFieldDesc makeMath( const size_t offset, const uint32 firstChild, const uint32 childCount ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind       = LayoutFieldKind::Math;
            desc._offset     = offset;
            desc._firstChild = firstChild;
            desc._childCount = childCount;
            return desc;
        }

        static constexpr LayoutFieldDesc makeInline( const size_t offset, const uint32 firstChild, const uint32 childCount ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind       = LayoutFieldKind::Inline;
            desc._offset     = offset;
            desc._firstChild = firstChild;
            desc._childCount = childCount;
            return desc;
        }

        static constexpr LayoutFieldDesc makeDependency( const size_t offset, const uint32 dependencyIndex ) noexcept
        {
            LayoutFieldDesc desc{};
            desc._kind            = LayoutFieldKind::Dependency;
            desc._offset          = offset;
            desc._dependencyIndex = dependencyIndex;
            return desc;
        }
    };
} // namespace sw

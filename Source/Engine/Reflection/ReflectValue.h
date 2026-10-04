/**
 * @file ReflectValue.h
 * @brief 리플렉션 호출 · 이벤트가 주고받는 "타입 이름이 붙은 값" 하나와, 인자 타입마다 값을 바꾸는 표입니다.
 * @details `TaskValue` 는 담긴 타입을 모릅니다(크기만 압니다). 이름으로 함수를 부르는 쪽(콘솔 명령 · 비주얼 스크립팅 · 기믹 배선)은
 *          인자의 C++ 타입을 모르고 값을 넘기므로, 값에 타입 이름을 붙이고(`ReflectValue`) 인자 자리마다 그 이름을 보고 바꿉니다
 *          (`ReflectTypeOps::_pConvert`). 같은 타입이면 그대로, 숫자끼리는 형 변환, 글이면 그 타입으로 읽습니다.
 */
#pragma once
#include "Core/Task/TaskTypes.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    struct float2;
    struct float3;
    struct float4;
    struct float4x4;
    struct quaternion;
    struct TagID;

    class ComponentHandle;
    class GameObjectHandle;
    class SlotHandle;
} // namespace sw

namespace sw
{
    /**
     * @brief `ReflectBuiltins.xxx` 의 C++ 타입 칸을 값이 실제로 쓰는 타입으로 바꿉니다.
     * @details 문자열 줄의 칸은 `std::string` 이지만 프로퍼티 · 생성 호출기(`args.get<string>`)는 `sw::string` 입니다
     *          (STL 컨테이너 빌드가 아니면 둘은 다른 타입이고 크기도 다릅니다).
     */
    template <typename T>
    struct ReflectBuiltinCppType
    {
        using Type = T;
    };

    template <>
    struct ReflectBuiltinCppType<std::string>
    {
        using Type = string;
    };

    template <typename T>
    using ReflectBuiltinCppTypeT = typename ReflectBuiltinCppType<T>::Type;
} // namespace sw

namespace sw
{
    /** @brief 내장 타입의 표 안 자리입니다. 순서는 `ReflectBuiltins.xxx` 의 줄 순서입니다. */
    struct ReflectBuiltinIndex
    {
        enum Value : uint8
        {
#define SW_REFLECT_BUILTIN_TYPE( Canon, ... ) k##Canon,
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
            kCount
        };
    };
} // namespace sw

namespace sw
{
    /** @brief C++ 타입 T 의 내장 타입 자리입니다. 내장 타입이 아니면 -1 입니다. */
    template <typename T>
    struct ReflectBuiltinOf
    {
        static constexpr int32 kIndex = -1;
    };

#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... )                 \
    template <>                                                        \
    struct ReflectBuiltinOf<ReflectBuiltinCppTypeT<CppType>>           \
    {                                                                  \
        static constexpr int32 kIndex = ReflectBuiltinIndex::k##Canon; \
    };
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
} // namespace sw

namespace sw
{
    struct ReflectValue;

    /** @brief 리플렉션 값 · 내장 타입 표를 다루는 도우미입니다. 템플릿이 아닌 몸통은 `ReflectValue.cpp` 에 있습니다. */
    struct SW_API ReflectValueUtil
    {
        /** @brief 내장 타입 자리의 정규 이름(`int32` · `string` · `float3` …)입니다. */
        static const hashed_string& getBuiltinTypeName( int32 builtinIndex );
        /** @brief 정규 이름의 내장 타입 자리입니다. 내장 타입이 아니면 -1 입니다(별칭은 `TypeRegistry` 로 정규 이름으로 바꿔 찾습니다). */
        static int32 findBuiltinIndex( const hashed_string& typeName );
        /**
         * @brief @p in 을 @p builtinIndex 내장 타입의 값으로 바꿉니다 — 숫자끼리 형 변환, 글은 읽기, 숫자는 글로.
         * @return 바꿀 수 없으면 false(@p outValue 는 그대로)
         */
        [[nodiscard]] static bool convertToBuiltin( const ReflectValue& in, int32 builtinIndex, TaskValue& outValue );
        /**
         * @brief @p in 을 열거형 @p enumTypeName 의 값으로 읽습니다 — 같은 열거형 · 정수(알려진 값만) · 이름 글.
         * @return 읽지 못하면 false
         */
        [[nodiscard]] static bool readEnumValue( const ReflectValue& in, const hashed_string& enumTypeName, int64& outValue );
        /** @brief 사람이 읽는 글입니다. 내장 타입 · 열거형만 값이 보이고 나머지는 `<타입 이름>` 입니다. */
        static string formatText( const ReflectValue& value );
        /**
         * @brief 기본 인자의 C++ 식(`1.0f` · `"idle"` · `Mode::Fast` · `{}`)을 변환이 읽는 글로 바꿉니다.
         * @param outbDefaultConstruct `{}` · `T()` · `T{}` 처럼 "기본값으로 만든다" 는 식이면 true(그때 글은 비어 있다)
         */
        static string normalizeDefaultLiteral( string_view cppExpression, bool& outbDefaultConstruct );
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 T 의 정규 이름입니다. 같은 T 는 어디서 물어도 같은 이름입니다.
     * @details 내장 타입은 표의 이름(`int32`), 나머지(열거형 · 구조체)는 컴파일러가 알려 주는 FQN(`sw::DamageEvent`)입니다.
     *          `ReflectValue::make<T>` 와 인자 변환이 이 이름 하나로 "같은 타입인가" 를 답합니다.
     */
    template <typename T>
    hashed_string getReflectTypeName()
    {
        if constexpr ( ReflectBuiltinOf<T>::kIndex >= 0 )
            return ReflectValueUtil::getBuiltinTypeName( ReflectBuiltinOf<T>::kIndex );
        else
            return typeFqn<T>();
    }
} // namespace sw

namespace sw
{
    /**
     * @brief 타입 이름이 붙은 값 하나입니다. 리플렉션 호출의 인자 · 반환, 이벤트의 인자로 오갑니다.
     * @details 값은 그 타입의 `TaskValue` 입니다(인자 하나를 꺼낼 때 생성 호출기가 `args.get<T>` 로 그대로 읽는 꼴). 이름이 비면
     *          "값 없음" 이고, 인자 자리에 넘기면 그 인자 타입의 기본값이 됩니다.
     */
    struct SW_API ReflectValue
    {
        hashed_string _typeName; ///< 담긴 값의 정규 타입 이름(`getReflectTypeName<T>`). 비면 값이 없다
        TaskValue     _value;

        ReflectValue() = default;
        ReflectValue( hashed_string typeName, TaskValue value );

        /** @brief T 의 값을 담습니다. */
        template <typename T>
        static ReflectValue make( T value )
        {
            using Stored = std::decay_t<T>;
            return ReflectValue( getReflectTypeName<Stored>(), TaskValue{ Stored( std::move( value ) ) } );
        }

        /** @brief 글을 담습니다(`string`). 인자 자리에서 그 타입으로 읽힙니다 — 콘솔 명령 · 기본 인자가 이 길입니다. */
        static ReflectValue makeText( string_view text );

        /** @brief 값이 없으면 true 입니다. */
        bool isEmpty() const { return _typeName.empty() || _value.hasValue() == false; }

        /** @brief 담긴 값이 T 면 그 자리, 아니면 nullptr 입니다. 바꾸지 않습니다(바꿔 읽으려면 `ReflectTypeOps::_pConvert`). */
        template <typename T>
        const T* findValue() const
        {
            if ( _value.hasValue() == false || _typeName != getReflectTypeName<T>() )
                return nullptr;
            return _value.getPtr<T>();
        }

        /** @brief 사람이 읽는 글입니다(`ReflectValueUtil::formatText`). */
        string toText() const { return ReflectValueUtil::formatText( *this ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 타입 하나의 이름과 값 변환입니다. 함수 인자 · 반환 · 이벤트 인자의 자리마다 하나를 가리킵니다. */
    struct ReflectTypeOps
    {
        /** @brief 정규 이름(`getReflectTypeName<T>`)입니다. */
        hashed_string ( *_pGetTypeName )();
        /**
         * @brief @p in 을 이 타입의 값으로 바꿔 @p outValue 에 씁니다. 같은 타입이면 그대로, 아니면 바꿀 수 있을 때만(내장 타입 · 열거형).
         *        빈 @p in 은 이 타입의 기본값입니다.
         * @return 바꿀 수 없으면 false
         */
        bool ( *_pConvert )( const ReflectValue& in, TaskValue& outValue );
    };
} // namespace sw

namespace sw
{
    /** @brief T 의 `ReflectTypeOps` 입니다. 생성 코드가 인자 타입마다 `&ReflectTypeOpsOf<T>::kOps` 를 적습니다. */
    template <typename T>
    struct ReflectTypeOpsOf
    {
        [[nodiscard]] static bool convert( const ReflectValue& in, TaskValue& outValue )
        {
            if ( in.isEmpty() )
            {
                if constexpr ( std::is_default_constructible_v<T> )
                {
                    outValue = TaskValue{ T{} };
                    return true;
                }
                else
                    return false;
            }
            if ( in._typeName == getReflectTypeName<T>() )
            {
                outValue = in._value;
                return true;
            }
            if constexpr ( ReflectBuiltinOf<T>::kIndex >= 0 )
                return ReflectValueUtil::convertToBuiltin( in, ReflectBuiltinOf<T>::kIndex, outValue );
            else if constexpr ( std::is_enum_v<T> )
            {
                int64 raw{ 0 };
                if ( ReflectValueUtil::readEnumValue( in, getReflectTypeName<T>(), raw ) == false )
                    return false;
                outValue = TaskValue{ static_cast<T>( raw ) };
                return true;
            }
            else
                return false;
        }

        static constexpr ReflectTypeOps kOps{ &getReflectTypeName<T>, &convert };
    };
} // namespace sw

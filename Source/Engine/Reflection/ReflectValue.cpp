#include "pch.h"

#include "Engine/Reflection/ReflectValue.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/ComponentHandle.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/SlotHandle.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringUtil.h"
#include "Core/String/TagID.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct ReflectValueInternal
        {
            /** @brief 바꾸기 전에 원본 값을 한 꼴로 읽어 둔 것입니다 — 숫자거나 글이거나 둘 다 아니거나. */
            struct SourceValue
            {
                string  _text;
                float64 _floatValue{ 0.0 };
                int64   _intValue{ 0 };
                bool    _bNumber{ false };
                bool    _bInteger{ false }; ///< 정수 타입에서 왔다(또는 소수부가 없다) — 정수 타입으로 바꿀 수 있다
                bool    _bText{ false };
            };

            static const vector<hashed_string>& getNameTable()
            {
                static const vector<hashed_string> s_listName{
#define SW_REFLECT_BUILTIN_TYPE( Canon, ... ) hashed_string( #Canon ),
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                };
                return s_listName;
            }

            static void setNumber( SourceValue& out, const float64 value, const bool bInteger, const int64 intValue )
            {
                out._bNumber    = true;
                out._floatValue = value;
                out._intValue   = intValue;
                out._bInteger   = bInteger;
            }

            /** @brief 내장 타입 값 하나를 숫자 · 글로 읽습니다. 숫자도 글도 아닌 타입(벡터 · 핸들 · 행렬)은 글로만 씁니다. */
            template <typename T>
            static void readSource( const void* pValue, SourceValue& out )
            {
                const T& value = *static_cast<const T*>( pValue );
                if constexpr ( std::is_same_v<T, bool> )
                    setNumber( out, value ? 1.0 : 0.0, true, value ? 1 : 0 );
                else if constexpr ( std::is_same_v<T, atomic<bool>> )
                {
                    const bool bValue = value.load( std::memory_order_relaxed );
                    setNumber( out, bValue ? 1.0 : 0.0, true, bValue ? 1 : 0 );
                }
                else if constexpr ( std::is_integral_v<T> )
                    setNumber( out, static_cast<float64>( value ), true, static_cast<int64>( value ) );
                else if constexpr ( std::is_floating_point_v<T> )
                {
                    const float64 asDouble  = static_cast<float64>( value );
                    const bool    bIntegral = asDouble == static_cast<float64>( static_cast<int64>( asDouble ) );
                    setNumber( out, asDouble, bIntegral, static_cast<int64>( asDouble ) );
                }
                else if constexpr ( std::is_same_v<T, string> )
                {
                    out._text  = value;
                    out._bText = true;
                }
                else if constexpr ( std::is_same_v<T, hashed_string> )
                {
                    out._text  = string( value.c_str() );
                    out._bText = true;
                }
                else if constexpr ( std::is_same_v<T, TagID> )
                {
                    out._text  = value.isValid() ? string( value.getString() ) : string();
                    out._bText = true;
                }
            }

            /** @brief 글을 숫자로도 읽어 둡니다 — 숫자 인자에 글을 넘기는 길(콘솔 · 기본 인자). */
            static void parseTextAsNumber( SourceValue& source )
            {
                const string_view trimmed = StringUtil::trim( source._text );
                int64             intValue{ 0 };
                float64           floatValue{ 0.0 };
                bool              boolValue{ false };
                if ( StringUtil::parseInt64( trimmed, intValue ) )
                    setNumber( source, static_cast<float64>( intValue ), true, intValue );
                else if ( StringUtil::parseDouble( trimmed, floatValue ) )
                    setNumber( source, floatValue, floatValue == static_cast<float64>( static_cast<int64>( floatValue ) ), static_cast<int64>( floatValue ) );
                else if ( StringUtil::tryParseBool( trimmed, boolValue ) )
                    setNumber( source, boolValue ? 1.0 : 0.0, true, boolValue ? 1 : 0 );
            }

            /** @brief `1, 2, 3` · `(1 2 3)` 꼴을 실수 @p count 개로 읽습니다. */
            [[nodiscard]] static bool parseFloatList( string_view text, float32* pOutValue, const uint32 count )
            {
                uint32 readCount = 0;
                size_t position  = 0;
                while ( position < text.size() && readCount < count )
                {
                    while ( position < text.size() && ( text[position] == ' ' || text[position] == ',' || text[position] == '(' || text[position] == ')' ||
                                                        text[position] == '\t' ) )
                    {
                        ++position;
                    }
                    size_t end = position;
                    while ( end < text.size() && text[end] != ' ' && text[end] != ',' && text[end] != ')' && text[end] != '\t' )
                    {
                        ++end;
                    }
                    if ( end == position )
                        break;
                    if ( StringUtil::parseFloat( text.substr( position, end - position ), pOutValue[readCount] ) == false )
                        return false;
                    ++readCount;
                    position = end;
                }
                return readCount == count;
            }

            /** @brief 원본 값을 내장 타입 T 로 바꿉니다. 숫자 범위를 넘거나 소수를 정수로 바꾸는 것은 거절합니다(조용히 깎지 않게). */
            template <typename T>
            [[nodiscard]] static bool convertSource( const SourceValue& source, TaskValue& outValue )
            {
                if constexpr ( std::is_same_v<T, bool> || std::is_same_v<T, atomic<bool>> )
                {
                    bool bValue = false;
                    if ( source._bNumber )
                        bValue = source._floatValue != 0.0;
                    else if ( source._bText == false || StringUtil::tryParseBool( source._text, bValue ) == false )
                        return false;
                    outValue = TaskValue{ T( bValue ) };
                    return true;
                }
                else if constexpr ( std::is_integral_v<T> )
                {
                    if ( source._bNumber == false || source._bInteger == false )
                        return false;
                    const bool bInRange = static_cast<float64>( std::numeric_limits<T>::lowest() ) <= source._floatValue &&
                                          source._floatValue <= static_cast<float64>( std::numeric_limits<T>::max() );
                    if ( bInRange == false )
                        return false;
                    outValue = TaskValue{ static_cast<T>( source._intValue ) };
                    return true;
                }
                else if constexpr ( std::is_floating_point_v<T> )
                {
                    if ( source._bNumber == false )
                        return false;
                    outValue = TaskValue{ static_cast<T>( source._floatValue ) };
                    return true;
                }
                else if constexpr ( std::is_same_v<T, string> )
                {
                    if ( source._bText )
                        outValue = TaskValue{ string( source._text ) };
                    else if ( source._bNumber )
                        outValue = TaskValue{ source._bInteger ? to_string( source._intValue ) : to_string( source._floatValue ) };
                    else
                        return false;
                    return true;
                }
                else if constexpr ( std::is_same_v<T, hashed_string> )
                {
                    if ( source._bText == false )
                        return false;
                    outValue = TaskValue{ hashed_string( source._text.c_str() ) };
                    return true;
                }
                else if constexpr ( std::is_same_v<T, TagID> )
                {
                    if ( source._bText == false )
                        return false;
                    outValue = TaskValue{ source._text.empty() ? TagID{} : TagID::request( source._text ) };
                    return true;
                }
                else if constexpr ( std::is_same_v<T, float2> || std::is_same_v<T, float3> || std::is_same_v<T, float4> )
                {
                    constexpr uint32 kCount = sizeof( T ) / sizeof( float32 );
                    float32          arrValue[4]{};
                    if ( source._bText == false || parseFloatList( source._text, arrValue, kCount ) == false )
                        return false;
                    outValue = TaskValue{ T( arrValue ) };
                    return true;
                }
                else if constexpr ( std::is_same_v<T, quaternion> )
                {
                    float32 arrValue[4]{};
                    if ( source._bText == false || parseFloatList( source._text, arrValue, 4 ) == false )
                        return false;
                    outValue = TaskValue{ quaternion( arrValue[0], arrValue[1], arrValue[2], arrValue[3] ) };
                    return true;
                }
                else
                {
                    // 행렬 · 핸들은 같은 타입으로만 받는다(같은 타입은 부르는 쪽이 이미 그대로 넘겼다).
                    (void)source;
                    (void)outValue;
                    return false;
                }
            }

            /** @brief 내장 타입 값 하나를 사람이 읽는 글로 씁니다. */
            template <typename T>
            static string formatBuiltin( const void* pValue )
            {
                const T&                              value = *static_cast<const T*>( pValue );
                fixed_string<constant::kMaxBuffer512> buf;
                if constexpr ( std::is_same_v<T, bool> )
                    return value ? "true" : "false";
                else if constexpr ( std::is_same_v<T, atomic<bool>> )
                    return value.load( std::memory_order_relaxed ) ? "true" : "false";
                else if constexpr ( std::is_arithmetic_v<T> )
                    formatstring( buf.data(), buf.capacity(), "%#", value );
                else if constexpr ( std::is_same_v<T, string> )
                    return value;
                else if constexpr ( std::is_same_v<T, hashed_string> )
                    return string( value.c_str() );
                else if constexpr ( std::is_same_v<T, TagID> )
                    return value.isValid() ? string( value.getString() ) : string();
                else if constexpr ( std::is_same_v<T, float2> )
                    formatstring( buf.data(), buf.capacity(), "%#, %#", value._x, value._y );
                else if constexpr ( std::is_same_v<T, float3> )
                    formatstring( buf.data(), buf.capacity(), "%#, %#, %#", value._x, value._y, value._z );
                else if constexpr ( std::is_same_v<T, float4> || std::is_same_v<T, quaternion> )
                    formatstring( buf.data(), buf.capacity(), "%#, %#, %#, %#", value._x, value._y, value._z, value._w );
                else if constexpr ( std::is_same_v<T, SlotHandle> )
                    formatstring( buf.data(), buf.capacity(), "%#/%#", value.index(), value.generation() );
                else if constexpr ( std::is_same_v<T, ComponentHandle> )
                    formatstring( buf.data(), buf.capacity(), "%#/%#", value.objectId(), value.componentId() );
                else if constexpr ( std::is_same_v<T, GameObjectHandle> )
                    formatstring( buf.data(), buf.capacity(), "%#", value.objectId() );
                else
                    formatstring( buf.data(), buf.capacity(), "<%#>", "float4x4" );
                return string( buf.c_str() );
            }

            using ReadSourceFn    = void ( * )( const void* pValue, SourceValue& out );
            using ConvertSourceFn = bool ( * )( const SourceValue& source, TaskValue& outValue );
            using FormatFn        = string ( * )( const void* pValue );

            /** @brief 내장 타입 줄마다 읽기 · 바꾸기 · 쓰기입니다. 순서는 `ReflectBuiltinIndex` 와 같습니다(같은 파일을 같은 순서로 include). */
            struct BuiltinRow
            {
                ReadSourceFn    _pReadSource;
                ConvertSourceFn _pConvertSource;
                FormatFn        _pFormat;
            };

            static constexpr BuiltinRow kArrBuiltinRow[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... )                                               \
    { &readSource<ReflectBuiltinCppTypeT<CppType>>, &convertSource<ReflectBuiltinCppTypeT<CppType>>, \
      &formatBuiltin<ReflectBuiltinCppTypeT<CppType>> },
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
            };
            static_assert( std::size( kArrBuiltinRow ) == ReflectBuiltinIndex::kCount, "kArrBuiltinRow must have one row per ReflectBuiltins.xxx type" );

            /** @brief 담긴 값을 숫자 · 글로 읽습니다. 내장 타입 · 열거형(이름 글)만 읽힙니다. */
            [[nodiscard]] static bool readSourceValue( const ReflectValue& in, SourceValue& out )
            {
                const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( in._typeName );
                if ( 0 <= builtinIndex )
                {
                    kArrBuiltinRow[builtinIndex]._pReadSource( in._value.getRawPtr(), out );
                    if ( out._bText && out._bNumber == false )
                        parseTextAsNumber( out );
                    return out._bText || out._bNumber;
                }
                const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( in._typeName );
                if ( pEnumInfo == nullptr || in._value.getStoredSize() != pEnumInfo->_size )
                    return false;
                const int64 raw = pEnumInfo->readValueFromMemory( in._value.getRawPtr() );
                setNumber( out, static_cast<float64>( raw ), true, raw );
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    ReflectValue::ReflectValue( hashed_string typeName, TaskValue value )
        : _typeName{ typeName }
        , _value{ std::move( value ) }
    {
    }

    ReflectValue ReflectValue::makeText( string_view text )
    {
        return ReflectValue( ReflectValueUtil::getBuiltinTypeName( ReflectBuiltinIndex::kstring ), TaskValue{ string( text ) } );
    }

    const hashed_string& ReflectValueUtil::getBuiltinTypeName( const int32 builtinIndex )
    {
        const vector<hashed_string>& listName = ReflectValueInternal::getNameTable();
        SW_ASSERT( 0 <= builtinIndex && static_cast<size_t>( builtinIndex ) < listName.size() );
        return listName[static_cast<size_t>( builtinIndex )];
    }

    int32 ReflectValueUtil::findBuiltinIndex( const hashed_string& typeName )
    {
        if ( typeName.empty() )
            return -1;
        const vector<hashed_string>& listName = ReflectValueInternal::getNameTable();
        for ( size_t index = 0; index < listName.size(); ++index )
        {
            if ( listName[index] == typeName )
                return static_cast<int32>( index );
        }
        // 별칭(`int` · `sw::string` …)은 레지스트리의 정규 이름으로 다시 찾는다.
        const hashed_string canonical = engine::getTypeRegistry().canonicalTypeName( typeName );
        if ( canonical == typeName )
            return -1;
        for ( size_t index = 0; index < listName.size(); ++index )
        {
            if ( listName[index] == canonical )
                return static_cast<int32>( index );
        }
        return -1;
    }

    bool ReflectValueUtil::convertToBuiltin( const ReflectValue& in, const int32 builtinIndex, TaskValue& outValue )
    {
        if ( builtinIndex < 0 || ReflectBuiltinIndex::kCount <= builtinIndex )
            return false;
        ReflectValueInternal::SourceValue source;
        if ( ReflectValueInternal::readSourceValue( in, source ) == false )
            return false;
        return ReflectValueInternal::kArrBuiltinRow[builtinIndex]._pConvertSource( source, outValue );
    }

    bool ReflectValueUtil::readEnumValue( const ReflectValue& in, const hashed_string& enumTypeName, int64& outValue )
    {
        const EnumInfo* pTarget = engine::getTypeRegistry().findEnum( enumTypeName );
        if ( pTarget == nullptr )
            return false;
        // 다른 열거형의 값은 받지 않는다 — 숫자가 같아도 뜻이 다르다.
        if ( ReflectValueUtil::findBuiltinIndex( in._typeName ) < 0 )
        {
            const EnumInfo* pSource = engine::getTypeRegistry().findEnum( in._typeName );
            if ( pSource != pTarget || in._value.getStoredSize() != pTarget->_size )
                return false;
            outValue = pTarget->readValueFromMemory( in._value.getRawPtr() );
            return true;
        }
        ReflectValueInternal::SourceValue source;
        if ( ReflectValueInternal::readSourceValue( in, source ) == false )
            return false;
        // 이름이든 숫자든 `tryParseText` 한 길로 — 알려진 값만 받는다(모르는 이름 · 숫자는 false).
        if ( source._bText )
            return pTarget->tryParseText( source._text, outValue );
        return source._bInteger && pTarget->tryParseText( to_string( source._intValue ), outValue );
    }

    string ReflectValueUtil::formatText( const ReflectValue& value )
    {
        if ( value.isEmpty() )
            return string();
        const int32 builtinIndex = findBuiltinIndex( value._typeName );
        if ( 0 <= builtinIndex )
            return ReflectValueInternal::kArrBuiltinRow[builtinIndex]._pFormat( value._value.getRawPtr() );
        const EnumInfo* pEnumInfo = engine::getTypeRegistry().findEnum( value._typeName );
        if ( pEnumInfo != nullptr && value._value.getStoredSize() == pEnumInfo->_size )
        {
            const int64 raw   = pEnumInfo->readValueFromMemory( value._value.getRawPtr() );
            const utf8* pName = pEnumInfo->valueToCString( raw );
            return pName != nullptr ? string( pName ) : to_string( raw );
        }
        string text = "<";
        text += value._typeName.c_str();
        text += ">";
        return text;
    }

    string ReflectValueUtil::normalizeDefaultLiteral( string_view cppExpression, bool& outbDefaultConstruct )
    {
        outbDefaultConstruct = false;
        string_view text     = StringUtil::trim( cppExpression );
        // `{}` · `T{}` · `T()` — 기본값으로 만든다.
        const bool bBraceInit = StringUtil::endsWith( text, "{}" ) || StringUtil::endsWith( text, "()" );
        if ( bBraceInit )
        {
            outbDefaultConstruct = true;
            return string();
        }
        // 문자열 리터럴(`"idle"` · `u8"idle"`)은 따옴표를 벗기고 이스케이프를 푼다.
        const size_t quote = text.find( '"' );
        if ( quote != string_view::npos && text.size() >= quote + 2 && text.back() == '"' )
        {
            string unescaped;
            for ( size_t charIndex = quote + 1; charIndex + 1 < text.size(); ++charIndex )
            {
                utf8 character = text[charIndex];
                if ( character == '\\' && charIndex + 2 < text.size() )
                {
                    character = text[++charIndex];
                    if ( character == 'n' )
                        character = '\n';
                    else if ( character == 't' )
                        character = '\t';
                }
                unescaped.push_back( character );
            }
            return unescaped;
        }
        // 실수 접미사(`1.5f`)는 숫자 읽기가 모른다.
        if ( text.size() > 1 && ( text.back() == 'f' || text.back() == 'F' ) && ( ( '0' <= text[text.size() - 2] && text[text.size() - 2] <= '9' ) || text[text.size() - 2] == '.' ) )
            text.remove_suffix( 1 );
        // 열거자(`Mode::Fast`)는 끝 이름만 — 열거형 읽기가 이름으로 찾는다.
        const size_t scope = text.rfind( "::" );
        if ( scope != string_view::npos )
            text = text.substr( scope + 2 );
        return string( text );
    }
} // namespace sw

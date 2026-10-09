#include "pch.h"

#include "Engine/UI/Binding/UiBindingValue.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

namespace sw
{
    namespace
    {
        struct UiBindingValueInternal
        {
            /** @brief 내장 숫자 타입 칸을 실수로 읽습니다. 숫자 타입이 아니면 false 입니다. */
            [[nodiscard]] static bool tryReadNumber( int32 builtinIndex, const void* pValue, float64& outNumber )
            {
                switch ( builtinIndex )
                {
                    case ReflectBuiltinIndex::kint8:
                    {
                        outNumber = *static_cast<const int8*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint16:
                    {
                        outNumber = *static_cast<const int16*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint32:
                    {
                        outNumber = *static_cast<const int32*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint64:
                    {
                        outNumber = static_cast<float64>( *static_cast<const int64*>( pValue ) );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint8:
                    {
                        outNumber = *static_cast<const uint8*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint16:
                    {
                        outNumber = *static_cast<const uint16*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint32:
                    {
                        outNumber = *static_cast<const uint32*>( pValue );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint64:
                    {
                        outNumber = static_cast<float64>( *static_cast<const uint64*>( pValue ) );
                        return true;
                    }
                    case ReflectBuiltinIndex::kfloat32:
                    {
                        outNumber = static_cast<float64>( *static_cast<const float32*>( pValue ) );
                        return true;
                    }
                    case ReflectBuiltinIndex::kfloat64:
                    {
                        outNumber = *static_cast<const float64*>( pValue );
                        return true;
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

            /** @brief 정수 칸에 반올림해 씁니다. 바뀌었으면 true 입니다. */
            template <typename IntegerType>
            [[nodiscard]] static bool writeInteger( void* pValue, float64 number )
            {
                const IntegerType rounded = static_cast<IntegerType>( MathUtil::round( number ) );
                IntegerType&      field   = *static_cast<IntegerType*>( pValue );
                if ( field == rounded )
                    return false;
                field = rounded;
                return true;
            }

            /** @brief 실수 칸에 씁니다. 바뀌었으면 true 입니다. */
            template <typename FloatType>
            [[nodiscard]] static bool writeFloat( void* pValue, float64 number )
            {
                const FloatType value = static_cast<FloatType>( number );
                FloatType&      field = *static_cast<FloatType*>( pValue );
                if ( field == value )
                    return false;
                field = value;
                return true;
            }

            /** @brief 내장 숫자 타입 칸에 씁니다. 숫자 타입이 아니면 false(쓰지 않음)이고 @p outbChanged 는 그대로입니다. */
            [[nodiscard]] static bool tryWriteNumber( int32 builtinIndex, void* pValue, float64 number, bool& outbChanged )
            {
                switch ( builtinIndex )
                {
                    case ReflectBuiltinIndex::kint8:
                    {
                        outbChanged = writeInteger<int8>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint16:
                    {
                        outbChanged = writeInteger<int16>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint32:
                    {
                        outbChanged = writeInteger<int32>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kint64:
                    {
                        outbChanged = writeInteger<int64>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint8:
                    {
                        outbChanged = writeInteger<uint8>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint16:
                    {
                        outbChanged = writeInteger<uint16>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint32:
                    {
                        outbChanged = writeInteger<uint32>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kuint64:
                    {
                        outbChanged = writeInteger<uint64>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kfloat32:
                    {
                        outbChanged = writeFloat<float32>( pValue, number );
                        return true;
                    }
                    case ReflectBuiltinIndex::kfloat64:
                    {
                        outbChanged = writeFloat<float64>( pValue, number );
                        return true;
                    }
                    default:
                    {
                        return false;
                    }
                }
            }

            /** @brief 정수 타입 칸인가입니다. */
            static bool isIntegerBuiltin( int32 builtinIndex )
            {
                return builtinIndex != ReflectBuiltinIndex::kfloat32 && builtinIndex != ReflectBuiltinIndex::kfloat64;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UiBindingValue UiBindingValue::makeBool( bool bValue )
    {
        UiBindingValue value{};
        value._kind   = UiBindingValueKind::Bool;
        value._bValue = bValue;
        return value;
    }

    UiBindingValue UiBindingValue::makeNumber( float64 number, bool bInteger )
    {
        UiBindingValue value{};
        value._kind     = UiBindingValueKind::Number;
        value._number   = number;
        value._bInteger = bInteger;
        return value;
    }

    UiBindingValue UiBindingValue::makeText( string_view text )
    {
        UiBindingValue value{};
        value._kind = UiBindingValueKind::Text;
        value._text = string( text );
        return value;
    }

    bool UiBindingValue::toBool() const
    {
        switch ( _kind )
        {
            case UiBindingValueKind::Bool:
                return _bValue;
            case UiBindingValueKind::Number:
                return _number != 0.0;
            case UiBindingValueKind::Text:
            case UiBindingValueKind::Other:
                return _text.empty() == false;
            case UiBindingValueKind::None:
                return false;
        }
        return false;
    }

    float64 UiBindingValue::toNumber() const
    {
        switch ( _kind )
        {
            case UiBindingValueKind::Bool:
                return _bValue ? 1.0 : 0.0;
            case UiBindingValueKind::Number:
                return _number;
            case UiBindingValueKind::Text:
            case UiBindingValueKind::Other:
            case UiBindingValueKind::None:
                return 0.0;
        }
        return 0.0;
    }

    string UiBindingValue::toText() const
    {
        switch ( _kind )
        {
            case UiBindingValueKind::Bool:
            {
                return _bValue ? "true" : "false";
            }
            case UiBindingValueKind::Number:
            {
                utf8 arrBuffer[constant::kMaxBuffer64];
                if ( _bInteger )
                    StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), static_cast<int64>( MathUtil::round( _number ) ) );
                else
                    StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), _number );
                return arrBuffer;
            }
            case UiBindingValueKind::Text:
            case UiBindingValueKind::Other:
            {
                return _text;
            }
            case UiBindingValueKind::None:
            {
                return {};
            }
        }
        return {};
    }
} // namespace sw

namespace sw
{
    const hashed_string& UiPropertyPath::getLeafTypeName() const
    {
        return getLeafProperty()._typeName;
    }

    void* UiPropertyPath::findLeafOwner( void* pRoot ) const
    {
        void* pOwner = pRoot;
        for ( uint32 index = 0; index + 1 < static_cast<uint32>( _listProperty.size() ) && pOwner != nullptr; ++index )
        {
            pOwner = _listProperty[index]->getRawPtr( pOwner );
        }
        return pOwner;
    }

    const void* UiPropertyPath::findLeafOwner( const void* pRoot ) const
    {
        return findLeafOwner( const_cast<void*>( pRoot ) );
    }
} // namespace sw

namespace sw
{
    UiBindingValueKind UiBindingValueUtil::classifyType( const hashed_string& typeName )
    {
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( typeName );
        if ( builtinIndex == ReflectBuiltinIndex::kbool )
            return UiBindingValueKind::Bool;
        if ( builtinIndex == ReflectBuiltinIndex::kstring || builtinIndex == ReflectBuiltinIndex::khashed_string )
            return UiBindingValueKind::Text;
        // 내장 표의 숫자 줄은 int8 .. float64 가 이어져 있다(ReflectBuiltins.xxx 의 줄 순서).
        const bool bNumber = ReflectBuiltinIndex::kint8 <= builtinIndex && builtinIndex <= ReflectBuiltinIndex::kfloat64;
        return bNumber ? UiBindingValueKind::Number : UiBindingValueKind::Other;
    }

    bool UiBindingValueUtil::canConvert( UiBindingValueKind from, UiBindingValueKind to )
    {
        if ( from == UiBindingValueKind::None || to == UiBindingValueKind::None )
            return false;
        if ( to == UiBindingValueKind::Text )
            return true;
        const bool bFromScalar = from == UiBindingValueKind::Bool || from == UiBindingValueKind::Number;
        const bool bToScalar   = to == UiBindingValueKind::Bool || to == UiBindingValueKind::Number;
        return bFromScalar && bToScalar;
    }

    const utf8* UiBindingValueUtil::getKindName( UiBindingValueKind kind )
    {
        switch ( kind )
        {
            case UiBindingValueKind::None:
                return "none";
            case UiBindingValueKind::Bool:
                return "bool";
            case UiBindingValueKind::Number:
                return "number";
            case UiBindingValueKind::Text:
                return "text";
            case UiBindingValueKind::Other:
                return "struct";
        }
        return "none";
    }

    bool UiBindingValueUtil::resolvePath( const TypeInfo& type, string_view path, UiPropertyPath& outPath, string& outError )
    {
        outPath                 = UiPropertyPath{};
        const TypeInfo* pType   = &type;
        size_t          begin   = 0;
        bool            bIsLast = false;
        while ( bIsLast == false )
        {
            const size_t      dot     = path.find( '.', begin );
            const string_view segment = path.substr( begin, dot == string_view::npos ? string_view::npos : dot - begin );
            bIsLast                   = dot == string_view::npos;
            if ( pType == nullptr )
            {
                outError = "'" + string( path ) + "' goes through a field that is not a reflected struct";
                return false;
            }
            const PropertyInfo* pProperty = pType->findPropertyInHierarchy( hashed_string( segment ) );
            if ( pProperty == nullptr )
            {
                outError = "'" + string( pType->_name.c_str() ) + "' has no property '" + string( segment ) + "'";
                return false;
            }
            if ( pProperty->_bIsContainer == SW_TRUE && bIsLast == false )
            {
                outError = "'" + string( path ) + "' goes through a container";
                return false;
            }
            outPath._listProperty.push_back( pProperty );
            if ( bIsLast == false )
            {
                pType = engine::getTypeRegistry().findType( pProperty->_typeName );
                begin = dot + 1;
            }
        }
        const PropertyInfo& leaf = outPath.getLeafProperty();
        outPath._kind            = leaf._bIsContainer == SW_TRUE ? UiBindingValueKind::Other : classifyType( leaf._typeName );
        return true;
    }

    UiBindingValue UiBindingValueUtil::readValue( const UiPropertyPath& path, const void* pRoot )
    {
        const void* pOwner = path.isValid() ? path.findLeafOwner( pRoot ) : nullptr;
        if ( pOwner == nullptr )
            return {};
        const PropertyInfo& leaf = path.getLeafProperty();
        if ( leaf._bIsContainer == SW_TRUE )
            return {};
        if ( leaf._bIsBitField == SW_TRUE )
            return UiBindingValue::makeBool( leaf.getValue<bool>( pOwner ) );
        const void* pValue       = leaf.getRawPtr( pOwner );
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( leaf._typeName );
        switch ( path._kind )
        {
            case UiBindingValueKind::Bool:
            {
                return UiBindingValue::makeBool( *static_cast<const bool*>( pValue ) );
            }
            case UiBindingValueKind::Number:
            {
                float64 number = 0.0;
                (void)UiBindingValueInternal::tryReadNumber( builtinIndex, pValue, number ); // Number 로 분류한 칸이라 실패하지 않는다(실패면 0)
                return UiBindingValue::makeNumber( number, UiBindingValueInternal::isIntegerBuiltin( builtinIndex ) );
            }
            case UiBindingValueKind::Text:
            {
                if ( builtinIndex == ReflectBuiltinIndex::khashed_string )
                    return UiBindingValue::makeText( static_cast<const hashed_string*>( pValue )->c_str() );
                return UiBindingValue::makeText( *static_cast<const string*>( pValue ) );
            }
            case UiBindingValueKind::Other:
            {
                UiBindingValue value = UiBindingValue::makeText( SerializerUtil::formatPropertyText( leaf, pOwner, SerializeContext::getDefault() ) );
                value._kind          = UiBindingValueKind::Other;
                return value;
            }
            case UiBindingValueKind::None:
            {
                return {};
            }
        }
        return {};
    }

    bool UiBindingValueUtil::writeValue( const UiPropertyPath& path, void* pRoot, const UiBindingValue& value )
    {
        void* pOwner = path.isValid() ? path.findLeafOwner( pRoot ) : nullptr;
        if ( pOwner == nullptr || value._kind == UiBindingValueKind::None )
            return false;
        const PropertyInfo& leaf = path.getLeafProperty();
        if ( leaf._bIsContainer == SW_TRUE )
            return false;
        if ( leaf._bIsBitField == SW_TRUE )
        {
            const bool bValue = value.toBool();
            if ( leaf.getValue<bool>( pOwner ) == bValue )
                return false;
            leaf.setValue<bool>( static_cast<uint8*>( pOwner ), bValue );
            return true;
        }
        void*       pValue       = leaf.getRawPtr( pOwner );
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( leaf._typeName );
        switch ( path._kind )
        {
            case UiBindingValueKind::Bool:
            {
                if ( value._kind != UiBindingValueKind::Bool && value._kind != UiBindingValueKind::Number )
                    return false;
                bool&      field  = *static_cast<bool*>( pValue );
                const bool bValue = value.toBool();
                if ( field == bValue )
                    return false;
                field = bValue;
                return true;
            }
            case UiBindingValueKind::Number:
            {
                if ( value._kind != UiBindingValueKind::Bool && value._kind != UiBindingValueKind::Number )
                    return false;
                bool bChanged = false;
                // 숫자 칸이 아니면 쓰지 않고 bChanged 는 거짓으로 남는다
                (void)UiBindingValueInternal::tryWriteNumber( builtinIndex, pValue, value.toNumber(), bChanged );
                return bChanged;
            }
            case UiBindingValueKind::Text:
            {
                const string text = value.toText();
                if ( builtinIndex == ReflectBuiltinIndex::khashed_string )
                {
                    hashed_string& field = *static_cast<hashed_string*>( pValue );
                    if ( field.isEqual( hashed_string( text ), NameCase::CaseSensitive ) )
                        return false;
                    field = hashed_string( text );
                    return true;
                }
                string& field = *static_cast<string*>( pValue );
                if ( field == text )
                    return false;
                field = text;
                return true;
            }
            case UiBindingValueKind::Other:
            {
                if ( value._kind != UiBindingValueKind::Other && value._kind != UiBindingValueKind::Text )
                    return false;
                const SerializeContext& context = SerializeContext::getDefault();
                if ( SerializerUtil::formatPropertyText( leaf, pOwner, context ) == value._text )
                    return false;
                return SerializerUtil::applyPropertyText( leaf, pOwner, value._text, context );
            }
            case UiBindingValueKind::None:
            {
                return false;
            }
        }
        return false;
    }

    bool UiBindingValueUtil::copyValue( const UiPropertyPath& sourcePath, const void* pSourceRoot, const UiPropertyPath& targetPath, void* pTargetRoot )
    {
        UiBindingValue value = readValue( sourcePath, pSourceRoot );
        if ( value._kind == UiBindingValueKind::Other && targetPath._kind == UiBindingValueKind::Text )
            value._kind = UiBindingValueKind::Text;
        return writeValue( targetPath, pTargetRoot, value );
    }
} // namespace sw

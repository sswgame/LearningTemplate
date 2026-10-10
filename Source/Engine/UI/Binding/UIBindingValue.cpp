#include "pch.h"

#include "Engine/UI/Binding/UIBindingValue.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"

namespace sw
{
    namespace
    {
        struct UIBindingValueInternal
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
    UIBindingValue UIBindingValue::makeBool( bool bValue )
    {
        UIBindingValue value{};
        value._kind   = UIBindingValueKind::Bool;
        value._bValue = bValue;
        return value;
    }

    UIBindingValue UIBindingValue::makeNumber( float64 number, bool bInteger )
    {
        UIBindingValue value{};
        value._kind     = UIBindingValueKind::Number;
        value._number   = number;
        value._bInteger = bInteger;
        return value;
    }

    UIBindingValue UIBindingValue::makeText( string_view text )
    {
        UIBindingValue value{};
        value._kind = UIBindingValueKind::Text;
        value._text = string( text );
        return value;
    }

    bool UIBindingValue::toBool() const
    {
        switch ( _kind )
        {
            case UIBindingValueKind::Bool:
                return _bValue;
            case UIBindingValueKind::Number:
                return _number != 0.0;
            case UIBindingValueKind::Text:
            case UIBindingValueKind::Other:
                return _text.empty() == false;
            case UIBindingValueKind::None:
                return false;
        }
        return false;
    }

    float64 UIBindingValue::toNumber() const
    {
        switch ( _kind )
        {
            case UIBindingValueKind::Bool:
                return _bValue ? 1.0 : 0.0;
            case UIBindingValueKind::Number:
                return _number;
            case UIBindingValueKind::Text:
            case UIBindingValueKind::Other:
            case UIBindingValueKind::None:
                return 0.0;
        }
        return 0.0;
    }

    string UIBindingValue::toText() const
    {
        switch ( _kind )
        {
            case UIBindingValueKind::Bool:
            {
                return _bValue ? "true" : "false";
            }
            case UIBindingValueKind::Number:
            {
                utf8 arrBuffer[constant::kMaxBuffer64];
                if ( _bInteger )
                    StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), static_cast<int64>( MathUtil::round( _number ) ) );
                else
                    StringUtil::formatNumber( arrBuffer, sizeof( arrBuffer ), _number );
                return arrBuffer;
            }
            case UIBindingValueKind::Text:
            case UIBindingValueKind::Other:
            {
                return _text;
            }
            case UIBindingValueKind::None:
            {
                return {};
            }
        }
        return {};
    }
} // namespace sw

namespace sw
{
    const hashed_string& UIPropertyPath::getLeafTypeName() const
    {
        return getLeafProperty()._typeName;
    }

    void* UIPropertyPath::findLeafOwner( void* pRoot ) const
    {
        void* pOwner = pRoot;
        for ( uint32 index = 0; index + 1 < static_cast<uint32>( _listProperty.size() ) && pOwner != nullptr; ++index )
        {
            pOwner = _listProperty[index]->getRawPtr( pOwner );
        }
        return pOwner;
    }

    const void* UIPropertyPath::findLeafOwner( const void* pRoot ) const
    {
        return findLeafOwner( const_cast<void*>( pRoot ) );
    }
} // namespace sw

namespace sw
{
    UIBindingValueKind UIBindingValueUtil::classifyType( const hashed_string& typeName )
    {
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( typeName );
        if ( builtinIndex == ReflectBuiltinIndex::kbool )
            return UIBindingValueKind::Bool;
        if ( builtinIndex == ReflectBuiltinIndex::kstring || builtinIndex == ReflectBuiltinIndex::khashed_string )
            return UIBindingValueKind::Text;
        // 내장 표의 숫자 줄은 int8 .. float64 가 이어져 있다(ReflectBuiltins.xxx 의 줄 순서).
        const bool bNumber = ReflectBuiltinIndex::kint8 <= builtinIndex && builtinIndex <= ReflectBuiltinIndex::kfloat64;
        return bNumber ? UIBindingValueKind::Number : UIBindingValueKind::Other;
    }

    bool UIBindingValueUtil::canConvert( UIBindingValueKind from, UIBindingValueKind to )
    {
        if ( from == UIBindingValueKind::None || to == UIBindingValueKind::None )
            return false;
        if ( to == UIBindingValueKind::Text )
            return true;
        const bool bFromScalar = from == UIBindingValueKind::Bool || from == UIBindingValueKind::Number;
        const bool bToScalar   = to == UIBindingValueKind::Bool || to == UIBindingValueKind::Number;
        return bFromScalar && bToScalar;
    }

    const utf8* UIBindingValueUtil::getKindName( UIBindingValueKind kind )
    {
        switch ( kind )
        {
            case UIBindingValueKind::None:
                return "none";
            case UIBindingValueKind::Bool:
                return "bool";
            case UIBindingValueKind::Number:
                return "number";
            case UIBindingValueKind::Text:
                return "text";
            case UIBindingValueKind::Other:
                return "struct";
        }
        return "none";
    }

    bool UIBindingValueUtil::resolvePath( const TypeInfo& type, string_view path, UIPropertyPath& outPath, string& outError )
    {
        outPath                 = UIPropertyPath{};
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
        outPath._kind            = leaf._bIsContainer == SW_TRUE ? UIBindingValueKind::Other : classifyType( leaf._typeName );
        return true;
    }

    UIBindingValue UIBindingValueUtil::readValue( const UIPropertyPath& path, const void* pRoot )
    {
        const void* pOwner = path.isValid() ? path.findLeafOwner( pRoot ) : nullptr;
        if ( pOwner == nullptr )
            return {};
        const PropertyInfo& leaf = path.getLeafProperty();
        if ( leaf._bIsContainer == SW_TRUE )
            return {};
        if ( leaf._bIsBitField == SW_TRUE )
            return UIBindingValue::makeBool( leaf.getValue<bool>( pOwner ) );
        const void* pValue       = leaf.getRawPtr( pOwner );
        const int32 builtinIndex = ReflectValueUtil::findBuiltinIndex( leaf._typeName );
        switch ( path._kind )
        {
            case UIBindingValueKind::Bool:
            {
                return UIBindingValue::makeBool( *static_cast<const bool*>( pValue ) );
            }
            case UIBindingValueKind::Number:
            {
                float64 number = 0.0;
                (void)UIBindingValueInternal::tryReadNumber( builtinIndex, pValue, number ); // Number 로 분류한 칸이라 실패하지 않는다(실패면 0)
                return UIBindingValue::makeNumber( number, UIBindingValueInternal::isIntegerBuiltin( builtinIndex ) );
            }
            case UIBindingValueKind::Text:
            {
                if ( builtinIndex == ReflectBuiltinIndex::khashed_string )
                    return UIBindingValue::makeText( static_cast<const hashed_string*>( pValue )->c_str() );
                return UIBindingValue::makeText( *static_cast<const string*>( pValue ) );
            }
            case UIBindingValueKind::Other:
            {
                UIBindingValue value = UIBindingValue::makeText( SerializerUtil::formatPropertyText( leaf, pOwner, SerializeContext::getDefault() ) );
                value._kind          = UIBindingValueKind::Other;
                return value;
            }
            case UIBindingValueKind::None:
            {
                return {};
            }
        }
        return {};
    }

    bool UIBindingValueUtil::writeValue( const UIPropertyPath& path, void* pRoot, const UIBindingValue& value )
    {
        void* pOwner = path.isValid() ? path.findLeafOwner( pRoot ) : nullptr;
        if ( pOwner == nullptr || value._kind == UIBindingValueKind::None )
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
            case UIBindingValueKind::Bool:
            {
                if ( value._kind != UIBindingValueKind::Bool && value._kind != UIBindingValueKind::Number )
                    return false;
                bool&      field  = *static_cast<bool*>( pValue );
                const bool bValue = value.toBool();
                if ( field == bValue )
                    return false;
                field = bValue;
                return true;
            }
            case UIBindingValueKind::Number:
            {
                if ( value._kind != UIBindingValueKind::Bool && value._kind != UIBindingValueKind::Number )
                    return false;
                bool bChanged = false;
                // 숫자 칸이 아니면 쓰지 않고 bChanged 는 거짓으로 남는다
                (void)UIBindingValueInternal::tryWriteNumber( builtinIndex, pValue, value.toNumber(), bChanged );
                return bChanged;
            }
            case UIBindingValueKind::Text:
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
            case UIBindingValueKind::Other:
            {
                if ( value._kind != UIBindingValueKind::Other && value._kind != UIBindingValueKind::Text )
                    return false;
                const SerializeContext& context = SerializeContext::getDefault();
                if ( SerializerUtil::formatPropertyText( leaf, pOwner, context ) == value._text )
                    return false;
                return SerializerUtil::applyPropertyText( leaf, pOwner, value._text, context );
            }
            case UIBindingValueKind::None:
            {
                return false;
            }
        }
        return false;
    }

    bool UIBindingValueUtil::copyValue( const UIPropertyPath& sourcePath, const void* pSourceRoot, const UIPropertyPath& targetPath, void* pTargetRoot )
    {
        UIBindingValue value = readValue( sourcePath, pSourceRoot );
        if ( value._kind == UIBindingValueKind::Other && targetPath._kind == UIBindingValueKind::Text )
            value._kind = UIBindingValueKind::Text;
        return writeValue( targetPath, pTargetRoot, value );
    }
} // namespace sw

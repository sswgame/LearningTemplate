#include "pch.h"

#include "Engine/UI/Binding/UIBindingValue.h"

#include "Core/Common/Defines.h"
#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
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
            /** @brief 숫자 내장 타입 한 줄의 읽기 · 쓰기입니다. */
            struct NumberOps
            {
                float64 ( *pRead )( const void* pValue );
                bool ( *pWrite )( void* pValue, float64 number ); ///< 바뀌었으면 true 입니다.
                bool bInteger;
            };

            template <typename NumberType>
            static float64 readNumber( const void* pValue )
            {
                return static_cast<float64>( *static_cast<const NumberType*>( pValue ) );
            }

            /** @brief 정수는 반올림해 쓰고 실수는 그대로 씁니다. 바뀌었으면 true 입니다. */
            template <typename NumberType>
            [[nodiscard]] static bool writeNumber( void* pValue, float64 number )
            {
                NumberType value{};
                if constexpr ( std::is_integral_v<NumberType> )
                    value = static_cast<NumberType>( MathUtil::round( number ) );
                else
                    value = static_cast<NumberType>( number );
                NumberType& field = *static_cast<NumberType*>( pValue );
                if ( field == value )
                    return false;
                field = value;
                return true;
            }

            template <typename NumberType>
            static constexpr NumberOps makeNumberOps()
            {
                return NumberOps{ &readNumber<NumberType>, &writeNumber<NumberType>, std::is_integral_v<NumberType> };
            }

            /** @brief 숫자 내장 타입 표입니다. 줄 순서는 내장 타입 표의 kint8 .. kfloat64 순서와 같다(아래 static_assert 가 확인한다). */
            static const NumberOps kArrNumberOps[];

            /** @brief 내장 숫자 타입의 표 줄입니다. 숫자 타입이 아니면 nullptr 입니다. */
            [[nodiscard]] static const NumberOps* findNumberOps( int32 builtinIndex )
            {
                if ( builtinIndex < ReflectBuiltinIndex::kint8 || builtinIndex > ReflectBuiltinIndex::kfloat64 )
                    return nullptr;
                return &kArrNumberOps[builtinIndex - ReflectBuiltinIndex::kint8];
            }

            /** @brief 종류 하나의 이름과 경로 필드 읽기 · 쓰기입니다. @p pValue 는 잎 필드 주소, @p builtinIndex 는 잎의 내장 타입 자리입니다. */
            struct KindOps
            {
                UIBindingValueKind kind; ///< 이 줄의 종류입니다. 줄 위치가 이 값과 같은지 static_assert 로 본다
                const utf8*        pName;
                UIBindingValue ( *pRead )( const PropertyInfo& leaf, const void* pOwner, const void* pValue, int32 builtinIndex );
                bool ( *pWrite )( const PropertyInfo& leaf, void* pOwner, void* pValue, int32 builtinIndex, const UIBindingValue& value ); ///< 바뀌었으면 true 입니다.
            };

            static UIBindingValue     readNone( const PropertyInfo&, const void*, const void*, int32 ) { return {}; }
            [[nodiscard]] static bool writeNone( const PropertyInfo&, void*, void*, int32, const UIBindingValue& ) { return false; }

            static UIBindingValue readBool( const PropertyInfo&, const void*, const void* pValue, int32 )
            {
                return UIBindingValue::makeBool( *static_cast<const bool*>( pValue ) );
            }
            [[nodiscard]] static bool writeBool( const PropertyInfo&, void*, void* pValue, int32, const UIBindingValue& value )
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

            static UIBindingValue readNumberKind( const PropertyInfo&, const void*, const void* pValue, int32 builtinIndex )
            {
                // Number 로 분류한 필드라 표 줄이 있다(없으면 0)
                const NumberOps* pOps = findNumberOps( builtinIndex );
                if ( pOps == nullptr )
                    return UIBindingValue::makeNumber( 0.0, false );
                return UIBindingValue::makeNumber( pOps->pRead( pValue ), pOps->bInteger );
            }
            [[nodiscard]] static bool writeNumberKind( const PropertyInfo&, void*, void* pValue, int32 builtinIndex, const UIBindingValue& value )
            {
                if ( value._kind != UIBindingValueKind::Bool && value._kind != UIBindingValueKind::Number )
                    return false;
                const NumberOps* pOps = findNumberOps( builtinIndex );
                return pOps != nullptr && pOps->pWrite( pValue, value.toNumber() );
            }

            static UIBindingValue readText( const PropertyInfo&, const void*, const void* pValue, int32 builtinIndex )
            {
                if ( builtinIndex == ReflectBuiltinIndex::khashed_string )
                    return UIBindingValue::makeText( static_cast<const hashed_string*>( pValue )->c_str() );
                return UIBindingValue::makeText( *static_cast<const string*>( pValue ) );
            }
            [[nodiscard]] static bool writeText( const PropertyInfo&, void*, void* pValue, int32 builtinIndex, const UIBindingValue& value )
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

            static UIBindingValue readOther( const PropertyInfo& leaf, const void* pOwner, const void*, int32 )
            {
                UIBindingValue value = UIBindingValue::makeText( SerializerUtil::formatPropertyText( leaf, ObjectAddress{ pOwner }, SerializeContext::getDefault( SerializePurpose::Authoring ) ) );
                value._kind          = UIBindingValueKind::Other;
                return value;
            }
            [[nodiscard]] static bool writeOther( const PropertyInfo& leaf, void* pOwner, void*, int32, const UIBindingValue& value )
            {
                if ( value._kind != UIBindingValueKind::Other && value._kind != UIBindingValueKind::Text )
                    return false;
                const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
                if ( SerializerUtil::formatPropertyText( leaf, ObjectAddress{ pOwner }, context ) == value._text )
                    return false;
                return SerializerUtil::applyPropertyText( leaf, MutableObjectAddress{ pOwner }, value._text, context );
            }

            /** @brief 종류 표입니다. 줄 순서는 `UIBindingValueKind` 의 값 순서와 같다. 종류를 더하면 여기에 한 줄을 더한다. */
            static const KindOps kArrKindOps[];

            /** @brief 종류의 줄입니다. 범위 밖 값(깨진 메모리 · 캐스트)은 'none' 줄로 떨어진다(이름 "none", 읽기 {}, 쓰기 false). */
            [[nodiscard]] static const KindOps& getKindOps( UIBindingValueKind kind )
            {
                const uint32 index = static_cast<uint32>( kind );
                return index < kUIBindingValueKindCount ? kArrKindOps[index] : kArrKindOps[static_cast<uint32>( UIBindingValueKind::None )];
            }

            /** @brief 종류 표의 줄마다 줄 위치가 그 줄의 종류 값과 같으면 true 입니다. 열거 순서가 바뀌어도 개수가 같으면 크기 검사만으로는 못 잡는다. */
            static constexpr bool areKindRowsInOrder();
        };

        constexpr UIBindingValueInternal::NumberOps UIBindingValueInternal::kArrNumberOps[] = {
            UIBindingValueInternal::makeNumberOps<int8>(),
            UIBindingValueInternal::makeNumberOps<int16>(),
            UIBindingValueInternal::makeNumberOps<int32>(),
            UIBindingValueInternal::makeNumberOps<int64>(),
            UIBindingValueInternal::makeNumberOps<uint8>(),
            UIBindingValueInternal::makeNumberOps<uint16>(),
            UIBindingValueInternal::makeNumberOps<uint32>(),
            UIBindingValueInternal::makeNumberOps<uint64>(),
            UIBindingValueInternal::makeNumberOps<float32>(),
            UIBindingValueInternal::makeNumberOps<float64>(),
        };
        static_assert( std::size( UIBindingValueInternal::kArrNumberOps ) == ReflectBuiltinIndex::kfloat64 - ReflectBuiltinIndex::kint8 + 1, "number table must cover kint8 .. kfloat64" );
        static_assert( ReflectBuiltinOf<int8>::kIndex == ReflectBuiltinIndex::kint8 && ReflectBuiltinOf<int16>::kIndex == ReflectBuiltinIndex::kint8 + 1 &&
                           ReflectBuiltinOf<int32>::kIndex == ReflectBuiltinIndex::kint8 + 2 && ReflectBuiltinOf<int64>::kIndex == ReflectBuiltinIndex::kint8 + 3 &&
                           ReflectBuiltinOf<uint8>::kIndex == ReflectBuiltinIndex::kint8 + 4 && ReflectBuiltinOf<uint16>::kIndex == ReflectBuiltinIndex::kint8 + 5 &&
                           ReflectBuiltinOf<uint32>::kIndex == ReflectBuiltinIndex::kint8 + 6 && ReflectBuiltinOf<uint64>::kIndex == ReflectBuiltinIndex::kint8 + 7 &&
                           ReflectBuiltinOf<float32>::kIndex == ReflectBuiltinIndex::kint8 + 8 && ReflectBuiltinOf<float64>::kIndex == ReflectBuiltinIndex::kint8 + 9,
                       "number table rows must follow the builtin table order" );

        constexpr UIBindingValueInternal::KindOps UIBindingValueInternal::kArrKindOps[] = {
            {  UIBindingValueKind::None,   "none",       &UIBindingValueInternal::readNone,       &UIBindingValueInternal::writeNone},
            {  UIBindingValueKind::Bool,   "bool",       &UIBindingValueInternal::readBool,       &UIBindingValueInternal::writeBool},
            {UIBindingValueKind::Number, "number", &UIBindingValueInternal::readNumberKind, &UIBindingValueInternal::writeNumberKind},
            {  UIBindingValueKind::Text,   "text",       &UIBindingValueInternal::readText,       &UIBindingValueInternal::writeText},
            { UIBindingValueKind::Other, "struct",      &UIBindingValueInternal::readOther,      &UIBindingValueInternal::writeOther},
        };
        static_assert( std::size( UIBindingValueInternal::kArrKindOps ) == kUIBindingValueKindCount, "kind table must have one row per UIBindingValueKind" );

        constexpr bool UIBindingValueInternal::areKindRowsInOrder()
        {
            for ( uint32 index = 0; index < kUIBindingValueKindCount; ++index )
            {
                if ( static_cast<uint32>( kArrKindOps[index].kind ) != index )
                    return false;
            }
            return true;
        }
        static_assert( UIBindingValueInternal::areKindRowsInOrder(), "kind table rows must follow the UIBindingValueKind value order" );
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
        // 내장 타입 테이블의 숫자 줄은 int8 .. float64 가 이어져 있다(ReflectBuiltins.xxx 의 줄 순서).
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
        return UIBindingValueInternal::getKindOps( kind ).pName;
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
                pType = engine::getSubsystem<TypeRegistry>().findType( pProperty->_typeName );
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
        return UIBindingValueInternal::getKindOps( path._kind ).pRead( leaf, pOwner, pValue, builtinIndex );
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
        return UIBindingValueInternal::getKindOps( path._kind ).pWrite( leaf, pOwner, pValue, builtinIndex, value );
    }

    bool UIBindingValueUtil::copyValue( const UIPropertyPath& sourcePath, const void* pSourceRoot, const UIPropertyPath& targetPath, void* pTargetRoot )
    {
        UIBindingValue value = readValue( sourcePath, pSourceRoot );
        if ( value._kind == UIBindingValueKind::Other && targetPath._kind == UIBindingValueKind::Text )
            value._kind = UIBindingValueKind::Text;
        return writeValue( targetPath, pTargetRoot, value );
    }
} // namespace sw

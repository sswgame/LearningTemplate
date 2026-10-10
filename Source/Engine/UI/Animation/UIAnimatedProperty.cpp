#include "pch.h"

#include "Engine/UI/Animation/UIAnimatedProperty.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Serialization/Base/SerializeContext.h"
#include "Engine/Serialization/Base/SerializerUtil.h"
#include "Engine/UI/Base/Widget.h"

namespace sw
{
    namespace
    {
        struct UIAnimatedPropertyInternal
        {
            /** @brief 실수 칸의 성분 수입니다. 실수 칸이 아니면 0 입니다. */
            static uint8 findComponentCount( int32 builtinIndex )
            {
                if ( builtinIndex == ReflectBuiltinIndex::kfloat32 || builtinIndex == ReflectBuiltinIndex::kfloat64 )
                    return 1;
                if ( builtinIndex == ReflectBuiltinIndex::kfloat2 )
                    return 2;
                if ( builtinIndex == ReflectBuiltinIndex::kfloat3 )
                    return 3;
                if ( builtinIndex == ReflectBuiltinIndex::kfloat4 )
                    return 4;
                return 0;
            }

            /** @brief 내장 실수 타입 값 @p pValue 를 성분으로 읽습니다. */
            static void readComponents( int32 builtinIndex, const void* pValue, float32 ( &outArrComponent )[4] )
            {
                if ( builtinIndex == ReflectBuiltinIndex::kfloat32 )
                {
                    outArrComponent[0] = *static_cast<const float32*>( pValue );
                }
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat64 )
                {
                    outArrComponent[0] = static_cast<float32>( *static_cast<const float64*>( pValue ) );
                }
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat2 )
                {
                    const float2& value = *static_cast<const float2*>( pValue );
                    outArrComponent[0]  = value._x;
                    outArrComponent[1]  = value._y;
                }
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat3 )
                {
                    const float3& value = *static_cast<const float3*>( pValue );
                    outArrComponent[0]  = value._x;
                    outArrComponent[1]  = value._y;
                    outArrComponent[2]  = value._z;
                }
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat4 )
                {
                    const float4& value = *static_cast<const float4*>( pValue );
                    outArrComponent[0]  = value._x;
                    outArrComponent[1]  = value._y;
                    outArrComponent[2]  = value._z;
                    outArrComponent[3]  = value._w;
                }
            }

            /** @brief 성분을 내장 실수 타입 값 @p pValue 에 씁니다. 바뀌었으면 true 입니다. */
            [[nodiscard]] static bool writeComponents( int32 builtinIndex, void* pValue, const float32 ( &arrComponent )[4] )
            {
                float32 arrCurrent[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
                readComponents( builtinIndex, pValue, arrCurrent );
                const uint8 count = findComponentCount( builtinIndex );
                bool        bSame = true;
                for ( uint8 index = 0; index < count; ++index )
                {
                    bSame = bSame && arrCurrent[index] == arrComponent[index];
                }
                if ( bSame )
                    return false;
                if ( builtinIndex == ReflectBuiltinIndex::kfloat32 )
                    *static_cast<float32*>( pValue ) = arrComponent[0];
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat64 )
                    *static_cast<float64*>( pValue ) = static_cast<float64>( arrComponent[0] );
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat2 )
                    *static_cast<float2*>( pValue ) = float2{ arrComponent[0], arrComponent[1] };
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat3 )
                    *static_cast<float3*>( pValue ) = float3{ arrComponent[0], arrComponent[1], arrComponent[2] };
                else if ( builtinIndex == ReflectBuiltinIndex::kfloat4 )
                    *static_cast<float4*>( pValue ) = float4{ arrComponent[0], arrComponent[1], arrComponent[2], arrComponent[3] };
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIAnimatedValue UIAnimatedValue::blend( const UIAnimatedValue& from, const UIAnimatedValue& to, float32 weight, uint32 componentCount )
    {
        UIAnimatedValue result{};
        for ( uint32 index = 0; index < componentCount && index < 4; ++index )
        {
            result._arrComponent[index] = MathUtil::lerp( from._arrComponent[index], to._arrComponent[index], weight );
        }
        result._text = weight >= 1.0f ? to._text : from._text;
        return result;
    }

    bool UIAnimatedProperty::resolve( const TypeInfo& type, string_view path, UIAnimatedProperty& outProperty, string& outError )
    {
        outProperty = UIAnimatedProperty{};
        if ( UIBindingValueUtil::resolvePath( type, path, outProperty._path, outError ) == false )
            return false;
        if ( outProperty._path.getLeafProperty()._bIsContainer == SW_TRUE )
        {
            outError    = "'" + string( path ) + "' is a container";
            outProperty = UIAnimatedProperty{};
            return false;
        }
        const PropertyInfo& leaf         = outProperty._path.getLeafProperty();
        const int32         builtinIndex = ReflectValueUtil::findBuiltinIndex( leaf._typeName );
        const uint8         count        = leaf._bIsBitField == SW_TRUE ? 0 : UIAnimatedPropertyInternal::findComponentCount( builtinIndex );
        outProperty._kind                = count > 0 ? UIAnimatedValueKind::Float : UIAnimatedValueKind::Discrete;
        outProperty._componentCount      = count;
        outProperty._builtinIndex        = count > 0 ? static_cast<uint8>( builtinIndex ) : 0;
        return true;
    }

    bool UIAnimatedProperty::parseValue( string_view text, UIAnimatedValue& outValue ) const
    {
        outValue = UIAnimatedValue{};
        if ( _kind == UIAnimatedValueKind::Discrete )
        {
            outValue._text = string( text );
            return true;
        }
        if ( _kind != UIAnimatedValueKind::Float )
            return false;
        // 잎 타입의 값으로 읽고 성분으로 옮긴다(XML 속성과 같은 글 — "0.5" · "0,12" · "1,0,0,1").
        float4              scratch{};
        const PropertyInfo& leaf = _path.getLeafProperty();
        if ( SerializerUtil::parseTextValue( &scratch, leaf._typeName, text, SerializeContext::getDefault() ) == false )
            return false;
        UIAnimatedPropertyInternal::readComponents( _builtinIndex, &scratch, outValue._arrComponent );
        return true;
    }

    UIAnimatedValue UIAnimatedProperty::readValue( const Widget& widget ) const
    {
        UIAnimatedValue value{};
        if ( isValid() == false )
            return value;
        const void*         pOwner = _path.findLeafOwner( &widget );
        const PropertyInfo& leaf   = _path.getLeafProperty();
        if ( pOwner == nullptr )
            return value;
        if ( _kind == UIAnimatedValueKind::Float )
            UIAnimatedPropertyInternal::readComponents( _builtinIndex, leaf.getRawPtr( pOwner ), value._arrComponent );
        else
            value._text = SerializerUtil::formatPropertyText( leaf, pOwner, SerializeContext::getDefault() );
        return value;
    }

    bool UIAnimatedProperty::writeValue( Widget& widget, const UIAnimatedValue& value ) const
    {
        if ( isValid() == false )
            return false;
        void*               pOwner = _path.findLeafOwner( &widget );
        const PropertyInfo& leaf   = _path.getLeafProperty();
        if ( pOwner == nullptr )
            return false;
        bool bChanged = false;
        if ( _kind == UIAnimatedValueKind::Float )
        {
            bChanged = UIAnimatedPropertyInternal::writeComponents( _builtinIndex, leaf.getRawPtr( pOwner ), value._arrComponent );
        }
        else
        {
            const SerializeContext& context = SerializeContext::getDefault();
            const string            before  = SerializerUtil::formatPropertyText( leaf, pOwner, context );
            if ( SerializerUtil::applyPropertyText( leaf, pOwner, value._text, context ) == false )
                return false;
            bChanged = SerializerUtil::formatPropertyText( leaf, pOwner, context ) != before;
        }
        if ( bChanged )
            widget.onBoundPropertyChanged( _path.getRootProperty() );
        return bChanged;
    }

    float32 evaluateUICurve( BlendCurve curve, float32 normalizedTime )
    {
        BlendCurveDef spec{};
        spec._curve    = curve;
        spec._duration = 1.0f;
        return evaluateBlendWeight( spec, normalizedTime );
    }
} // namespace sw

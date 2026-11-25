#include "pch.h"

#include "Engine/UI/Animation/UIAnimatedProperty.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Serializer/SerializeContext.h"
#include "Engine/Serialization/Serializer/SerializerUtil.h"
#include "Engine/UI/WidgetTree/Widget.h"

namespace sw
{
    namespace
    {
        struct UIAnimatedPropertyInternal
        {
            /** @brief 성분 칸 수입니다(`UIAnimatedValue::_arrComponent`). */
            static constexpr uint8 kMaxComponentCount = 4;

            /**
             * @brief 성분으로 섞는 필드의 성분 수입니다. 아니면 0 입니다(글로 바꿔 끝에 갈아 끼운다).
             * @details `ReflectBuiltins.xxx` 의 Interp 역할과 Layout 이 정한다 — 실수 num · angle 은 1, f32 는 멤버 수(4 이하), 정수 num 은 0. 쿼터니언은 성분 직선 보간이 회전 보간이 아니라 뺀다
             *          (시퀀서는 slerp 로 섞는다, `PropertyRoleUtil`).
             */
            static uint8 findComponentCount( const ReflectBuiltinIndex builtinIndex )
            {
                const ReflectBuiltinTraits traits = ReflectBuiltinTraitTable::get( builtinIndex );
                if ( traits.hasRole( ReflectBuiltinRole::Interp ) == false || builtinIndex == ReflectBuiltinIndex::kquaternion )
                    return 0;
                switch ( traits._layout )
                {
                    case ReflectBuiltinLayout::Number:
                        // 정수 칸은 성분으로 섞지 않고 끝에서 바꾼다(Discrete) — 반올림한 중간값은 UI 에 뜻이 없고, int64 · uint64 는 float32 성분을 거치면 2^24 넘는 값이 깨진다.
                        // 시퀀서 값 트랙(`PropertyRoleUtil`)은 그 타입 그대로 섞는다.
                        return ReflectedValueUtil::isBuiltinInteger( builtinIndex ) ? 0 : 1;
                    case ReflectBuiltinLayout::Angle:
                        return 1;
                    case ReflectBuiltinLayout::Float32:
                    {
                        const uint8 count = ReflectedValueUtil::getBuiltinComponentCount( builtinIndex );
                        return count <= kMaxComponentCount ? count : 0;
                    }
                    case ReflectBuiltinLayout::None:
                    case ReflectBuiltinLayout::Int32:
                        break;
                }
                return 0;
            }

            /** @brief 성분으로 섞는 내장 타입 값 @p pValue 를 성분으로 읽습니다(`findComponentCount` 가 0 이 아닌 타입). */
            static void readComponents( const ReflectBuiltinIndex builtinIndex, const void* pValue, float32 ( &outArrComponent )[kMaxComponentCount] )
            {
                switch ( ReflectBuiltinTraitTable::get( builtinIndex )._layout )
                {
                    case ReflectBuiltinLayout::Number:
                    {
                        float64 number = 0.0;
                        if ( ReflectedValueUtil::readNumber( builtinIndex, pValue, number ) )
                            outArrComponent[0] = static_cast<float32>( number );
                        return;
                    }
                    case ReflectBuiltinLayout::Angle:
                        outArrComponent[0] = static_cast<const Radians*>( pValue )->radians(); // 성분은 라디안으로 섞는다(키 글은 도로 읽힌다)
                        return;
                    case ReflectBuiltinLayout::Float32:
                    {
                        const float32* pComponent = static_cast<const float32*>( pValue ); // f32 Layout 은 빈틈 없는 float32 성분이다(`ReflectedValue.cpp` 가 컴파일 때 본다)
                        const uint8    count      = findComponentCount( builtinIndex );
                        for ( uint8 index = 0; index < count; ++index )
                        {
                            outArrComponent[index] = pComponent[index];
                        }
                        return;
                    }
                    case ReflectBuiltinLayout::None:
                    case ReflectBuiltinLayout::Int32:
                        return;
                }
            }

            /** @brief 성분을 내장 타입 값 @p pValue 에 씁니다. 바뀌었으면 true 입니다(num 은 실수 칸만 온다 — `findComponentCount`). */
            [[nodiscard]] static bool writeComponents( const ReflectBuiltinIndex builtinIndex, void* pValue, const float32 ( &arrComponent )[kMaxComponentCount] )
            {
                float32 arrCurrent[kMaxComponentCount]{ 0.0f, 0.0f, 0.0f, 0.0f };
                readComponents( builtinIndex, pValue, arrCurrent );
                const uint8 count = findComponentCount( builtinIndex );
                bool        bSame = true;
                for ( uint8 index = 0; index < count; ++index )
                {
                    bSame = bSame && arrCurrent[index] == arrComponent[index];
                }
                if ( bSame )
                    return false;
                switch ( ReflectBuiltinTraitTable::get( builtinIndex )._layout )
                {
                    case ReflectBuiltinLayout::Number:
                        return ReflectedValueUtil::writeNumber( builtinIndex, pValue, static_cast<float64>( arrComponent[0] ) );
                    case ReflectBuiltinLayout::Angle:
                        *static_cast<Radians*>( pValue ) = Radians::fromRadians( arrComponent[0] );
                        return true;
                    case ReflectBuiltinLayout::Float32:
                    {
                        float32* pComponent = static_cast<float32*>( pValue );
                        for ( uint8 index = 0; index < count; ++index )
                        {
                            pComponent[index] = arrComponent[index];
                        }
                        return true;
                    }
                    case ReflectBuiltinLayout::None:
                    case ReflectBuiltinLayout::Int32:
                        break;
                }
                return false;
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

    expected<void> UIAnimatedProperty::resolve( const TypeInfo& type, string_view path, UIAnimatedProperty& outProperty )
    {
        outProperty = UIAnimatedProperty{};
        if ( expected<void> resolved = UIBindingValue::resolvePath( type, path, outProperty._path ); resolved.has_value() == false )
            return resolved;
        if ( outProperty._path.getLeafProperty()._bIsContainer == SW_TRUE )
        {
            outProperty = UIAnimatedProperty{};
            return unexpected( Error{ ErrorKind::Unsupported, "an animated property cannot be a container", path } );
        }
        const PropertyInfo&       leaf         = outProperty._path.getLeafProperty();
        const ReflectBuiltinIndex builtinIndex = leaf.resolveValueFacts( engine::getSubsystem<TypeRegistry>() )._builtinIndex;
        const uint8               count        = leaf._bIsBitField == SW_TRUE ? 0 : UIAnimatedPropertyInternal::findComponentCount( builtinIndex );
        outProperty._kind                      = count > 0 ? UIAnimatedValueKind::Float : UIAnimatedValueKind::Discrete;
        outProperty._componentCount            = count;
        outProperty._builtinIndex              = count > 0 ? builtinIndex : ReflectBuiltinIndex::kNone;
        return {};
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
        // 잎 타입의 값으로 읽고 성분으로 옮긴다(XML 속성과 같은 텍스트이며 "0.5", "0,12", "1,0,0,1" 같은 형식이다).
        float4              scratch{};
        const PropertyInfo& leaf = _path.getLeafProperty();
        if ( SerializerUtil::parseTextValue( MutableValueAddress{ &scratch }, leaf._typeName, text, SerializeContext::getDefault( SerializePurpose::Authoring ) ) == false )
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
            value._text = SerializerUtil::formatPropertyText( leaf, ObjectAddress{ pOwner }, SerializeContext::getDefault( SerializePurpose::Authoring ) );
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
            const SerializeContext& context = SerializeContext::getDefault( SerializePurpose::Authoring );
            const string            before  = SerializerUtil::formatPropertyText( leaf, ObjectAddress{ pOwner }, context );
            if ( SerializerUtil::applyPropertyText( leaf, MutableObjectAddress{ pOwner }, value._text, context ) == false )
                return false;
            bChanged = SerializerUtil::formatPropertyText( leaf, ObjectAddress{ pOwner }, context ) != before;
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

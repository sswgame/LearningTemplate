#include "pch.h"

#include "Engine/Reflection/PropertyRoleUtil.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/Radians.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Reflection/ReflectValueBinding.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionMetadata.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
    namespace
    {
        struct PropertyRoleUtilInternal
        {
            /** @brief 두 값 사이를 섞습니다. 정수는 반올림합니다. 섞을 수 없는 타입은 false 입니다. */
            template <typename T>
            static bool blend( const void* pFrom, const void* pTo, const float32 alpha, void* pOut )
            {
                const T& from = *static_cast<const T*>( pFrom );
                const T& to   = *static_cast<const T*>( pTo );
                T&       out  = *static_cast<T*>( pOut );
                if constexpr ( std::is_same_v<T, bool> )
                {
                    (void)from;
                    (void)to;
                    (void)out;
                    return false;
                }
                else if constexpr ( std::is_floating_point_v<T> )
                    out = from + ( to - from ) * static_cast<T>( alpha );
                else if constexpr ( std::is_integral_v<T> )
                {
                    const float64 mixed = static_cast<float64>( from ) + ( static_cast<float64>( to ) - static_cast<float64>( from ) ) * static_cast<float64>( alpha );
                    out                 = static_cast<T>( mixed < 0.0 ? mixed - 0.5 : mixed + 0.5 );
                }
                else if constexpr ( std::is_same_v<T, quaternion> )
                    out = quaternion::slerp( from, to, alpha );
                else if constexpr ( std::is_same_v<T, Radians> )
                    out = from + ( to - from ) * alpha; // 감지 않는 직선 보간이다 — float32 각 칸이던 때와 같은 값(키가 정한 경로를 따른다)
                else
                    out = T::lerp( from, to, alpha );
                return true;
            }

            using BlendFn = bool ( * )( const void* pFrom, const void* pTo, float32 alpha, void* pOut );

            /** @brief Interp 역할 줄이면 그 타입의 섞기, 아니면 nullptr 입니다. 섞지 않는 타입(글 · 핸들)은 `blend<T>` 를 인스턴스화하지 않는다. */
            template <typename T, bool bInterp>
            static constexpr BlendFn pickBlend()
            {
                if constexpr ( bInterp )
                    return &blend<T>;
                else
                    return nullptr;
            }

            /** @brief 섞을 수 있는 내장 타입이면 그 함수, 아니면 nullptr 입니다. 목록은 `ReflectBuiltins.xxx` 의 Interp 역할 하나다(파서의 `PROPERTY( Interp )` 검사와 같은 칸). */
            static BlendFn findBlend( const PropertyInfo& prop )
            {
                static constexpr BlendFn kArrBlend[] = {
#define SW_REFLECT_BUILTIN_TYPE( Canon, CppType, ... ) pickBlend<CppType, ReflectBuiltinTraitTable::get( ReflectBuiltinIndex::k##Canon ).hasRole( ReflectBuiltinRole::Interp )>(),
#define SW_REFLECT_BUILTIN_CONTAINER( ... )
#include "Engine/Reflection/ReflectBuiltins.xxx"
#undef SW_REFLECT_BUILTIN_TYPE
#undef SW_REFLECT_BUILTIN_CONTAINER
                };
                static_assert( SW_COUNT_OF( kArrBlend ) == static_cast<size_t>( ReflectBuiltinIndex::kCount ), "kArrBlend must have one row per ReflectBuiltins.xxx type" );
                if ( prop._bIsContainer == SW_TRUE || prop._bIsBitField == SW_TRUE )
                    return nullptr;
                const ReflectBuiltinIndex builtinIndex = prop.resolveValueFacts( engine::getSubsystem<TypeRegistry>() )._builtinIndex;
                return builtinIndex < ReflectBuiltinIndex::kCount ? kArrBlend[static_cast<size_t>( builtinIndex )] : nullptr;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void PropertyRoleUtil::collectReplicatedProperties( const TypeInfo& type, vector<const PropertyInfo*>& outListProperty )
    {
        outListProperty.clear();
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata->_bReplicated == SW_TRUE )
                outListProperty.push_back( &prop );
        }
    }

    bool PropertyRoleUtil::callRepNotify( const PropertyInfo& prop, const MutableObjectAddress instance, const ValueAddress oldValue )
    {
        if ( prop._pRepNotify == nullptr || instance.get() == nullptr )
            return false;
        prop._pRepNotify( instance.get(), oldValue.get() );
        return true;
    }

    bool PropertyRoleUtil::isInterpolatable( const PropertyInfo& prop )
    {
        return PropertyRoleUtilInternal::findBlend( prop ) != nullptr;
    }

    void PropertyRoleUtil::collectInterpProperties( const TypeInfo& type, vector<const PropertyInfo*>& outListProperty )
    {
        outListProperty.clear();
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            if ( prop._metadata->_bInterp == SW_TRUE )
                outListProperty.push_back( &prop );
        }
    }

    bool PropertyRoleUtil::applyInterpolated( const PropertyInfo& prop, const MutableObjectAddress instance, const ValueAddress from, const ValueAddress to, const float32 alpha )
    {
        const PropertyRoleUtilInternal::BlendFn pBlend            = PropertyRoleUtilInternal::findBlend( prop );
        const bool                              bBlendUnavailable = pBlend == nullptr || instance.get() == nullptr || from.get() == nullptr || to.get() == nullptr;
        if ( bBlendUnavailable )
            return false;
        return pBlend( from.get(), to.get(), alpha, prop.getRawPtr( instance.get() ) );
    }

    void PropertyRoleUtil::blendProperties( const TypeInfo& type, const ObjectAddress from, const ObjectAddress to, const float32 alpha, const MutableObjectAddress out )
    {
        for ( const PropertyInfo& prop : type.getPropertiesWithBase() )
        {
            const PropertyRoleUtilInternal::BlendFn pBlend = PropertyRoleUtilInternal::findBlend( prop );
            if ( pBlend != nullptr )
                (void)pBlend( prop.getRawPtr( from.get() ), prop.getRawPtr( to.get() ), alpha, prop.getRawPtr( out.get() ) );
        }
    }

    const TypeInfo& PropertyRoleUtil::requireType( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            reportFatalError( "PropertyRoleUtil::blendValues: the value type is not registered with reflection" );
        return *pType;
    }
} // namespace sw

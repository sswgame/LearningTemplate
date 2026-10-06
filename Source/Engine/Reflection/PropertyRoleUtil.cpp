#include "pch.h"

#include "Engine/Reflection/PropertyRoleUtil.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"

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
                else
                    out = T::lerp( from, to, alpha );
                return true;
            }

            using BlendFn = bool ( * )( const void* pFrom, const void* pTo, float32 alpha, void* pOut );

            /** @brief 섞을 수 있는 내장 타입이면 그 함수, 아니면 nullptr 입니다. */
            static BlendFn findBlend( const PropertyInfo& prop )
            {
                if ( prop._bIsContainer == SW_TRUE || prop._bIsBitField == SW_TRUE )
                    return nullptr;
                switch ( ReflectValueUtil::findBuiltinIndex( prop._typeName ) )
                {
                    case ReflectBuiltinIndex::kint8:
                        return &blend<int8>;
                    case ReflectBuiltinIndex::kint16:
                        return &blend<int16>;
                    case ReflectBuiltinIndex::kint32:
                        return &blend<int32>;
                    case ReflectBuiltinIndex::kint64:
                        return &blend<int64>;
                    case ReflectBuiltinIndex::kuint8:
                        return &blend<uint8>;
                    case ReflectBuiltinIndex::kuint16:
                        return &blend<uint16>;
                    case ReflectBuiltinIndex::kuint32:
                        return &blend<uint32>;
                    case ReflectBuiltinIndex::kuint64:
                        return &blend<uint64>;
                    case ReflectBuiltinIndex::kfloat32:
                        return &blend<float32>;
                    case ReflectBuiltinIndex::kfloat64:
                        return &blend<float64>;
                    case ReflectBuiltinIndex::kfloat2:
                        return &blend<float2>;
                    case ReflectBuiltinIndex::kfloat3:
                        return &blend<float3>;
                    case ReflectBuiltinIndex::kfloat4:
                        return &blend<float4>;
                    case ReflectBuiltinIndex::kquaternion:
                        return &blend<quaternion>;
                    default:
                        return nullptr;
                }
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
            if ( prop._metadata._bReplicated == SW_TRUE )
                outListProperty.push_back( &prop );
        }
    }

    bool PropertyRoleUtil::callRepNotify( const PropertyInfo& prop, void* pInstance, const void* pOldValue )
    {
        if ( prop._pRepNotify == nullptr || pInstance == nullptr )
            return false;
        prop._pRepNotify( pInstance, pOldValue );
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
            if ( prop._metadata._bInterp == SW_TRUE )
                outListProperty.push_back( &prop );
        }
    }

    bool PropertyRoleUtil::applyInterpolated( const PropertyInfo& prop, void* pInstance, const void* pFrom, const void* pTo, const float32 alpha )
    {
        const PropertyRoleUtilInternal::BlendFn pBlend = PropertyRoleUtilInternal::findBlend( prop );
        if ( pBlend == nullptr || pInstance == nullptr || pFrom == nullptr || pTo == nullptr )
            return false;
        return pBlend( pFrom, pTo, alpha, prop.getRawPtr( pInstance ) );
    }
} // namespace sw

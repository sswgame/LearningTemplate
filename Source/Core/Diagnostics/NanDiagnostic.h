/**
 * @file NanDiagnostic.h
 * @brief 벡터 · 쿼터니언 · 행렬에 NaN · 무한대가 **처음 들어온 자리**를 알리는 개발 구성 검사입니다(언리얼 `ENABLE_NAN_DIAGNOSTIC` · `DiagnosticCheckNaN_All`).
 * @details NaN 은 한 번 들어오면 부모 → 자식 → 본 → 스키닝 팔레트로 번져, 화면에 보일 때는 어디서 왔는지 알 수 없다. 그래서 값이 들어오는 입구(트랜스폼 설정 ·
 *          애니메이션 포즈 · 본 행렬 · 팔레트 업로드 · 물리 결과 · 카메라 행렬)마다 검사하고, 객체 · 본 이름을 넣어 **이름마다 한 번** Error 를 남긴다(`SW_LOG_ERROR_ONCE_FOR_ID`).
 *          검사는 `SW_DO_CHECKS` 구성에서만 컴파일된다 — 배포(Shipping)에서는 식도 평가하지 않는다. 텍스처 출력의 NaN 은 GPU 쪽 `GPUNanCheck` 가 본다.
 *          본 수천 개를 도는 배열은 `findFirstNonFinite` 로 한 번에 훑는다 — 지수 비트를 OR 로 모으는 분기 없는 첫 훑기라 벡터화되고, 걸렸을 때만 자리를 찾는다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/Log/LogOnce.h"
#include "Core/Math/BitUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @struct NanDiagnostic
     * @brief 배열에서 유한하지 않은 값을 찾는 static 도우미입니다. 알림은 `SW_DIAGNOSTIC_CHECK_NAN_ID` · 부르는 쪽의 `SW_LOG_ERROR_ONCE_FOR_ID` 가 한다.
     */
    struct NanDiagnostic
    {
        /** @brief float32 의 지수 비트입니다. 모두 켜져 있으면 무한대나 NaN 입니다(IEEE 754). */
        static constexpr uint32 kFloatExponentMask = 0x7F800000u;

        /**
         * @brief @p pValue 의 @p count 개 가운데 처음 유한하지 않은 값의 번호입니다. 모두 유한하면 @p count 입니다(`std::find` 처럼 끝).
         * @details 첫 훑기는 "지수가 모두 켜졌는가" 를 정수 OR 로 모은다 — 정수 OR 는 결합 법칙이 서서 fast-math 없이도 벡터화된다. 걸렸을 때만 다시 훑어 자리를 찾는다.
         */
        [[nodiscard]] static size_t findFirstNonFinite( const float32* pValue, const size_t count ) noexcept
        {
            uint32 nonFinite = 0;
            for ( size_t index = 0; index < count; ++index )
            {
                nonFinite |= static_cast<uint32>( ( BitUtil::floatToBits( pValue[index] ) & kFloatExponentMask ) == kFloatExponentMask );
            }
            if ( nonFinite == 0 )
                return count;
            for ( size_t index = 0; index < count; ++index )
            {
                if ( MathUtil::isFinite( pValue[index] ) == false )
                    return index;
            }
            return count;
        }

        /**
         * @brief float32 성분만 든 형식(`float2` · `float3` · `float4` · `quaternion` · `float4x4`)의 배열에서 처음 유한하지 않은 **원소**의 번호입니다. 모두 유한하면 @p count 입니다.
         */
        template <typename T>
        [[nodiscard]] static size_t findFirstNonFinite( const T* pElement, const size_t count ) noexcept
        {
            static_assert( std::is_same_v<T, float2> || std::is_same_v<T, float3> || std::is_same_v<T, float4> || std::is_same_v<T, quaternion> || std::is_same_v<T, float4x4>,
                           "float32 성분만 든 수학 형식만 받는다(double3 은 성분이 float64 다)" );
            constexpr size_t kFloatCountPerElement = sizeof( T ) / sizeof( float32 );
            const size_t     floatIndex            = findFirstNonFinite( reinterpret_cast<const float32*>( pElement ), count * kFloatCountPerElement );
            return floatIndex / kFloatCountPerElement;
        }
    };
} // namespace sw

#if defined( SW_DO_CHECKS )
/**
 * @brief @p value(`isFinite()` 가 있는 수학 형식)가 유한하지 않으면 이 호출 지점 × @p name 마다 한 번 Error 를 남깁니다(언리얼 `DiagnosticCheckNaN`).
 * @details @p name 은 `data()` · `size()` 를 가진 이름이고 나머지 인자는 `SW_LOG_ERROR_ID` 와 같다. 이름 · 서식 인자는 걸렸을 때만 평가된다 — 이름을 꺼내는 비용을 늘 내지 않게
 *          이름을 꺼내는 식을 그대로 넘긴다. 배포에서는 아무것도 평가하지 않는다.
 */
    #define SW_DIAGNOSTIC_CHECK_NAN_ID( id, value, name, ... )     \
        do                                                         \
        {                                                          \
            if ( ( value ).isFinite() == false )                   \
                SW_LOG_ERROR_ONCE_FOR_ID( id, name, __VA_ARGS__ ); \
        } while ( 0 )
#else
    #define SW_DIAGNOSTIC_CHECK_NAN_ID( id, value, name, ... ) \
        do                                                     \
        {                                                      \
        } while ( 0 )
#endif

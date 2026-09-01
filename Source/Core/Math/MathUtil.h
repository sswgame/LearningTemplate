/**
 * @file MathUtil.h
 * @brief 각도·보간·클램프·난수 등 스칼라 수학 유틸 (전부 static).
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"

namespace sw
{

	// ------------------------------------------------------------------------------
	// 1) MathUtil — 상수 · 각도 · 비교 · min/max/clamp
	//    벡터/행렬은 VectorMath / MatrixMath
	// ------------------------------------------------------------------------------
	/**
	 * @class MathUtil
	 * @brief 전역적으로 사용되는 순수 수학 계산 및 편의 기능을 제공하는 static 클래스
	 */
	class MathUtil
	{
	public:
		static constexpr float32 Pi				= 3.1415926535f;
		static constexpr float32 HalfPi			= Pi * 0.5f;
		static constexpr float32 DegreeToRadian = Pi / 180.f;
		static constexpr float32 RadianToDegree = 180.f / Pi;
		/** @brief float32 비교용 머신 엡실론입니다. */
		static constexpr float32 Epsilon = 1e-6f;
		/** @brief float64 비교용 머신 엡실론입니다. */
		static constexpr float64 Epsilon64 = std::numeric_limits<float64>::epsilon();

		static constexpr float32 MaxFloat	= std::numeric_limits<float32>::max();
		static constexpr float32 MinFloat	= std::numeric_limits<float32>::lowest();
		static constexpr float64 MaxFloat64 = std::numeric_limits<float64>::max();
		static constexpr float64 MinFloat64 = std::numeric_limits<float64>::lowest();
		static constexpr int8	 MaxInt8	= static_cast<int8>( invalid_index::kUint8 >> 1 );
		static constexpr int8	 MinInt8	= -MaxInt8 + invalid_index::kInt8;
		static constexpr int16	 MaxInt16	= static_cast<int16>( invalid_index::kUint16 >> 1 );
		static constexpr int16	 MinInt16	= -MaxInt16 + invalid_index::kInt16;
		static constexpr int32	 MaxInt32	= static_cast<int32>( invalid_index::kUint32 >> 1 );
		static constexpr int32	 MinInt32	= -MaxInt32 + invalid_index::kInt32;
		static constexpr int64	 MaxInt64	= static_cast<int64>( invalid_index::kUint64 >> 1 );
		static constexpr int64	 MinInt64	= -MaxInt64 + invalid_index::kInt64;
		static constexpr uint8	 MaxUInt8	= invalid_index::kUint8;
		static constexpr uint16	 MaxUInt16	= invalid_index::kUint16;
		static constexpr uint32	 MaxUInt32	= invalid_index::kUint32;
		static constexpr uint64	 MaxUInt64	= invalid_index::kUint64;

		/** @brief degree 단위의 각도를 radian 으로 변환 */
		[[nodiscard]] static constexpr float32 toRadian( const float32 degree ) noexcept { return degree * DegreeToRadian; }

		/** @brief radian 단위의 각도를 degree 로 변환 */
		[[nodiscard]] static constexpr float32 toDegree( const float32 radian ) noexcept { return radian * RadianToDegree; }

		/** @brief 입력값을 0.0과 1.0 사이로 강제 고정 */
		[[nodiscard]] static constexpr float32 saturate( const float32 value ) noexcept { return clamp( value, 0.f, 1.f ); }

		/** @brief 거의 같은지 비교합니다. */
		[[nodiscard]] static bool nearEqual( const float32 a, const float32 b, const float32 epsilon = Epsilon ) noexcept { return abs( a - b ) < epsilon; }
		/** @brief 거의 같은지 비교합니다. */
		[[nodiscard]] static bool nearEqual( const float64 a, const float64 b, const float64 epsilon = Epsilon64 ) noexcept { return abs( a - b ) < epsilon; }

		/** @brief 무한대(Infinity)인지 확인합니다. */
		[[nodiscard]] static bool isInfinite( const float32 value ) noexcept { return std::isinf( value ); }
		[[nodiscard]] static bool isInfinite( const float64 value ) noexcept { return std::isinf( value ); }

		/** @brief NaN(Not a Number)인지 확인합니다. */
		[[nodiscard]] static bool isNan( const float32 value ) noexcept { return std::isnan( value ); }
		[[nodiscard]] static bool isNan( const float64 value ) noexcept { return std::isnan( value ); }

		/** @brief 역제곱근(1 / sqrt(x))을 계산합니다. */
		[[nodiscard]] static float32 invSqrt( const float32 x ) noexcept
		{
			if ( x <= 0.f )
				return 0.f;
			return 1.0f / sqrtf( x );
		}

		/** @brief 제곱근을 계산합니다. */
		[[nodiscard]] static float32 sqrt( const float32 x ) noexcept { return ::sqrtf( x ); }
		[[nodiscard]] static float64 sqrt( const float64 x ) noexcept { return ::sqrt( x ); }

		/** @brief 거듭제곱(x^y)을 계산합니다. */
		[[nodiscard]] static float32 pow( const float32 base, const float32 exp ) noexcept { return ::powf( base, exp ); }
		[[nodiscard]] static float64 pow( const float64 base, const float64 exp ) noexcept { return ::pow( base, exp ); }

		/** @brief 삼각함수 sin을 계산합니다. */
		[[nodiscard]] static float32 sin( const float32 radian ) noexcept { return ::sinf( radian ); }
		[[nodiscard]] static float64 sin( const float64 radian ) noexcept { return ::sin( radian ); }

		/** @brief 삼각함수 cos을 계산합니다. */
		[[nodiscard]] static float32 cos( const float32 radian ) noexcept { return ::cosf( radian ); }
		[[nodiscard]] static float64 cos( const float64 radian ) noexcept { return ::cos( radian ); }

		/** @brief 삼각함수 tan을 계산합니다. */
		[[nodiscard]] static float32 tan( const float32 radian ) noexcept { return ::tanf( radian ); }
		[[nodiscard]] static float64 tan( const float64 radian ) noexcept { return ::tan( radian ); }

		/** @brief 아크사인 asin(x)를 계산합니다. */
		[[nodiscard]] static float32 asin( const float32 x ) noexcept { return ::asinf( x ); }
		[[nodiscard]] static float64 asin( const float64 x ) noexcept { return ::asin( x ); }

		/** @brief 아크코사인 acos(x)를 계산합니다. */
		[[nodiscard]] static float32 acos( const float32 x ) noexcept { return ::acosf( x ); }
		[[nodiscard]] static float64 acos( const float64 x ) noexcept { return ::acos( x ); }

		/** @brief 아크탄젠트 atan2(y, x)를 계산합니다. */
		[[nodiscard]] static float32 atan2( const float32 y, const float32 x ) noexcept { return ::atan2f( y, x ); }
		[[nodiscard]] static float64 atan2( const float64 y, const float64 x ) noexcept { return ::atan2( y, x ); }

		/** @brief 부동소수점 나머지(fmod)를 계산합니다. */
		[[nodiscard]] static float32 fmod( const float32 x, const float32 y ) noexcept { return ::fmodf( x, y ); }
		[[nodiscard]] static float64 fmod( const float64 x, const float64 y ) noexcept { return ::fmod( x, y ); }

		/** @brief 반올림(round)을 계산합니다. */
		[[nodiscard]] static float32 round( const float32 x ) noexcept { return ::roundf( x ); }
		[[nodiscard]] static float64 round( const float64 x ) noexcept { return ::round( x ); }

		/** @brief 내림(floor)을 계산합니다. */
		[[nodiscard]] static float32 floor( const float32 x ) noexcept { return ::floorf( x ); }
		[[nodiscard]] static float64 floor( const float64 x ) noexcept { return ::floor( x ); }

		/** @brief 올림(ceil)을 계산합니다. */
		[[nodiscard]] static float32 ceil( const float32 x ) noexcept { return ::ceilf( x ); }
		[[nodiscard]] static float64 ceil( const float64 x ) noexcept { return ::ceil( x ); }

		/** @brief 최솟값을 반환합니다. */
		template <typename T>
		[[nodiscard]] static constexpr T min( T a, T b ) noexcept { return ( a < b ) ? a : b; }

		/** @brief 최댓값을 반환합니다. */
		template <typename T>
		[[nodiscard]] static constexpr T max( T a, T b ) noexcept { return ( a > b ) ? a : b; }

		/** @brief 절댓값을 반환합니다. */
		template <typename T>
		[[nodiscard]] static constexpr T abs( T a ) noexcept { return ( a < T( 0 ) ) ? -a : a; }

		/** @brief 지정된 범위를 벗어나지 않도록 값을 제한(clamp)합니다. */
		template <typename T>
		[[nodiscard]] static constexpr T clamp( const T value, const T minValue, const T maxValue ) noexcept
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return ( value < minValue ) ? minValue : ( ( value > maxValue ) ? maxValue : value );
		}

		/** @brief 2의 거듭제곱인지 확인합니다. */
		template <typename T>
		[[nodiscard]] static constexpr bool isPowerOfTwo( T value ) noexcept
		{
			static_assert( std::is_integral_v<T>, "T should be integral" );
			return value > T( 0 ) && ( value & ( value - T( 1 ) ) ) == 0;
		}

		/** @brief 값의 제곱(x^2)을 계산합니다. */
		template <typename T>
		static constexpr T square( T x ) noexcept
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * x;
		}

		/** @brief 값의 제곱(x^2)을 계산합니다 (오타 호환성 유지용). */
		template <typename T>
		static constexpr T sqaure( T x ) noexcept
		{
			return square( x );
		}

		/** @brief 값의 4제곱(x^4)을 계산합니다. */
		template <typename T>
		static constexpr T pow4( T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * x * x * x;
		}

		// ------------------------------------------------------------------------------
		// 2) 보간 · 정렬 · 난수 · 분수
		// ------------------------------------------------------------------------------
		/** @brief 이중 선형 보간(Bilinear Interpolation)을 수행합니다. */
		template <typename float4, typename float2>
		static constexpr float32 bilinear( float4 gather, float2 pixel_frac )
		{
			const float32 top_row	 = lerp( gather.w, gather.z, pixel_frac.x );
			const float32 bottom_row = lerp( gather.x, gather.y, pixel_frac.x );
			return MathUtil::lerp( top_row, bottom_row, pixel_frac.y );
		}

		/** @brief 선형 보간(Linear Interpolation)을 수행합니다. */
		template <typename T>
		static constexpr T lerp( T x, T y, T a )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * ( T( 1 ) - a ) + y * a;
		}

		/** @brief 두 값 사이의 위치를 통해 t 값(0.0~1.0)을 역으로 구합니다. */
		template <typename T>
		static constexpr T inverse_lerp( T value1, T value2, T pos, const float32 epsilon = Epsilon )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return ( MathUtil::abs( value2 - value1 ) < epsilon ) ? T( 0 ) : ( ( pos - value1 ) / ( value2 - value1 ) );
		}

		/** @brief 부드러운 S자 곡선을 그리는 보간을 수행합니다 (Smoothstep). */
		template <typename T>
		static constexpr T smoothstep( T edge0, T edge1, T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			const T t = MathUtil::saturate( MathUtil::inverse_lerp( edge0, edge1, x ) );
			return t * t * ( T( 3 ) - T( 2 ) * t );
		}

		/** @brief Catmull-Rom 스플라인 보간을 수행합니다. */
		template <typename T>
		static constexpr T catmullRom( T v1, T v2, T v3, T v4, T t )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			const T t2 = t * t;
			const T t3 = t2 * t;
			return T( 0.5 ) * ( ( T( 2 ) * v2 ) + ( -v1 + v3 ) * t + ( T( 2 ) * v1 - T( 5 ) * v2 + T( 4 ) * v3 - v4 ) * t2 + ( -v1 + T( 3 ) * v2 - T( 3 ) * v3 + v4 ) * t3 );
		}

		/** @brief Hermite 스플라인 보간을 수행합니다. */
		template <typename T>
		static constexpr T hermite( T p1, T s1, T p2, T s2, T t )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			const T t2 = t * t;
			const T t3 = t2 * t;
			const T h1 = T( 2 ) * t3 - T( 3 ) * t2 + T( 1 );
			const T h2 = -T( 2 ) * t3 + T( 3 ) * t2;
			const T h3 = t3 - T( 2 ) * t2 + t;
			const T h4 = t3 - t2;
			return h1 * p1 + h2 * p2 + h3 * s1 + h4 * s2;
		}

		/** @brief 지정된 alignment 배수에 맞춰 올림(align) 처리합니다. */
		template <typename T>
		static constexpr T align( T value, T alignment )
		{
			if constexpr ( std::is_integral_v<T> )
			{
				if ( ( alignment & ( alignment - 1 ) ) == 0 )
					return ( value + alignment - 1 ) & ~( alignment - 1 );
			}
			return ( ( value + alignment - T( 1 ) ) / alignment ) * alignment;
		}

		/** @brief 임의의 난수를 반환합니다. */
		template <typename T>
		static T getRandom()
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );

			thread_local std::mt19937_64 t_generator{ std::random_device{}() };

			std::conditional_t<std::is_floating_point_v<T>, std::uniform_real_distribution<T>, std::uniform_int_distribution<T>> dist{};
			return static_cast<T>( dist( t_generator ) );
		}

		/** @brief 지정된 범위 [from, to] 내의 임의의 난수를 반환합니다. */
		template <typename T>
		static T getRandomRange( T from, T to )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );

			thread_local std::mt19937_64 t_generator{ std::random_device{}() };

			T start = static_cast<T>( from );
			T end	= static_cast<T>( to );

			std::conditional_t<std::is_floating_point_v<T>, std::uniform_real_distribution<T>, std::uniform_int_distribution<T>> dist{ start, end };

			return static_cast<T>( dist( t_generator ) );
		}

		/** @brief 소수점 이하의 분수(fraction) 부분만 반환합니다. */
		template <typename T>
		static constexpr T frac( T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			if constexpr ( std::is_integral_v<T> )
			{
				return T( 0 );
			}
			else
			{
				return x - MathUtil::floor( x );
			}
		}
	};
} // namespace sw

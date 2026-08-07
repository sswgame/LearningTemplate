#pragma once

/**
 * @file MathUtil.h
 * @brief 상용 게임 엔진 및 그래픽스 프로그래밍에서 빈번하게 사용되는 수학 상수와 유틸리티 함수들을 모아둔 클래스입니다.
 */
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{

	/**
	 * @class MathUtil
	 * @brief 전역적으로 사용되는 순수 수학 계산 및 편의 기능을 제공하는 static 클래스
	 */
	class MathUtil
	{
	public:
		static constexpr float32 kPI			 = 3.1415926535f;							/**< 원주율 PI */
		static constexpr float32 kDegreeToRadian = kPI / 180.f;								/**< 각도(Degree)를 호도(Radian)로 변환하는 상수 */
		static constexpr float32 kRadianToDegree = 180.f / kPI;								/**< 호도(Radian)를 각도(Degree)로 변환하는 상수 */
		static constexpr float32 kEpsilon		 = std::numeric_limits<float32>::epsilon(); /**< 32비트 부동소수점 오차 허용치 */
		static constexpr float64 kEpsilon64		 = std::numeric_limits<float64>::epsilon(); /**< 64비트 부동소수점 오차 허용치 */

		static constexpr float32 Pi		 = kPI;
		static constexpr float32 HalfPi	 = kPI * 0.5f;
		static constexpr float32 Epsilon = kEpsilon;

		/** @brief degree 단위의 각도를 radian 으로 변환 */
		static constexpr float32 toRadian( const float32 degree ) noexcept { return degree * kDegreeToRadian; }

		/** @brief radian 단위의 각도를 degree 로 변환 */
		static constexpr float32 toDegree( const float32 radian ) noexcept { return radian * kRadianToDegree; }

		/** @brief 입력값을 0.0과 1.0 사이로 강제 고정 */
		static constexpr float32 saturate( const float32 value ) noexcept { return clamp( value, 0.f, 1.f ); }

		static bool nearEqual( const float32 a, const float32 b, const float32 epsilon = kEpsilon ) noexcept { return std::abs( a - b ) < epsilon; }
		static bool nearEqual( const float64 a, const float64 b, const float64 epsilon = kEpsilon64 ) noexcept { return std::abs( a - b ) < epsilon; }

		static float32 invSqrt( const float32 x ) noexcept
		{
			if ( x <= 0.f )
				return 0.f;
			return 1.0f / sqrtf( x );
		}

		template <typename T>
		static constexpr T clamp( const T value, const T minValue, const T maxValue ) noexcept
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return ( value < minValue ) ? minValue : ( ( value > maxValue ) ? maxValue : value );
		}

		template <typename T>
		static constexpr T sqaure( T x ) noexcept
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * x;
		}

		template <typename T>
		static constexpr T pow4( T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * x * x * x;
		}

		template <typename float4, typename float2>
		static constexpr float32 bilinear( float4 gather, float2 pixel_frac )
		{
			const float32 top_row	 = lerp( gather.w, gather.z, pixel_frac.x );
			const float32 bottom_row = lerp( gather.x, gather.y, pixel_frac.x );
			return MathUtil::lerp( top_row, bottom_row, pixel_frac.y );
		}

		template <typename T>
		static constexpr T lerp( T x, T y, T a )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return x * ( T( 1 ) - a ) + y * a;
		}

		template <typename T>
		static constexpr T inverse_lerp( T value1, T value2, T pos, const float32 epsilon = kEpsilon )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			return ( std::abs( value2 - value1 ) < epsilon ) ? T( 0 ) : ( ( pos - value1 ) / ( value2 - value1 ) );
		}

		template <typename T>
		static constexpr T smoothstep( T edge0, T edge1, T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			const T t = MathUtil::saturate( MathUtil::inverse_lerp( edge0, edge1, x ) );
			return t * t * ( T( 3 ) - T( 2 ) * t );
		}

		template <typename T>
		static constexpr T smoothStep( T edge0, T edge1, T x )
		{
			return smoothstep( edge0, edge1, x );
		}

		template <typename T>
		static constexpr T catmullRom( T v1, T v2, T v3, T v4, T t )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			const T t2 = t * t;
			const T t3 = t2 * t;
			return T( 0.5 ) * ( ( T( 2 ) * v2 ) + ( -v1 + v3 ) * t + ( T( 2 ) * v1 - T( 5 ) * v2 + T( 4 ) * v3 - v4 ) * t2 + ( -v1 + T( 3 ) * v2 - T( 3 ) * v3 + v4 ) * t3 );
		}

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

		template <typename T>
		static T getRandom()
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );

			static std::random_device device{};
			static std::mt19937_64	  generator{ device() };

			std::conditional_t<std::is_floating_point_v<T>, std::uniform_real_distribution<T>, std::uniform_int_distribution<T>> dist{};
			return static_cast<T>( dist( generator ) );
		}

		template <typename T>
		static T getRandomRange( T from, T to )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );

			static std::random_device device{};
			static std::mt19937_64	  generator{ device() };

			T start = static_cast<T>( from );
			T end	= static_cast<T>( to );

			std::conditional_t<std::is_floating_point_v<T>, std::uniform_real_distribution<T>, std::uniform_int_distribution<T>> dist{ start, end };

			return static_cast<T>( dist( generator ) );
		}

		template <typename T>
		static constexpr T frac( T x )
		{
			static_assert( std::is_arithmetic_v<T>, "T should be arithmetic" );
			T f = x - T( static_cast<int32>( x ) );
			f	= f < 0 ? ( 1 + f ) : f;
			return f;
		}
	};
} // namespace sw

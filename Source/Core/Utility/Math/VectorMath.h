#pragma once

/**
 * @file VectorMath.h
 * @brief 2D/3D/4D 벡터 타입과 관련 연산 (HLSL 레이아웃 정렬 유지). 행렬·쿼터니언은 MatrixMath.h.
 * @details Direct3D 12의 HLSL 구조 메모리 레이아웃과 일치하도록 정렬(Alignment)을 유지합니다.
 */

#include "Core/Common/Types.h"
#include "Core/Utility/Math/MathUtil.h"

namespace sw
{
	struct quaternion;
	struct float4x4;
} // namespace sw

namespace sw
{

	/**
	 * @struct float2
	 * @brief 2차원 부동소수점 벡터. (UV 좌표, 화면 픽셀 좌표 등에 사용)
	 */
	struct SW_API float2 final
	{
		static const float2 Zero;
		static const float2 UnitX;
		static const float2 UnitY;
		static const float2 UnitScale;

		float32 _x;
		float32 _y;

		constexpr float2() noexcept
			: _x{ 0.f }, _y{ 0.f }
		{
		}

		constexpr explicit float2( const float32 value ) noexcept
			: _x{ value }, _y{ value }
		{
		}

		constexpr float2( const float32 x, const float32 y ) noexcept
			: _x{ x }, _y{ y }
		{
		}

		constexpr explicit float2( const float32* pArray ) noexcept
			: _x{ pArray[0] }, _y{ pArray[1] }
		{
		}

	public:
		/** @brief 두 벡터 간의 유클리드 거리를 계산합니다. */
		static float32 getDistance( const float2& from, const float2& to ) noexcept;

		/** @brief 두 벡터 간의 거리의 제곱을 계산합니다. (제곱근 연산 회피용) */
		static float32 getDistanceSquared( const float2& from, const float2& to ) noexcept;

		/** @brief 두 벡터의 각 성분별 최솟값으로 구성된 벡터를 반환합니다. */
		static float2 min( const float2& lhs, const float2& rhs ) noexcept;

		/** @brief 두 벡터의 각 성분별 최댓값으로 구성된 벡터를 반환합니다. */
		static float2 max( const float2& lhs, const float2& rhs ) noexcept;

		/** @brief 두 벡터 사이를 선형 보간(Linear Interpolation)합니다. t는 0.0 ~ 1.0 사이입니다. */
		static float2 lerp( const float2& from, const float2& to, float32 t ) noexcept;

		/** @brief 두 벡터 사이를 부드럽게 보간(Smooth Step)합니다. */
		static float2 smoothStep( const float2& from, const float2& to, float32 t ) noexcept;

		/** @brief 베리센트릭(Barycentric) 좌표계를 사용하여 2D 벡터를 계산합니다. */
		static float2 barycentric( const float2& v1, const float2& v2, const float2& v3, float32 f, float32 g ) noexcept;

		/** @brief 캣멀-롬(Catmull-Rom) 스플라인 보간을 수행합니다. */
		static float2 catmullRom( const float2& v1, const float2& v2, const float2& v3, const float2& v4, float32 t ) noexcept;

		/** @brief 에르미트(Hermite) 스플라인 보간을 수행합니다. */
		static float2 hermite( const float2& p1, const float2& slope1, const float2& p2, const float2& slope2, float32 t ) noexcept;

		/** @brief 평면 법선(normal)을 기준으로 입사 벡터(source)의 반사 벡터를 계산합니다. */
		static float2 reflect( const float2& source, const float2& normal ) noexcept;

		/** @brief 쿼터니언을 사용하여 2D 벡터를 회전 변환합니다. */
		static float2 transform( const float2& v, const quaternion& rotation ) noexcept;

		/** @brief 4x4 행렬을 사용하여 2D 벡터를 좌표 변환합니다. */
		static float2 transform( const float2& v, const float4x4& matrix ) noexcept;

		/** @brief 4x4 행렬을 사용하여 2D 법선 벡터를 변환합니다. (평행 이동 무시) */
		static float2 transformNormal( const float2& v, const float4x4& matrix ) noexcept;

	public:
		/** @brief 지정된 경계 영역(bound) 내에 있는지 확인합니다. */
		bool inBounds( const float2& bound ) const noexcept;

		/** @brief 벡터의 성분 중 하나라도 무한대(Infinity)인지 확인합니다. */
		bool isInfinite() const noexcept;

		/** @brief 다른 벡터와의 내적(Dot Product)을 계산합니다. */
		float32 dot( const float2& other ) const noexcept;

		/** @brief 이 벡터의 길이를 1.0으로 정규화(Normalize)하고 자신을 반환합니다. */
		float2& normalize() noexcept;

		/** @brief 이 벡터가 정규화된 새로운 벡터 사본을 반환합니다. */
		float2 normalize() const noexcept;

		/** @brief 이 벡터의 각 성분을 minValue와 maxValue 사이로 강제 고정(Clamp)합니다. */
		void clamp( const float2& minValue, const float2& maxValue ) noexcept;

		/** @brief 성분들이 고정(Clamp)된 새로운 벡터 사본을 반환합니다. */
		float2 clamp( const float2& minValue, const float2& maxValue ) const noexcept;

		/** @brief clamp()와 동일한 역할을 수행합니다. */
		float2 clamped( const float2& minValue, const float2& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

		/** @brief 벡터의 길이(크기)를 반환합니다. */
		float32 getLength() const noexcept;

		/** @brief 벡터의 길이의 제곱을 반환합니다. */
		float32 getLengthSquared() const noexcept;

	public:
		bool operator==( const float2& other ) const noexcept;
		bool operator!=( const float2& other ) const noexcept;

		float2& operator+=( const float2& other ) noexcept;
		float2& operator-=( const float2& other ) noexcept;
		float2& operator*=( float32 scale ) noexcept;
		float2& operator/=( float32 scale ) noexcept;

		float2 operator+() const noexcept { return *this; }
		float2 operator-() const noexcept;
	};

	float2 operator+( const float2& lhs, const float2& rhs ) noexcept;
	float2 operator-( const float2& lhs, const float2& rhs ) noexcept;
	float2 operator*( const float2& v, float32 scale ) noexcept;
	float2 operator/( const float2& v, float32 scale ) noexcept;
	float2 operator*( float32 scale, const float2& v ) noexcept;

	/**
	 * @struct float3
	 * @brief 3차원 부동소수점 벡터. (월드 좌표, 방향 벡터, RGB 색상 등에 사용)
	 */
	struct SW_API float3 final
	{
		static const float3 Zero;
		static const float3 UnitX;
		static const float3 UnitY;
		static const float3 UnitZ;
		static const float3 UnitScale;
		static const float3 Up;
		static const float3 Down;
		static const float3 Right;
		static const float3 Left;
		static const float3 Forward;
		static const float3 Backward;

		float32 _x;
		float32 _y;
		float32 _z;

		constexpr float3() noexcept
			: _x{ 0.f }, _y{ 0.f }, _z{ 0.f }
		{
		}

		constexpr explicit float3( const float32 value ) noexcept
			: _x{ value }, _y{ value }, _z{ value }
		{
		}

		constexpr float3( const float32 x, const float32 y, const float32 z ) noexcept
			: _x{ x }, _y{ y }, _z{ z }
		{
		}

		constexpr explicit float3( const float32* pArray ) noexcept
			: _x{ pArray[0] }, _y{ pArray[1] }, _z{ pArray[2] }
		{
		}

	public:
		/** @brief 두 3D 벡터 간의 거리를 반환합니다. */
		static float32 getDistance( const float3& from, const float3& to ) noexcept;

		/** @brief 두 3D 벡터 간의 거리의 제곱을 반환합니다. */
		static float32 getDistanceSquared( const float3& from, const float3& to ) noexcept;

		/** @brief 두 3D 벡터의 최소 성분들로 이루어진 벡터를 반환합니다. */
		static float3 min( const float3& lhs, const float3& rhs ) noexcept;

		/** @brief 두 3D 벡터의 최대 성분들로 이루어진 벡터를 반환합니다. */
		static float3 max( const float3& lhs, const float3& rhs ) noexcept;

		/** @brief 두 3D 벡터 사이를 t만큼 선형 보간(Linear Interpolation)합니다. */
		static float3 lerp( const float3& from, const float3& to, float32 t ) noexcept;

		/** @brief 두 3D 벡터 사이를 부드럽게 보간(Smooth Step)합니다. */
		static float3 smoothStep( const float3& from, const float3& to, float32 t ) noexcept;

		/** @brief 베리센트릭 보간된 3D 벡터를 반환합니다. */
		static float3 barycentric( const float3& v1, const float3& v2, const float3& v3, float32 f, float32 g ) noexcept;

		/** @brief 캣멀-롬 스플라인 보간된 3D 벡터를 반환합니다. */
		static float3 catmullRom( const float3& v1, const float3& v2, const float3& v3, const float3& v4, float32 t ) noexcept;

		/** @brief 에르미트 스플라인 보간된 3D 벡터를 반환합니다. */
		static float3 hermite( const float3& p1, const float3& slope1, const float3& p2, const float3& slope2, float32 t ) noexcept;

		/** @brief 노멀에 대한 입사 벡터의 반사(Reflection) 벡터를 구합니다. */
		static float3 reflect( const float3& source, const float3& normal ) noexcept;

		/** @brief 벡터를 다른 벡터에 투영(Project)합니다. */
		static float3 project( const float3& from, const float3& to ) noexcept;

		/** @brief 벡터의 다른 벡터에 대한 수직(Perpendicular) 성분을 구합니다. */
		static float3 perpedicular( const float3& from, const float3& to ) noexcept;

		/** @brief 노멀에 대한 입사 벡터의 굴절(Refraction) 벡터를 구합니다. */
		static float3 refract( const float3& source, const float3& normal, float32 refractionIndex ) noexcept;

		/** @brief 쿼터니언을 사용하여 3D 벡터를 회전합니다. */
		static float3 transform( const float3& v, const quaternion& rotation ) noexcept;

		/** @brief 4x4 행렬을 사용하여 3D 좌표 벡터(w=1)를 변환합니다. */
		static float3 transform( const float3& v, const float4x4& matrix ) noexcept;

		/** @brief 4x4 행렬을 사용하여 3D 법선 벡터(w=0)를 변환합니다. */
		static float3 transformNormal( const float3& v, const float4x4& matrix ) noexcept;

	public:
		/** @brief 다른 3D 벡터와의 내적(Dot Product)을 반환합니다. */
		float32 dot( const float3& other ) const noexcept;

		/** @brief 다른 3D 벡터와의 외적(Cross Product)된 벡터를 반환합니다. (오른손 좌표계 기준) */
		float3 cross( const float3& other ) const noexcept;

		/** @brief 현재 벡터를 정규화하여 길이 1로 만들고 자신을 반환합니다. */
		float3& normalize() noexcept;

		/** @brief 정규화된 새로운 벡터 사본을 반환합니다. */
		float3 normalize() const noexcept;

		/** @brief 주어진 AABB 바운딩 영역 내에 포함되는지 검사합니다. */
		bool inBounds( const float3& bound ) const noexcept;

		/** @brief 벡터의 성분 중 무한대(Infinity) 값이 있는지 검사합니다. */
		bool isInfinite() const noexcept;

		/** @brief 3D 벡터 성분을 minValue와 maxValue 사이로 강제 제한(Clamp)합니다. */
		void clamp( const float3& minValue, const float3& maxValue ) noexcept;

		/** @brief 성분들이 제한된(Clamp) 새로운 3D 벡터를 반환합니다. */
		float3 clamp( const float3& minValue, const float3& maxValue ) const noexcept;

		/** @brief clamp()와 동일하게 제한된 사본을 반환합니다. */
		float3 clamped( const float3& minValue, const float3& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

		/** @brief 이 벡터의 길이(크기)를 반환합니다. */
		float32 getLength() const noexcept;

		/** @brief 길이의 제곱(Square Magnitude)을 반환합니다. */
		float32 getLengthSquared() const noexcept;

		/** @brief 두 벡터 사이의 사잇각(Radian)을 구합니다. */
		float32 getAngleBetween( const float3& other ) const noexcept;

	public:
		bool operator==( const float3& other ) const noexcept;
		bool operator!=( const float3& other ) const noexcept;

		float3& operator+=( const float3& other ) noexcept;
		float3& operator-=( const float3& other ) noexcept;
		float3& operator*=( float32 scale ) noexcept;
		float3& operator/=( float32 scale ) noexcept;

		float3 operator+() const noexcept { return *this; }
		float3 operator-() const noexcept;
	};

	float3 operator+( const float3& lhs, const float3& rhs ) noexcept;
	float3 operator-( const float3& lhs, const float3& rhs ) noexcept;
	float3 operator*( const float3& v, float32 scale ) noexcept;
	float3 operator/( const float3& v, float32 scale ) noexcept;
	float3 operator*( float32 scale, const float3& v ) noexcept;

	/**
	 * @struct float4
	 * @brief 4차원 부동소수점 벡터. (동차 좌표, RGBA 색상, 셰이더 상수 버퍼 정렬 단위로 사용)
	 */
	struct SW_API float4 final
	{
		static const float4 Zero;
		static const float4 UnitX;
		static const float4 UnitY;
		static const float4 UnitZ;
		static const float4 UnitW;
		static const float4 UnitScale;

		float32 _x;
		float32 _y;
		float32 _z;
		float32 _w;

		constexpr float4() noexcept
			: _x{ 0.f }, _y{ 0.f }, _z{ 0.f }, _w{ 0.f }
		{
		}

		constexpr explicit float4( const float32 value ) noexcept
			: _x{ value }, _y{ value }, _z{ value }, _w{ value }
		{
		}

		constexpr float4( const float32 x, const float32 y, const float32 z, const float32 w ) noexcept
			: _x{ x }, _y{ y }, _z{ z }, _w{ w }
		{
		}

		constexpr explicit float4( const float32* pArray ) noexcept
			: _x{ pArray[0] }, _y{ pArray[1] }, _z{ pArray[2] }, _w{ pArray[3] }
		{
		}

	public:
		/**
		 * @brief 거리를 계산합니다
		 */
		static float32 getDistance( const float4& from, const float4& to ) noexcept;
		/**
		 * @brief 거리 제곱을 계산합니다
		 */
		static float32 getDistanceSquared( const float4& from, const float4& to ) noexcept;

		/**
		 * @brief 성분별 최솟값을 반환합니다
		 */
		static float4 min( const float4& lhs, const float4& rhs ) noexcept;
		/**
		 * @brief 성분별 최댓값을 반환합니다
		 */
		static float4 max( const float4& lhs, const float4& rhs ) noexcept;

		/**
		 * @brief 선형 보간합니다
		 */
		static float4 lerp( const float4& from, const float4& to, float32 ratio ) noexcept;
		/**
		 * @brief SmoothStep 보간합니다
		 */
		static float4 smoothStep( const float4& from, const float4& to, float32 ratio ) noexcept;

		/**
		 * @brief 무게중심 좌표로 보간합니다
		 */
		static float4 barycentric( const float4& v1, const float4& v2, const float4& v3, float32 f, float32 g ) noexcept;
		/**
		 * @brief Catmull-Rom 스플라인 보간합니다
		 */
		static float4 catmullRom( const float4& v1, const float4& v2, const float4& v3, const float4& v4, float32 t ) noexcept;
		/**
		 * @brief Hermite 스플라인 보간합니다
		 */
		static float4 hermite( const float4& p1, const float4& slope1, const float4& p2, const float4& slope2, float32 t ) noexcept;

		/**
		 * @brief 바이트코드에서 리플렉션 데이터를 추출합니다
		 */
		static float4 reflect( const float4& source, const float4& normal ) noexcept;

		/**
		 * @brief 변환을 적용합니다
		 */
		static float4 transform( const float4& v, const quaternion& rotation ) noexcept;
		/**
		 * @brief 변환을 적용합니다
		 */
		static float4 transform( const float4& v, const float4x4& matrix ) noexcept;
		/**
		 * @brief 법선을 변환합니다
		 */
		static float4 transformNormal( const float4& v, const float4x4& matrix ) noexcept;

	public:
		/**
		 * @brief 내적을 계산합니다
		 */
		float32 dot( const float4& other ) const noexcept;
		/**
		 * @brief 정규화합니다
		 */
		float4& normalize() noexcept;
		/**
		 * @brief 정규화합니다
		 */
		float4 normalize() const noexcept;

		/**
		 * @brief 범위 안에 있는지 반환합니다
		 */
		bool inBounds( const float4& bound ) const noexcept;

		/**
		 * @brief 범위를 제한합니다
		 */
		void clamp( const float4& minValue, const float4& maxValue ) noexcept;
		/**
		 * @brief 범위를 제한합니다
		 */
		float4 clamp( const float4& minValue, const float4& maxValue ) const noexcept;
		float4 clamped( const float4& minValue, const float4& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

		/**
		 * @brief 길이를 반환합니다
		 */
		float32 getLength() const noexcept;
		/**
		 * @brief 길이 제곱을 반환합니다
		 */
		float32 getLengthSquared() const noexcept;

	public:
		bool operator==( const float4& other ) const noexcept;
		bool operator!=( const float4& other ) const noexcept;

		float4& operator+=( const float4& other ) noexcept;
		float4& operator-=( const float4& other ) noexcept;
		float4& operator*=( float32 scale ) noexcept;
		float4& operator/=( float32 scale ) noexcept;

		float4 operator+() const noexcept { return *this; }
		float4 operator-() const noexcept;
	};

	float4 operator+( const float4& lhs, const float4& rhs ) noexcept;
	float4 operator-( const float4& lhs, const float4& rhs ) noexcept;
	float4 operator*( const float4& v, float32 scale ) noexcept;
	float4 operator/( const float4& v, float32 scale ) noexcept;
	float4 operator*( float32 scale, const float4& v ) noexcept;

	struct double3 final
	{
		static const double3 Zero;
		static const double3 UnitX;
		static const double3 UnitY;
		static const double3 UnitZ;
		static const double3 UnitScale;

		float64 _x;
		float64 _y;
		float64 _z;

		constexpr double3() noexcept
			: _x{ 0.0 }, _y{ 0.0 }, _z{ 0.0 }
		{
		}

		constexpr explicit double3( const float64 value ) noexcept
			: _x{ value }, _y{ value }, _z{ value }
		{
		}

		constexpr explicit double3( const float3& f ) noexcept
			: _x{ static_cast<float64>( f._x ) }, _y{ static_cast<float64>( f._y ) }, _z{ static_cast<float64>( f._z ) }
		{
		}

		constexpr double3( const float64 x, const float64 y, const float64 z ) noexcept
			: _x{ x }, _y{ y }, _z{ z }
		{
		}

		constexpr explicit double3( const float64* pArray ) noexcept
			: _x{ pArray[0] }, _y{ pArray[1] }, _z{ pArray[2] }
		{
		}

	public:
		/**
		 * @brief 거리를 계산합니다
		 */
		static float64 getDistance( const double3& from, const double3& to ) noexcept;
		/**
		 * @brief 거리 제곱을 계산합니다
		 */
		static float64 getDistanceSquared( const double3& from, const double3& to ) noexcept;

		/**
		 * @brief 성분별 최솟값을 반환합니다
		 */
		static double3 min( const double3& lhs, const double3& rhs ) noexcept;
		/**
		 * @brief 성분별 최댓값을 반환합니다
		 */
		static double3 max( const double3& lhs, const double3& rhs ) noexcept;

		/**
		 * @brief 선형 보간합니다
		 */
		static double3 lerp( const double3& from, const double3& to, float64 t ) noexcept;

	public:
		/**
		 * @brief 범위 안에 있는지 반환합니다
		 */
		bool inBounds( const double3& bound ) const noexcept;
		/**
		 * @brief Infinite 여부를 반환합니다
		 */
		bool isInfinite() const noexcept;

		/**
		 * @brief 내적을 계산합니다
		 */
		float64 dot( const double3& other ) const noexcept;
		/**
		 * @brief 외적을 계산합니다
		 */
		double3 cross( const double3& other ) const noexcept;
		/**
		 * @brief 정규화합니다
		 */
		double3& normalize() noexcept;
		/**
		 * @brief 정규화합니다
		 */
		double3 normalize() const noexcept;

		/**
		 * @brief 범위를 제한합니다
		 */
		void clamp( const double3& minValue, const double3& maxValue ) noexcept;
		/**
		 * @brief 범위를 제한합니다
		 */
		double3 clamp( const double3& minValue, const double3& maxValue ) const noexcept;
		double3 clamped( const double3& minValue, const double3& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

		float3 toFloat3() const noexcept { return float3{ static_cast<float32>( _x ), static_cast<float32>( _y ), static_cast<float32>( _z ) }; }

		/**
		 * @brief 길이를 반환합니다
		 */
		float64 getLength() const noexcept;
		/**
		 * @brief 길이 제곱을 반환합니다
		 */
		float64 getLengthSquared() const noexcept;

	public:
		bool operator==( const double3& other ) const noexcept;
		bool operator!=( const double3& other ) const noexcept;

		double3& operator+=( const double3& other ) noexcept;
		double3& operator-=( const double3& other ) noexcept;
		double3& operator*=( float64 scale ) noexcept;
		double3& operator/=( float64 scale ) noexcept;

		double3 operator+() const noexcept { return *this; }
		double3 operator-() const noexcept;
	};

	double3 operator+( const double3& lhs, const double3& rhs ) noexcept;
	double3 operator-( const double3& lhs, const double3& rhs ) noexcept;
	double3 operator*( const double3& v, float64 scale ) noexcept;
	double3 operator/( const double3& v, float64 scale ) noexcept;
	double3 operator*( float64 scale, const double3& v ) noexcept;
} // namespace sw

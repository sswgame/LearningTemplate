#pragma once
/**
 * @file MatrixMath.h
 * @brief 쿼터니언(quaternion)과 4x4 행렬(float4x4) 및 관련 변환 연산.
 */
#include "Core/Common/Types.h"
#include "Core/Utility/Math/VectorMath.h"

namespace sw
{

	/**
	 * @struct quaternion
	 * @brief 3차원 회전을 표현하는 4차원 복소수 기반 쿼터니언 구조체입니다. 짐벌락(Gimbal Lock) 없는 부드러운 회전 보간에 사용됩니다.
	 */
	struct SW_API quaternion final
	{
		static const quaternion Identity; /**< 회전이 적용되지 않은 단위 쿼터니언 (0, 0, 0, 1) */

		float32 _x;
		float32 _y;
		float32 _z;
		float32 _w;

		constexpr explicit quaternion() noexcept
			: _x{ 0.f }, _y{ 0.f }, _z{ 0.f }, _w{ 1.f }
		{
		}

		constexpr explicit quaternion( const float32 x, const float32 y, const float32 z, const float32 w ) noexcept
			: _x{ x }, _y{ y }, _z{ z }, _w{ w }
		{
		}

		constexpr explicit quaternion( const float3& v, const float32 scalar ) noexcept
			: _x{ v._x }, _y{ v._y }, _z{ v._z }, _w{ scalar }
		{
		}

		constexpr explicit quaternion( const float4& v ) noexcept
			: _x{ v._x }, _y{ v._y }, _z{ v._z }, _w{ v._w }
		{
		}

		constexpr explicit quaternion( const float32* pArray ) noexcept
			: _x{ pArray[0] }, _y{ pArray[1] }, _z{ pArray[2] }, _w{ pArray[3] }
		{
		}

	public:
		/**
		 * @brief 축-각에서 쿼터니언을 생성합니다
		 */
		static quaternion createFromAxisAngle( const float3& axis, float32 angle ) noexcept;
		/**
		 * @brief 요/피치/롤에서 행렬을 생성합니다
		 */
		static quaternion createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept;
		/**
		 * @brief 요/피치/롤에서 행렬을 생성합니다
		 */
		static quaternion createFromYawPitchRoll( const float3& angles ) noexcept;
		/**
		 * @brief 회전 행렬에서 쿼터니언을 생성합니다
		 */
		static quaternion createFromRotationMatrix( const float4x4& matrix ) noexcept;

		/**
		 * @brief 목표 방향으로 회전합니다
		 */
		static quaternion rotateTowards( const quaternion& from, const quaternion& to, float32 maxAngle ) noexcept;

		/**
		 * @brief 선형 보간합니다
		 */
		static quaternion lerp( const quaternion& from, const quaternion& to, float32 t ) noexcept;
		/**
		 * @brief 구면 선형 보간합니다
		 */
		static quaternion slerp( const quaternion& from, const quaternion& to, float32 t ) noexcept;

		/**
		 * @brief 변환을 이어 붙입니다
		 */
		static quaternion concatenate( const quaternion& q1, const quaternion& q2 ) noexcept;
		/**
		 * @brief 방향 전환 회전을 생성합니다
		 */
		static quaternion fromToRotation( const float3& from, const float3& to ) noexcept;
		/**
		 * @brief 바라보는 회전을 생성합니다
		 */
		static quaternion lookRotation( const float3& direction, const float3& up ) noexcept;
		/**
		 * @brief AngleBetween을(를) 반환합니다
		 */
		static float32	  getAngleBetween( const quaternion& lhs, const quaternion& rhs ) noexcept;

	public:
		/**
		 * @brief 노름을 반환합니다
		 */
		float32 norm() const noexcept;
		/**
		 * @brief 노름 제곱을 반환합니다
		 */
		float32 normSquared() const noexcept;

		/**
		 * @brief 정규화합니다
		 */
		quaternion& normalize() noexcept;
		/**
		 * @brief 정규화합니다
		 */
		quaternion	normalize() const noexcept;

		/**
		 * @brief 켤레를 반환합니다
		 */
		void	   conjugate() noexcept;
		/**
		 * @brief 켤레를 반환합니다
		 */
		quaternion conjugate() const noexcept;

		/**
		 * @brief 역을 구합니다
		 */
		void	   inverse() noexcept;
		/**
		 * @brief 역을 구합니다
		 */
		quaternion inverse() const noexcept;

		/**
		 * @brief 내적을 계산합니다
		 */
		float32 dot( const quaternion& other ) const noexcept;

		/**
		 * @brief EulerAngles을(를) 반환합니다
		 */
		float3	 getEulerAngles() const noexcept;
		float3	 toEuler() const noexcept { return getEulerAngles(); }
		/**
		 * @brief 행렬로 변환합니다
		 */
		float4x4 toMatrix() const noexcept;

	public:
		bool operator==( const quaternion& other ) const noexcept;
		bool operator!=( const quaternion& other ) const noexcept;

		quaternion& operator+=( const quaternion& other ) noexcept;
		quaternion& operator-=( const quaternion& other ) noexcept;
		quaternion& operator*=( const quaternion& other ) noexcept;
		quaternion& operator*=( float32 scale ) noexcept;
		quaternion& operator/=( float32 scale ) noexcept;

		quaternion operator+() const noexcept { return *this; }
		quaternion operator-() const noexcept;
	};

	quaternion operator+( const quaternion& lhs, const quaternion& rhs ) noexcept;
	quaternion operator-( const quaternion& lhs, const quaternion& rhs ) noexcept;
	quaternion operator*( const quaternion& lhs, const quaternion& rhs ) noexcept;
	quaternion operator*( const quaternion& q, float32 scale ) noexcept;
	quaternion operator/( const quaternion& q, float32 scale ) noexcept;
	quaternion operator*( float32 scale, const quaternion& q ) noexcept;

	struct SW_API float4x4 final
	{
		static const float4x4 Identity;

		float32 _11, _12, _13, _14;
		float32 _21, _22, _23, _24;
		float32 _31, _32, _33, _34;
		float32 _41, _42, _43, _44;

		constexpr float4x4() noexcept
			: _11{ 1.f }, _12{ 0.f }, _13{ 0.f }, _14{ 0.f }, _21{ 0.f }, _22{ 1.f }, _23{ 0.f }, _24{ 0.f }, _31{ 0.f }, _32{ 0.f }, _33{ 1.f }, _34{ 0.f }, _41{ 0.f }, _42{ 0.f }, _43{ 0.f }, _44{ 1.f }
		{
		}

		constexpr float4x4( float32 m00, float32 m01, float32 m02, float32 m03,
							float32 m10, float32 m11, float32 m12, float32 m13,
							float32 m20, float32 m21, float32 m22, float32 m23,
							float32 m30, float32 m31, float32 m32, float32 m33 ) noexcept
			: _11{ m00 }, _12{ m01 }, _13{ m02 }, _14{ m03 }, _21{ m10 }, _22{ m11 }, _23{ m12 }, _24{ m13 }, _31{ m20 }, _32{ m21 }, _33{ m22 }, _34{ m23 }, _41{ m30 }, _42{ m31 }, _43{ m32 }, _44{ m33 }
		{
		}

		constexpr explicit float4x4( const float3& right, const float3& up, const float3& front ) noexcept
			: _11{ right._x }, _12{ right._y }, _13{ right._z }, _14{ 0.f }, _21{ up._x }, _22{ up._y }, _23{ up._z }, _24{ 0.f }, _31{ front._x }, _32{ front._y }, _33{ front._z }, _34{ 0.f }, _41{ 0.f }, _42{ 0.f }, _43{ 0.f }, _44{ 1.f }
		{
		}

		constexpr explicit float4x4( const float4& right, const float4& up, const float4& front, const float4& translation ) noexcept
			: _11{ right._x }, _12{ right._y }, _13{ right._z }, _14{ right._w }, _21{ up._x }, _22{ up._y }, _23{ up._z }, _24{ up._w }, _31{ front._x }, _32{ front._y }, _33{ front._z }, _34{ front._w }, _41{ translation._x }, _42{ translation._y }, _43{ translation._z }, _44{ translation._w }
		{
		}

		/**
		 * @brief 4x4 행렬을 생성합니다
		 */
		explicit float4x4( const float32* pArray ) noexcept;

	public:
		/**
		 * @brief 이동 행렬을 생성합니다
		 */
		static float4x4 createTranslation( const float3& position ) noexcept;
		/**
		 * @brief 이동 행렬을 생성합니다
		 */
		static float4x4 createTranslation( float32 x, float32 y, float32 z ) noexcept;

		/**
		 * @brief 스케일 행렬을 생성합니다
		 */
		static float4x4 createScale( const float3& scales ) noexcept;
		/**
		 * @brief 스케일 행렬을 생성합니다
		 */
		static float4x4 createScale( float32 x, float32 y, float32 z ) noexcept;
		/**
		 * @brief 스케일 행렬을 생성합니다
		 */
		static float4x4 createScale( float32 scale ) noexcept;

		/**
		 * @brief X축 회전 행렬을 생성합니다
		 */
		static float4x4 createRotationX( float32 radians ) noexcept;
		/**
		 * @brief Y축 회전 행렬을 생성합니다
		 */
		static float4x4 createRotationY( float32 radians ) noexcept;
		/**
		 * @brief Z축 회전 행렬을 생성합니다
		 */
		static float4x4 createRotationZ( float32 radians ) noexcept;

		/**
		 * @brief 축-각에서 쿼터니언을 생성합니다
		 */
		static float4x4 createFromAxisAngle( const float3& axis, float32 angle ) noexcept;

		/**
		 * @brief PerspectiveFieldOfView을(를) 생성합니다
		 */
		static float4x4 createPerspectiveFieldOfView( float32 fov, float32 aspectRatio, float32 nearPlane, float32 farPlane ) noexcept;
		/**
		 * @brief 원근 투영 행렬을 생성합니다
		 */
		static float4x4 createPerspective( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept;
		/**
		 * @brief PerspectiveOffCenter을(를) 생성합니다
		 */
		static float4x4 createPerspectiveOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept;

		/**
		 * @brief 직교 투영 행렬을 생성합니다
		 */
		static float4x4 createOrthographic( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept;
		/**
		 * @brief 오프센터 직교 투영 행렬을 생성합니다
		 */
		static float4x4 createOrthographicOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept;

		/**
		 * @brief LookAt 뷰 행렬을 생성합니다
		 */
		static float4x4 createLookAt( const float3& position, const float3& target, const float3& up ) noexcept;
		/**
		 * @brief World을(를) 생성합니다
		 */
		static float4x4 createWorld( const float3& position, const float3& forward, const float3& up ) noexcept;

		/**
		 * @brief FromQuaternion을(를) 생성합니다
		 */
		static float4x4 createFromQuaternion( const quaternion& quaternion ) noexcept;
		/**
		 * @brief 요/피치/롤에서 행렬을 생성합니다
		 */
		static float4x4 createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept;
		/**
		 * @brief 요/피치/롤에서 행렬을 생성합니다
		 */
		static float4x4 createFromYawPitchRoll( const float3& angles ) noexcept;

		/**
		 * @brief 선형 보간합니다
		 */
		static float4x4 lerp( const float4x4& from, const float4x4& to, float32 t ) noexcept;
		/**
		 * @brief 변환을 적용합니다
		 */
		static float4x4 transform( const float4x4& matrix, const quaternion& rotation ) noexcept;

	public:
		/**
		 * @brief TRS로 분해합니다
		 */
		bool decompose( float3& outScale, quaternion& outRotation, float3& outTranslation ) const noexcept;

		/**
		 * @brief Scale을(를) 반환합니다
		 */
		float3	   getScale() const noexcept;
		/**
		 * @brief Rotation을(를) 반환합니다
		 */
		quaternion getRotation() const noexcept;
		/**
		 * @brief Translation을(를) 반환합니다
		 */
		float3	   getTranslation() const noexcept;

		/**
		 * @brief Scale을(를) 설정합니다
		 */
		void setScale( const float3& scale ) noexcept;
		/**
		 * @brief Rotation을(를) 설정합니다
		 */
		void setRotation( const quaternion& rotation ) noexcept;
		/**
		 * @brief Translation을(를) 설정합니다
		 */
		void setTranslation( const float3& translation ) noexcept;

		/**
		 * @brief 행렬식을 계산합니다
		 */
		float32	 determinant() const noexcept;
		/**
		 * @brief 전치합니다
		 */
		float4x4 transpose() const noexcept;
		/**
		 * @brief 역행렬을 구합니다
		 */
		float4x4 invert() const noexcept;

	public:
		bool operator==( const float4x4& other ) const noexcept;
		bool operator!=( const float4x4& other ) const noexcept;

		float4x4& operator+=( const float4x4& other ) noexcept;
		float4x4& operator-=( const float4x4& other ) noexcept;
		float4x4& operator*=( const float4x4& other ) noexcept;
		float4x4& operator*=( float32 scale ) noexcept;
		float4x4& operator/=( float32 scale ) noexcept;

		float4x4 operator+() const noexcept { return *this; }
		float4x4 operator-() const noexcept;
	};

	float4x4 operator+( const float4x4& lhs, const float4x4& rhs ) noexcept;
	float4x4 operator-( const float4x4& lhs, const float4x4& rhs ) noexcept;
	float4x4 operator*( const float4x4& lhs, const float4x4& rhs ) noexcept;
	float4x4 operator*( const float4x4& matrix, float32 scale ) noexcept;
	float4x4 operator/( const float4x4& matrix, float32 scale ) noexcept;
	float4x4 operator*( float32 scale, const float4x4& matrix ) noexcept;
}

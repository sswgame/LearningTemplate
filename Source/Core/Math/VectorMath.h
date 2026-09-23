/**
 * @file VectorMath.h
 * @brief 2D · 3D · 4D 벡터 타입과 그 연산입니다. 행렬 · 쿼터니언은 MatrixMath.h 에 있습니다.
 * @details Direct3D 12 HLSL 의 구조체 메모리 배치와 맞도록 정렬을 유지합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 회전에 쓰는 단위 쿼터니언입니다. 정의는 MatrixMath.h 에 있습니다. */
    struct quaternion;
    /** @brief 4x4 변환 행렬입니다. 정의는 MatrixMath.h 에 있습니다. */
    struct float4x4;
} // namespace sw

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) float2 — 2D. 생성 · 성분 연산 · 길이 · 정규화
    // ------------------------------------------------------------------------------
    /**
     * @struct float2
     * @brief 2차원 부동소수점 벡터입니다(UV 좌표, 화면 좌표 등).
     */
    struct SW_API float2 final
    {
        static const float2 Zero;
        static const float2 UnitX;
        static const float2 UnitY;
        static const float2 UnitScale;

        float32 _x;
        float32 _y;

        /** @brief (0, 0) 으로 둡니다. */
        constexpr float2() noexcept
            : _x{ 0.f }
            , _y{ 0.f } {}

        /** @brief x=y=value 로 둡니다. */
        constexpr explicit float2( const float32 value ) noexcept
            : _x{ value }
            , _y{ value } {}

        /** @brief (x, y) 로 둡니다. */
        constexpr float2( const float32 x, const float32 y ) noexcept
            : _x{ x }
            , _y{ y } {}

        /** @brief 배열 [0],[1] 을 x,y 로 읽습니다. */
        constexpr explicit float2( const float32* pArray ) noexcept
            : _x{ pArray[0] }
            , _y{ pArray[1] } {}

        /** @brief 두 벡터 사이의 거리(유클리드)를 구합니다. */
        static float32 getDistance( const float2& from, const float2& to ) noexcept;

        /** @brief 두 벡터 사이 거리의 제곱을 구합니다. 제곱근을 피하고 싶을 때 씁니다. */
        static float32 getDistanceSquared( const float2& from, const float2& to ) noexcept;

        /** @brief 성분별 최솟값으로 된 벡터를 반환합니다. */
        static float2 min( const float2& lhs, const float2& rhs ) noexcept;

        /** @brief 성분별 최댓값으로 된 벡터를 반환합니다. */
        static float2 max( const float2& lhs, const float2& rhs ) noexcept;

        /** @brief 두 벡터 사이를 t(0.0 ~ 1.0)만큼 선형 보간합니다. */
        static float2 lerp( const float2& from, const float2& to, float32 t ) noexcept;

        /** @brief 두 벡터 사이를 부드럽게 보간합니다(smoothstep). */
        static float2 smoothStep( const float2& from, const float2& to, float32 t ) noexcept;

        /** @brief 무게중심(barycentric) 좌표 f, g 로 세 점 사이의 점을 구합니다(v1 + f(v2 - v1) + g(v3 - v1)). */
        static float2 barycentric( const float2& v1, const float2& v2, const float2& v3, float32 f, float32 g ) noexcept;

        /** @brief Catmull-Rom 스플라인으로 보간합니다. */
        static float2 catmullRom( const float2& v1, const float2& v2, const float2& v3, const float2& v4, float32 t ) noexcept;

        /** @brief Hermite 스플라인으로 보간합니다. */
        static float2 hermite( const float2& p1, const float2& slope1, const float2& p2, const float2& slope2, float32 t ) noexcept;

        /** @brief 법선 normal 에 대해 입사 벡터 source 가 반사된 벡터를 구합니다. */
        static float2 reflect( const float2& source, const float2& normal ) noexcept;

        /** @brief 쿼터니언으로 2D 벡터를 회전합니다. */
        static float2 transform( const float2& v, const quaternion& rotation ) noexcept;

        /** @brief 4x4 행렬로 2D 좌표(z = 0, w = 1)를 변환합니다. */
        static float2 transform( const float2& v, const float4x4& matrix ) noexcept;

        /** @brief 4x4 행렬로 2D 법선을 변환합니다(평행 이동은 무시합니다). */
        static float2 transformNormal( const float2& v, const float4x4& matrix ) noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float2& bound ) const noexcept;

        /** @brief 성분 중 하나라도 무한대이면 true 입니다. */
        bool isInfinite() const noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float2& other ) const noexcept;

        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float2& normalize() noexcept;

        /** @brief 정규화한 사본을 반환합니다. */
        float2 normalize() const noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자릅니다(clamp). */
        void clamp( const float2& minValue, const float2& maxValue ) noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자른 사본을 반환합니다. */
        float2 clamp( const float2& minValue, const float2& maxValue ) const noexcept;

        /** @brief `clamp() const` 와 같습니다(자른 사본을 반환합니다). */
        float2 clamped( const float2& minValue, const float2& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

        /** @brief 길이를 반환합니다. */
        float32 getLength() const noexcept;

        /** @brief 길이의 제곱을 반환합니다. */
        float32 getLengthSquared() const noexcept;

        /** @brief 같은지 비교합니다. */
        bool operator==( const float2& other ) const noexcept;
        /** @brief 다른지 비교합니다. */
        bool operator!=( const float2& other ) const noexcept;

        /** @brief 더한 뒤 대입합니다. */
        float2& operator+=( const float2& other ) noexcept;
        /** @brief 뺀 뒤 대입합니다. */
        float2& operator-=( const float2& other ) noexcept;
        /** @brief 곱한 뒤 대입합니다. */
        float2& operator*=( float32 scale ) noexcept;
        /** @brief 나눈 뒤 대입합니다. */
        float2& operator/=( float32 scale ) noexcept;

        /** @brief 그대로 반환합니다(단항 +). */
        float2 operator+() const noexcept { return *this; }
        /** @brief 부호를 뒤집습니다(단항 -). */
        float2 operator-() const noexcept;
    };

    /** @brief 성분별로 더합니다. */
    inline float2 operator+( const float2& lhs, const float2& rhs ) noexcept { return float2{ lhs._x + rhs._x, lhs._y + rhs._y }; }
    /** @brief 성분별로 뺍니다. */
    inline float2 operator-( const float2& lhs, const float2& rhs ) noexcept { return float2{ lhs._x - rhs._x, lhs._y - rhs._y }; }
    /** @brief 스칼라를 곱합니다. */
    inline float2 operator*( const float2& v, float32 scale ) noexcept { return float2{ v._x * scale, v._y * scale }; }
    /** @brief 스칼라로 나눕니다(역수를 한 번 구해 곱합니다). */
    inline float2 operator/( const float2& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float2{ v._x * inv, v._y * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float2 operator*( float32 scale, const float2& v ) noexcept { return v * scale; }

    // ------------------------------------------------------------------------------
    // 1-a) int2 — 2D 정수 좌표. 타일 · 픽셀 · 정수 경계
    // ------------------------------------------------------------------------------
    /**
     * @struct int2
     * @brief 2차원 정수 벡터입니다(타일 좌표, 화면 픽셀 좌표, 정수 경계).
     *
     * @details **float2 로 대신하지 않는 이유.** 타일과 픽셀은 세는 값이지 재는 값이 아닙니다. float2 에 담으면 정수가 아닌
     *          값이 섞여 들 수 있고, 비교가 오차에 걸립니다. 언리얼도 같은 이유로 `FVector2D` 와 별개로 `FIntPoint` 를 둡니다.
     *
     * @note **일부러 얇게 두었습니다.** 정규화 · 길이 · 보간 · 행렬 변환은 넣지 않았습니다. 정수 좌표에서는 뜻이 없거나 실수로
     *       넘어가야 하는 연산이라, 필요해지면 쓰는 쪽을 보고 더합니다. 3성분(`int3`)도 만들지 않았습니다. 유일한 후보였던
     *       `PhysicsWorld::CellCoord` 는 공간 해시의 **키**라서 벡터 연산을 하지 않고 전용 해시 함수 객체를 달기 때문에,
     *       벡터 타입으로 바꿀 이유가 없습니다.
     */
    struct int2 final
    {
        int32 _x;
        int32 _y;

        /** @brief (0, 0) 으로 둡니다. */
        constexpr int2() noexcept
            : _x{ 0 }
            , _y{ 0 } {}

        /** @brief x=y=value 로 둡니다. */
        constexpr explicit int2( const int32 value ) noexcept
            : _x{ value }
            , _y{ value } {}

        /** @brief (x, y) 로 둡니다. */
        constexpr int2( const int32 x, const int32 y ) noexcept
            : _x{ x }
            , _y{ y } {}

        /** @brief 성분별 최솟값으로 구성된 벡터를 반환합니다. */
        static constexpr int2 min( const int2& lhs, const int2& rhs ) noexcept
        {
            return int2{ lhs._x < rhs._x ? lhs._x : rhs._x, lhs._y < rhs._y ? lhs._y : rhs._y };
        }

        /** @brief 성분별 최댓값으로 구성된 벡터를 반환합니다. */
        static constexpr int2 max( const int2& lhs, const int2& rhs ) noexcept
        {
            return int2{ lhs._x > rhs._x ? lhs._x : rhs._x, lhs._y > rhs._y ? lhs._y : rhs._y };
        }

        /** @brief 실수 벡터로 넓힙니다. 재는 값으로 넘어갈 때만 씁니다. */
        constexpr float2 toFloat2() const noexcept { return float2{ static_cast<float32>( _x ), static_cast<float32>( _y ) }; }

        /** @brief 같은지 비교합니다. 정수라 오차가 없습니다. */
        constexpr bool operator==( const int2& other ) const noexcept { return _x == other._x && _y == other._y; }
        /** @brief 다른지 비교합니다. */
        constexpr bool operator!=( const int2& other ) const noexcept { return ( *this == other ) == false; }

        /** @brief 더한 뒤 대입합니다. */
        constexpr int2& operator+=( const int2& other ) noexcept
        {
            _x += other._x;
            _y += other._y;
            return *this;
        }

        /** @brief 뺀 뒤 대입합니다. */
        constexpr int2& operator-=( const int2& other ) noexcept
        {
            _x -= other._x;
            _y -= other._y;
            return *this;
        }

        /** @brief 그대로 반환합니다(단항 +). */
        constexpr int2 operator+() const noexcept { return *this; }
        /** @brief 부호를 뒤집습니다(단항 -). */
        constexpr int2 operator-() const noexcept { return int2{ -_x, -_y }; }
    };

    /** @brief 성분별로 더합니다. */
    constexpr int2 operator+( const int2& lhs, const int2& rhs ) noexcept { return int2{ lhs._x + rhs._x, lhs._y + rhs._y }; }
    /** @brief 성분별로 뺍니다. */
    constexpr int2 operator-( const int2& lhs, const int2& rhs ) noexcept { return int2{ lhs._x - rhs._x, lhs._y - rhs._y }; }
    /** @brief 정수를 곱합니다. */
    constexpr int2 operator*( const int2& v, int32 scale ) noexcept { return int2{ v._x * scale, v._y * scale }; }
    /** @brief 정수를 곱합니다(scale * v). */
    constexpr int2 operator*( int32 scale, const int2& v ) noexcept { return v * scale; }

    // ------------------------------------------------------------------------------
    // 2) float3 — 3D. 내적 · 외적 · 거리 · 스플라인
    // ------------------------------------------------------------------------------
    /**
     * @struct float3
     * @brief 3차원 부동소수점 벡터입니다(월드 좌표, 방향 벡터, RGB 색상 등).
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

        /**
         * @brief 세 성분을 연속된 float 배열로 봅니다.
         * @details `&v._x` 를 `const float32*` 로 넘겨 `[1]` · `[2]` 를 읽는 코드가 여럿 있습니다. 형식상으로는 배열이 아닌 멤버를
         *          배열처럼 읽는 것이라 정적 분석기가 경계 위반으로 짚습니다. 그 가정을 이 함수 한 곳에 모으고, 아래 static_assert 가
         *          가정(패딩 없음)을 컴파일 타임에 지킵니다.
         */
        const float32* data() const noexcept { return &_x; }
        /** @brief 쓰기 가능한 오버로드입니다. */
        float32* data() noexcept { return &_x; }

        /** @brief (0, 0, 0) 으로 둡니다. */
        constexpr float3() noexcept
            : _x{ 0.f }
            , _y{ 0.f }
            , _z{ 0.f } {}

        /** @brief x = y = z = value 로 둡니다. */
        constexpr explicit float3( const float32 value ) noexcept
            : _x{ value }
            , _y{ value }
            , _z{ value } {}

        /** @brief (x, y, z) 로 둡니다. */
        constexpr float3( const float32 x, const float32 y, const float32 z ) noexcept
            : _x{ x }
            , _y{ y }
            , _z{ z } {}

        /** @brief 배열 [0],[1],[2] 를 x, y, z 로 읽습니다. */
        constexpr explicit float3( const float32* pArray ) noexcept
            : _x{ pArray[0] }
            , _y{ pArray[1] }
            , _z{ pArray[2] } {}

        /** @brief 2D 벡터와 z 성분으로 만듭니다. */
        constexpr float3( const float2& xy, const float32 z ) noexcept
            : _x{ xy._x }
            , _y{ xy._y }
            , _z{ z } {}

        /** @brief 두 벡터 사이의 거리를 구합니다. */
        static float32 getDistance( const float3& from, const float3& to ) noexcept;

        /** @brief 두 벡터 사이 거리의 제곱을 구합니다. */
        static float32 getDistanceSquared( const float3& from, const float3& to ) noexcept;

        /** @brief 성분별 최솟값으로 된 벡터를 반환합니다. */
        static float3 min( const float3& lhs, const float3& rhs ) noexcept;

        /** @brief 성분별 최댓값으로 된 벡터를 반환합니다. */
        static float3 max( const float3& lhs, const float3& rhs ) noexcept;

        /** @brief 두 벡터 사이를 t 만큼 선형 보간합니다. */
        static float3 lerp( const float3& from, const float3& to, float32 t ) noexcept;

        /** @brief 두 벡터 사이를 부드럽게 보간합니다(smoothstep). */
        static float3 smoothStep( const float3& from, const float3& to, float32 t ) noexcept;

        /** @brief 무게중심(barycentric) 좌표 f, g 로 세 점 사이의 점을 구합니다. */
        static float3 barycentric( const float3& v1, const float3& v2, const float3& v3, float32 f, float32 g ) noexcept;

        /** @brief Catmull-Rom 스플라인으로 보간합니다. */
        static float3 catmullRom( const float3& v1, const float3& v2, const float3& v3, const float3& v4, float32 t ) noexcept;

        /** @brief Hermite 스플라인으로 보간합니다. */
        static float3 hermite( const float3& p1, const float3& slope1, const float3& p2, const float3& slope2, float32 t ) noexcept;

        /** @brief 법선 normal 에 대해 입사 벡터 source 가 반사된 벡터를 구합니다. */
        static float3 reflect( const float3& source, const float3& normal ) noexcept;

        /** @brief from 을 to 방향으로 투영한 벡터를 구합니다. to 가 거의 0 이면 영벡터입니다. */
        static float3 project( const float3& from, const float3& to ) noexcept;

        /** @brief from 에서 to 방향 성분을 뺀 나머지(to 에 수직인 성분)를 구합니다. */
        static float3 perpendicular( const float3& from, const float3& to ) noexcept;

        /** @brief 법선 normal 에 대해 입사 벡터 source 가 굴절률 refractionIndex 로 굴절된 벡터를 구합니다. */
        static float3 refract( const float3& source, const float3& normal, float32 refractionIndex ) noexcept;

        /** @brief 쿼터니언으로 3D 벡터를 회전합니다. */
        static float3 transform( const float3& v, const quaternion& rotation ) noexcept;

        /** @brief 4x4 행렬로 3D 좌표(w = 1)를 변환합니다. 원근 나눗셈은 하지 않습니다. */
        static float3 transform( const float3& v, const float4x4& matrix ) noexcept;

        /** @brief 4x4 행렬로 3D 법선(w = 0)을 변환합니다. */
        static float3 transformNormal( const float3& v, const float4x4& matrix ) noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float3& other ) const noexcept;

        /** @brief 다른 벡터와의 외적을 구합니다. */
        float3 cross( const float3& other ) const noexcept;

        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float3& normalize() noexcept;

        /** @brief 정규화한 사본을 반환합니다. */
        float3 normalize() const noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float3& bound ) const noexcept;

        /** @brief 성분 중 하나라도 무한대이면 true 입니다. */
        bool isInfinite() const noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자릅니다(clamp). */
        void clamp( const float3& minValue, const float3& maxValue ) noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자른 사본을 반환합니다. */
        float3 clamp( const float3& minValue, const float3& maxValue ) const noexcept;

        /** @brief `clamp() const` 와 같습니다(자른 사본을 반환합니다). */
        float3 clamped( const float3& minValue, const float3& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

        /** @brief 길이를 반환합니다. */
        float32 getLength() const noexcept;

        /** @brief 길이의 제곱을 반환합니다. */
        float32 getLengthSquared() const noexcept;

        /** @brief 두 벡터 사이의 각도(라디안)를 구합니다. */
        float32 getAngleBetween( const float3& other ) const noexcept;

        /** @brief 같은지 비교합니다. */
        bool operator==( const float3& other ) const noexcept;
        /** @brief 다른지 비교합니다. */
        bool operator!=( const float3& other ) const noexcept;

        /** @brief 더한 뒤 대입합니다. */
        float3& operator+=( const float3& other ) noexcept;
        /** @brief 뺀 뒤 대입합니다. */
        float3& operator-=( const float3& other ) noexcept;
        /** @brief 곱한 뒤 대입합니다. */
        float3& operator*=( float32 scale ) noexcept;
        /** @brief 나눈 뒤 대입합니다. */
        float3& operator/=( float32 scale ) noexcept;

        /** @brief 그대로 반환합니다(단항 +). */
        float3 operator+() const noexcept { return *this; }
        /** @brief 부호를 뒤집습니다(단항 -). */
        float3 operator-() const noexcept;
    };

    static_assert( sizeof( float3 ) == 3 * sizeof( float32 ), "float3 must be 3 contiguous floats" );

    /** @brief 성분별로 더합니다. */
    inline float3 operator+( const float3& lhs, const float3& rhs ) noexcept { return float3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z }; }
    /** @brief 성분별로 뺍니다. */
    inline float3 operator-( const float3& lhs, const float3& rhs ) noexcept { return float3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z }; }
    /** @brief 스칼라를 곱합니다. */
    inline float3 operator*( const float3& v, float32 scale ) noexcept { return float3{ v._x * scale, v._y * scale, v._z * scale }; }
    /** @brief 스칼라로 나눕니다. */
    inline float3 operator/( const float3& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float3{ v._x * inv, v._y * inv, v._z * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float3 operator*( float32 scale, const float3& v ) noexcept { return v * scale; }

    // ------------------------------------------------------------------------------
    // 3) float4 — 동차 좌표 · RGBA. HLSL float4 와 같은 배치
    // ------------------------------------------------------------------------------
    /**
     * @struct float4
     * @brief 4차원 부동소수점 벡터입니다(동차 좌표, RGBA 색상, 셰이더 상수 버퍼의 정렬 단위).
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

        /** @brief (0, 0, 0, 0) 으로 둡니다. */
        constexpr float4() noexcept
            : _x{ 0.f }
            , _y{ 0.f }
            , _z{ 0.f }
            , _w{ 0.f } {}

        /** @brief x = y = z = w = value 로 둡니다. */
        constexpr explicit float4( const float32 value ) noexcept
            : _x{ value }
            , _y{ value }
            , _z{ value }
            , _w{ value } {}

        /** @brief (x, y, z, w) 로 둡니다. */
        constexpr float4( const float32 x, const float32 y, const float32 z, const float32 w ) noexcept
            : _x{ x }
            , _y{ y }
            , _z{ z }
            , _w{ w } {}

        /** @brief 배열 [0] ~ [3] 을 x, y, z, w 로 읽습니다. */
        constexpr explicit float4( const float32* pArray ) noexcept
            : _x{ pArray[0] }
            , _y{ pArray[1] }
            , _z{ pArray[2] }
            , _w{ pArray[3] } {}

        /** @brief 3D 벡터와 w 성분으로 만듭니다. */
        constexpr float4( const float3& xyz, const float32 w ) noexcept
            : _x{ xyz._x }
            , _y{ xyz._y }
            , _z{ xyz._z }
            , _w{ w } {}

        /** @brief 두 2D 벡터(xy, zw)로 만듭니다. */
        constexpr float4( const float2& xy, const float2& zw ) noexcept
            : _x{ xy._x }
            , _y{ xy._y }
            , _z{ zw._x }
            , _w{ zw._y } {}

        /** @brief 두 벡터 사이의 거리를 구합니다. */
        static float32 getDistance( const float4& from, const float4& to ) noexcept;
        /** @brief 두 벡터 사이 거리의 제곱을 구합니다. */
        static float32 getDistanceSquared( const float4& from, const float4& to ) noexcept;

        /** @brief 성분별 최솟값으로 된 벡터를 반환합니다. */
        static float4 min( const float4& lhs, const float4& rhs ) noexcept;
        /** @brief 성분별 최댓값으로 된 벡터를 반환합니다. */
        static float4 max( const float4& lhs, const float4& rhs ) noexcept;

        /** @brief 두 벡터 사이를 ratio 만큼 선형 보간합니다. */
        static float4 lerp( const float4& from, const float4& to, float32 ratio ) noexcept;
        /** @brief 두 벡터 사이를 부드럽게 보간합니다(smoothstep). */
        static float4 smoothStep( const float4& from, const float4& to, float32 ratio ) noexcept;

        /** @brief 무게중심(barycentric) 좌표 f, g 로 세 점 사이의 점을 구합니다. */
        static float4 barycentric( const float4& v1, const float4& v2, const float4& v3, float32 f, float32 g ) noexcept;
        /** @brief Catmull-Rom 스플라인으로 보간합니다. */
        static float4 catmullRom( const float4& v1, const float4& v2, const float4& v3, const float4& v4, float32 t ) noexcept;
        /** @brief Hermite 스플라인으로 보간합니다. */
        static float4 hermite( const float4& p1, const float4& slope1, const float4& p2, const float4& slope2, float32 t ) noexcept;

        /** @brief 법선 normal 에 대해 입사 벡터 source 가 반사된 벡터를 구합니다. */
        static float4 reflect( const float4& source, const float4& normal ) noexcept;

        /** @brief 쿼터니언으로 회전합니다. */
        static float4 transform( const float4& v, const quaternion& rotation ) noexcept;
        /** @brief 4x4 행렬로 변환합니다. */
        static float4 transform( const float4& v, const float4x4& matrix ) noexcept;
        /** @brief 4x4 행렬의 회전 · 스케일 부분(3x3)으로 법선을 변환합니다. 결과의 w 는 0 입니다. */
        static float4 transformNormal( const float4& v, const float4x4& matrix ) noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float4& other ) const noexcept;
        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float4& normalize() noexcept;
        /** @brief 정규화한 사본을 반환합니다. */
        float4 normalize() const noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float4& bound ) const noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자릅니다(clamp). */
        void clamp( const float4& minValue, const float4& maxValue ) noexcept;
        /** @brief 각 성분을 minValue ~ maxValue 로 자른 사본을 반환합니다. */
        float4 clamp( const float4& minValue, const float4& maxValue ) const noexcept;
        /** @brief `clamp() const` 와 같습니다(자른 사본을 반환합니다). */
        float4 clamped( const float4& minValue, const float4& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

        /** @brief 길이를 반환합니다. */
        float32 getLength() const noexcept;
        /** @brief 길이의 제곱을 반환합니다. */
        float32 getLengthSquared() const noexcept;

        /** @brief 같은지 비교합니다. */
        bool operator==( const float4& other ) const noexcept;
        /** @brief 다른지 비교합니다. */
        bool operator!=( const float4& other ) const noexcept;

        /** @brief 더한 뒤 대입합니다. */
        float4& operator+=( const float4& other ) noexcept;
        /** @brief 뺀 뒤 대입합니다. */
        float4& operator-=( const float4& other ) noexcept;
        /** @brief 곱한 뒤 대입합니다. */
        float4& operator*=( float32 scale ) noexcept;
        /** @brief 나눈 뒤 대입합니다. */
        float4& operator/=( float32 scale ) noexcept;

        /** @brief 그대로 반환합니다(단항 +). */
        float4 operator+() const noexcept { return *this; }
        /** @brief 부호를 뒤집습니다(단항 -). */
        float4 operator-() const noexcept;
    };

    /** @brief 성분별로 더합니다. */
    inline float4 operator+( const float4& lhs, const float4& rhs ) noexcept { return float4{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z, lhs._w + rhs._w }; }
    /** @brief 성분별로 뺍니다. */
    inline float4 operator-( const float4& lhs, const float4& rhs ) noexcept { return float4{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z, lhs._w - rhs._w }; }
    /** @brief 스칼라를 곱합니다. */
    inline float4 operator*( const float4& v, float32 scale ) noexcept { return float4{ v._x * scale, v._y * scale, v._z * scale, v._w * scale }; }
    /** @brief 스칼라로 나눕니다. */
    inline float4 operator/( const float4& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float4{ v._x * inv, v._y * inv, v._z * inv, v._w * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float4 operator*( float32 scale, const float4& v ) noexcept { return v * scale; }

    // ------------------------------------------------------------------------------
    // 4) double3 — 배정밀도 3D
    // ------------------------------------------------------------------------------
    /** @brief 배정밀도 3D 벡터입니다. */
    struct SW_API double3 final
    {
        static const double3 Zero;
        static const double3 UnitX;
        static const double3 UnitY;
        static const double3 UnitZ;
        static const double3 UnitScale;

        float64 _x;
        float64 _y;
        float64 _z;

        /** @brief (0, 0, 0) 으로 둡니다. */
        constexpr double3() noexcept
            : _x{ 0.0 }
            , _y{ 0.0 }
            , _z{ 0.0 } {}

        /** @brief x = y = z = value 로 둡니다. */
        constexpr explicit double3( const float64 value ) noexcept
            : _x{ value }
            , _y{ value }
            , _z{ value } {}

        /** @brief float3 를 배정밀도로 넓힙니다. */
        constexpr explicit double3( const float3& f ) noexcept
            : _x{ static_cast<float64>( f._x ) }
            , _y{ static_cast<float64>( f._y ) }
            , _z{ static_cast<float64>( f._z ) } {}

        /** @brief (x, y, z) 로 둡니다. */
        constexpr double3( const float64 x, const float64 y, const float64 z ) noexcept
            : _x{ x }
            , _y{ y }
            , _z{ z } {}

        /** @brief 배열 [0],[1],[2] 를 x, y, z 로 읽습니다. */
        constexpr explicit double3( const float64* pArray ) noexcept
            : _x{ pArray[0] }
            , _y{ pArray[1] }
            , _z{ pArray[2] } {}

        /** @brief 두 벡터 사이의 거리를 구합니다. */
        static float64 getDistance( const double3& from, const double3& to ) noexcept;
        /** @brief 두 벡터 사이 거리의 제곱을 구합니다. */
        static float64 getDistanceSquared( const double3& from, const double3& to ) noexcept;

        /** @brief 성분별 최솟값으로 된 벡터를 반환합니다. */
        static double3 min( const double3& lhs, const double3& rhs ) noexcept;
        /** @brief 성분별 최댓값으로 된 벡터를 반환합니다. */
        static double3 max( const double3& lhs, const double3& rhs ) noexcept;

        /** @brief 두 벡터 사이를 t 만큼 선형 보간합니다. */
        static double3 lerp( const double3& from, const double3& to, float64 t ) noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const double3& bound ) const noexcept;
        /** @brief 성분 중 하나라도 무한대이면 true 입니다. */
        bool isInfinite() const noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float64 dot( const double3& other ) const noexcept;
        /** @brief 다른 벡터와의 외적을 구합니다. */
        double3 cross( const double3& other ) const noexcept;
        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        double3& normalize() noexcept;
        /** @brief 정규화한 사본을 반환합니다. */
        double3 normalize() const noexcept;

        /** @brief 각 성분을 minValue ~ maxValue 로 자릅니다(clamp). */
        void clamp( const double3& minValue, const double3& maxValue ) noexcept;
        /** @brief 각 성분을 minValue ~ maxValue 로 자른 사본을 반환합니다. */
        double3 clamp( const double3& minValue, const double3& maxValue ) const noexcept;
        /** @brief `clamp() const` 와 같습니다(자른 사본을 반환합니다). */
        double3 clamped( const double3& minValue, const double3& maxValue ) const noexcept { return clamp( minValue, maxValue ); }

        /** @brief float3 로 좁힙니다(정밀도가 떨어집니다). */
        float3 toFloat3() const noexcept { return float3{ static_cast<float32>( _x ), static_cast<float32>( _y ), static_cast<float32>( _z ) }; }

        /** @brief 길이를 반환합니다. */
        float64 getLength() const noexcept;
        /** @brief 길이의 제곱을 반환합니다. */
        float64 getLengthSquared() const noexcept;

        bool operator==( const double3& other ) const noexcept;
        bool operator!=( const double3& other ) const noexcept;

        double3& operator+=( const double3& other ) noexcept;
        double3& operator-=( const double3& other ) noexcept;
        double3& operator*=( float64 scale ) noexcept;
        double3& operator/=( float64 scale ) noexcept;

        double3 operator+() const noexcept { return *this; }
        double3 operator-() const noexcept;
    };

    /** @brief 성분별로 더합니다. */
    inline double3 operator+( const double3& lhs, const double3& rhs ) noexcept { return double3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z }; }
    /** @brief 성분별로 뺍니다. */
    inline double3 operator-( const double3& lhs, const double3& rhs ) noexcept { return double3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z }; }
    /** @brief 스칼라를 곱합니다. */
    inline double3 operator*( const double3& v, float64 scale ) noexcept { return double3{ v._x * scale, v._y * scale, v._z * scale }; }
    /** @brief 스칼라로 나눕니다. */
    inline double3 operator/( const double3& v, float64 scale ) noexcept
    {
        const float64 inv = 1.0 / scale;
        return double3{ v._x * inv, v._y * inv, v._z * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline double3 operator*( float64 scale, const double3& v ) noexcept { return v * scale; }
} // namespace sw

/**
 * @file VectorMath.h
 * @brief 2D · 3D · 4D 벡터 타입과 그 연산입니다. 행렬 · 쿼터니언은 MatrixMath.h 에 있습니다.
 * @details Direct3D 12 HLSL 의 구조체 메모리 배치와 맞도록 정렬을 유지합니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

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

        /** @brief 4x4 행렬로 2D **방향**(w = 0)을 변환합니다 — 평행 이동은 무시하고 회전 · 스케일만 겁니다(언리얼 `TransformVector`). */
        static float2 transformVector( const float2& v, const float4x4& matrix ) noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float2& bound ) const noexcept;

        /** @brief 성분 중 하나라도 무한대이면 true 입니다. */
        bool isInfinite() const noexcept;
        /** @brief 성분이 모두 유한하면(NaN · 무한대가 없으면) true 입니다(언리얼 `ContainsNaN` 의 반대). `isInfinite` 는 NaN 을 잡지 못한다. */
        bool isFinite() const noexcept;
        /** @brief 성분마다 차이가 @p tolerance 보다 작으면 true 입니다(언리얼 `Equals( V, Tolerance )`). `==` 는 허용치가 `MathUtil::kEpsilon` 으로 고정이다. */
        bool nearEqual( const float2& other, float32 tolerance ) const noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float2& other ) const noexcept;

        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float2& normalize() noexcept;

        /** @brief 정규화한 사본을 반환합니다. */
        float2 normalize() const noexcept;
        /** @brief 정규화한 사본입니다. 길이가 @p minLength 이하이거나 유한하지 않으면 @p fallback 입니다(방향이 없는 입력 — 문턱은 부르는 쪽의 단위가 정한다). */
        float2 normalizeOr( const float2& fallback, float32 minLength ) const noexcept;

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
} // namespace sw

namespace sw
{
    /** @brief 성분별로 더합니다. */
    inline float2 operator+( const float2& lhs, const float2& rhs ) noexcept
    {
        return float2{ lhs._x + rhs._x, lhs._y + rhs._y };
    }
    /** @brief 성분별로 뺍니다. */
    inline float2 operator-( const float2& lhs, const float2& rhs ) noexcept
    {
        return float2{ lhs._x - rhs._x, lhs._y - rhs._y };
    }
    /** @brief 스칼라를 곱합니다. */
    inline float2 operator*( const float2& v, float32 scale ) noexcept
    {
        return float2{ v._x * scale, v._y * scale };
    }
    /** @brief 스칼라로 나눕니다(역수를 한 번 구해 곱합니다). */
    inline float2 operator/( const float2& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float2{ v._x * inv, v._y * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float2 operator*( float32 scale, const float2& v ) noexcept
    {
        return v * scale;
    }

    // ------------------------------------------------------------------------------
    // 1-a) int2 — 2D 정수 좌표. 타일 · 픽셀 · 정수 경계
    // ------------------------------------------------------------------------------
    /**
     * @struct int2
     * @brief 2차원 정수 벡터입니다(타일 좌표, 화면 픽셀 좌표, 정수 경계).
     *
     * @details **float2 로 대신하지 않는 이유.** 타일과 픽셀은 세는 값이지 측정하는 값이 아닙니다. float2 에 담으면 정수가 아닌
     *          값이 섞여 들 수 있고, 비교가 오차에 걸립니다. 언리얼도 같은 이유로 `FVector2D` 와 별개로 `FIntPoint` 를 둡니다.
     *
     * @note **일부러 얇게 두었습니다.** 정규화 · 길이 · 보간 · 행렬 변환은 넣지 않았습니다. 정수 좌표에서는 뜻이 없거나 실수로
     *       넘어가야 하는 연산이라, 필요해지면 쓰는 쪽을 보고 더합니다. 3성분(`int3`)도 만들지 않았습니다. 유일한 후보였던
     *       공간 해시의 셀 좌표(옛 겹침 월드의 `CellCoord`)는 공간 해시의 **키**라서 벡터 연산을 하지 않고 전용 해시 함수 객체를 달기 때문에,
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

        /** @brief 실수 벡터로 넓힙니다. 측정하는 값으로 넘어갈 때만 씁니다. */
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
} // namespace sw

namespace sw
{
    /** @brief 성분별로 더합니다. */
    constexpr int2 operator+( const int2& lhs, const int2& rhs ) noexcept
    {
        return int2{ lhs._x + rhs._x, lhs._y + rhs._y };
    }
    /** @brief 성분별로 뺍니다. */
    constexpr int2 operator-( const int2& lhs, const int2& rhs ) noexcept
    {
        return int2{ lhs._x - rhs._x, lhs._y - rhs._y };
    }
    /** @brief 정수를 곱합니다. */
    constexpr int2 operator*( const int2& v, int32 scale ) noexcept
    {
        return int2{ v._x * scale, v._y * scale };
    }
    /** @brief 정수를 곱합니다(scale * v). */
    constexpr int2 operator*( int32 scale, const int2& v ) noexcept
    {
        return v * scale;
    }

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

        /** @brief 높이(y)를 뺀 수평(XZ 평면) 거리를 구합니다. */
        static float32 getHorizontalDistance( const float3& from, const float3& to ) noexcept;

        /** @brief 높이(y)를 뺀 수평(XZ 평면) 거리의 제곱을 구합니다. */
        static float32 getHorizontalDistanceSquared( const float3& from, const float3& to ) noexcept;

        /** @brief 성분별 최솟값으로 된 벡터를 반환합니다. */
        static float3 min( const float3& lhs, const float3& rhs ) noexcept;

        /** @brief 성분별 최댓값으로 된 벡터를 반환합니다. */
        static float3 max( const float3& lhs, const float3& rhs ) noexcept;

        /** @brief 성분별 곱(스케일 적용 · 색 곱)입니다. `operator*` 는 스칼라 곱만이라 손으로 세 성분을 적던 자리가 쓴다. */
        static float3 multiply( const float3& lhs, const float3& rhs ) noexcept;

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

        /**
         * @brief 4x4 행렬로 3D **방향**(w = 0)을 변환합니다 — 평행 이동은 무시하고 회전 · 스케일만 겁니다(언리얼 `TransformVector`).
         * @details 법선 변환이 아니다 — 비균등 스케일에서 법선은 역전치(여인수) 행렬로 옮겨야 한다
         *          (셰이더의 `Transform::computeWorldNormal`). 법선을 옮기려면 `matrix.invert().transpose()` 를 넘긴다.
         */
        static float3 transformVector( const float3& v, const float4x4& matrix ) noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float3& other ) const noexcept;

        /** @brief 다른 벡터와의 외적을 구합니다. */
        float3 cross( const float3& other ) const noexcept;

        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float3& normalize() noexcept;

        /** @brief 정규화한 사본을 반환합니다. */
        float3 normalize() const noexcept;
        /** @brief 정규화한 사본입니다. 길이가 @p minLength 이하이거나 유한하지 않으면 @p fallback 입니다(방향이 없는 입력 — 문턱은 부르는 쪽의 단위가 정한다). */
        float3 normalizeOr( const float3& fallback, float32 minLength ) const noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float3& bound ) const noexcept;

        /** @brief 성분 중 하나라도 무한대이면 true 입니다. */
        bool isInfinite() const noexcept;
        /** @brief 성분이 모두 유한하면(NaN · 무한대가 없으면) true 입니다(언리얼 `ContainsNaN` 의 반대). `isInfinite` 는 NaN 을 잡지 못한다. */
        bool isFinite() const noexcept;
        /** @brief 성분마다 차이가 @p tolerance 보다 작으면 true 입니다(언리얼 `Equals( V, Tolerance )`). `==` 는 허용치가 `MathUtil::kEpsilon` 으로 고정이다. */
        bool nearEqual( const float3& other, float32 tolerance ) const noexcept;

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
} // namespace sw

namespace sw
{
    static_assert( sizeof( float3 ) == 3 * sizeof( float32 ), "float3 must be 3 contiguous floats" );

    /** @brief 성분별로 더합니다. */
    inline float3 operator+( const float3& lhs, const float3& rhs ) noexcept
    {
        return float3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z };
    }
    /** @brief 성분별로 뺍니다. */
    inline float3 operator-( const float3& lhs, const float3& rhs ) noexcept
    {
        return float3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z };
    }
    /** @brief 스칼라를 곱합니다. */
    inline float3 operator*( const float3& v, float32 scale ) noexcept
    {
        return float3{ v._x * scale, v._y * scale, v._z * scale };
    }
    /** @brief 스칼라로 나눕니다. */
    inline float3 operator/( const float3& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float3{ v._x * inv, v._y * inv, v._z * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float3 operator*( float32 scale, const float3& v ) noexcept
    {
        return v * scale;
    }

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
        /** @brief 4x4 행렬의 회전 · 스케일 부분(3x3)으로 **방향**을 변환합니다(언리얼 `TransformVector`). 결과의 w 는 0 입니다. */
        static float4 transformVector( const float4& v, const float4x4& matrix ) noexcept;

        /** @brief 다른 벡터와의 내적을 구합니다. */
        float32 dot( const float4& other ) const noexcept;
        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. */
        float4& normalize() noexcept;
        /** @brief 정규화한 사본을 반환합니다. */
        float4 normalize() const noexcept;

        /** @brief 각 성분이 [-bound, bound] 안에 있으면 true 입니다. */
        bool isInBounds( const float4& bound ) const noexcept;
        /** @brief 성분이 모두 유한하면(NaN · 무한대가 없으면) true 입니다(언리얼 `ContainsNaN` 의 반대). `isInfinite` 는 NaN 을 잡지 못한다. */
        bool isFinite() const noexcept;
        /** @brief 성분마다 차이가 @p tolerance 보다 작으면 true 입니다(언리얼 `Equals( V, Tolerance )`). `==` 는 허용치가 `MathUtil::kEpsilon` 으로 고정이다. */
        bool nearEqual( const float4& other, float32 tolerance ) const noexcept;

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
} // namespace sw

namespace sw
{
    /** @brief 성분별로 더합니다. */
    inline float4 operator+( const float4& lhs, const float4& rhs ) noexcept
    {
        return float4{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z, lhs._w + rhs._w };
    }
    /** @brief 성분별로 뺍니다. */
    inline float4 operator-( const float4& lhs, const float4& rhs ) noexcept
    {
        return float4{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z, lhs._w - rhs._w };
    }
    /** @brief 스칼라를 곱합니다. */
    inline float4 operator*( const float4& v, float32 scale ) noexcept
    {
        return float4{ v._x * scale, v._y * scale, v._z * scale, v._w * scale };
    }
    /** @brief 스칼라로 나눕니다. */
    inline float4 operator/( const float4& v, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return float4{ v._x * inv, v._y * inv, v._z * inv, v._w * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline float4 operator*( float32 scale, const float4& v ) noexcept
    {
        return v * scale;
    }

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
        /** @brief 성분이 모두 유한하면(NaN · 무한대가 없으면) true 입니다(언리얼 `ContainsNaN` 의 반대). `isInfinite` 는 NaN 을 잡지 못한다. */
        bool isFinite() const noexcept;

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
    inline double3 operator+( const double3& lhs, const double3& rhs ) noexcept
    {
        return double3{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z };
    }
    /** @brief 성분별로 뺍니다. */
    inline double3 operator-( const double3& lhs, const double3& rhs ) noexcept
    {
        return double3{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z };
    }
    /** @brief 스칼라를 곱합니다. */
    inline double3 operator*( const double3& v, float64 scale ) noexcept
    {
        return double3{ v._x * scale, v._y * scale, v._z * scale };
    }
    /** @brief 스칼라로 나눕니다. */
    inline double3 operator/( const double3& v, float64 scale ) noexcept
    {
        const float64 inv = 1.0 / scale;
        return double3{ v._x * inv, v._y * inv, v._z * inv };
    }

    /** @brief 스칼라를 곱합니다(scale * v). */
    inline double3 operator*( float64 scale, const double3& v ) noexcept
    {
        return v * scale;
    }
} // namespace sw

// ------------------------------------------------------------------------------
// 6) 벡터 기본 연산의 인라인 정의 — 부동소수 축약(FMA 합치기)을 이 구간에서만 끈다
//
//    예전에는 결정성(파괴 · 파쇄 · 롤백이 구성 · 플랫폼을 넘어 같은 비트) 때문에 이 연산들을 축약을 끈 TU(`VectorMath.cpp`, `sw_mark_deterministic_sources`)의
//    줄 밖 함수로 두어 `dot` 하나가 호출 하나였다. 축약 금지를 TU 옵션이 아니라 이 구간의 pragma 로 걸면 인라인해도 비트가 같다 — 클랭은 축약을
//    프런트엔드가 식 단위로 정하고(`llvm.fmuladd` · `contract` 깃발), 이 구간의 곱 · 덧셈은 깃발 없는 명령으로 나와 인라인된 뒤에도 백엔드가 합치지 않는다
//    (유니티 Burst 의 결정성 모드 · 언리얼 `FVector` 도 기본 연산은 헤더 인라인이다).
//    - 클랭(clang-cl · Android · 웹): `float_control( push )` 가 축약 상태를 포함한 부동소수 상태 전체를 쌓고 `pop` 이 되돌린다. 그래서 이 헤더를 먼저 include 한
//      유니티 묶음(`sw_set_unity_build` — 여러 .cpp 를 한 TU 로 잇는다)에서도 구간 뒤 · 다음 파일은 명령줄 설정(Release 의 `-ffp-contract=on`, 결정성 TU 의 `off`)
//      그대로다 — 되돌리기를 `contract( on )` 으로 적으면 결정성 TU 의 나머지가 축약 켜짐으로 바뀌므로 쓰지 않는다. `#pragma once` 라 묶음 안에서 구간은 한 번이다.
//    - GCC: `push_options` · `optimize( "fp-contract=off" )` · `pop_options`. GCC 는 최적화 옵션이 다른 함수를 인라인하지 않아 호출로 남는다(비트는 같다).
//    - cl.exe: `/fp:precise` 는 VS2022 부터 축약하지 않고 이 저장소는 `/fp:contract` 를 켜지 않는다 — pragma 가 없어도 같다.
//    libm 을 부르는 것(`getAngleBetween`)과 행렬 · 쿼터니언이 필요한 것(`transform*`)은 `VectorMath.cpp` 의 줄 밖에 남는다.
// ------------------------------------------------------------------------------
#if defined( SW_COMPILER_CLANG )
    #pragma float_control( push )
    #pragma clang fp contract( off )
#elif defined( SW_COMPILER_GCC )
    #pragma GCC push_options
    #pragma GCC optimize( "fp-contract=off" )
#endif

namespace sw
{
    /**
     * @brief 이 구간 안의 보간 다항식입니다 — `MathUtil::lerp` · `smoothstep` · `catmullRom` · `hermite` 와 같은 식을 축약을 끈 구간에서 다시 적었다.
     * @details `MathUtil` 의 것은 이 구간 밖(템플릿 정의 자리)의 부동소수 상태로 인스턴스화되어, 부르는 TU 가 축약을 켜면 `a * b + c` 가 FMA 가 된다 —
     *          그러면 이 구간의 벡터 보간이 TU 마다 다른 비트를 낸다. 식을 바꿀 때는 `MathUtil` 과 함께 바꾼다.
     */
    struct VectorMathInternal
    {
        template <typename T>
        static SW_INLINE constexpr T lerp( T x, T y, T a ) noexcept
        {
            return x * ( T( 1 ) - a ) + y * a;
        }

        template <typename T>
        static SW_INLINE constexpr T smoothstep( T edge0, T edge1, T x ) noexcept
        {
            const T t = MathUtil::saturate( MathUtil::inverseLerp( edge0, edge1, x ) );
            return t * t * ( T( 3 ) - T( 2 ) * t );
        }

        template <typename T>
        static SW_INLINE constexpr T catmullRom( T v1, T v2, T v3, T v4, T t ) noexcept
        {
            const T t2 = t * t;
            const T t3 = t2 * t;
            return T( 0.5 ) * ( ( T( 2 ) * v2 ) + ( -v1 + v3 ) * t + ( T( 2 ) * v1 - T( 5 ) * v2 + T( 4 ) * v3 - v4 ) * t2 + ( -v1 + T( 3 ) * v2 - T( 3 ) * v3 + v4 ) * t3 );
        }

        template <typename T>
        static SW_INLINE constexpr T hermite( T p1, T s1, T p2, T s2, T t ) noexcept
        {
            const T t2 = t * t;
            const T t3 = t2 * t;
            const T h1 = T( 2 ) * t3 - T( 3 ) * t2 + T( 1 );
            const T h2 = -T( 2 ) * t3 + T( 3 ) * t2;
            const T h3 = t3 - T( 2 ) * t2 + t;
            const T h4 = t3 - t2;
            return h1 * p1 + h2 * p2 + h3 * s1 + h4 * s2;
        }
    };
} // namespace sw

namespace sw
{
    inline float32 float2::getDistance( const float2& from, const float2& to ) noexcept
    {
        return MathUtil::sqrt( getDistanceSquared( from, to ) );
    }

    inline float32 float2::getDistanceSquared( const float2& from, const float2& to ) noexcept
    {
        const float2 diff = to - from;
        return diff.getLengthSquared();
    }

    inline float2 float2::min( const float2& lhs, const float2& rhs ) noexcept
    {
        return float2{ MathUtil::min( lhs._x, rhs._x ), MathUtil::min( lhs._y, rhs._y ) };
    }

    inline float2 float2::max( const float2& lhs, const float2& rhs ) noexcept
    {
        return float2{ MathUtil::max( lhs._x, rhs._x ), MathUtil::max( lhs._y, rhs._y ) };
    }

    inline float2 float2::lerp( const float2& from, const float2& to, float32 t ) noexcept
    {
        return float2{ VectorMathInternal::lerp( from._x, to._x, t ), VectorMathInternal::lerp( from._y, to._y, t ) };
    }

    inline float2 float2::smoothStep( const float2& from, const float2& to, float32 t ) noexcept
    {
        float32 s = VectorMathInternal::smoothstep( 0.0f, 1.0f, t );
        return float2{ VectorMathInternal::lerp( from._x, to._x, s ), VectorMathInternal::lerp( from._y, to._y, s ) };
    }

    inline float2 float2::barycentric( const float2& v1, const float2& v2, const float2& v3, float32 f, float32 g ) noexcept
    {
        return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
    }

    inline float2 float2::catmullRom( const float2& v1, const float2& v2, const float2& v3, const float2& v4, float32 t ) noexcept
    {
        return float2{ VectorMathInternal::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), VectorMathInternal::catmullRom( v1._y, v2._y, v3._y, v4._y, t ) };
    }

    inline float2 float2::hermite( const float2& p1, const float2& slope1, const float2& p2, const float2& slope2, float32 t ) noexcept
    {
        return float2{ VectorMathInternal::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), VectorMathInternal::hermite( p1._y, slope1._y, p2._y, slope2._y, t ) };
    }

    inline float2 float2::reflect( const float2& source, const float2& normal ) noexcept
    {
        const float32 normalLenSq = normal.getLengthSquared();
        if ( normalLenSq < MathUtil::kEpsilon )
            return source;
        const float2 n = normal / MathUtil::sqrt( normalLenSq );
        return source - ( 2.f * source.dot( n ) ) * n;
    }

    inline bool float2::isInBounds( const float2& bound ) const noexcept
    {
        return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y );
    }

    inline bool float2::isInfinite() const noexcept
    {
        return MathUtil::isInfinite( _x ) || MathUtil::isInfinite( _y );
    }

    inline bool float2::isFinite() const noexcept
    {
        return MathUtil::isFinite( _x ) && MathUtil::isFinite( _y );
    }

    inline bool float2::nearEqual( const float2& other, float32 tolerance ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x, tolerance ) && MathUtil::nearEqual( _y, other._y, tolerance );
    }

    inline float32 float2::dot( const float2& other ) const noexcept
    {
        return ( _x * other._x ) + ( _y * other._y );
    }

    inline float2& float2::normalize() noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            ( *this ) *= invLen;
        else
            ( *this ) = Zero;
        return *this;
    }

    inline float2 float2::normalizeOr( const float2& fallback, float32 minLength ) const noexcept
    {
        const float32 length = getLength();
        if ( length <= minLength || MathUtil::isFinite( length ) == false )
            return fallback;
        return ( *this ) / length;
    }

    inline float2 float2::normalize() const noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            return ( *this ) * invLen;
        return Zero;
    }

    inline void float2::clamp( const float2& minValue, const float2& maxValue ) noexcept
    {
        _x = MathUtil::clamp( _x, minValue._x, maxValue._x );
        _y = MathUtil::clamp( _y, minValue._y, maxValue._y );
    }

    inline float2 float2::clamp( const float2& minValue, const float2& maxValue ) const noexcept
    {
        float2 result = *this;
        result.clamp( minValue, maxValue );
        return result;
    }

    inline float32 float2::getLength() const noexcept
    {
        return MathUtil::sqrt( getLengthSquared() );
    }

    inline float32 float2::getLengthSquared() const noexcept
    {
        return ( _x * _x ) + ( _y * _y );
    }

    inline bool float2::operator==( const float2& other ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y );
    }

    inline bool float2::operator!=( const float2& other ) const noexcept
    {
        return !( *this == other );
    }

    inline float2& float2::operator+=( const float2& other ) noexcept
    {
        _x += other._x;
        _y += other._y;
        return *this;
    }

    inline float2& float2::operator-=( const float2& other ) noexcept
    {
        _x -= other._x;
        _y -= other._y;
        return *this;
    }

    inline float2& float2::operator*=( float32 scale ) noexcept
    {
        _x *= scale;
        _y *= scale;
        return *this;
    }

    inline float2& float2::operator/=( float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        _x *= inv;
        _y *= inv;
        return *this;
    }

    inline float2 float2::operator-() const noexcept
    {
        return float2{ -_x, -_y };
    }

    inline float32 float3::getDistance( const float3& from, const float3& to ) noexcept
    {
        return MathUtil::sqrt( getDistanceSquared( from, to ) );
    }

    inline float32 float3::getDistanceSquared( const float3& from, const float3& to ) noexcept
    {
        const float3 result = to - from;
        return result.getLengthSquared();
    }

    inline float32 float3::getHorizontalDistance( const float3& from, const float3& to ) noexcept
    {
        return MathUtil::sqrt( getHorizontalDistanceSquared( from, to ) );
    }

    inline float32 float3::getHorizontalDistanceSquared( const float3& from, const float3& to ) noexcept
    {
        const float32 deltaX = to._x - from._x;
        const float32 deltaZ = to._z - from._z;
        return deltaX * deltaX + deltaZ * deltaZ;
    }

    inline float3 float3::multiply( const float3& lhs, const float3& rhs ) noexcept
    {
        return float3{ lhs._x * rhs._x, lhs._y * rhs._y, lhs._z * rhs._z };
    }

    inline float3 float3::min( const float3& lhs, const float3& rhs ) noexcept
    {
        return float3{ MathUtil::min( lhs._x, rhs._x ), MathUtil::min( lhs._y, rhs._y ), MathUtil::min( lhs._z, rhs._z ) };
    }

    inline float3 float3::max( const float3& lhs, const float3& rhs ) noexcept
    {
        return float3{ MathUtil::max( lhs._x, rhs._x ), MathUtil::max( lhs._y, rhs._y ), MathUtil::max( lhs._z, rhs._z ) };
    }

    inline float3 float3::lerp( const float3& from, const float3& to, float32 t ) noexcept
    {
        return float3{ VectorMathInternal::lerp( from._x, to._x, t ), VectorMathInternal::lerp( from._y, to._y, t ), VectorMathInternal::lerp( from._z, to._z, t ) };
    }

    inline float3 float3::smoothStep( const float3& from, const float3& to, float32 t ) noexcept
    {
        float32 s = VectorMathInternal::smoothstep( 0.0f, 1.0f, t );
        return float3{ VectorMathInternal::lerp( from._x, to._x, s ), VectorMathInternal::lerp( from._y, to._y, s ), VectorMathInternal::lerp( from._z, to._z, s ) };
    }

    inline float3 float3::barycentric( const float3& v1, const float3& v2, const float3& v3, float32 f, float32 g ) noexcept
    {
        return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
    }

    inline float3 float3::catmullRom( const float3& v1, const float3& v2, const float3& v3, const float3& v4, float32 t ) noexcept
    {
        return float3{ VectorMathInternal::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), VectorMathInternal::catmullRom( v1._y, v2._y, v3._y, v4._y, t ), VectorMathInternal::catmullRom( v1._z, v2._z, v3._z, v4._z, t ) };
    }

    inline float3 float3::hermite( const float3& p1, const float3& slope1, const float3& p2, const float3& slope2, float32 t ) noexcept
    {
        return float3{ VectorMathInternal::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), VectorMathInternal::hermite( p1._y, slope1._y, p2._y, slope2._y, t ), VectorMathInternal::hermite( p1._z, slope1._z, p2._z, slope2._z, t ) };
    }

    inline float3 float3::refract( const float3& source, const float3& normal, float32 refractionIndex ) noexcept
    {
        // 식은 DirectXMath XMVector3Refract 와 같다(법선은 단위 벡터로 만든다).
        // 전반사(근이 음수)면 영 벡터다.
        const float32 normalLenSq = normal.getLengthSquared();
        if ( normalLenSq < MathUtil::kEpsilon )
            return source;
        const float3  direction   = normal / MathUtil::sqrt( normalLenSq );
        const float32 incidentDot = source.dot( direction );
        const float32 radicand    = 1.f - refractionIndex * refractionIndex * ( 1.f - incidentDot * incidentDot );
        if ( radicand < 0.f )
            return float3{};
        return refractionIndex * source - ( refractionIndex * incidentDot + MathUtil::sqrt( radicand ) ) * direction;
    }

    inline float3 float3::reflect( const float3& source, const float3& normal ) noexcept
    {
        const float32 normalLenSq = normal.getLengthSquared();
        if ( normalLenSq < MathUtil::kEpsilon )
            return source;
        const float3  direction  = normal / MathUtil::sqrt( normalLenSq );
        const float32 dotProduct = source.dot( direction );
        return source - ( 2.f * dotProduct ) * direction;
    }

    inline float3 float3::project( const float3& from, const float3& to ) noexcept
    {
        const float32 toLenSq = to.getLengthSquared();
        if ( toLenSq < MathUtil::kEpsilon )
            return Zero;
        const float3  direction  = to / MathUtil::sqrt( toLenSq );
        const float32 dotProduct = from.dot( direction );
        return direction * dotProduct;
    }

    inline float3 float3::perpendicular( const float3& from, const float3& to ) noexcept
    {
        return from - project( from, to );
    }

    inline float32 float3::dot( const float3& other ) const noexcept
    {
        return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z );
    }

    inline float3 float3::cross( const float3& other ) const noexcept
    {
        float3 result{};
        result._x = ( _y * other._z - _z * other._y );
        result._y = ( _z * other._x - _x * other._z );
        result._z = ( _x * other._y - _y * other._x );
        return result;
    }

    inline float3& float3::normalize() noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            ( *this ) *= invLen;
        else
            ( *this ) = Zero;
        return *this;
    }

    inline float3 float3::normalizeOr( const float3& fallback, float32 minLength ) const noexcept
    {
        const float32 length = getLength();
        if ( length <= minLength || MathUtil::isFinite( length ) == false )
            return fallback;
        return ( *this ) / length;
    }

    inline float3 float3::normalize() const noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            return ( *this ) * invLen;
        return Zero;
    }

    inline bool float3::isInBounds( const float3& bound ) const noexcept
    {
        return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y ) && ( -bound._z <= _z && _z <= bound._z );
    }

    inline bool float3::isInfinite() const noexcept
    {
        return MathUtil::isInfinite( _x ) || MathUtil::isInfinite( _y ) || MathUtil::isInfinite( _z );
    }

    inline bool float3::isFinite() const noexcept
    {
        return MathUtil::isFinite( _x ) && MathUtil::isFinite( _y ) && MathUtil::isFinite( _z );
    }

    inline bool float3::nearEqual( const float3& other, float32 tolerance ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x, tolerance ) && MathUtil::nearEqual( _y, other._y, tolerance ) && MathUtil::nearEqual( _z, other._z, tolerance );
    }

    inline void float3::clamp( const float3& minValue, const float3& maxValue ) noexcept
    {
        _x = MathUtil::clamp( _x, minValue._x, maxValue._x );
        _y = MathUtil::clamp( _y, minValue._y, maxValue._y );
        _z = MathUtil::clamp( _z, minValue._z, maxValue._z );
    }

    inline float3 float3::clamp( const float3& minValue, const float3& maxValue ) const noexcept
    {
        float3 result = *this;
        result.clamp( minValue, maxValue );
        return result;
    }

    inline float32 float3::getLength() const noexcept
    {
        return MathUtil::sqrt( getLengthSquared() );
    }

    inline float32 float3::getLengthSquared() const noexcept
    {
        return ( _x * _x ) + ( _y * _y ) + ( _z * _z );
    }

    inline bool float3::operator==( const float3& other ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z );
    }

    inline bool float3::operator!=( const float3& other ) const noexcept
    {
        return !( *this == other );
    }

    inline float3& float3::operator+=( const float3& other ) noexcept
    {
        _x += other._x;
        _y += other._y;
        _z += other._z;
        return *this;
    }

    inline float3& float3::operator-=( const float3& other ) noexcept
    {
        _x -= other._x;
        _y -= other._y;
        _z -= other._z;
        return *this;
    }

    inline float3& float3::operator*=( float32 scale ) noexcept
    {
        _x *= scale;
        _y *= scale;
        _z *= scale;
        return *this;
    }

    inline float3& float3::operator/=( float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        _x *= inv;
        _y *= inv;
        _z *= inv;
        return *this;
    }

    inline float3 float3::operator-() const noexcept
    {
        return float3{ -_x, -_y, -_z };
    }

    inline float32 float4::getDistance( const float4& from, const float4& to ) noexcept
    {
        return MathUtil::sqrt( getDistanceSquared( from, to ) );
    }

    inline float32 float4::getDistanceSquared( const float4& from, const float4& to ) noexcept
    {
        const float4 result = to - from;
        return result.getLengthSquared();
    }

    inline float4 float4::min( const float4& lhs, const float4& rhs ) noexcept
    {
        return float4{ MathUtil::min( lhs._x, rhs._x ), MathUtil::min( lhs._y, rhs._y ), MathUtil::min( lhs._z, rhs._z ), MathUtil::min( lhs._w, rhs._w ) };
    }

    inline float4 float4::max( const float4& lhs, const float4& rhs ) noexcept
    {
        return float4{ MathUtil::max( lhs._x, rhs._x ), MathUtil::max( lhs._y, rhs._y ), MathUtil::max( lhs._z, rhs._z ), MathUtil::max( lhs._w, rhs._w ) };
    }

    inline float4 float4::lerp( const float4& from, const float4& to, float32 ratio ) noexcept
    {
        return float4{ VectorMathInternal::lerp( from._x, to._x, ratio ), VectorMathInternal::lerp( from._y, to._y, ratio ), VectorMathInternal::lerp( from._z, to._z, ratio ), VectorMathInternal::lerp( from._w, to._w, ratio ) };
    }

    inline float4 float4::smoothStep( const float4& from, const float4& to, float32 ratio ) noexcept
    {
        float32 s = VectorMathInternal::smoothstep( 0.0f, 1.0f, ratio );
        return float4{ VectorMathInternal::lerp( from._x, to._x, s ), VectorMathInternal::lerp( from._y, to._y, s ), VectorMathInternal::lerp( from._z, to._z, s ), VectorMathInternal::lerp( from._w, to._w, s ) };
    }

    inline float4 float4::barycentric( const float4& v1, const float4& v2, const float4& v3, float32 f, float32 g ) noexcept
    {
        return v1 + f * ( v2 - v1 ) + g * ( v3 - v1 );
    }

    inline float4 float4::catmullRom( const float4& v1, const float4& v2, const float4& v3, const float4& v4, float32 t ) noexcept
    {
        return float4{ VectorMathInternal::catmullRom( v1._x, v2._x, v3._x, v4._x, t ), VectorMathInternal::catmullRom( v1._y, v2._y, v3._y, v4._y, t ), VectorMathInternal::catmullRom( v1._z, v2._z, v3._z, v4._z, t ), VectorMathInternal::catmullRom( v1._w, v2._w, v3._w, v4._w, t ) };
    }

    inline float4 float4::hermite( const float4& p1, const float4& slope1, const float4& p2, const float4& slope2, float32 t ) noexcept
    {
        return float4{ VectorMathInternal::hermite( p1._x, slope1._x, p2._x, slope2._x, t ), VectorMathInternal::hermite( p1._y, slope1._y, p2._y, slope2._y, t ), VectorMathInternal::hermite( p1._z, slope1._z, p2._z, slope2._z, t ), VectorMathInternal::hermite( p1._w, slope1._w, p2._w, slope2._w, t ) };
    }

    inline float4 float4::reflect( const float4& source, const float4& normal ) noexcept
    {
        const float4  direction  = normal.normalize();
        const float32 dotProduct = source.dot( direction );
        return source - ( 2.f * dotProduct ) * direction;
    }

    inline bool float4::isFinite() const noexcept
    {
        return MathUtil::isFinite( _x ) && MathUtil::isFinite( _y ) && MathUtil::isFinite( _z ) && MathUtil::isFinite( _w );
    }

    inline bool float4::nearEqual( const float4& other, float32 tolerance ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x, tolerance ) && MathUtil::nearEqual( _y, other._y, tolerance ) && MathUtil::nearEqual( _z, other._z, tolerance ) && MathUtil::nearEqual( _w, other._w, tolerance );
    }

    inline float32 float4::dot( const float4& other ) const noexcept
    {
        return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z ) + ( _w * other._w );
    }

    inline float4& float4::normalize() noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            ( *this ) *= invLen;
        else
            ( *this ) = Zero;
        return *this;
    }

    inline float4 float4::normalize() const noexcept
    {
        const float32 invLen = MathUtil::invSqrt( getLengthSquared() );
        if ( invLen > 0.f )
            return ( *this ) * invLen;
        return Zero;
    }

    inline bool float4::isInBounds( const float4& bound ) const noexcept
    {
        // 범위 비교는 값을 가운데에 둔다(-bound <= v <= bound). float2 · float3 · double3 도 같고, docs/agents/CppStyle.md 의 규칙이기도 하다.
        return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y ) && ( -bound._z <= _z && _z <= bound._z ) && ( -bound._w <= _w && _w <= bound._w );
    }

    inline void float4::clamp( const float4& minValue, const float4& maxValue ) noexcept
    {
        _x = MathUtil::clamp( _x, minValue._x, maxValue._x );
        _y = MathUtil::clamp( _y, minValue._y, maxValue._y );
        _z = MathUtil::clamp( _z, minValue._z, maxValue._z );
        _w = MathUtil::clamp( _w, minValue._w, maxValue._w );
    }

    inline float4 float4::clamp( const float4& minValue, const float4& maxValue ) const noexcept
    {
        float4 result = *this;
        result.clamp( minValue, maxValue );
        return result;
    }

    inline float32 float4::getLength() const noexcept
    {
        return MathUtil::sqrt( getLengthSquared() );
    }

    inline float32 float4::getLengthSquared() const noexcept
    {
        return ( _x * _x ) + ( _y * _y ) + ( _z * _z ) + ( _w * _w );
    }

    inline bool float4::operator==( const float4& other ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z ) && MathUtil::nearEqual( _w, other._w );
    }

    inline bool float4::operator!=( const float4& other ) const noexcept
    {
        return !( *this == other );
    }

    inline float4& float4::operator+=( const float4& other ) noexcept
    {
        _x += other._x;
        _y += other._y;
        _z += other._z;
        _w += other._w;
        return *this;
    }

    inline float4& float4::operator-=( const float4& other ) noexcept
    {
        _x -= other._x;
        _y -= other._y;
        _z -= other._z;
        _w -= other._w;
        return *this;
    }

    inline float4& float4::operator*=( float32 scale ) noexcept
    {
        _x *= scale;
        _y *= scale;
        _z *= scale;
        _w *= scale;
        return *this;
    }

    inline float4& float4::operator/=( float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        _x *= inv;
        _y *= inv;
        _z *= inv;
        _w *= inv;
        return *this;
    }

    inline float4 float4::operator-() const noexcept
    {
        return float4{ -_x, -_y, -_z, -_w };
    }

    inline float64 double3::getDistance( const double3& from, const double3& to ) noexcept
    {
        return MathUtil::sqrt( getDistanceSquared( from, to ) );
    }

    inline float64 double3::getDistanceSquared( const double3& from, const double3& to ) noexcept
    {
        const double3 result = to - from;
        return result.getLengthSquared();
    }

    inline double3 double3::min( const double3& lhs, const double3& rhs ) noexcept
    {
        return double3{ MathUtil::min( lhs._x, rhs._x ), MathUtil::min( lhs._y, rhs._y ), MathUtil::min( lhs._z, rhs._z ) };
    }

    inline double3 double3::max( const double3& lhs, const double3& rhs ) noexcept
    {
        return double3{ MathUtil::max( lhs._x, rhs._x ), MathUtil::max( lhs._y, rhs._y ), MathUtil::max( lhs._z, rhs._z ) };
    }

    inline double3 double3::lerp( const double3& from, const double3& to, float64 t ) noexcept
    {
        return double3{ VectorMathInternal::lerp( from._x, to._x, t ), VectorMathInternal::lerp( from._y, to._y, t ), VectorMathInternal::lerp( from._z, to._z, t ) };
    }

    inline bool double3::isInBounds( const double3& bound ) const noexcept
    {
        return ( -bound._x <= _x && _x <= bound._x ) && ( -bound._y <= _y && _y <= bound._y ) && ( -bound._z <= _z && _z <= bound._z );
    }

    inline bool double3::isFinite() const noexcept
    {
        return MathUtil::isFinite( _x ) && MathUtil::isFinite( _y ) && MathUtil::isFinite( _z );
    }

    inline bool double3::isInfinite() const noexcept
    {
        return MathUtil::isInfinite( _x ) || MathUtil::isInfinite( _y ) || MathUtil::isInfinite( _z );
    }

    inline float64 double3::dot( const double3& other ) const noexcept
    {
        return ( _x * other._x ) + ( _y * other._y ) + ( _z * other._z );
    }

    inline double3 double3::cross( const double3& other ) const noexcept
    {
        double3 result{};
        result._x = ( _y * other._z - _z * other._y );
        result._y = ( _z * other._x - _x * other._z );
        result._z = ( _x * other._y - _y * other._x );
        return result;
    }

    inline double3& double3::normalize() noexcept
    {
        const float64 lenSq = getLengthSquared();
        if ( lenSq > 0.0 )
        {
            const float64 invLen = 1.0 / MathUtil::sqrt( lenSq );
            ( *this ) *= invLen;
        }
        else
            ( *this ) = Zero;
        return *this;
    }

    inline double3 double3::normalize() const noexcept
    {
        const float64 lenSq = getLengthSquared();
        if ( lenSq > 0.0 )
        {
            const float64 invLen = 1.0 / MathUtil::sqrt( lenSq );
            return ( *this ) * invLen;
        }
        return Zero;
    }

    inline void double3::clamp( const double3& minValue, const double3& maxValue ) noexcept
    {
        _x = MathUtil::clamp( _x, minValue._x, maxValue._x );
        _y = MathUtil::clamp( _y, minValue._y, maxValue._y );
        _z = MathUtil::clamp( _z, minValue._z, maxValue._z );
    }

    inline double3 double3::clamp( const double3& minValue, const double3& maxValue ) const noexcept
    {
        double3 result = *this;
        result.clamp( minValue, maxValue );
        return result;
    }

    inline float64 double3::getLength() const noexcept
    {
        return MathUtil::sqrt( getLengthSquared() );
    }

    inline float64 double3::getLengthSquared() const noexcept
    {
        return ( _x * _x ) + ( _y * _y ) + ( _z * _z );
    }

    inline bool double3::operator==( const double3& other ) const noexcept
    {
        return MathUtil::nearEqual( _x, other._x ) && MathUtil::nearEqual( _y, other._y ) && MathUtil::nearEqual( _z, other._z );
    }

    inline bool double3::operator!=( const double3& other ) const noexcept
    {
        return !( *this == other );
    }

    inline double3& double3::operator+=( const double3& other ) noexcept
    {
        _x += other._x;
        _y += other._y;
        _z += other._z;
        return *this;
    }

    inline double3& double3::operator-=( const double3& other ) noexcept
    {
        _x -= other._x;
        _y -= other._y;
        _z -= other._z;
        return *this;
    }

    inline double3& double3::operator*=( float64 scale ) noexcept
    {
        _x *= scale;
        _y *= scale;
        _z *= scale;
        return *this;
    }

    inline double3& double3::operator/=( float64 scale ) noexcept
    {
        const float64 inv = 1.0 / scale;
        _x *= inv;
        _y *= inv;
        _z *= inv;
        return *this;
    }

    inline double3 double3::operator-() const noexcept
    {
        return double3{ -_x, -_y, -_z };
    }
} // namespace sw

#if defined( SW_COMPILER_CLANG )
    #pragma float_control( pop )
#elif defined( SW_COMPILER_GCC )
    #pragma GCC pop_options
#endif

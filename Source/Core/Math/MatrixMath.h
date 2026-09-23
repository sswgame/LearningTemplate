/**
 * @file MatrixMath.h
 * @brief 쿼터니언(quaternion)과 4x4 행렬(float4x4), 그리고 둘로 하는 변환 연산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{

    // ------------------------------------------------------------------------------
    // 1) quaternion — 축-각 · 오일러 각 · 행렬에서 만들고 곱으로 합성
    // ------------------------------------------------------------------------------
    /**
     * @struct quaternion
     * @brief 3차원 회전을 나타내는 쿼터니언입니다(허수부 x, y, z · 실수부 w). 짐벌 락 없이 보간할 수 있습니다.
     */
    struct SW_API quaternion final
    {
        static const quaternion Identity; /**< 회전이 없는 단위 쿼터니언 (0, 0, 0, 1) */

        float32 _x;
        float32 _y;
        float32 _z;
        float32 _w;

        /** @brief 단위 쿼터니언 (0,0,0,1) 으로 둡니다. */
        constexpr explicit quaternion() noexcept
            : _x{ 0.f }
            , _y{ 0.f }
            , _z{ 0.f }
            , _w{ 1.f } {}

        /** @brief (x, y, z, w) 로 둡니다. */
        constexpr explicit quaternion( const float32 x, const float32 y, const float32 z, const float32 w ) noexcept
            : _x{ x }
            , _y{ y }
            , _z{ z }
            , _w{ w } {}

        /** @brief 허수부는 v, 실수부는 scalar 입니다. */
        constexpr explicit quaternion( const float3& v, const float32 scalar ) noexcept
            : _x{ v._x }
            , _y{ v._y }
            , _z{ v._z }
            , _w{ scalar } {}

        /** @brief float4 의 xyzw 를 그대로 씁니다. */
        constexpr explicit quaternion( const float4& v ) noexcept
            : _x{ v._x }
            , _y{ v._y }
            , _z{ v._z }
            , _w{ v._w } {}

        /** @brief 배열의 네 값을 x, y, z, w 로 읽습니다. */
        constexpr explicit quaternion( const float32* pArray ) noexcept
            : _x{ pArray[0] }
            , _y{ pArray[1] }
            , _z{ pArray[2] }
            , _w{ pArray[3] } {}

        /** @brief 축(axis)과 각(angle, 라디안)으로 회전을 만듭니다. 축은 정규화해서 씁니다. */
        static quaternion createFromAxisAngle( const float3& axis, float32 angle ) noexcept;
        /** @brief 요 · 피치 · 롤(라디안)로 회전을 만듭니다. */
        static quaternion createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept;
        /** @brief 오일러 각(라디안)으로 회전을 만듭니다. `_x` 가 피치, `_y` 가 요, `_z` 가 롤입니다. */
        static quaternion createFromYawPitchRoll( const float3& angles ) noexcept;
        /** @brief 회전 행렬의 위 3x3 에서 쿼터니언을 뽑습니다. 스케일이 섞인 행렬이면 decompose() 를 쓰십시오. */
        static quaternion createFromRotationMatrix( const float4x4& matrix ) noexcept;

        /** @brief from 에서 to 쪽으로 최대 maxAngle(라디안)만큼 돌린 회전을 구합니다. to 를 지나치지는 않습니다. */
        static quaternion rotateTowards( const quaternion& from, const quaternion& to, float32 maxAngle ) noexcept;

        /** @brief 선형 보간한 뒤 정규화합니다(nlerp). 두 회전 사이의 짧은 쪽으로 갑니다. */
        static quaternion lerp( const quaternion& from, const quaternion& to, float32 t ) noexcept;
        /** @brief 구면 선형 보간합니다(짧은 쪽). 두 회전이 거의 같으면 lerp() 로 대신합니다. */
        static quaternion slerp( const quaternion& from, const quaternion& to, float32 t ) noexcept;

        /**
         * @brief q1 을 먼저, q2 를 나중에 적용하는 회전을 구합니다(q2 * q1).
         * @note `operator*` 와 인자 순서가 반대입니다. 행렬로 옮기면 `q1.toMatrix() * q2.toMatrix()` 와 같습니다.
         */
        static quaternion concatenate( const quaternion& q1, const quaternion& q2 ) noexcept;
        /** @brief 방향 from 을 방향 to 로 돌리는 가장 작은 회전을 구합니다. 두 방향이 정반대면 수직인 축으로 180도 돕니다. */
        static quaternion fromToRotation( const float3& from, const float3& to ) noexcept;
        /** @brief 앞(+Z)이 direction 을, 위가 up 쪽을 향하게 하는 회전을 구합니다. direction 이 0 이면 단위 쿼터니언입니다. */
        static quaternion lookRotation( const float3& direction, const float3& up ) noexcept;
        /** @brief 두 회전 사이의 각(라디안)을 구합니다. */
        static float32 getAngleBetween( const quaternion& lhs, const quaternion& rhs ) noexcept;

        /** @brief 노름(길이)을 반환합니다. */
        float32 norm() const noexcept;
        /** @brief 노름의 제곱을 반환합니다. */
        float32 normSquared() const noexcept;

        /** @brief 길이 1 로 정규화하고 자신을 반환합니다. 길이가 0 이면 단위 쿼터니언이 됩니다. */
        quaternion& normalize() noexcept;
        /** @brief 정규화한 사본을 반환합니다. 길이가 0 이면 단위 쿼터니언입니다. */
        quaternion normalize() const noexcept;

        /** @brief 켤레로 바꿉니다(허수부의 부호를 뒤집습니다). */
        void conjugate() noexcept;
        /** @brief 켤레를 반환합니다. */
        quaternion conjugate() const noexcept;

        /** @brief 역원으로 바꿉니다. 노름이 거의 0 이면 단위 쿼터니언이 됩니다. */
        void inverse() noexcept;
        /** @brief 역원을 반환합니다. 노름이 거의 0 이면 단위 쿼터니언입니다. */
        quaternion inverse() const noexcept;

        /** @brief 다른 쿼터니언과의 내적을 구합니다. */
        float32 dot( const quaternion& other ) const noexcept;

        /** @brief 오일러 각(라디안)을 구합니다. `_x` 가 피치, `_y` 가 요, `_z` 가 롤로, createFromYawPitchRoll( angles ) 와 같은 배치입니다. */
        float3 getEulerAngles() const noexcept;
        /** @brief 회전 행렬로 바꿉니다. */
        float4x4 toMatrix() const noexcept;

        /** @brief 같은지 비교합니다. */
        bool operator==( const quaternion& other ) const noexcept;
        /** @brief 다른지 비교합니다. */
        bool operator!=( const quaternion& other ) const noexcept;

        /** @brief 더한 뒤 대입합니다. */
        quaternion& operator+=( const quaternion& other ) noexcept;
        /** @brief 뺀 뒤 대입합니다. */
        quaternion& operator-=( const quaternion& other ) noexcept;
        /** @brief 쿼터니언을 곱한 뒤 대입합니다(*this = *this * other). */
        quaternion& operator*=( const quaternion& other ) noexcept;
        /** @brief 스칼라를 곱한 뒤 대입합니다. */
        quaternion& operator*=( float32 scale ) noexcept;
        /** @brief 스칼라로 나눈 뒤 대입합니다. */
        quaternion& operator/=( float32 scale ) noexcept;

        /** @brief 그대로 반환합니다(단항 +). */
        quaternion operator+() const noexcept { return *this; }
        /** @brief 모든 성분의 부호를 뒤집습니다(단항 -). 같은 회전을 나타냅니다. */
        quaternion operator-() const noexcept;
    };

    /** @brief 성분별로 더합니다. */
    inline quaternion operator+( const quaternion& lhs, const quaternion& rhs ) noexcept { return quaternion{ lhs._x + rhs._x, lhs._y + rhs._y, lhs._z + rhs._z, lhs._w + rhs._w }; }
    /** @brief 성분별로 뺍니다. */
    inline quaternion operator-( const quaternion& lhs, const quaternion& rhs ) noexcept { return quaternion{ lhs._x - rhs._x, lhs._y - rhs._y, lhs._z - rhs._z, lhs._w - rhs._w }; }
    /** @brief 해밀턴 곱을 구합니다. `lhs * rhs` 는 rhs 를 먼저, lhs 를 나중에 적용하는 회전입니다. */
    SW_API quaternion operator*( const quaternion& lhs, const quaternion& rhs ) noexcept;
    /** @brief 스칼라를 곱합니다. */
    inline quaternion operator*( const quaternion& q, float32 scale ) noexcept { return quaternion{ q._x * scale, q._y * scale, q._z * scale, q._w * scale }; }
    /** @brief 스칼라로 나눕니다. */
    inline quaternion operator/( const quaternion& q, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return q * inv;
    }

    /** @brief 스칼라를 곱합니다(scale * q). */
    inline quaternion operator*( float32 scale, const quaternion& q ) noexcept { return q * scale; }

    // ------------------------------------------------------------------------------
    // 2) float4x4 — 이동 · 회전 · 스케일 · 투영 · LookAt. 행 우선, HLSL 과 같은 배치
    // ------------------------------------------------------------------------------
    /** @brief 4x4 변환 행렬입니다. 행 우선으로 저장해 HLSL cbuffer 와 배치를 맞춥니다. 벡터는 행벡터(`v * M`)로 곱합니다. */
    struct SW_API float4x4 final
    {
        static const float4x4 Identity;

        float32 _11, _12, _13, _14;
        float32 _21, _22, _23, _24;
        float32 _31, _32, _33, _34;
        float32 _41, _42, _43, _44;

        /**
         * @brief 16개 성분을 연속된 float 배열(행 우선)로 봅니다.
         * @details GPU 업로드나 절두체 추출처럼 행렬을 배열로 훑는 코드는 `&m._11` 을 받아 `[0..15]` 로 읽습니다. 형식상으로는
         *          배열이 아닌 멤버를 배열처럼 읽는 것이라 정적 분석기가 경계 위반으로 짚습니다. 그 가정을 이 함수 한 곳에 모으고,
         *          아래 static_assert 가 가정(패딩 없음)을 컴파일 타임에 지킵니다.
         */
        const float32* data() const noexcept { return &_11; }
        /** @brief 쓰기 가능한 오버로드입니다. */
        float32* data() noexcept { return &_11; }

        /** @brief 단위 행렬로 둡니다. */
        constexpr float4x4() noexcept
            : _11{ 1.f }
            , _12{ 0.f }
            , _13{ 0.f }
            , _14{ 0.f }
            , _21{ 0.f }
            , _22{ 1.f }
            , _23{ 0.f }
            , _24{ 0.f }
            , _31{ 0.f }
            , _32{ 0.f }
            , _33{ 1.f }
            , _34{ 0.f }
            , _41{ 0.f }
            , _42{ 0.f }
            , _43{ 0.f }
            , _44{ 1.f } {}

        /** @brief 16개 성분을 행 우선으로 채웁니다. */
        constexpr float4x4( float32 m00, float32 m01, float32 m02, float32 m03,
                            float32 m10, float32 m11, float32 m12, float32 m13,
                            float32 m20, float32 m21, float32 m22, float32 m23,
                            float32 m30, float32 m31, float32 m32, float32 m33 ) noexcept
            : _11{ m00 }
            , _12{ m01 }
            , _13{ m02 }
            , _14{ m03 }
            , _21{ m10 }
            , _22{ m11 }
            , _23{ m12 }
            , _24{ m13 }
            , _31{ m20 }
            , _32{ m21 }
            , _33{ m22 }
            , _34{ m23 }
            , _41{ m30 }
            , _42{ m31 }
            , _43{ m32 }
            , _44{ m33 } {}

        /** @brief right · up · front 를 1 · 2 · 3 행에 놓은 회전 행렬을 만듭니다. 이동은 0 입니다. */
        constexpr explicit float4x4( const float3& right, const float3& up, const float3& front ) noexcept
            : _11{ right._x }
            , _12{ right._y }
            , _13{ right._z }
            , _14{ 0.f }
            , _21{ up._x }
            , _22{ up._y }
            , _23{ up._z }
            , _24{ 0.f }
            , _31{ front._x }
            , _32{ front._y }
            , _33{ front._z }
            , _34{ 0.f }
            , _41{ 0.f }
            , _42{ 0.f }
            , _43{ 0.f }
            , _44{ 1.f } {}

        /** @brief 네 행(right · up · front · translation)을 그대로 채웁니다. */
        constexpr explicit float4x4( const float4& right, const float4& up, const float4& front, const float4& translation ) noexcept
            : _11{ right._x }
            , _12{ right._y }
            , _13{ right._z }
            , _14{ right._w }
            , _21{ up._x }
            , _22{ up._y }
            , _23{ up._z }
            , _24{ up._w }
            , _31{ front._x }
            , _32{ front._y }
            , _33{ front._z }
            , _34{ front._w }
            , _41{ translation._x }
            , _42{ translation._y }
            , _43{ translation._z }
            , _44{ translation._w } {}

        /** @brief 배열의 16개 값을 행 우선으로 읽습니다. nullptr 이면 아무 값도 채우지 않습니다. */
        explicit float4x4( const float32* pArray ) noexcept;

        /** @brief 이동 행렬을 만듭니다. */
        static float4x4 createTranslation( const float3& position ) noexcept;
        /** @brief 이동 행렬을 만듭니다. */
        static float4x4 createTranslation( float32 x, float32 y, float32 z ) noexcept;

        /** @brief 축별 스케일 행렬을 만듭니다. */
        static float4x4 createScale( const float3& scales ) noexcept;
        /** @brief 축별 스케일 행렬을 만듭니다. */
        static float4x4 createScale( float32 x, float32 y, float32 z ) noexcept;
        /** @brief 세 축에 같은 값을 거는 스케일 행렬을 만듭니다. */
        static float4x4 createScale( float32 scale ) noexcept;

        /** @brief X축 회전 행렬(라디안)을 만듭니다. */
        static float4x4 createRotationX( float32 radians ) noexcept;
        /** @brief Y축 회전 행렬(라디안)을 만듭니다. */
        static float4x4 createRotationY( float32 radians ) noexcept;
        /** @brief Z축 회전 행렬(라디안)을 만듭니다. */
        static float4x4 createRotationZ( float32 radians ) noexcept;

        /** @brief 축(axis)과 각(angle, 라디안)으로 회전 행렬을 만듭니다. */
        static float4x4 createFromAxisAngle( const float3& axis, float32 angle ) noexcept;

        /** @brief 세로 시야각(fov, 라디안)과 종횡비로 원근 투영 행렬을 만듭니다. 깊이는 D3D 규약대로 [0, 1] 입니다. */
        static float4x4 createPerspectiveFieldOfView( float32 fov, float32 aspectRatio, float32 nearPlane, float32 farPlane ) noexcept;
        /** @brief 가까운 면의 너비 · 높이로 원근 투영 행렬을 만듭니다. */
        static float4x4 createPerspective( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept;
        /** @brief 가까운 면의 left · right · bottom · top 으로 비대칭 원근 투영 행렬을 만듭니다. */
        static float4x4 createPerspectiveOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept;

        /** @brief 너비 · 높이로 직교 투영 행렬을 만듭니다. */
        static float4x4 createOrthographic( float32 width, float32 height, float32 nearPlane, float32 farPlane ) noexcept;
        /** @brief left · right · bottom · top 으로 비대칭 직교 투영 행렬을 만듭니다. */
        static float4x4 createOrthographicOffCenter( float32 left, float32 right, float32 bottom, float32 top, float32 nearPlane, float32 farPlane ) noexcept;

        /** @brief position 에서 target 을 바라보는 뷰 행렬을 만듭니다. */
        static float4x4 createLookAt( const float3& position, const float3& target, const float3& up ) noexcept;
        /** @brief 위치 · 앞 방향 · 위 방향으로 월드 행렬을 만듭니다. */
        static float4x4 createWorld( const float3& position, const float3& forward, const float3& up ) noexcept;

        /** @brief 쿼터니언 회전을 4x4 행렬로 바꿉니다. */
        static float4x4 createFromQuaternion( const quaternion& quaternion ) noexcept;
        /** @brief 요 · 피치 · 롤(라디안)로 회전 행렬을 만듭니다. */
        static float4x4 createFromYawPitchRoll( float32 yaw, float32 pitch, float32 roll ) noexcept;
        /** @brief 오일러 각(라디안)으로 회전 행렬을 만듭니다. `_x` 가 피치, `_y` 가 요, `_z` 가 롤입니다. */
        static float4x4 createFromYawPitchRoll( const float3& angles ) noexcept;

        /**
         * @brief 스케일 · 회전 · 이동을 한 번에 합성합니다(행벡터 규약의 S * R * T).
         * @details `createScale( s ) * createFromYawPitchRoll( … ) * createTranslation( p )` 와 같은 값이지만 행렬 곱을 하지 않습니다.
         *          스케일은 대각 행렬이고 이동은 마지막 행에만 있어서 결과의 모양이 이미 정해져 있기 때문입니다. 위 3x3 은 회전
         *          행렬의 각 행에 스케일을 곱한 것이고, 마지막 행이 곧 위치입니다. 행렬 곱 두 번(실수 곱 128번)이 곱 아홉 번으로
         *          줄어듭니다. 움직이는 컴포넌트가 모두 매 프레임 지나가는 자리라 이 차이가 프레임 시간에 드러납니다.
         */
        static float4x4 createTrs( const float3& position, const quaternion& rotation, const float3& scale ) noexcept;
        /**
         * @brief 스케일 · 오일러 회전(요 · 피치 · 롤) · 이동을 한 번에 합성합니다.
         * @param rotation 라디안 단위 오일러 각. `_x` 가 피치, `_y` 가 요, `_z` 가 롤입니다
         *                 (`createFromYawPitchRoll( angles )` 와 같은 해석입니다).
         * @details 세 각이 모두 0 이면 쿼터니언을 거치지 않고 대각선의 스케일과 위치만 채웁니다. 회전 없는 물체가 흔하기 때문입니다.
         */
        static float4x4 createTrs( const float3& position, const float3& rotation, const float3& scale ) noexcept;

        /** @brief 성분별로 선형 보간합니다. 회전이 든 행렬이면 결과가 더는 직교하지 않을 수 있습니다. */
        static float4x4 lerp( const float4x4& from, const float4x4& to, float32 t ) noexcept;
        /** @brief matrix 를 적용한 뒤 rotation 으로 돌리는 행렬을 반환합니다(matrix * R). */
        static float4x4 transform( const float4x4& matrix, const quaternion& rotation ) noexcept;

        /** @brief 스케일 · 회전 · 이동으로 분해합니다. 스케일 성분이 하나라도 0 이면 회전을 단위 쿼터니언으로 두고 false 를 반환합니다. */
        bool decompose( float3& outScale, quaternion& outRotation, float3& outTranslation ) const noexcept;

        /** @brief 1 · 2 · 3 행의 길이로 축별 스케일을 구합니다. 음수 스케일은 구분하지 못합니다. */
        float3 getScale() const noexcept;
        /** @brief 회전을 구합니다(decompose() 의 회전 부분). */
        quaternion getRotation() const noexcept;
        /** @brief 이동(마지막 행)을 반환합니다. */
        float3 getTranslation() const noexcept;

        /** @brief 회전과 이동은 그대로 두고 스케일만 바꿉니다. */
        void setScale( const float3& scale ) noexcept;
        /** @brief 스케일과 이동은 그대로 두고 회전만 바꿉니다. */
        void setRotation( const quaternion& rotation ) noexcept;
        /** @brief 이동(마지막 행)만 바꿉니다. */
        void setTranslation( const float3& translation ) noexcept;

        /** @brief 행렬식을 구합니다. */
        float32 determinant() const noexcept;
        /** @brief 전치 행렬을 반환합니다. */
        float4x4 transpose() const noexcept;
        /**
         * @brief 역행렬을 구합니다.
         * @return 특이 행렬이면 `Identity` 를 반환합니다. 실패를 알릴 다른 방법이 없습니다.
         * @warning 그래서 호출하는 쪽이 결과가 Identity 인지 확인해야 할 때가 있습니다. 스케일이 0 인 트랜스폼처럼 역행렬이 없는
         *          행렬을 넘기면 오류 없이 Identity 로 이어지고, 렌더러에서는 물체가 엉뚱한 자리에 조용히 그려지는 모습으로
         *          나타납니다. 판정 기준은 `|determinant()| < 1e-7` 이며, 미리 알고 싶으면 `determinant()` 를 직접 보십시오.
         */
        float4x4 invert() const noexcept;

        /** @brief 같은지 비교합니다. */
        bool operator==( const float4x4& other ) const noexcept;
        /** @brief 다른지 비교합니다. */
        bool operator!=( const float4x4& other ) const noexcept;

        /** @brief 더한 뒤 대입합니다. */
        float4x4& operator+=( const float4x4& other ) noexcept;
        /** @brief 뺀 뒤 대입합니다. */
        float4x4& operator-=( const float4x4& other ) noexcept;
        /** @brief 행렬을 곱한 뒤 대입합니다(*this = *this * other). */
        float4x4& operator*=( const float4x4& other ) noexcept;
        /** @brief 스칼라를 곱한 뒤 대입합니다. */
        float4x4& operator*=( float32 scale ) noexcept;
        /** @brief 스칼라로 나눈 뒤 대입합니다. */
        float4x4& operator/=( float32 scale ) noexcept;

        /** @brief 그대로 반환합니다(단항 +). */
        float4x4 operator+() const noexcept { return *this; }
        /** @brief 모든 성분의 부호를 뒤집습니다(단항 -). */
        float4x4 operator-() const noexcept;
    };

    // data() 로 [0..15] 를 읽을 수 있다는 가정을 여기서 고정한다. 패딩이 끼면 컴파일이 멈춘다.
    static_assert( sizeof( float4x4 ) == 16 * sizeof( float32 ), "float4x4 must be 16 contiguous floats" );

    /** @brief 성분별로 더합니다. */
    inline float4x4 operator+( const float4x4& lhs, const float4x4& rhs ) noexcept { return float4x4{ lhs._11 + rhs._11, lhs._12 + rhs._12, lhs._13 + rhs._13, lhs._14 + rhs._14, lhs._21 + rhs._21, lhs._22 + rhs._22, lhs._23 + rhs._23, lhs._24 + rhs._24, lhs._31 + rhs._31, lhs._32 + rhs._32, lhs._33 + rhs._33, lhs._34 + rhs._34, lhs._41 + rhs._41, lhs._42 + rhs._42, lhs._43 + rhs._43, lhs._44 + rhs._44 }; }
    /** @brief 성분별로 뺍니다. */
    inline float4x4 operator-( const float4x4& lhs, const float4x4& rhs ) noexcept { return float4x4{ lhs._11 - rhs._11, lhs._12 - rhs._12, lhs._13 - rhs._13, lhs._14 - rhs._14, lhs._21 - rhs._21, lhs._22 - rhs._22, lhs._23 - rhs._23, lhs._24 - rhs._24, lhs._31 - rhs._31, lhs._32 - rhs._32, lhs._33 - rhs._33, lhs._34 - rhs._34, lhs._41 - rhs._41, lhs._42 - rhs._42, lhs._43 - rhs._43, lhs._44 - rhs._44 }; }
    /** @brief 행렬 곱을 구합니다. 행벡터 규약이라 `v * ( A * B )` 는 A 를 먼저, B 를 나중에 적용합니다. */
    SW_API float4x4 operator*( const float4x4& lhs, const float4x4& rhs ) noexcept;
    /** @brief 스칼라를 곱합니다. */
    inline float4x4 operator*( const float4x4& matrix, float32 scale ) noexcept { return float4x4{ matrix._11 * scale, matrix._12 * scale, matrix._13 * scale, matrix._14 * scale, matrix._21 * scale, matrix._22 * scale, matrix._23 * scale, matrix._24 * scale, matrix._31 * scale, matrix._32 * scale, matrix._33 * scale, matrix._34 * scale, matrix._41 * scale, matrix._42 * scale, matrix._43 * scale, matrix._44 * scale }; }
    /** @brief 스칼라로 나눕니다. */
    inline float4x4 operator/( const float4x4& matrix, float32 scale ) noexcept
    {
        const float32 inv = 1.f / scale;
        return matrix * inv;
    }

    /** @brief 스칼라를 곱합니다(scale * m). */
    inline float4x4 operator*( float32 scale, const float4x4& matrix ) noexcept { return matrix * scale; }
} // namespace sw

#include "pch.h"

#include "Engine/Graphics/Mesh/MeshUtil.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
    namespace
    {
        /** @brief 법선에서 정점 색을 만든다 — forwardlit 은 정점 색을 쓰므로 이게 곡면의 음영이 된다. */
        RHIVertex makeShadedVertex( const float3& position, const float3& normal )
        {
            // 축마다 다른 밝기를 주어 곡면이 단색 덩어리로 보이지 않게 한다(큐브의 면별 색과 같은 목적).
            const float32 shade = 0.45f + 0.55f * MathUtil::max( 0.0f, normal.dot( float3{ 0.35f, 0.85f, 0.40f }.normalize() ) );
            RHIVertex     vertex{};
            vertex._arrPosition[0] = position._x;
            vertex._arrPosition[1] = position._y;
            vertex._arrPosition[2] = position._z;
            vertex._arrColor[0]    = shade * ( 0.55f + 0.45f * MathUtil::abs( normal._x ) );
            vertex._arrColor[1]    = shade * ( 0.55f + 0.45f * MathUtil::abs( normal._y ) );
            vertex._arrColor[2]    = shade * ( 0.55f + 0.45f * MathUtil::abs( normal._z ) );
            vertex._arrColor[3]    = 1.0f;
            return vertex;
        }

        /**
         * @brief 삼각형 하나를 **바깥을 향하도록** 넣습니다. 감김이 반대면 두 정점을 바꿉니다.
         * @details 감김이 뒤집힌 면은 후면 컬링에 걸려 **화면에서 그냥 사라진다.** 그림으로는
         *          "안 그려진다" 로만 보여서 렌더러 버그로 오인하기 쉽고, 실제로 이 생성기들을 처음
         *          쓴 판에서 구·실린더·원뿔이 그렇게 뒤집혀 있었다. 손으로 맞추는 대신 여기서 바로잡는다.
         * @warning **원점 중심 볼록 도형에서만 맞는 판정이다** — 삼각형 중심이 곧 바깥 방향이라는 가정을
         *          쓴다. 오목하거나 원점을 품지 않는 기하를 넣으려면 이 함수를 쓰면 안 된다.
         */
        void pushOutwardTriangle( vector<RHIVertex>& outList, const float3& a, const float3& b, const float3& c )
        {
            float3 normal = ( b - a ).cross( c - a );
            if ( normal.getLengthSquared() <= MathUtil::Epsilon )
                return; // 면적 0 — 극에서 접힌 자리다. 넣어 봐야 그려지지 않는다.

            normal                  = normal.normalize();
            const float3 centroid   = ( a + b + c ) * ( 1.0f / 3.0f );
            const bool   bOutward   = normal.dot( centroid ) > 0.0f;
            const float3 corner1    = bOutward ? b : c;
            const float3 corner2    = bOutward ? c : b;
            const float3 faceNormal = bOutward ? normal : ( normal * -1.0f );

            outList.push_back( makeShadedVertex( a, faceNormal ) );
            outList.push_back( makeShadedVertex( corner1, faceNormal ) );
            outList.push_back( makeShadedVertex( corner2, faceNormal ) );
        }

        /** @brief 둘레 각도. */
        float32 sliceAngle( uint32 slice, uint32 sliceCount )
        {
            return 2.0f * MathUtil::Pi * static_cast<float32>( slice ) / static_cast<float32>( sliceCount );
        }
    } // namespace

    shared_ptr<Mesh> MeshUtil::createUnitCube()
    {
        auto              mesh     = Mesh::create();
        vector<RHIVertex> listVert = {
            // +Z
            { { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            // -Z
            { { 0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            // +X
            {  { 0.5f, -0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            // -X
            {{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            // +Y
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {   { 0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { -0.5f, 0.5f, 0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            {  { 0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            { { -0.5f, 0.5f, -0.5f }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            // -Y
            {{ -0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            { { 0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {{ -0.5f, -0.5f, -0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            {  { 0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
            { { -0.5f, -0.5f, 0.5f }, { 0.45f, 0.45f, 0.50f, 1.0f }},
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createPrimitive( string_view meshId )
    {
        if ( meshId.empty() || StringUtil::equals( meshId, "Cube", true ) )
            return createUnitCube();
        if ( StringUtil::equals( meshId, "Quad", true ) || StringUtil::equals( meshId, "Rect", true ) )
            return createRectMesh();
        if ( StringUtil::equals( meshId, "Sphere", true ) )
            return createSphere();
        if ( StringUtil::equals( meshId, "Cylinder", true ) )
            return createCylinder();
        if ( StringUtil::equals( meshId, "Capsule", true ) )
            return createCapsule();
        if ( StringUtil::equals( meshId, "Cone", true ) )
            return createCone();
        return {};
    }

    shared_ptr<Mesh> MeshUtil::createSphere( uint32 stackCount, uint32 sliceCount )
    {
        stackCount = MathUtil::max( stackCount, 2u );
        sliceCount = MathUtil::max( sliceCount, 3u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( stackCount ) * sliceCount * 6 );

        constexpr float32 kRadius = 0.5f;
        auto              pointAt = [&]( uint32 stack, uint32 slice ) -> float3
        {
            const float32 phi   = MathUtil::Pi * static_cast<float32>( stack ) / static_cast<float32>( stackCount );
            const float32 theta = sliceAngle( slice, sliceCount );
            return float3{ kRadius * MathUtil::sin( phi ) * MathUtil::cos( theta ),
                           kRadius * MathUtil::cos( phi ),
                           kRadius * MathUtil::sin( phi ) * MathUtil::sin( theta ) };
        };

        for ( uint32 stack = 0; stack < stackCount; ++stack )
        {
            for ( uint32 slice = 0; slice < sliceCount; ++slice )
            {
                const float3 topLeft     = pointAt( stack, slice );
                const float3 topRight    = pointAt( stack, slice + 1 );
                const float3 bottomLeft  = pointAt( stack + 1, slice );
                const float3 bottomRight = pointAt( stack + 1, slice + 1 );
                // 극에서는 한 쪽이 한 점으로 모여 퇴화 삼각형이 된다 — 그건 넣지 않는다.
                if ( stack != 0 )
                    pushOutwardTriangle( listVert, topLeft, bottomLeft, topRight );
                if ( stack + 1 != stackCount )
                    pushOutwardTriangle( listVert, topRight, bottomLeft, bottomRight );
            }
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createCylinder( uint32 sliceCount )
    {
        sliceCount = MathUtil::max( sliceCount, 3u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( sliceCount ) * 12 );

        constexpr float32 kRadius = 0.5f;
        constexpr float32 kHalfY  = 0.5f;
        for ( uint32 slice = 0; slice < sliceCount; ++slice )
        {
            const float32 angle0 = sliceAngle( slice, sliceCount );
            const float32 angle1 = sliceAngle( slice + 1, sliceCount );
            const float3  lower0{ kRadius * MathUtil::cos( angle0 ), -kHalfY, kRadius * MathUtil::sin( angle0 ) };
            const float3  lower1{ kRadius * MathUtil::cos( angle1 ), -kHalfY, kRadius * MathUtil::sin( angle1 ) };
            const float3  upper0{ lower0._x, kHalfY, lower0._z };
            const float3  upper1{ lower1._x, kHalfY, lower1._z };

            pushOutwardTriangle( listVert, lower0, upper0, lower1 );
            pushOutwardTriangle( listVert, lower1, upper0, upper1 );
            pushOutwardTriangle( listVert, float3{ 0.0f, kHalfY, 0.0f }, upper0, upper1 );
            pushOutwardTriangle( listVert, float3{ 0.0f, -kHalfY, 0.0f }, lower1, lower0 );
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createCapsule( uint32 stackCount, uint32 sliceCount )
    {
        stackCount = MathUtil::max( stackCount, 2u );
        sliceCount = MathUtil::max( sliceCount, 3u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( stackCount ) * sliceCount * 12 );

        constexpr float32 kRadius = 0.5f;
        constexpr float32 kHalfY  = 0.5f;
        // 반구는 중심을 ±kHalfY 로 옮긴 구면이다. 원통부와 이어 붙이면 캡슐이 된다.
        auto capPoint = [&]( uint32 stack, uint32 slice, bool bTop ) -> float3
        {
            const float32 phi    = MathUtil::HalfPi * static_cast<float32>( stack ) / static_cast<float32>( stackCount );
            const float32 theta  = sliceAngle( slice, sliceCount );
            const float32 ringY  = kRadius * MathUtil::sin( phi );
            const float32 ringR  = kRadius * MathUtil::cos( phi );
            const float32 offset = bTop ? kHalfY : -kHalfY;
            return float3{ ringR * MathUtil::cos( theta ), offset + ( bTop ? ringY : -ringY ), ringR * MathUtil::sin( theta ) };
        };

        for ( uint32 slice = 0; slice < sliceCount; ++slice )
        {
            const float32 angle0 = sliceAngle( slice, sliceCount );
            const float32 angle1 = sliceAngle( slice + 1, sliceCount );
            const float3  lower0{ kRadius * MathUtil::cos( angle0 ), -kHalfY, kRadius * MathUtil::sin( angle0 ) };
            const float3  lower1{ kRadius * MathUtil::cos( angle1 ), -kHalfY, kRadius * MathUtil::sin( angle1 ) };
            const float3  upper0{ lower0._x, kHalfY, lower0._z };
            const float3  upper1{ lower1._x, kHalfY, lower1._z };
            pushOutwardTriangle( listVert, lower0, upper0, lower1 );
            pushOutwardTriangle( listVert, lower1, upper0, upper1 );

            for ( uint32 stack = 0; stack < stackCount; ++stack )
            {
                for ( uint32 capIndex = 0; capIndex < 2; ++capIndex )
                {
                    const bool   bTop      = ( capIndex == 0 );
                    const float3 nearLeft  = capPoint( stack, slice, bTop );
                    const float3 nearRight = capPoint( stack, slice + 1, bTop );
                    const float3 farLeft   = capPoint( stack + 1, slice, bTop );
                    const float3 farRight  = capPoint( stack + 1, slice + 1, bTop );
                    if ( bTop )
                    {
                        pushOutwardTriangle( listVert, nearLeft, farLeft, nearRight );
                        if ( stack + 1 != stackCount )
                            pushOutwardTriangle( listVert, nearRight, farLeft, farRight );
                    }
                    else
                    {
                        pushOutwardTriangle( listVert, nearLeft, nearRight, farLeft );
                        if ( stack + 1 != stackCount )
                            pushOutwardTriangle( listVert, nearRight, farRight, farLeft );
                    }
                }
            }
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createCone( uint32 sliceCount )
    {
        sliceCount = MathUtil::max( sliceCount, 3u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( sliceCount ) * 6 );

        constexpr float32 kRadius = 0.5f;
        constexpr float32 kHalfY  = 0.5f;
        const float3      apex{ 0.0f, kHalfY, 0.0f };
        for ( uint32 slice = 0; slice < sliceCount; ++slice )
        {
            const float32 angle0 = sliceAngle( slice, sliceCount );
            const float32 angle1 = sliceAngle( slice + 1, sliceCount );
            const float3  base0{ kRadius * MathUtil::cos( angle0 ), -kHalfY, kRadius * MathUtil::sin( angle0 ) };
            const float3  base1{ kRadius * MathUtil::cos( angle1 ), -kHalfY, kRadius * MathUtil::sin( angle1 ) };
            pushOutwardTriangle( listVert, base0, apex, base1 );
            pushOutwardTriangle( listVert, float3{ 0.0f, -kHalfY, 0.0f }, base1, base0 );
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createRectMesh()
    {
        auto              mesh     = Mesh::create();
        vector<RHIVertex> listVert = {
            {{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            { { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {{ -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            {  { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
            { { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f }},
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }
} // namespace sw

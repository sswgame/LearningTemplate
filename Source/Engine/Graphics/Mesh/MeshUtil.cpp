#include "pch.h"

#include "Engine/Graphics/Mesh/MeshUtil.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Graphics/Mesh/Mesh.h"

namespace sw
{
    namespace
    {
        /**
         * @struct BuildVertex
         * @brief 생성기가 다루는 정점입니다. 위치 · 노멀 · UV 가 **같이 다닙니다**.
         * @details 셋을 따로 넘기면 인자가 아홉 개가 되고, 감김을 바로잡느라 정점을 맞바꿀 때
         *          하나를 빠뜨리기 쉽습니다. 그러면 노멀이나 UV 만 짝이 어긋나 조용히 틀린 그림이 나옵니다.
         */
        struct BuildVertex
        {
            float3 _position;
            float3 _normal;
            float2 _uv;
        };

        /** @brief 생성기 정점을 RHI 정점으로 바꿉니다. 색은 노멀에서 뽑아 곡면이 단색 덩어리로 보이지 않게 합니다. */
        RHIVertex makeShadedVertex( const BuildVertex& source )
        {
            // 축마다 다른 밝기를 주어 곡면이 단색 덩어리로 보이지 않게 한다(큐브의 면별 색과 같은 목적).
            const float32 shade =
                0.45f + 0.55f * MathUtil::max( 0.0f, source._normal.dot( float3{ 0.35f, 0.85f, 0.40f }.normalize() ) );
            RHIVertex vertex{};
            vertex._arrPosition[0] = source._position._x;
            vertex._arrPosition[1] = source._position._y;
            vertex._arrPosition[2] = source._position._z;
            vertex._arrNormal[0]   = source._normal._x;
            vertex._arrNormal[1]   = source._normal._y;
            vertex._arrNormal[2]   = source._normal._z;
            vertex._arrUv[0]       = source._uv._x;
            vertex._arrUv[1]       = source._uv._y;
            vertex._arrColor[0]    = shade * ( 0.55f + 0.45f * MathUtil::abs( source._normal._x ) );
            vertex._arrColor[1]    = shade * ( 0.55f + 0.45f * MathUtil::abs( source._normal._y ) );
            vertex._arrColor[2]    = shade * ( 0.55f + 0.45f * MathUtil::abs( source._normal._z ) );
            vertex._arrColor[3]    = 1.0f;
            return vertex;
        }

        /**
         * @brief 삼각형 하나를 **바깥을 향하도록** 넣습니다. 감김이 반대면 두 정점을 맞바꿉니다.
         * @details 감김이 뒤집힌 면은 후면 컬링에 걸려 **화면에서 그냥 사라집니다.** 그림으로는
         *          "안 그려진다" 로만 보여서 렌더러 버그로 오인하기 쉽고, 실제로 이 생성기들을 처음
         *          쓴 판에서 구 · 실린더 · 원뿔이 그렇게 뒤집혀 있었습니다. 손으로 맞추는 대신 여기서 바로잡습니다.
         *          맞바꿀 때 노멀 · UV 가 **위치와 같이** 따라갑니다. `BuildVertex` 로 묶어 둔 이유입니다.
         * @warning **원점 중심 볼록 도형에서만 맞는 판정입니다.** 삼각형 중심이 곧 바깥 방향이라는 가정을
         *          씁니다. 오목하거나 원점을 품지 않는 기하를 넣으려면 이 함수를 쓰면 안 됩니다.
         */
        void pushTriangle( vector<RHIVertex>& outList, const BuildVertex& a, const BuildVertex& b, const BuildVertex& c )
        {
            const float3 faceNormal = ( b._position - a._position ).cross( c._position - a._position );
            if ( faceNormal.getLengthSquared() <= MathUtil::Epsilon )
                return; // 면적 0. 극에서 접힌 자리다. 넣어 봐야 그려지지 않는다.

            const float3 centroid = ( a._position + b._position + c._position ) * ( 1.0f / 3.0f );
            const bool   bOutward = faceNormal.normalize().dot( centroid ) > 0.0f;

            outList.push_back( makeShadedVertex( a ) );
            outList.push_back( makeShadedVertex( bOutward ? b : c ) );
            outList.push_back( makeShadedVertex( bOutward ? c : b ) );
        }

        /**
         * @brief 축에 가장 가까운 평면에 UV 를 펼칩니다. 평평한 면(큐브 면 · 뚜껑) 전용입니다.
         * @note 원점 중심 단위 도형(범위 [-0.5, 0.5])을 전제로 0..1 로 맞춥니다. 곡면은 이 함수를
         *       쓰지 않습니다. 구 · 실린더 옆면은 각도 · 높이를 그대로 쓰는 편이 훨씬 고르게 펼쳐집니다.
         */
        float2 planarUv( const float3& position, const float3& normal )
        {
            const float3 axis{ MathUtil::abs( normal._x ), MathUtil::abs( normal._y ), MathUtil::abs( normal._z ) };
            if ( axis._x >= axis._y && axis._x >= axis._z )
                return float2{ position._z + 0.5f, 0.5f - position._y };
            if ( axis._y >= axis._x && axis._y >= axis._z )
                return float2{ position._x + 0.5f, position._z + 0.5f };
            return float2{ position._x + 0.5f, 0.5f - position._y };
        }

        /**
         * @brief 평평한 삼각형 하나를 넣습니다. 면 노멀을 셋이 나눠 갖고 UV 는 그 면에 펼칩니다.
         * @details 큐브 면 · 실린더 뚜껑 · 원뿔 밑면처럼 **실제로 평평한** 곳에 씁니다. 곡면에 쓰면
         *          면마다 노멀이 뚝뚝 끊겨 각져 보입니다.
         */
        void pushFlatTriangle( vector<RHIVertex>& outList, const float3& a, const float3& b, const float3& c )
        {
            float3 normal = ( b - a ).cross( c - a );
            if ( normal.getLengthSquared() <= MathUtil::Epsilon )
                return;

            normal                  = normal.normalize();
            const float3 centroid   = ( a + b + c ) * ( 1.0f / 3.0f );
            const float3 faceNormal = ( normal.dot( centroid ) > 0.0f ) ? normal : ( normal * -1.0f );

            pushTriangle( outList, BuildVertex{ a, faceNormal, planarUv( a, faceNormal ) },
                          BuildVertex{ b, faceNormal, planarUv( b, faceNormal ) },
                          BuildVertex{ c, faceNormal, planarUv( c, faceNormal ) } );
        }

        /** @brief 원점 중심 도형의 바깥 방향입니다. 길이가 0 이면 +Y 로 둡니다(극에서만 생깁니다). */
        float3 radialNormal( const float3& position )
        {
            return ( position.getLengthSquared() > MathUtil::Epsilon ) ? position.normalize() : float3::Up;
        }

        /** @brief 둘레 각도입니다. */
        float32 sliceAngle( uint32 slice, uint32 sliceCount )
        {
            return 2.0f * MathUtil::Pi * static_cast<float32>( slice ) / static_cast<float32>( sliceCount );
        }

        /** @brief 둘레를 도는 도형의 UV 입니다. u 는 각도 비율, v 는 부르는 쪽이 줍니다(높이 · 극각). */
        float2 revolvedUv( uint32 slice, uint32 sliceCount, float32 v )
        {
            return float2{ static_cast<float32>( slice ) / static_cast<float32>( sliceCount ), v };
        }

        /** @brief 회전체 옆면 한 조각의 네 꼭짓점입니다. 원통과 캡슐의 몸통이 같은 조각입니다. */
        struct RevolvedQuad
        {
            float3 _lower0;
            float3 _lower1;
            float3 _upper0;
            float3 _upper1;
        };

        RevolvedQuad makeRevolvedQuad( uint32 slice, uint32 sliceCount, float32 radius, float32 halfY )
        {
            const float32 angle0 = sliceAngle( slice, sliceCount );
            const float32 angle1 = sliceAngle( slice + 1, sliceCount );
            RevolvedQuad  quad{};
            quad._lower0 = float3{ radius * MathUtil::cos( angle0 ), -halfY, radius * MathUtil::sin( angle0 ) };
            quad._lower1 = float3{ radius * MathUtil::cos( angle1 ), -halfY, radius * MathUtil::sin( angle1 ) };
            quad._upper0 = float3{ quad._lower0._x, halfY, quad._lower0._z };
            quad._upper1 = float3{ quad._lower1._x, halfY, quad._lower1._z };
            return quad;
        }

        /** @brief 옆면 조각을 두 삼각형으로 냅니다. 노멀은 **축을 뺀 방사 방향**입니다. 위치를 그대로 정규화하면 위아래로 기웁니다. */
        void pushRevolvedSide( vector<RHIVertex>& outList, const RevolvedQuad& quad, uint32 slice, uint32 sliceCount, float32 vUpper, float32 vLower )
        {
            const float3      side0 = float3{ quad._lower0._x, 0.0f, quad._lower0._z }.normalize();
            const float3      side1 = float3{ quad._lower1._x, 0.0f, quad._lower1._z }.normalize();
            const BuildVertex sideLower0{ quad._lower0, side0, revolvedUv( slice, sliceCount, vLower ) };
            const BuildVertex sideLower1{ quad._lower1, side1, revolvedUv( slice + 1, sliceCount, vLower ) };
            const BuildVertex sideUpper0{ quad._upper0, side0, revolvedUv( slice, sliceCount, vUpper ) };
            const BuildVertex sideUpper1{ quad._upper1, side1, revolvedUv( slice + 1, sliceCount, vUpper ) };
            pushTriangle( outList, sideLower0, sideUpper0, sideLower1 );
            pushTriangle( outList, sideLower1, sideUpper0, sideUpper1 );
        }
    } // namespace

    shared_ptr<Mesh> MeshUtil::createUnitCube()
    {
        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( 36 );

        // 면마다 색을 달리한다. 어느 면을 보고 있는지가 그림에서 바로 읽혀야 검증이 된다.
        // 노멀 · UV 는 `pushFlatTriangle` 이 면에서 만든다(예전에는 36 줄을 손으로 적고 있었다).
        struct CubeFace
        {
            float3 _corner0;
            float3 _corner1;
            float3 _corner2;
            float3 _corner3;
            float4 _color;
        };
        constexpr float32 kHalf      = 0.5f;
        const CubeFace    arrFace[6] = {
            // +Z
            { { -kHalf, -kHalf, kHalf },   { kHalf, -kHalf, kHalf },   { kHalf, kHalf, kHalf },  { -kHalf, kHalf, kHalf }, { 0.92f, 0.35f, 0.28f, 1.0f }},
            // -Z
            { { kHalf, -kHalf, -kHalf }, { -kHalf, -kHalf, -kHalf }, { -kHalf, kHalf, -kHalf },  { kHalf, kHalf, -kHalf }, { 0.28f, 0.45f, 0.92f, 1.0f }},
            // +X
            {  { kHalf, -kHalf, kHalf },  { kHalf, -kHalf, -kHalf },  { kHalf, kHalf, -kHalf },   { kHalf, kHalf, kHalf }, { 0.32f, 0.82f, 0.40f, 1.0f }},
            // -X
            {{ -kHalf, -kHalf, -kHalf },  { -kHalf, -kHalf, kHalf },  { -kHalf, kHalf, kHalf }, { -kHalf, kHalf, -kHalf }, { 0.95f, 0.72f, 0.22f, 1.0f }},
            // +Y
            {  { -kHalf, kHalf, kHalf },    { kHalf, kHalf, kHalf },  { kHalf, kHalf, -kHalf }, { -kHalf, kHalf, -kHalf }, { 0.95f, 0.95f, 0.95f, 1.0f }},
            // -Y
            {{ -kHalf, -kHalf, -kHalf },  { kHalf, -kHalf, -kHalf },  { kHalf, -kHalf, kHalf }, { -kHalf, -kHalf, kHalf }, { 0.45f, 0.45f, 0.50f, 1.0f }},
        };

        for ( const CubeFace& face : arrFace )
        {
            const size_t firstVertex = listVert.size();
            pushFlatTriangle( listVert, face._corner0, face._corner1, face._corner2 );
            pushFlatTriangle( listVert, face._corner0, face._corner2, face._corner3 );
            // 면 색은 노멀에서 뽑은 음영 대신 이 색을 쓴다. 큐브는 면별 색이 곧 검증 수단이다.
            for ( size_t slot = firstVertex; slot < listVert.size(); ++slot )
            {
                listVert[slot]._arrColor[0] = face._color._x;
                listVert[slot]._arrColor[1] = face._color._y;
                listVert[slot]._arrColor[2] = face._color._z;
                listVert[slot]._arrColor[3] = face._color._w;
            }
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    /**
     * @brief 프리미티브 id 를 기준 이름 하나로 모읍니다(별칭 · 대소문자 흡수). 모르면 nullptr 입니다.
     * @details 만들기와 공유 캐시가 **같은 판정**을 써야 합니다. 따로 적으면 "Quad" 와 "Rect" 가
     *          같은 기하인데 캐시에서는 다른 자리를 차지합니다(배치도 그만큼 갈립니다).
     */
    static const utf8* canonicalPrimitiveIdVal( string_view meshId )
    {
        if ( meshId.empty() || StringUtil::equals( meshId, "Cube", true ) )
            return "cube";
        if ( StringUtil::equals( meshId, "Quad", true ) || StringUtil::equals( meshId, "Rect", true ) )
            return "quad";
        if ( StringUtil::equals( meshId, "Plane", true ) || StringUtil::equals( meshId, "Ground", true ) )
            return "plane";
        if ( StringUtil::equals( meshId, "Sphere", true ) )
            return "sphere";
        if ( StringUtil::equals( meshId, "Cylinder", true ) )
            return "cylinder";
        if ( StringUtil::equals( meshId, "Capsule", true ) )
            return "capsule";
        if ( StringUtil::equals( meshId, "Cone", true ) )
            return "cone";
        return nullptr;
    }

    shared_ptr<Mesh> MeshUtil::createPrimitive( string_view meshId )
    {
        const utf8* pCanonical = canonicalPrimitiveIdVal( meshId );
        if ( pCanonical == nullptr )
            return {};

        const string_view canonical( pCanonical );
        if ( canonical == "cube" )
            return createUnitCube();
        if ( canonical == "quad" )
            return createRectMesh();
        if ( canonical == "plane" )
            return createPlane();
        if ( canonical == "sphere" )
            return createSphere();
        if ( canonical == "cylinder" )
            return createCylinder();
        if ( canonical == "capsule" )
            return createCapsule();
        return createCone();
    }

    shared_ptr<Mesh> MeshUtil::acquirePrimitive( string_view meshId )
    {
        const utf8* pCanonical = canonicalPrimitiveIdVal( meshId );
        if ( pCanonical == nullptr )
            return {};

        // 캐시는 **약한 참조**다. 소유는 쓰는 쪽에 있고, 마지막 사용자가 놓으면 메시도 같이 사라진다.
        // 그래서 디바이스가 내려갈 때 이 표가 붙들고 있는 GPU 자원이 없다.
        static mutex                                        s_mutexPrimitive;
        static unordered_map<hashed_string, weak_ptr<Mesh>> s_mapPrimitive;

        const hashed_string key( pCanonical );

        std::scoped_lock<mutex> lock{ s_mutexPrimitive };
        auto                    iter = s_mapPrimitive.find( key );
        if ( iter != s_mapPrimitive.end() )
        {
            if ( shared_ptr<Mesh> pShared = iter->second.lock() )
                return pShared;
        }

        shared_ptr<Mesh> pMesh = createPrimitive( meshId );
        if ( pMesh != nullptr )
            s_mapPrimitive[key] = pMesh;
        return pMesh;
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
        // UV 는 위도 · 경도 그대로 편다. 구에 평면 투영을 쓰면 극 근처가 뭉개진다.
        auto vertexAt = [&]( uint32 stack, uint32 slice ) -> BuildVertex
        {
            const float3  position = pointAt( stack, slice );
            const float32 v        = static_cast<float32>( stack ) / static_cast<float32>( stackCount );
            return BuildVertex{ position, radialNormal( position ), revolvedUv( slice, sliceCount, v ) };
        };

        for ( uint32 stack = 0; stack < stackCount; ++stack )
        {
            for ( uint32 slice = 0; slice < sliceCount; ++slice )
            {
                const BuildVertex topLeft     = vertexAt( stack, slice );
                const BuildVertex topRight    = vertexAt( stack, slice + 1 );
                const BuildVertex bottomLeft  = vertexAt( stack + 1, slice );
                const BuildVertex bottomRight = vertexAt( stack + 1, slice + 1 );
                // 구의 노멀은 원점 기준 방향 그대로다. 면 노멀을 쓰면 각져 보인다.
                // 극에서는 한 쪽이 한 점으로 모여 퇴화 삼각형이 된다. 그건 넣지 않는다.
                if ( stack != 0 )
                    pushTriangle( listVert, topLeft, bottomLeft, topRight );
                if ( stack + 1 != stackCount )
                    pushTriangle( listVert, topRight, bottomLeft, bottomRight );
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
            // 옆면 UV: u 는 둘레, v 는 높이. 뚜껑은 평면 투영이라 `pushFlatTriangle` 이 알아서 한다.
            const RevolvedQuad quad = makeRevolvedQuad( slice, sliceCount, kRadius, kHalfY );
            pushRevolvedSide( listVert, quad, slice, sliceCount, 0.0f, 1.0f );
            // 뚜껑은 실제로 평평하다. 면 노멀(±Y)이 맞다.
            pushFlatTriangle( listVert, float3{ 0.0f, kHalfY, 0.0f }, quad._upper0, quad._upper1 );
            pushFlatTriangle( listVert, float3{ 0.0f, -kHalfY, 0.0f }, quad._lower1, quad._lower0 );
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
            // 원통부의 v 는 [0.25, 0.75] 를 쓴다. 위아래 반구가 나머지 절반을 나눠 갖는다.
            pushRevolvedSide( listVert, makeRevolvedQuad( slice, sliceCount, kRadius, kHalfY ), slice, sliceCount, 0.25f, 0.75f );

            // 반구의 노멀은 **그 반구의 중심**(0, ±kHalfY, 0) 기준 방향이다. 원점 기준으로 잡으면
            // 캡슐이 길수록 어긋난다(구가 아니라 원통부만큼 밀려 있다).
            auto capNormal = [&]( const float3& position, bool bTop ) -> float3
            {
                const float3 center{ 0.0f, bTop ? kHalfY : -kHalfY, 0.0f };
                const float3 delta = position - center;
                return ( delta.getLengthSquared() > MathUtil::Epsilon ) ? delta.normalize() : float3{ 0.0f, bTop ? 1.0f : -1.0f, 0.0f };
            };

            for ( uint32 stack = 0; stack < stackCount; ++stack )
            {
                for ( uint32 capIndex = 0; capIndex < 2; ++capIndex )
                {
                    const bool bTop = ( capIndex == 0 );
                    // 반구의 v 는 위 [0, 0.25] · 아래 [0.75, 1] 이다. 원통부(0.25~0.75)와 이어진다.
                    auto capVertex = [&]( uint32 capStack, uint32 capSlice ) -> BuildVertex
                    {
                        const float3  position = capPoint( capStack, capSlice, bTop );
                        const float32 ratio    = static_cast<float32>( capStack ) / static_cast<float32>( stackCount );
                        const float32 v        = bTop ? ( 0.25f - ratio * 0.25f ) : ( 0.75f + ratio * 0.25f );
                        return BuildVertex{ position, capNormal( position, bTop ), revolvedUv( capSlice, sliceCount, v ) };
                    };

                    const BuildVertex nearLeft  = capVertex( stack, slice );
                    const BuildVertex nearRight = capVertex( stack, slice + 1 );
                    const BuildVertex farLeft   = capVertex( stack + 1, slice );
                    const BuildVertex farRight  = capVertex( stack + 1, slice + 1 );
                    if ( bTop )
                    {
                        pushTriangle( listVert, nearLeft, farLeft, nearRight );
                        if ( stack + 1 != stackCount )
                            pushTriangle( listVert, nearRight, farLeft, farRight );
                    }
                    else
                    {
                        pushTriangle( listVert, nearLeft, nearRight, farLeft );
                        if ( stack + 1 != stackCount )
                            pushTriangle( listVert, nearRight, farRight, farLeft );
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

            // 옆면 노멀은 **기울기를 반영한다.** 높이 h, 밑반지름 r 인 원뿔의 옆면 노멀은
            // normalize( h*cosθ, r, h*sinθ ) 다. 방사 방향(수평)으로 두면 원뿔이 원통처럼 칠해진다.
            constexpr float32 kHeight    = kHalfY * 2.0f;
            auto              sideNormal = [&]( const float3& basePoint ) -> float3
            {
                return float3{ kHeight * basePoint._x / kRadius, kRadius, kHeight * basePoint._z / kRadius }.normalize();
            };
            const float3 sideNormal0 = sideNormal( base0 );
            const float3 sideNormal1 = sideNormal( base1 );
            // 꼭짓점의 노멀은 정의되지 않는다. 양옆 노멀의 평균을 쓴다(관례). UV 도 같은 이유로 중간이다.
            const float3  apexNormal = ( sideNormal0 + sideNormal1 ).normalize();
            const float32 apexU      = ( static_cast<float32>( slice ) + 0.5f ) / static_cast<float32>( sliceCount );
            pushTriangle( listVert, BuildVertex{
                                        base0, sideNormal0, revolvedUv( slice, sliceCount, 1.0f )
            },
                          BuildVertex{ apex, apexNormal, float2{ apexU, 0.0f } }, BuildVertex{ base1, sideNormal1, revolvedUv( slice + 1, sliceCount, 1.0f ) } );
            // 밑면은 실제로 평평하다. 면 노멀(-Y)이 맞다.
            pushFlatTriangle( listVert, float3{ 0.0f, -kHalfY, 0.0f }, base1, base0 );
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createRectMesh()
    {
        auto mesh = Mesh::create();
        // XY 평면이라 노멀은 +Z 다(이 감김의 면 노멀). UV 는 좌하단 (0,0) → 우상단 (1,1).
        auto quadVertex = []( float32 x, float32 y ) -> RHIVertex
        {
            return RHIVertex{
                { x, y, 0.0f },
                { 0.0f, 0.0f, 1.0f },
                { x + 0.5f, 0.5f - y },
                { 1.0f, 1.0f, 1.0f, 1.0f }
            };
        };
        vector<RHIVertex> listVert = {
            quadVertex( -0.5f, -0.5f ),
            quadVertex( 0.5f, -0.5f ),
            quadVertex( 0.5f, 0.5f ),
            quadVertex( -0.5f, -0.5f ),
            quadVertex( 0.5f, 0.5f ),
            quadVertex( -0.5f, 0.5f ),
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createPlane( uint32 segmentCount )
    {
        segmentCount = MathUtil::max( segmentCount, 1u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( segmentCount ) * segmentCount * 6 );

        // 면은 로컬 y = 0 이다. 예전에는 y = +0.5(큐브 윗면 자리)에 두어야 했다 — 정점에 노멀이 없어
        // 셰이더가 위치로 노멀을 지어냈고, y = 0 인 평면은 |y| 가 0 이라 ±X/±Z 노멀을 받았기 때문이다.
        // 이제 정점이 노멀을 들고 다니므로 그 회피가 필요 없다.
        constexpr float32 kSurfaceY = 0.0f;
        constexpr float3  kUp{ 0.0f, 1.0f, 0.0f };
        const float32     step = 1.0f / static_cast<float32>( segmentCount );

        auto cornerAt = [&]( uint32 ix, uint32 iz ) -> float3
        {
            return float3{ -0.5f + static_cast<float32>( ix ) * step, kSurfaceY,
                           -0.5f + static_cast<float32>( iz ) * step };
        };

        for ( uint32 iz = 0; iz < segmentCount; ++iz )
        {
            for ( uint32 ix = 0; ix < segmentCount; ++ix )
            {
                const float3 p00 = cornerAt( ix, iz );
                const float3 p10 = cornerAt( ix + 1, iz );
                const float3 p11 = cornerAt( ix + 1, iz + 1 );
                const float3 p01 = cornerAt( ix, iz + 1 );

                // 체크무늬로 칠한다. 단색이면 그림자 경계는 보여도 **면이 어디까지인지**가 안 보여서,
                // 그림자가 맞는 자리에 졌는지 판단할 기준이 사라진다.
                const bool    bDark = ( ( ix + iz ) & 1u ) != 0u;
                const float32 tone  = bDark ? 0.52f : 0.72f;
                const float4  color{ tone, tone, tone * 1.05f, 1.0f };

                // 위에서 내려다볼 때 앞면이 되도록 감는다(이 엔진의 앞면 규약). 노멀은 모두 +Y 다.
                // UV 는 격자 칸이 아니라 **평면 전체**에 0..1 로 편다. 칸마다 0..1 을 주면
                // 텍스처가 칸마다 반복돼 "바닥 한 장" 이 아니라 타일이 된다.
                auto pushPlaneVertex = [&]( const float3& position )
                {
                    listVert.push_back( RHIVertex{
                        { position._x, position._y, position._z },
                        { kUp._x, kUp._y, kUp._z },
                        { position._x + 0.5f, position._z + 0.5f },
                        { color._x, color._y, color._z, color._w }
                    } );
                };
                pushPlaneVertex( p00 );
                pushPlaneVertex( p01 );
                pushPlaneVertex( p11 );
                pushPlaneVertex( p00 );
                pushPlaneVertex( p11 );
                pushPlaneVertex( p10 );
            }
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }
} // namespace sw

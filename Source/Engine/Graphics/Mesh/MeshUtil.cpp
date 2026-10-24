#include "pch.h"

#include "Engine/Graphics/Mesh/MeshUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/XorShift32.h"
#include "Core/Memory/LifetimeScope.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Resource/Cache/BuiltInAssetCache.h"
#include "Engine/Resource/Cache/WeakInternCache.h"

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
         *          "안 그려진다" 로만 보여서 렌더러 버그로 오인하기 쉽습니다. 손으로 맞추는 대신 여기서 바로잡습니다.
         *          맞바꿀 때 노멀 · UV 가 **위치와 같이** 따라갑니다. `BuildVertex` 로 묶어 둔 이유입니다.
         * @warning **원점 중심 볼록 도형에서만 맞는 판정입니다.** 삼각형 중심이 곧 바깥 방향이라는 가정을
         *          씁니다. 오목하거나 원점을 품지 않는 기하를 넣으려면 이 함수를 쓰면 안 됩니다.
         */
        void pushTriangle( vector<RHIVertex>& outList, const BuildVertex& a, const BuildVertex& b, const BuildVertex& c )
        {
            const float3 faceNormal = ( b._position - a._position ).cross( c._position - a._position );
            if ( faceNormal.getLengthSquared() <= MathUtil::kEpsilon )
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
            if ( normal.getLengthSquared() <= MathUtil::kEpsilon )
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
            return ( position.getLengthSquared() > MathUtil::kEpsilon ) ? position.normalize() : float3::Up;
        }

        /** @brief 둘레 각도입니다. */
        float32 sliceAngle( uint32 slice, uint32 sliceCount )
        {
            return 2.0f * MathUtil::kPi * static_cast<float32>( slice ) / static_cast<float32>( sliceCount );
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
            const float32 angleStart = sliceAngle( slice, sliceCount );
            const float32 angleEnd   = sliceAngle( slice + 1, sliceCount );
            RevolvedQuad  quad{};
            quad._lower0 = float3{ radius * MathUtil::cos( angleStart ), -halfY, radius * MathUtil::sin( angleStart ) };
            quad._lower1 = float3{ radius * MathUtil::cos( angleEnd ), -halfY, radius * MathUtil::sin( angleEnd ) };
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

        /** @brief 위치 · 노멀 · UV · 색을 RHI 정점으로 묶습니다. 탄젠트는 0 으로 둡니다 — `Mesh::setVertices` 가 위치 · 노멀 · UV 로 구합니다. */
        RHIVertex makeRHIVertex( const float3& position, const float3& normal, const float2& uv, const float4& color )
        {
            return RHIVertex{
                { position._x, position._y, position._z },
                { normal._x, normal._y, normal._z },
                { uv._x, uv._y },
                { color._x, color._y, color._z, color._w },
                { 0.0f, 0.0f, 0.0f, 0.0f }
            };
        }

        /** @brief 구의 (stack, slice) 꼭짓점입니다. UV 는 위도 · 경도 그대로 편다. 구에 평면 투영을 쓰면 극 근처가 뭉개진다. */
        BuildVertex makeSphereVertex( uint32 stack, uint32 slice, uint32 stackCount, uint32 sliceCount, float32 radius )
        {
            const float32 phi      = MathUtil::kPi * static_cast<float32>( stack ) / static_cast<float32>( stackCount );
            const float32 theta    = sliceAngle( slice, sliceCount );
            const float3  position = float3{ radius * MathUtil::sin( phi ) * MathUtil::cos( theta ),
                                            radius * MathUtil::cos( phi ),
                                            radius * MathUtil::sin( phi ) * MathUtil::sin( theta ) };
            const float32 v        = static_cast<float32>( stack ) / static_cast<float32>( stackCount );
            return BuildVertex{ position, radialNormal( position ), revolvedUv( slice, sliceCount, v ) };
        }

        /** @brief 캡슐 반구를 짓는 데 필요한 모양입니다. */
        struct CapsuleShape
        {
            uint32  _stackCount;
            uint32  _sliceCount;
            float32 _radius;
            float32 _halfY;
        };

        /**
         * @brief 캡슐 반구의 (stack, slice) 꼭짓점입니다. 반구는 중심을 ±halfY 로 옮긴 구면이다.
         * @details 노멀은 **그 반구의 중심**(0, ±halfY, 0) 기준 방향이다. 원점 기준으로 잡으면 캡슐이 길수록 어긋난다.
         *          반구의 v 는 위 [0, 0.25] · 아래 [0.75, 1] 이다. 원통부(0.25~0.75)와 이어진다.
         */
        BuildVertex makeCapVertex( const CapsuleShape& shape, uint32 stack, uint32 slice, bool bTop )
        {
            const float32 phi    = MathUtil::kHalfPi * static_cast<float32>( stack ) / static_cast<float32>( shape._stackCount );
            const float32 theta  = sliceAngle( slice, shape._sliceCount );
            const float32 ringY  = shape._radius * MathUtil::sin( phi );
            const float32 ringR  = shape._radius * MathUtil::cos( phi );
            const float32 offset = bTop ? shape._halfY : -shape._halfY;
            const float3  position{ ringR * MathUtil::cos( theta ), offset + ( bTop ? ringY : -ringY ), ringR * MathUtil::sin( theta ) };
            const float3  center{ 0.0f, offset, 0.0f };
            const float3  delta  = position - center;
            const float3  normal = ( delta.getLengthSquared() > MathUtil::kEpsilon ) ? delta.normalize() : float3{ 0.0f, bTop ? 1.0f : -1.0f, 0.0f };
            const float32 ratio  = static_cast<float32>( stack ) / static_cast<float32>( shape._stackCount );
            const float32 v      = bTop ? ( 0.25f - ratio * 0.25f ) : ( 0.75f + ratio * 0.25f );
            return BuildVertex{ position, normal, revolvedUv( slice, shape._sliceCount, v ) };
        }

        /**
         * @brief 원뿔 옆면 노멀입니다. **기울기를 반영한다.** 높이 h, 밑반지름 r 인 원뿔의 옆면 노멀은 normalize( h*cosθ, r, h*sinθ ) 다.
         *        방사 방향(수평)으로 두면 원뿔이 원통처럼 칠해진다.
         */
        float3 coneSideNormal( const float3& basePoint, float32 height, float32 radius )
        {
            return float3{ height * basePoint._x / radius, radius, height * basePoint._z / radius }.normalize();
        }

        /** @brief 사각형 메시(XY 평면)의 꼭짓점입니다. 노멀은 +Z(이 감김의 면 노멀), UV 는 좌하단 (0,0) → 우상단 (1,1) 입니다. */
        RHIVertex makeRectVertex( float32 x, float32 y )
        {
            return makeRHIVertex( float3{ x, y, 0.0f }, float3{ 0.0f, 0.0f, 1.0f }, float2{ x + 0.5f, 0.5f - y }, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        }

        /**
         * @brief 스프라이트 사각형의 꼭짓점입니다. 앞면(-Z 를 향함)은 +Z 를 보는 카메라의 화면 오른쪽이 +X 라 u 는 +X 로 는다.
         *        뒷면(+Z 를 향함)은 그 카메라의 화면 오른쪽이 -X 라 u 는 -X 로 는다. v 는 두 면 모두 위가 0 이다.
         */
        RHIVertex makeSpriteVertex( float32 x, float32 y, float32 normalZ )
        {
            const float32 u = ( normalZ < 0.0f ) ? ( x + 0.5f ) : ( 0.5f - x );
            return makeRHIVertex( float3{ x, y, 0.0f }, float3{ 0.0f, 0.0f, normalZ }, float2{ u, 0.5f - y }, float4{ 1.0f, 1.0f, 1.0f, 1.0f } );
        }

        /** @brief 잔디 잎의 정점 하나를 넣습니다. 색은 흰색입니다. */
        void pushGrassVertex( vector<RHIVertex>& outList, const float3& normal, const float3& position, float32 u, float32 v )
        {
            outList.push_back( makeRHIVertex( position, normal, float2{ u, v }, float4{ 1.0f, 1.0f, 1.0f, 1.0f } ) );
        }

        /** @brief 평면 격자의 (ix, iz) 모서리입니다. 면은 로컬 y = 0, 범위는 [-0.5, 0.5] 입니다. */
        float3 makePlaneCorner( uint32 ix, uint32 iz, float32 step )
        {
            return float3{ -0.5f + static_cast<float32>( ix ) * step, 0.0f, -0.5f + static_cast<float32>( iz ) * step };
        }

        struct MeshUtilInternal
        {
            using PrimitiveCache = WeakInternCache<hashed_string, Mesh>;

            /** @brief 기준 이름(`canonicalPrimitiveIDVal`)으로 흰색 도형을 짓습니다. */
            static shared_ptr<Mesh> createSharedPrimitive( const hashed_string& canonicalID ) { return MeshUtil::createPrimitive( canonicalID.c_str() ); }

            /** @brief 캐시를 엔진 수명 범위가 만들 수 있게 종류 이름을 쥔 껍데기입니다(`ScopeOwned` 는 기본 생성한다). */
            struct PrimitiveCacheHolder
            {
                PrimitiveCache _cache{ "PrimitiveMesh" };
            };

            /** @brief 내장 도형 표의 소유자입니다 — 엔진 수명 범위가 소유해 엔진 종료 때 사라진다(처음 쓸 때 만들어진다). Engine.dll 안이라 모듈 핫 리로드에 사라지지 않는다. */
            inline static ScopeOwned<PrimitiveCacheHolder> s_primitiveCache{};

            /** @brief 내장 도형 표입니다. */
            static PrimitiveCache& getPrimitiveCache()
            {
                PrimitiveCacheHolder* const pHolder = s_primitiveCache.getOrCreate();
                SW_ASSERT( pHolder != nullptr ); // nullptr 은 엔진 수명 범위가 닫힌 뒤의 접근이다 — 캐시를 쓸 엔진이 없다
                return pHolder->_cache;
            }
        };
    } // namespace

    shared_ptr<Mesh> MeshUtil::createUnitCube()
    {
        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( 36 );

        // 면마다 색을 달리한다. 어느 면을 보고 있는지가 그림에서 바로 읽혀야 검증이 된다.
        // 노멀 · UV 는 `pushFlatTriangle` 이 면에서 만든다.
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
    static const utf8* canonicalPrimitiveIDVal( string_view meshID )
    {
        if ( meshID.empty() || StringUtil::equals( meshID, "Cube", true ) )
            return "cube";
        if ( StringUtil::equals( meshID, "Quad", true ) || StringUtil::equals( meshID, "Rect", true ) )
            return "quad";
        if ( StringUtil::equals( meshID, "Sprite", true ) )
            return "sprite";
        if ( StringUtil::equals( meshID, "Plane", true ) || StringUtil::equals( meshID, "Ground", true ) )
            return "plane";
        if ( StringUtil::equals( meshID, "Sphere", true ) )
            return "sphere";
        if ( StringUtil::equals( meshID, "Cylinder", true ) )
            return "cylinder";
        if ( StringUtil::equals( meshID, "Capsule", true ) )
            return "capsule";
        if ( StringUtil::equals( meshID, "Cone", true ) )
            return "cone";
        if ( StringUtil::equals( meshID, "GrassClump", true ) )
            return "grassclump";
        return nullptr;
    }

    shared_ptr<Mesh> MeshUtil::createPrimitive( string_view meshID, PrimitiveVertexColor vertexColor )
    {
        SW_MEMORY_SCOPE( Mesh );
        const utf8* pCanonical = canonicalPrimitiveIDVal( meshID );
        if ( pCanonical == nullptr )
            return {};

        const string_view canonical( pCanonical );
        shared_ptr<Mesh>  pMesh;
        if ( canonical == "cube" )
            pMesh = createUnitCube();
        else if ( canonical == "quad" )
            pMesh = createRectMesh();
        else if ( canonical == "sprite" )
            pMesh = createSpriteQuad();
        else if ( canonical == "plane" )
            pMesh = createPlane();
        else if ( canonical == "sphere" )
            pMesh = createSphere();
        else if ( canonical == "cylinder" )
            pMesh = createCylinder();
        else if ( canonical == "capsule" )
            pMesh = createCapsule();
        else if ( canonical == "grassclump" )
            pMesh = createGrassClump();
        else
            pMesh = createCone();
        if ( pMesh != nullptr && vertexColor == PrimitiveVertexColor::White )
        {
            // 생성기는 검증 색으로 만든다. 게임이 쓰는 도형은 색을 모두 1 로 — 머티리얼 색이 그대로 보이게.
            vector<RHIVertex> listVertex = pMesh->getVertices();
            for ( RHIVertex& vertex : listVertex )
            {
                vertex._arrColor[0] = 1.0f;
                vertex._arrColor[1] = 1.0f;
                vertex._arrColor[2] = 1.0f;
                vertex._arrColor[3] = 1.0f;
            }
            pMesh->setVertices( std::move( listVertex ) );
        }
        return pMesh;
    }

    shared_ptr<Mesh> MeshUtil::acquirePrimitive( string_view meshID )
    {
        SW_MEMORY_SCOPE( Mesh );
        const utf8* pCanonical = canonicalPrimitiveIDVal( meshID );
        if ( pCanonical == nullptr )
            return {};

        // 캐시는 **약한 참조**다. 소유는 쓰는 쪽에 있고, 마지막 사용자가 놓으면 메시도 같이 사라진다.
        // 그래서 디바이스가 내려갈 때 이 표가 붙들고 있는 GPU 자원이 없다.
        return MeshUtilInternal::getPrimitiveCache().acquire( hashed_string( pCanonical ), &MeshUtilInternal::createSharedPrimitive );
    }

    IAssetCache& MeshUtil::getPrimitiveCache()
    {
        return MeshUtilInternal::getPrimitiveCache();
    }

    shared_ptr<Mesh> MeshUtil::createSphere( uint32 stackCount, uint32 sliceCount )
    {
        stackCount = MathUtil::max( stackCount, 2u );
        sliceCount = MathUtil::max( sliceCount, 3u );

        auto              mesh = Mesh::create();
        vector<RHIVertex> listVert;
        listVert.reserve( static_cast<size_t>( stackCount ) * sliceCount * 6 );

        constexpr float32 kRadius = 0.5f;
        for ( uint32 stack = 0; stack < stackCount; ++stack )
        {
            for ( uint32 slice = 0; slice < sliceCount; ++slice )
            {
                const BuildVertex topLeft     = makeSphereVertex( stack, slice, stackCount, sliceCount, kRadius );
                const BuildVertex topRight    = makeSphereVertex( stack, slice + 1, stackCount, sliceCount, kRadius );
                const BuildVertex bottomLeft  = makeSphereVertex( stack + 1, slice, stackCount, sliceCount, kRadius );
                const BuildVertex bottomRight = makeSphereVertex( stack + 1, slice + 1, stackCount, sliceCount, kRadius );
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
        const CapsuleShape shape{ stackCount, sliceCount, kRadius, kHalfY };

        for ( uint32 slice = 0; slice < sliceCount; ++slice )
        {
            // 원통부의 v 는 [0.25, 0.75] 를 쓴다. 위아래 반구가 나머지 절반을 나눠 갖는다.
            pushRevolvedSide( listVert, makeRevolvedQuad( slice, sliceCount, kRadius, kHalfY ), slice, sliceCount, 0.25f, 0.75f );

            for ( uint32 stack = 0; stack < stackCount; ++stack )
            {
                for ( uint32 capIndex = 0; capIndex < 2; ++capIndex )
                {
                    const bool bTop = ( capIndex == 0 );

                    const BuildVertex nearLeft  = makeCapVertex( shape, stack, slice, bTop );
                    const BuildVertex nearRight = makeCapVertex( shape, stack, slice + 1, bTop );
                    const BuildVertex farLeft   = makeCapVertex( shape, stack + 1, slice, bTop );
                    const BuildVertex farRight  = makeCapVertex( shape, stack + 1, slice + 1, bTop );
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
            const float32 angleStart = sliceAngle( slice, sliceCount );
            const float32 angleEnd   = sliceAngle( slice + 1, sliceCount );
            const float3  base0{ kRadius * MathUtil::cos( angleStart ), -kHalfY, kRadius * MathUtil::sin( angleStart ) };
            const float3  base1{ kRadius * MathUtil::cos( angleEnd ), -kHalfY, kRadius * MathUtil::sin( angleEnd ) };

            // 옆면 노멀은 기울기를 반영한다(`coneSideNormal`).
            constexpr float32 kHeight     = kHalfY * 2.0f;
            const float3      sideNormal0 = coneSideNormal( base0, kHeight, kRadius );
            const float3      sideNormal1 = coneSideNormal( base1, kHeight, kRadius );
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
        auto              mesh     = Mesh::create();
        vector<RHIVertex> listVert = {
            makeRectVertex( -0.5f, -0.5f ),
            makeRectVertex( 0.5f, -0.5f ),
            makeRectVertex( 0.5f, 0.5f ),
            makeRectVertex( -0.5f, -0.5f ),
            makeRectVertex( 0.5f, 0.5f ),
            makeRectVertex( -0.5f, 0.5f ),
        };
        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createSpriteQuad()
    {
        auto mesh = Mesh::create();
        // 감김은 이 엔진의 앞면 규약((b - a) x (c - a) 가 그 면의 노멀)을 따른다. 앞 · 뒷면의 u 방향은 `makeSpriteVertex` 주석.
        vector<RHIVertex> listVert = {
            makeSpriteVertex( -0.5f, -0.5f, -1.0f ),
            makeSpriteVertex( 0.5f, 0.5f, -1.0f ),
            makeSpriteVertex( 0.5f, -0.5f, -1.0f ),
            makeSpriteVertex( -0.5f, -0.5f, -1.0f ),
            makeSpriteVertex( -0.5f, 0.5f, -1.0f ),
            makeSpriteVertex( 0.5f, 0.5f, -1.0f ),
            makeSpriteVertex( -0.5f, -0.5f, 1.0f ),
            makeSpriteVertex( 0.5f, -0.5f, 1.0f ),
            makeSpriteVertex( 0.5f, 0.5f, 1.0f ),
            makeSpriteVertex( -0.5f, -0.5f, 1.0f ),
            makeSpriteVertex( 0.5f, 0.5f, 1.0f ),
            makeSpriteVertex( -0.5f, 0.5f, 1.0f ),
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

        // 면은 로컬 y = 0 이다(`makePlaneCorner`). 노멀은 정점이 들고 다닌다(+Y).
        constexpr float3 kUp{ 0.0f, 1.0f, 0.0f };
        const float32    step = 1.0f / static_cast<float32>( segmentCount );

        for ( uint32 iz = 0; iz < segmentCount; ++iz )
        {
            for ( uint32 ix = 0; ix < segmentCount; ++ix )
            {
                const float3 p00 = makePlaneCorner( ix, iz, step );
                const float3 p10 = makePlaneCorner( ix + 1, iz, step );
                const float3 p11 = makePlaneCorner( ix + 1, iz + 1, step );
                const float3 p01 = makePlaneCorner( ix, iz + 1, step );

                // 체크무늬로 칠한다. 단색이면 그림자 경계는 보여도 **면이 어디까지인지**가 안 보여서,
                // 그림자가 맞는 자리에 졌는지 판단할 기준이 사라진다.
                const bool    bDark = ( ( ix + iz ) & 1u ) != 0u;
                const float32 tone  = bDark ? 0.52f : 0.72f;
                const float4  color{ tone, tone, tone * 1.05f, 1.0f };

                // 위에서 내려다볼 때 앞면이 되도록 감는다(이 엔진의 앞면 규약). 노멀은 모두 +Y 다.
                // UV 는 격자 칸이 아니라 **평면 전체**에 0..1 로 편다. 칸마다 0..1 을 주면
                // 텍스처가 칸마다 반복돼 "바닥 한 장" 이 아니라 타일이 된다.
                for ( const float3& position : { p00, p01, p11, p00, p11, p10 } )
                {
                    listVert.push_back( makeRHIVertex( position, kUp, float2{ position._x + 0.5f, position._z + 0.5f }, color ) );
                }
            }
        }

        mesh->setVertices( std::move( listVert ) );
        return mesh;
    }

    shared_ptr<Mesh> MeshUtil::createGrassClump( uint32 bladeCount )
    {
        bladeCount = MathUtil::max( bladeCount, 1u );
        // 고정 씨앗 xorshift — 포기 모양이 실행마다 같아야 스크린샷이 비교된다.
        uint32 state    = 0x2545F491u;
        auto   nextUnit = [&state]() -> float32
        {
            state = XorShift32::next( state );
            return XorShift32::toUnit24( state );
        };

        constexpr uint32  kSegmentCount = 3;
        vector<RHIVertex> listVertex;
        listVertex.reserve( static_cast<size_t>( bladeCount ) * kSegmentCount * 12 );
        for ( uint32 bladeIndex = 0; bladeIndex < bladeCount; ++bladeIndex )
        {
            const float32 angle  = nextUnit() * MathUtil::kTwoPi;
            const float32 radius = nextUnit() * 0.22f;
            const float32 height = 0.5f + nextUnit() * 0.3f;
            const float32 width  = 0.05f + nextUnit() * 0.03f;
            const float32 lean   = 0.08f + nextUnit() * 0.18f;
            const float32 facing = nextUnit() * MathUtil::kTwoPi;
            const float3  root{ MathUtil::cos( angle ) * radius, 0.0f, MathUtil::sin( angle ) * radius };
            const float3  across{ MathUtil::cos( facing ), 0.0f, MathUtil::sin( facing ) };
            const float3  outward{ -across._z, 0.0f, across._x };
            const float3  normal = ( outward * 0.35f + float3::Up ).normalize();
            // 잎은 위로 갈수록 가늘어지고 outward 쪽으로 휜다(포물선). 마디마다 (왼쪽, 오른쪽) 두 점.
            float3 arrLeft[kSegmentCount + 1];
            float3 arrRight[kSegmentCount + 1];
            for ( uint32 node = 0; node <= kSegmentCount; ++node )
            {
                const float32 ratio     = static_cast<float32>( node ) / static_cast<float32>( kSegmentCount );
                const float32 halfWidth = 0.5f * width * ( 1.0f - ratio );
                const float3  center    = root + float3::Up * ( height * ratio ) + outward * ( lean * ratio * ratio );
                arrLeft[node]           = center - across * halfWidth;
                arrRight[node]          = center + across * halfWidth;
            }
            for ( uint32 node = 0; node < kSegmentCount; ++node )
            {
                const float32 v0 = 1.0f - static_cast<float32>( node ) / static_cast<float32>( kSegmentCount );
                const float32 v1 = 1.0f - static_cast<float32>( node + 1 ) / static_cast<float32>( kSegmentCount );
                // 앞면과 뒷면(감김을 뒤집은 같은 삼각형) — 끝 마디는 폭이 0 이라 삼각형 하나가 납작해지지만 그대로 둔다(넓이 0 은 그려지지 않는다).
                pushGrassVertex( listVertex, normal, arrLeft[node], 0.0f, v0 );
                pushGrassVertex( listVertex, normal, arrLeft[node + 1], 0.0f, v1 );
                pushGrassVertex( listVertex, normal, arrRight[node + 1], 1.0f, v1 );
                pushGrassVertex( listVertex, normal, arrLeft[node], 0.0f, v0 );
                pushGrassVertex( listVertex, normal, arrRight[node + 1], 1.0f, v1 );
                pushGrassVertex( listVertex, normal, arrRight[node], 1.0f, v0 );
                pushGrassVertex( listVertex, normal, arrLeft[node], 0.0f, v0 );
                pushGrassVertex( listVertex, normal, arrRight[node + 1], 1.0f, v1 );
                pushGrassVertex( listVertex, normal, arrLeft[node + 1], 0.0f, v1 );
                pushGrassVertex( listVertex, normal, arrLeft[node], 0.0f, v0 );
                pushGrassVertex( listVertex, normal, arrRight[node], 1.0f, v0 );
                pushGrassVertex( listVertex, normal, arrRight[node + 1], 1.0f, v1 );
            }
        }
        shared_ptr<Mesh> mesh = Mesh::create();
        mesh->setVertices( std::move( listVertex ) );
        return mesh;
    }
} // namespace sw

namespace sw
{
    SW_BUILT_IN_SHARED_ASSET_CACHE( PrimitiveMesh, &MeshUtil::getPrimitiveCache );
} // namespace sw

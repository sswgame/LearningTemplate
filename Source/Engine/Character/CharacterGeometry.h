/**
 * @file CharacterGeometry.h
 * @brief 캐릭터 외형 계산(피팅 · 병합 · 절단 · 체형 · 소켓)이 주고받는 중립 형상(`AppearanceGeometry`)과 본 배열(`CharacterBoneArray`)입니다.
 * @details 메시 에셋 · 포즈 버퍼를 모르는 값 타입입니다. 외형을 조립하는 쪽이 `Mesh` · 포즈를 이 꼴로 바꿔 넘기고, 결과(삼각형 마스크 · 정점
 *          델타 · 병합된 형상)를 다시 GPU 쪽에 싣습니다. 그래서 이 폴더의 계산은 GPU · 씬 없이 시험할 수 있습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 캐릭터 형상의 공통 상수입니다. */
    struct CharacterGeometryConstant
    {
        static constexpr uint32 kMaxSkinInfluence = 4;      ///< 정점 하나가 따르는 본 수의 상한
        static constexpr uint16 kNoGroup          = 0xFFFF; ///< 정점 그룹 · 몸 영역이 없음
        static constexpr uint16 kNoPart           = 0xFFFF; ///< 삼각형의 부품 번호가 없음
    };
} // namespace sw

namespace sw
{
    /** @brief 정점 하나의 스킨 가중치입니다. 가중치 합은 1 이고, 쓰지 않는 칸은 가중치 0 입니다. */
    struct SW_API SkinInfluence
    {
        uint16  _arrJoint[CharacterGeometryConstant::kMaxSkinInfluence]{ 0, 0, 0, 0 };
        float32 _arrWeight[CharacterGeometryConstant::kMaxSkinInfluence]{ 0.0f, 0.0f, 0.0f, 0.0f };

        /** @brief 가중치가 가장 큰 본입니다. 가중치가 모두 0 이면 -1 입니다. */
        int32 findDominantJoint() const;
    };
} // namespace sw

namespace sw
{
    /** @brief 모프 대상 하나 — 이름과 정점마다의 위치 · 법선 델타입니다(형상의 정점 수와 같은 길이). */
    struct GeometryMorphTarget
    {
        hashed_string  _name{};
        vector<float3> _listPositionDelta{};
        vector<float3> _listNormalDelta{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 외형 계산의 형상입니다. 바인드 공간(유닛 뿌리 기준)의 위치 · 법선 · UV · 삼각형 인덱스와 선택 칸들입니다.
     * @details 선택 칸은 비어 있으면 없는 것입니다 — 스킨(`_listSkin`, 비면 강체 부품), 칠한 정점 그룹(`_listVertexGroup` 이 `_listGroupName` 의
     *          번호를 듦), 삼각형마다 부품 번호(`_listTrianglePart`, 병합 결과가 채움), 모프 대상.
     */
    struct SW_API AppearanceGeometry
    {
        vector<float3>              _listPosition{};
        vector<float3>              _listNormal{};
        vector<float2>              _listUv{};
        vector<uint32>              _listIndex{};
        vector<SkinInfluence>       _listSkin{};
        vector<uint16>              _listVertexGroup{};
        vector<hashed_string>       _listGroupName{};
        vector<uint16>              _listTrianglePart{};
        vector<GeometryMorphTarget> _listMorph{};

        /** @brief 정점 수입니다. */
        uint32 getVertexCount() const { return static_cast<uint32>( _listPosition.size() ); }
        /** @brief 삼각형 수입니다. */
        uint32 getTriangleCount() const { return static_cast<uint32>( _listIndex.size() / 3 ); }
        /** @brief 스킨 가중치가 있으면 true 입니다. */
        bool isSkinned() const { return _listSkin.empty() == false && _listSkin.size() == _listPosition.size(); }
        /** @brief 칸 길이가 서로 맞고 인덱스가 정점 안을 가리키면 true 입니다. */
        bool isValid() const;
        /** @brief 이름의 정점 그룹 번호입니다. 없으면 `kNoGroup` 입니다. */
        uint16 findGroup( const hashed_string& name ) const;
        /** @brief 이름의 모프 대상입니다. 없으면 nullptr 입니다. */
        const GeometryMorphTarget* findMorph( const hashed_string& name ) const;
        /** @brief 위치에서 면적 가중 정점 법선을 다시 계산합니다. */
        void computeVertexNormals();
        /** @brief 정점 · 삼각형을 모두 비웁니다. */
        void clear();
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 본 배열 — 이름 · 부모 · 로컬 변환 · 모델(유닛 뿌리 기준) 변환입니다. 부모는 항상 자기보다 앞입니다.
     * @details 행벡터 규칙이라 모델 = 로컬 × 부모 모델입니다. 2D 본은 Z 축 회전과 Z = 0 인 같은 배열입니다.
     */
    struct SW_API CharacterBoneArray
    {
        vector<hashed_string> _listName{};
        vector<int32>         _listParent{};
        vector<float4x4>      _listLocal{};
        vector<float4x4>      _listModel{};

        /** @brief 본을 끝에 붙이고 번호를 돌려줍니다. 부모가 이미 있는 본이 아니면(루트는 -1) 붙이지 않고 -1 입니다. */
        int32 addBone( const hashed_string& name, int32 parentIndex, const float4x4& localTransform );
        /** @brief 이름의 본 번호입니다. 없으면 -1 입니다. */
        int32 findBone( const hashed_string& name ) const;
        /** @brief 본 수입니다. */
        uint32 getBoneCount() const { return static_cast<uint32>( _listName.size() ); }
        /** @brief 로컬 변환에서 모델 변환을 다시 계산합니다. */
        void computeModelTransforms();
    };
} // namespace sw

namespace sw
{
    /** @brief 광선이 삼각형에 맞은 자리입니다. */
    struct GeometryRayHit
    {
        float3  _normal{};               ///< 맞은 삼각형의 면 법선(감은 방향 기준)
        uint32  _triangle{ 0 };          ///< 삼각형 번호
        float32 _distance{ 0.0f };       ///< 광선 시작에서의 거리
        float32 _baryU{ 0.0f };          ///< 무게중심 좌표(두 번째 정점 몫)
        float32 _baryV{ 0.0f };          ///< 무게중심 좌표(세 번째 정점 몫)
        uint16  _part{ 0 };              ///< 삼각형이 속한 표면(부품) 번호 — `SurfaceBvh::addSurface` 에 준 것
        uint8   _bFrontFace{ SW_FALSE }; ///< 광선이 면의 앞(법선 쪽)에서 들어왔는가
    };
} // namespace sw

namespace sw
{
    /** @brief 표면 위에서 한 점에 가장 가까운 자리입니다. */
    struct GeometryClosestPoint
    {
        float3  _point{};          ///< 가장 가까운 점
        float3  _normal{};         ///< 그 삼각형의 면 법선(단위)
        uint32  _triangle{ 0 };    ///< 표면 안의 삼각형 번호
        float32 _distance{ 0.0f }; ///< 질의 점까지의 거리
        float32 _baryU{ 0.0f };    ///< 무게중심 좌표(두 번째 정점 몫)
        float32 _baryV{ 0.0f };    ///< 무게중심 좌표(세 번째 정점 몫)
        uint16  _part{ 0 };        ///< 삼각형이 속한 표면(부품) 번호
    };
} // namespace sw

namespace sw
{
    /** @brief 형상 계산의 작은 수학 도우미입니다(전부 static). */
    struct SW_API CharacterGeometryUtil
    {
        /** @brief 길이 1 로 만든 사본입니다. 길이가 거의 0 이면 @p fallback 입니다. */
        static float3 makeUnitOr( const float3& value, const float3& fallback );
        /** @brief 이동 · 회전 · 스케일로 변환 행렬을 만듭니다(행벡터 — 스케일 → 회전 → 이동). */
        static float4x4 makeTransform( const float3& translation, const quaternion& rotation, const float3& scale );
        /** @brief 두 변환을 섞습니다 — 이동 · 스케일은 직선, 회전은 짧은 쪽 구면 보간입니다. */
        static float4x4 blendTransforms( const float4x4& from, const float4x4& to, float32 weight );
        /** @brief 삼각형 위에서 @p point 에 가장 가까운 점의 무게중심 좌표(@p outU = 두 번째 몫, @p outV = 세 번째 몫)입니다. */
        static float3 findClosestPointOnTriangle( const float3& point, const float3& a, const float3& b, const float3& c, float32& outU, float32& outV );
        /** @brief 광선과 삼각형(양면)의 교차입니다. 맞으면 true 와 거리 · 무게중심 좌표 · 앞면 여부입니다. */
        static bool intersectRayTriangle( const float3& origin, const float3& direction, const float3& a, const float3& b, const float3& c, float32& outDistance,
                                          float32& outU, float32& outV, bool& outFrontFace );
        /** @brief 무게중심 좌표로 세 값을 섞습니다. */
        static float3 interpolateBarycentric( const float3& a, const float3& b, const float3& c, float32 u, float32 v );
        /** @brief 0..1 을 부드럽게 1 → 0 으로 내리는 감쇠입니다(0 이하 1, 1 이상 0, 사이는 smoothstep 의 반대). */
        static float32 computeFalloff( float32 normalizedDistance );
        /** @brief 위치 배열과 인덱스로 면적 가중 정점 법선을 계산합니다. */
        static void computeVertexNormals( vector_reference<const float3> listPosition, vector_reference<const uint32> listIndex, vector<float3>& outListNormal );
    };
} // namespace sw

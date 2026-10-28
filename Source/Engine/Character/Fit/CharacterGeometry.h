/**
 * @file CharacterGeometry.h
 * @brief 캐릭터 외형 계산(피팅, 병합, 절단, 체형, 소켓)이 주고받는 중립 형상(`AppearanceGeometry`)과 본 배열(`CharacterBoneArray`)입니다.
 * @details 메시 에셋과 포즈 버퍼를 모르는 값 타입이다. 외형을 조립하는 쪽이 `Mesh` 와 포즈를 이 형태로 바꿔 넘기고, 결과(삼각형 마스크, 정점
 *          델타, 병합된 형상)를 다시 GPU 쪽에 싣는다. 그래서 이 폴더의 계산은 GPU 나 씬 없이 테스트할 수 있다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    /** @brief 캐릭터 형상의 공통 상수입니다. */
    struct CharacterGeometryConstant
    {
        static constexpr uint32 kMaxSkinInfluence = 4;      ///< 정점 하나가 따르는 본 수의 상한이다.
        static constexpr uint16 kNoGroup          = 0xFFFF; ///< 정점 그룹이나 몸 영역이 없음을 나타낸다.
        static constexpr uint16 kNoPart           = 0xFFFF; ///< 삼각형의 부품 번호가 없음을 나타낸다.
    };
} // namespace sw

namespace sw
{
    /** @brief 정점 하나의 스킨 가중치입니다. 가중치 합은 1 이고, 쓰지 않는 슬롯은 가중치가 0 입니다. */
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
    /** @brief 모프 대상 하나입니다. 이름과 정점마다의 위치와 법선 델타를 담습니다(형상의 정점 수와 같은 길이입니다). */
    struct GeometryMorphTarget
    {
        hashed_string  _name{};
        vector<float3> _listPositionDelta{};
        vector<float3> _listNormalDelta{};
    };
} // namespace sw

namespace sw
{
    struct BodyMorphWeight;
    struct CharacterBoneArray;

    /**
     * @brief 외형 계산의 형상입니다. 바인드 공간(유닛 루트 기준)의 위치, 법선, UV, 삼각형 인덱스와 선택 필드들을 담습니다.
     * @details 선택 필드는 비어 있으면 없는 것이다. 스킨(`_listSkin`, 비면 강체 부품), 칠한 정점 그룹(`_listVertexGroup` 이 `_listGroupName` 의
     *          번호를 가리킨다), 삼각형마다 부품 번호(`_listTrianglePart`, 병합 결과가 채운다), 모프 대상이 있다.
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
        /** @brief 배열 길이가 서로 맞고 인덱스가 정점 안을 가리키면 true 를 반환합니다. */
        bool isValid() const;
        /** @brief 이름의 정점 그룹 번호입니다. 없으면 `kNoGroup` 입니다. */
        uint16 findGroup( const hashed_string& name ) const;
        /** @brief 이름의 모프 대상입니다. 없으면 nullptr 입니다. */
        const GeometryMorphTarget* findMorph( const hashed_string& name ) const;
        /** @brief 위치에서 면적 가중 정점 법선을 다시 계산합니다. */
        void computeVertexNormals();
        /** @brief 체형의 모프 가중치를 위치(와 법선)에 겁니다. 형상에 없는 모프는 건너뜁니다. GPU 모프 풀과 같은 일을 CPU 에서 해 피팅 · 소켓 보정이 체형을 본다. */
        void applyMorphs( vector_reference<const BodyMorphWeight> listMorphWeight );
        /**
         * @brief 이 바인드 형상을 다른 포즈(본 비율을 적용한 레퍼런스 포즈나 애니메이션 포즈)로 선형 블렌드 스키닝해 @p outGeometry 에 씁니다.
         * @details 스킨 행렬 = 바인드 모델의 역 × 포즈 모델(행 벡터)이다. 스킨이 없는 형상은 그대로 복사한다. 법선도 같은 행렬로 돌린다.
         */
        void skinToPose( const CharacterBoneArray& bindBones, const CharacterBoneArray& poseBones, AppearanceGeometry& outGeometry ) const;
        /** @brief 정점과 삼각형을 모두 비웁니다. */
        void clear();
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 본 배열입니다. 이름, 부모, 로컬 변환, 모델(유닛 루트 기준) 변환을 담습니다. 부모는 항상 자기보다 앞에 옵니다.
     * @details 행 벡터 규칙이라 모델 = 로컬 × 부모 모델이다. 2D 본은 Z 축 회전과 Z = 0 인 같은 배열을 쓴다.
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
    /** @brief 광선이 삼각형에 맞은 지점입니다. */
    struct GeometryRayHit
    {
        float3  _normal{};               ///< 맞은 삼각형의 면 법선이다(감은 방향 기준).
        uint32  _triangle{ 0 };          ///< 삼각형 번호다.
        float32 _distance{ 0.0f };       ///< 광선 시작에서의 거리다.
        float32 _baryU{ 0.0f };          ///< 무게중심 좌표다(두 번째 정점 가중치).
        float32 _baryV{ 0.0f };          ///< 무게중심 좌표다(세 번째 정점 가중치).
        uint16  _part{ 0 };              ///< 삼각형이 속한 표면(부품) 번호다. `SurfaceBVH::addSurface` 에 준 값이다.
        uint8   _bFrontFace{ SW_FALSE }; ///< 광선이 면의 앞(법선 쪽)에서 들어왔는지 나타낸다.
    };
} // namespace sw

namespace sw
{
    /** @brief 표면 위에서 한 점에 가장 가까운 지점입니다. */
    struct GeometryClosestPoint
    {
        float3  _point{};          ///< 가장 가까운 점이다.
        float3  _normal{};         ///< 그 삼각형의 면 법선이다(단위 벡터).
        uint32  _triangle{ 0 };    ///< 표면 안의 삼각형 번호다.
        float32 _distance{ 0.0f }; ///< 질의 점까지의 거리다.
        float32 _baryU{ 0.0f };    ///< 무게중심 좌표다(두 번째 정점 가중치).
        float32 _baryV{ 0.0f };    ///< 무게중심 좌표다(세 번째 정점 가중치).
        uint16  _part{ 0 };        ///< 삼각형이 속한 표면(부품) 번호다.
    };
} // namespace sw

namespace sw
{
    /** @brief 형상 계산의 작은 수학 도우미입니다(전부 static). */
    struct SW_API CharacterGeometryUtil
    {
        /** @brief 길이 1 로 만든 사본입니다. 길이가 거의 0 이면 @p fallback 입니다. */
        static float3 makeUnitOr( const float3& value, const float3& fallback );
        /** @brief 이동, 회전, 스케일로 변환 행렬을 만듭니다(행 벡터이므로 스케일, 회전, 이동 순서입니다). */
        static float4x4 makeTransform( const float3& translation, const quaternion& rotation, const float3& scale );
        /** @brief 두 변환을 섞습니다. 이동과 스케일은 선형 보간이고, 회전은 짧은 쪽 구면 보간입니다. */
        static float4x4 blendTransforms( const float4x4& from, const float4x4& to, float32 weight );
        /** @brief 삼각형 위에서 @p point 에 가장 가까운 점의 무게중심 좌표를 구합니다(@p outU 는 두 번째 가중치, @p outV 는 세 번째 가중치입니다). */
        static float3 findClosestPointOnTriangle( const float3& point, const float3& a, const float3& b, const float3& c, float32& outU, float32& outV );
        /** @brief 광선과 삼각형(양면)의 교차를 검사합니다. 맞으면 true 와 함께 거리, 무게중심 좌표, 앞면 여부를 반환합니다. */
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

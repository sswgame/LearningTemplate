/**
 * @file MeshMerger.h
 * @brief 같은 애니메이션 단위(스켈레톤 하나)의 부품을 한 형상으로 병합합니다 — 잘린 삼각형을 빼고 인덱스 · 정점을 압축하고, 머티리얼(아틀라스) 묶음별 구간을 냅니다.
 * @details **스켈레톤을 넘어 병합하지 않습니다** — 부품마다 `_skeletonId` 가 같아야 하고, 다르면 실패입니다(애니메이션 단위가 다르면 포즈 버퍼가
 *          다르다). 쉬고 있는 강체 부품(칼집에 꽂힌 검)은 쉬는 변환으로 옮겨 소켓 본에 가중치 1 로 묶어 같이 병합하고, 뽑을 때(`extractPart`)
 *          다시 떼어 냅니다. 머티리얼 묶음 · UV 아틀라스는 훅(`IMeshMergeHooks`)이 정합니다 — 아틀라스를 굽는 쪽이 UV 를 옮깁니다.
 *          참고: 언리얼 Leader Pose / Skeletal Mesh Merge, Mutable.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/CharacterGeometry.h"

namespace sw
{
    struct FitPartResult;

    /** @brief 병합에 들어가는 부품 하나입니다. */
    struct MeshMergeSource
    {
        hashed_string             _name{};
        hashed_string             _material{};
        const AppearanceGeometry* _pGeometry{ nullptr };
        const FitPartResult*      _pFit{ nullptr };  ///< 있으면 보임 마스크로 잘린 삼각형을 빼고 정점 델타를 더함
        float4x4                  _restTransform{};  ///< 강체 부품의 쉬는 자리(유닛 공간). 스킨 부품은 항등
        uint32                    _skeletonId{ 0 };  ///< 애니메이션 단위(스켈레톤) 번호 — 모두 같아야 병합
        int32                     _socketBone{ -1 }; ///< 0 이상이면 강체 부품 — 모든 정점을 이 본에 가중치 1 로 묶음
    };
} // namespace sw

namespace sw
{
    /** @brief 병합 결과의 머티리얼 묶음 구간(그리기 호출 하나)입니다. */
    struct MergedSection
    {
        hashed_string _materialGroup{};
        uint32        _indexStart{ 0 };
        uint32        _indexCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 병합 결과에서 부품 하나가 차지한 정점 범위와 떼어 낼 때 필요한 것입니다. */
    struct MergedPartRange
    {
        hashed_string _name{};
        float4x4      _restTransform{};
        uint32        _vertexStart{ 0 };
        uint32        _vertexCount{ 0 };
        int32         _socketBone{ -1 };
    };
} // namespace sw

namespace sw
{
    /** @brief 병합 결과입니다. 형상의 `_listTrianglePart` 가 삼각형마다 부품 번호(`_listPart` 순서)입니다. */
    struct MergedMesh
    {
        AppearanceGeometry      _geometry{};
        vector<MergedSection>   _listSection{};
        vector<MergedPartRange> _listPart{};
        uint32                  _skeletonId{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 병합의 머티리얼 묶음 · 아틀라스 훅입니다. 기본(널)이면 묶음 = 부품 머티리얼, UV 그대로입니다. */
    class SW_API IMeshMergeHooks
    {
    public:
        IMeshMergeHooks()                                    = default;
        virtual ~IMeshMergeHooks()                           = default;
        IMeshMergeHooks( const IMeshMergeHooks& )            = delete;
        IMeshMergeHooks& operator=( const IMeshMergeHooks& ) = delete;

        /** @brief 부품이 들어갈 머티리얼 묶음(아틀라스 하나 = 묶음 하나)입니다. */
        virtual hashed_string getMaterialGroup( const MeshMergeSource& source ) const = 0;
        /** @brief 부품의 UV 를 묶음(아틀라스) 안의 자리로 옮깁니다. */
        virtual float2 remapUv( const MeshMergeSource& source, const float2& uv ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 병합 · 떼어 내기입니다(전부 static). */
    struct SW_API MeshMerger
    {
        /**
         * @brief 부품들을 병합합니다. 보이는 삼각형만 남기고, 그것이 쓰는 정점만 남깁니다(압축). 모프는 이름으로 합칩니다(없는 부품은 0).
         * @return 스켈레톤이 섞였거나 형상이 없으면 오류 글을 남기고 false 입니다.
         */
        static bool merge( vector_reference<const MeshMergeSource> listSource, const IMeshMergeHooks* pHooks, MergedMesh& outMerged, string* pOutError );
        /**
         * @brief 병합된 형상에서 부품 하나를 떼어 냅니다(강체 부품을 뽑을 때). 강체 부품은 쉬는 변환의 역으로 부품 공간에 돌려놓습니다.
         * @return 이름의 부품이 없으면 false 입니다.
         */
        static bool extractPart( const MergedMesh& merged, const hashed_string& partName, AppearanceGeometry& outGeometry );
    };
} // namespace sw

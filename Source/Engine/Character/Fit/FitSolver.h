/**
 * @file FitSolver.h
 * @brief 장비 피팅 관리자 — 잘라 내기 · 조임 · 밀어내기 · 보고를 한 곳에서 돕니다(`FitSolver`). 결과는 부품마다 삼각형 보임 마스크 + 바인드 공간 정점 델타 + 보고입니다.
 * @details 외형을 조립할 때(장비가 바뀔 때 한 번, 체형 모프를 건 바인드 포즈에서) 부릅니다. 순서:
 *          1. 겹마다 표면 BVH 를 한 번 짓고 연산들이 나눠 쓴다(`FitSolveState::_listLayerSurface`).
 *          2. 변형 단계(`FitPhase::Deform`) — 안쪽 < 바깥 짝마다 상호작용 표의 변형 연산(조임 · 밀어내기)이 정점 변위를 쌓고, 몇 번의 야코비 반복으로
 *             평균 변위를 적용한다. 움직인 겹의 BVH 는 다시 짓는다.
 *          3. 손 보정 조각(체형 모프 가중치만큼).
 *          4. 덮임 단계(`FitPhase::Coverage`) — 변형 뒤의 자리로 덮인 삼각형을 표시하고(잘라 내기), 덮이지 않은 삼각형과 정점을 나누는 삼각형은
 *             남겨 경계 한 겹을 막는다. 부품이 적은 숨김 영역은 경계 없이 뺀다.
 *          5. 검증 단계(`FitPhase::Validate`) — 단단함끼리의 관통 목록(자동으로 풀지 않음).
 *          변위는 스키닝 앞의 생성 보정 델타(모프 풀)로 싣고, 잘린 삼각형은 병합(`MeshMerger`)이 인덱스에서 뺍니다. 그래서 오버드로 · Z 파이팅이 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/FitOperator.h"
#include "Engine/Character/Fit/SurfaceBvh.h"

namespace sw
{
    struct AppearanceGeometry;
    struct CharacterBoneArray;
    struct FitProfileDef;

    class FitPartData;
    class FitTables;

    /** @brief 피팅에 들어가는 부품 하나입니다(몸도 부품 — 겹 `Body`). 형상은 체형을 건 바인드 공간입니다. */
    struct FitPartInput
    {
        hashed_string             _name{};
        const AppearanceGeometry* _pGeometry{ nullptr };
        const FitPartData*        _pFitData{ nullptr };
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 입력 — 부품들, 바인드 본(고리 · 영역 · 감쇠 축), 체형 모프 가중치(체형별 보정 조각)입니다. */
    struct FitInput
    {
        vector<FitPartInput>      _listPart{};
        vector<BodyMorphWeight>   _listMorphWeight{};
        const CharacterBoneArray* _pBindBones{ nullptr };
    };
} // namespace sw

namespace sw
{
    /** @brief 부품 하나의 피팅 결과입니다. */
    struct SW_API FitPartResult
    {
        hashed_string  _name{};
        vector<uint32> _listVisibleBit{};  ///< 삼각형마다 한 비트(1 = 보임), 32 개씩 한 칸
        vector<float3> _listVertexDelta{}; ///< 바인드 공간 정점 델타(입력 형상의 정점 수)
        uint32         _triangleCount{ 0 };
        uint32         _cutTriangleCount{ 0 };

        /** @brief 모든 삼각형이 보이고 델타가 0 인 결과로 둡니다(피팅 없이 절단 · 찢김만 쓸 때). */
        void initialize( uint32 triangleCount, uint32 vertexCount );
        /** @brief 삼각형이 보이면 true 입니다. */
        bool isTriangleVisible( uint32 triangle ) const;
        /** @brief 삼각형의 보임 비트를 바꿉니다(절단 · 찢김이 같은 마스크에 씁니다). */
        void setTriangleVisible( uint32 triangle, bool bVisible );
        /** @brief 마스크가 0 이 아닌 삼각형을 숨깁니다(절단 · 다 찢긴 삼각형 — 잘라 내기와 같은 길). 숨긴 수를 돌려줍니다. */
        uint32 hideTriangles( vector_reference<const uint8> listTriangleHidden );
    };
} // namespace sw

namespace sw
{
    /** @brief 단단함끼리의 관통 하나입니다(부품 번호는 입력 순서). */
    struct FitPenetration
    {
        uint32  _partA{ 0 };
        uint32  _partB{ 0 };
        uint32  _vertexCount{ 0 };
        float32 _maxDepth{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 보고 — 관통 목록과 설명 글입니다(편집 창이 보임). */
    struct FitReport
    {
        vector<FitPenetration> _listPenetration{};
        vector<string>         _listMessage{};
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 결과 전부입니다. 부품 순서는 입력 순서입니다. */
    struct FitResult
    {
        vector<FitPartResult> _listPart{};
        FitReport             _report{};
    };
} // namespace sw

namespace sw
{
    /** @brief 풀이 중인 부품 하나의 상태입니다(연산이 읽고 씁니다). */
    struct FitPartState
    {
        vector<float3>       _listBindPosition{}; ///< 입력 위치(변하지 않음)
        vector<float3>       _listPosition{};     ///< 지금 위치
        vector<float3>       _listNormal{};       ///< 입력 법선(변위 방향)
        vector<float3>       _listDisplacement{}; ///< 이번 반복에 쌓인 변위
        vector<uint32>       _listDisplacementCount{};
        vector<uint16>       _listVertexRegion{}; ///< 정점마다 몸 영역 번호(`FitTables` 순서)
        vector<uint8>        _listCovered{};      ///< 삼각형마다 덮임 표시(덮임 단계)
        vector<uint8>        _listHidden{};       ///< 삼각형마다 숨김 영역 표시
        vector<float3>       _listFalloffAxis{};  ///< 정점마다 감쇠 축(주 본 → 첫 자식), 없으면 0
        const FitPartInput*  _pInput{ nullptr };
        const FitProfileDef* _pProfile{ nullptr };
        int32                _layerOrder{ 0 };
        uint32               _layerIndex{ 0 };
        uint32               _surfaceIndex{ 0 }; ///< 겹 표면 안의 표면 번호
        uint8                _bMoved{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 풀이 상태 — 연산(`IFitOperator`)이 받는 것입니다. 사용자 연산도 이것만 봅니다. */
    struct SW_API FitSolveState
    {
        vector<FitPartState>      _listPart{};
        vector<SurfaceBvh>        _listLayerSurface{}; ///< 겹마다 표면 BVH(삼각형의 `_part` 는 부품 번호)
        FitReport*                _pReport{ nullptr };
        const FitTables*          _pTables{ nullptr };
        const CharacterBoneArray* _pBindBones{ nullptr };

        /** @brief 정점 변위를 쌓습니다(반복 끝에 평균). */
        void addDisplacement( uint32 part, uint32 vertex, const float3& displacement );
        /** @brief 부품의 겹 표면입니다. */
        const SurfaceBvh& getLayerSurface( uint32 part ) const { return _listLayerSurface[_listPart[part]._layerIndex]; }
    };
} // namespace sw

namespace sw
{
    /** @brief 피팅 관리자입니다. 연산 등록부를 갖고(기본 넷이 올라 있음), 표 · 입력을 받아 결과를 냅니다. */
    class SW_API FitSolver
    {
    public:
        FitSolver();

        /** @brief 연산 등록부입니다. 표를 읽기 전에 사용자 연산을 올립니다. */
        FitOperatorRegistry& getOperatorRegistry() { return _operatorRegistry; }
        /** @brief 연산 등록부입니다(읽기). */
        const FitOperatorRegistry& getOperatorRegistry() const { return _operatorRegistry; }
        /** @brief 변형 단계의 야코비 반복 수입니다(기본 3). */
        void setIterationCount( uint32 iterationCount ) { _iterationCount = iterationCount == 0 ? 1 : iterationCount; }

        /**
         * @brief 피팅을 풉니다.
         * @return 입력이 틀리면(형상 없음 · 모르는 겹 · 프로필 · 표에 없는 연산) 보고에 글을 남기고 false 입니다.
         */
        bool solve( const FitTables& tables, const FitInput& input, FitResult& outResult ) const;

    private:
        bool prepareState( const FitTables& tables, const FitInput& input, FitSolveState& outState, FitReport& outReport ) const;
        void runPhase( FitPhase phase, const FitTables& tables, FitSolveState& state ) const;

    private:
        FitOperatorRegistry _operatorRegistry;
        uint32              _iterationCount;
    };
} // namespace sw

/**
 * @file RHIPipelineStateDesc.h
 * @brief 그래픽스 · 컴퓨트 파이프라인 상태 서술체입니다.
 * @details PSO 를 짓는 곳(백엔드 팩토리 · PSO 캐시)만 읽는다 — 서술체 묶음(`RHIDescriptors.h`)에서 떼어 렌더 상태 · 메시 정점 헤더를 그 몇 곳만 끈다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/Mesh/MeshVertex.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RenderState/RenderState.h"

namespace sw
{
    /**
     * @struct RHIPipelineStateDesc
     * @brief 그래픽스 파이프라인과 컴퓨트 파이프라인의 상태를 만들 때 쓰는 서술체입니다.
     */
    struct SW_API RHIPipelineStateDesc
    {
        string _vertexShaderPath;  ///< 버텍스 셰이더 소스 경로
        string _vertexEntryPoint;  ///< 버텍스 셰이더 진입점
        string _pixelShaderPath;   ///< 픽셀 셰이더 소스 경로
        string _pixelEntryPoint;   ///< 픽셀 셰이더 진입점
        string _computeShaderPath; ///< 컴퓨트 셰이더 소스 경로
        string _computeEntryPoint; ///< 컴퓨트 셰이더 진입점
        /// 선택 스테이지의 진입점입니다. 비면 그 스테이지가 없습니다. 소스는 `_vertexShaderPath` 와 같은 파일입니다. 헐과 도메인은 함께 있고, 그때 위상은 `PatchList3` 입니다.
        string _geometryEntryPoint;
        string _hullEntryPoint;
        string _domainEntryPoint;

        vector<string> _listShaderDefine; ///< 컴파일 매크로("NAME" 또는 "NAME=VALUE"). 셰이더 퍼뮤테이션을 만든다.

        RHIPrimitiveTopology _topology;                           ///< 프리미티브 위상
        uint32               _numRenderTargets;                   ///< 컬러 RT 개수 (MRT)
        RHIFormat            _arrRTVFormat[kMaxColorAttachments]; ///< RT별 포맷
        RHIFormat            _depthStencilFormat;
        RenderState          _renderState; ///< 블렌드 · 깊이 · 래스터 · 스텐실. 칸은 `RenderState` 한 곳에서 더한다
        /**
         * @brief 정점 셰이더가 메시 속성을 읽을 때 쓸 속성 스트림 형식입니다(입력 레이아웃 표를 고른다). 정점 입력이 없는 셰이더는 쓰지 않는다.
         * @details 그리는 메시의 형식과 같아야 한다 — 머티리얼 PSO 는 배치의 형식으로 짓는다(`MaterialPSOKey::_vertexFormat`). 다르면 속성 스트림 원소 크기가 달라
         *          드로우가 버려진다(`RHIDrawDiagnostics::validateVertexSlotsBound`). 언리얼에서 정점 선언이 vertex factory 로 PSO 에 드는 자리다.
         */
        RHIMeshVertexFormat _meshVertexFormat;

        /** @brief 기본 토폴로지로, 컬링 · 깊이 테스트 없이 만듭니다. */
        RHIPipelineStateDesc() noexcept;

        /**
         * @brief 서술체 전체(셰이더 · 진입점 · define · 위상 · 타깃 포맷 · 렌더 상태 · 정점 형식)의 지문입니다. 코드가 짓는 PSO 저장소의 키입니다(`FramePSOManager::getOrCreateGlobalPSO`).
         * @details 칸을 골라 키를 따로 만들지 않는다 — 포맷 하나를 키에서 빠뜨려 다른 포맷의 PSO 를 돌려주던 일(쿠키 아틀라스)이 형식으로 막힌다(언리얼 global shader PSO 캐시가 서술 전체로 찾는 자리).
         *          칸을 더하면 이 함수(RHI.cpp)에도 더한다. 쓰는 타깃 수(`_numRenderTargets`)를 넘는 포맷 칸은 섞지 않는다.
         */
        [[nodiscard]] uint64 computeHash() const noexcept;
    };
} // namespace sw

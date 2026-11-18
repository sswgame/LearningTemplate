/**
 * @file PathFollowModifier.h
 * @brief 스플라인 경로를 따라 정점을 놓는 모디파이어(Path Deform · World Path Deform · Rope Deform)의 공통 바탕입니다.
 * @details 셋이 저마다 적던 것 — 경로 참조 칸, `applyToMesh` 동안의 회전 최소화 프레임 표, GPU 경로 표 싣기 — 을 한 곳에 둔다. 파생은 자기 칸과
 *          `deformPoint`(주축 좌표 → 호 길이 · 단면 좌표)만 정한다. 경로 칸은 늘 첫 매개변수다(바탕 생성자가 먼저 돈다 — 저장된 스택 · GPU 매개변수 순서가 그대로다).
 *          언리얼 Spline Mesh · 3ds Max Path Deform 처럼 경로 공간(자기 공간 · 월드)만 다른 같은 사상이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/Plugin/Editor/MeshModifiers/Modifiers/PathModifierUtil.h"
#include "GameFramework/Plugin/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    /**
     * @class PathFollowModifier
     * @brief 경로 참조 하나를 따라 정점을 놓는 모디파이어의 바탕입니다. 경로 참조가 비었거나 표본에 실패하면 파생은 항등으로 둡니다.
     */
    class PathFollowModifier : public MeshModifier
    {
    public:
        bool supportsGPU() const final { return true; }

        /** @brief 경로 표본(위치 · 접선 · 단면 축)을 싣는다 - 경로 참조는 CPU 만 풀 수 있어서 GPU 는 표만 읽는다. 못 얻으면 셰이더가 항등으로 둔다(CPU 와 같다). */
        bool packGPUInputs( const MeshModifierContext& context, vector<float4>& outListValue ) const final
        {
            (void)PathModifierUtil::packGPUPathTable( context, getText( _pathParam ), _space, outListValue );
            return true;
        }

        /** @brief 경로의 회전 최소화 프레임 표를 한 번 만들고(단일 스레드) 기본 정점 루프를 돈다 — 정점 루프는 표를 읽기만 한다. */
        void applyToMesh( MeshModifierMesh& inoutMesh, const MeshModifierContext& context ) const final
        {
            _frameCache.populate( context, getText( _pathParam ), _space );
            MeshModifier::applyToMesh( inoutMesh, context );
            _frameCache.clear();
        }

    protected:
        /**
         * @param space 경로를 읽는 공간(자기 공간 · 월드)
         * @param pPathTooltip 경로 칸의 도움말(수명이 프로그램과 같은 글)
         */
        PathFollowModifier( PathModifierUtil::PathSpace space, const utf8* pPathTooltip )
            : MeshModifier()
            , _pathParam{
                  addParam( MeshModifierParamDef{ "Path", MeshModifierParamKind::Reference, 0.0f, 0.0f, float3{ 0.0f, 0.0f, 0.0f }, 1, "Spline object" }
                                .withLayout( "Path", pPathTooltip )
                                .withRequiredComponent( "SplineComponent" ) )
        },
            _space{ space }
        {
        }

        /** @brief 경로 길이입니다. 경로를 못 얻으면 false 입니다(파생은 원래 점을 둔다). */
        [[nodiscard]] bool tryGetPathLength( const MeshModifierContext& context, float32& outLength ) const
        {
            return PathModifierUtil::tryGetPathLength( context, getText( _pathParam ), outLength );
        }

        /** @brief 경로 위 호 길이 @p distance 자리에 단면 좌표 (u, v) 를 놓은 점입니다. 경로를 못 얻으면 변형 없이 @p point 의 자리입니다. */
        float3 placeOnPath( const MeshModifierPoint& point, const MeshModifierContext& context, float32 length, float32 distance, bool bLoop, float32 coordU, float32 coordV ) const
        {
            float3 result{ 0.0f, 0.0f, 0.0f };
            if ( PathModifierUtil::tryPlaceOnPath( context, getText( _pathParam ), _space, length, distance, bLoop, coordU, coordV, result, &_frameCache ) == false )
                return point._position;
            return result;
        }

    private:
        mutable PathModifierUtil::PathFrameCache _frameCache; ///< `applyToMesh` 동안만 채워지는 경로 프레임 표
        uint32                                   _pathParam;
        PathModifierUtil::PathSpace              _space;
    };
} // namespace sw::editor

/**
 * @file FrameMeshDeformState.h
 * @brief `FrameRenderer` 가 값 멤버로 가지는 정점 변형 상태입니다. 인스턴스 애니메이션, 모프, 스킨, 정점 애니메이션(VAT) 컴퓨트가 함께 쓰는 상수 버퍼, 풀, 스크래치, 시계를 묶습니다.
 * @details 데이터만 담는 비가상 부품입니다. 디스패치 순서(컬링보다 앞)와 기록은 `FrameRenderer` 가 정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Time/GameTimer.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

namespace sw
{
    class Mesh;

    /** @brief 한 프레임의 정점 변형 컴퓨트가 쓰는 상태입니다. 렌더 스레드만 만집니다. */
    struct FrameMeshDeformState
    {
        RHIConstantBufferSlot _instanceAnimationCb{}; ///< 인스턴스 애니메이션(instanceanimation.hlsl) 상수 버퍼다.
        RHIConstantBufferSlot _meshMorphCb{};         ///< 모프 디스패치 상수 버퍼다.
        RHIConstantBufferSlot _meshSkinCb{};          ///< 스킨 디스패치 상수 버퍼다.
        /// @brief GPU 가 변형한 정점 풀입니다. RT 가 소유합니다(GPUMeshMorphPool 참고).
        GPUMeshMorphPool _meshMorphPool{};
        /// @brief 정점 애니메이션(VAT) 테이블 풀입니다. RT 가 소유합니다(GPUVertexAnimationPool 참고).
        GPUVertexAnimationPool _vertexAnimationPool{};
        /// @brief 진단(`-gv_morphDiag=3`)이 업로드하는 태그 정점입니다. 스크래치이므로 프레임 밖에서는 의미가 없습니다.
        vector<GPUMorphVertex> _listScratchMorphTag{};
        /// @brief 이번 프레임 모프 대상 메시입니다. 프레임마다 할당하지 않으려고 보관합니다.
        vector<Mesh*> _listScratchMorphMesh{};
        /// @brief 이번 프레임 스킨드 메시 목록입니다. 모프 풀의 스킨 구간 순서이며 프레임마다 재사용합니다.
        vector<Mesh*> _listScratchSkinMesh{};
        /// @brief 이번 프레임 VAT 메시 목록입니다. 프레임마다 재사용합니다.
        vector<Mesh*> _listScratchVertexAnimationMesh{};
        /// @brief 이번 프레임에 이미 분류한 메시입니다. 배치마다 세 목록을 선형 탐색하지 않으려고 둡니다. clear 는 버킷을 남기므로 프레임마다 할당하지 않습니다.
        unordered_set<const Mesh*> _setScratchSeenMesh{};
        /**
         * @brief 시간으로 구동하는 컴퓨트에 넘기는 절대 시간(초)의 시계입니다.
         * @details 각도를 프레임마다 누적하지 않고 **이 절대 시간에서 매번 새로 계산한다**. 누적하면 프레임
         *          간격의 흔들림이 그대로 쌓여 백엔드와 실행마다 다른 각도가 나오고, 스크린샷을 비교할 수 없게 된다.
         *          렌더러가 자기 시계를 가지므로 델타를 여기까지 실어 나르지 않아도 되고, 렌더 스레드에서
         *          게임 시간을 건드리지 않는다.
         */
        GameTimer _animationTimer{};
        /// @brief 진단(`-gv_morphDiag=2|3`)에서 정점 셰이더에 결과 대신 **레스트** 버퍼를 바인딩했는지 여부입니다.
        uint8 _bMorphBindsRest{ SW_FALSE };
    };
} // namespace sw

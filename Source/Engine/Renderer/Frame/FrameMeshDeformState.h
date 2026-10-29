/**
 * @file FrameMeshDeformState.h
 * @brief `FrameRenderer` 가 값 멤버로 가지는 정점 변형 상태입니다. 인스턴스 애니메이션, 모프, 스킨, 정점 애니메이션(VAT) 컴퓨트가 함께 쓰는 상수 버퍼, 풀, 스크래치, 시계를 묶습니다.
 * @details 데이터만 담는 비가상 부품입니다. 디스패치 순서(컬링보다 앞)와 기록은 `FrameRenderer` 가 정합니다.
 */
#pragma once
#include "Core/Container/unordered_set.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/GameTimer.h"

#include "Engine/Graphics/RHI/RHIConstantBufferSlot.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Scene/GPUMeshMorphPool.h"
#include "Engine/Renderer/Scene/GPUVertexAnimationPool.h"

namespace sw
{
    class Mesh;

    /**
     * @enum MeshMorphDiagMode
     * @brief GPU 메시 모프 진단 모드입니다(`-gv_meshMorphDebugMode=IndexTag`). 백엔드 능력 표와 상관없이 모프 경로를 돌려 보는 스위치입니다.
     * @details 리플렉션 enum 이라 명령줄이 이름으로 고르고 숫자는 받지 않는다(`GlobalVariableInfo::setValueFromCommandLine`).
     */
    ENUM()
    enum class MeshMorphDiagMode : uint8
    {
        Normal,       ///< 평소대로 — `RHICapabilities::_bGPUMeshMorph` 를 따른다
        ForceOn,      ///< 능력 표를 무시하고 모프를 켠다
        SkipDispatch, ///< 켜되 컴퓨트 디스패치를 건너뛰고 레스트 버퍼를 정점 셰이더에 그대로 바인딩한다(정답은 레스트 포즈와 같은 이미지)
        IndexTag,     ///< SkipDispatch 에 더해 풀 원소의 노멀 위치에 원소 번호를 기록한다(인덱싱이 틀린 것인지 내용이 틀린 것인지 가른다)
    };

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
        /// @brief 진단(`-gv_meshMorphDebugMode=IndexTag`)이 업로드하는 태그 정점입니다. 스크래치이므로 프레임 밖에서는 의미가 없습니다.
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
        /// @brief 진단(`-gv_meshMorphDebugMode=SkipDispatch|IndexTag`)에서 정점 셰이더에 결과 대신 **레스트** 버퍼를 바인딩했는지 여부입니다.
        uint8 _bMorphBindsRest{ SW_FALSE };
    };
} // namespace sw

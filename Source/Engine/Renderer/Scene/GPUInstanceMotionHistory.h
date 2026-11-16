/**
 * @file GPUInstanceMotionHistory.h
 * @brief 렌더 쪽이 소유하는 인스턴스 움직임 기록입니다. 지난번 그린 월드 행렬을 열쇠(`GPUInstance::_motionKey`)로 찾아 화면 속도의 지난 위치를 만듭니다.
 * @details 게임 스레드는 정체성(열쇠)만 싣고 기록은 렌더 쪽이 가진다 — 패킷은 그리지 않고 버려질 수 있어(docs/agents/CppStyle.md "Thread and GPU resource ownership" 의 패킷 차분 · 알림)
 *          게임 스레드가 "지난 프레임 행렬" 을 싣으면 버려진 프레임 뒤에는 그 값이 실제로 그린 프레임의 것이 아니다. 렌더 쪽은 실제로 그린 프레임마다 한 번
 *          바꾸므로 지난 뷰-투영(`FrameViewTarget::_prevViewProj` — "지난번 이 뷰를 그린")과 같은 뜻이다.
 *          언리얼은 프리미티브 프록시가 PreviousLocalToWorld 를 들고 GPUScene 이 그것을 올리고, 유니티는 렌더러마다 지난 행렬을 엔진이 들고 있다 — 둘 다 렌더 쪽 기록이다.
 *          인스턴스 번호는 배치를 다시 나누면 바뀌므로 번호가 아니라 열쇠로 찾는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /**
     * @class GPUInstanceMotionHistory
     * @brief 열쇠 → 지난번 그린 월드 행렬의 기록입니다. 프레임마다 `update` 한 번으로 이번 인스턴스들의 지난 행렬을 내고 기록을 이번 것으로 바꿉니다.
     * @details 기록이 없는 인스턴스(열쇠 0 · 처음 본 열쇠 · 순간이동으로 바뀐 열쇠 · GPU 가 행렬을 고쳐 쓰는 회전 인스턴스)는 "기록 없음" 행렬(모두 0 — `_44` 가 0)을 낸다.
     *          셰이더는 그 칸을 이번 월드 행렬로 읽는다(binding.hlsli `InstanceData::loadPreviousWorld`) — 화면 속도가 카메라 움직임뿐이다.
     *          GPU 가 고쳐 쓰는 행렬(instance_animation · VFX 메시 입자)은 CPU 가 지난 값을 모르므로 같은 처리다.
     */
    class SW_API GPUInstanceMotionHistory
    {
    public:
        /** @brief "기록 없음" 행렬인지 반환합니다(아핀 행렬의 `_44` 는 늘 1 이라 0 이 표시가 된다). */
        static bool isNoHistory( const float4x4& previousWorld ) { return previousWorld._44 == 0.0f; }

        /**
         * @brief 이번 인스턴스들의 지난 월드 행렬을 @p outListPrevWorld 에 인스턴스 순서대로 채우고, 기록을 이번 행렬로 바꿉니다.
         * @return 기록이 있는 인스턴스 가운데 지난 행렬이 이번 것과 비트로 다른 것이 하나라도 있으면 true(물체가 움직였다)입니다.
         */
        [[nodiscard]] bool update( const vector<GPUInstance>& listInstance, vector<float4x4>& outListPrevWorld );

        /** @brief 기록을 모두 버립니다. 다음 `update` 의 모든 인스턴스가 기록 없음입니다(씬을 통째로 바꿀 때). */
        void reset();

        /** @brief 기록에 든 열쇠 수입니다(테스트 · 진단). */
        size_t getKeyCount() const { return _mapPreviousWorld.size(); }

    private:
        /// @brief 지난번 `update` 의 열쇠 → 월드 행렬입니다.
        unordered_map<uint64, float4x4> _mapPreviousWorld;
        /// @brief 이번 `update` 가 채우는 기록입니다. 끝에서 `_mapPreviousWorld` 와 맞바꿔 할당을 되풀이하지 않습니다.
        unordered_map<uint64, float4x4> _mapScratch;
    };
} // namespace sw

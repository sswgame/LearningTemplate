/**
 * @file GPUInstanceMotionHistory.h
 * @brief 렌더 쪽이 소유하는 인스턴스 움직임 기록입니다. 지난번 그린 월드 행렬을 열쇠(`GPUInstance::_motionKey`)로 찾아 화면 속도의 지난 위치를 만듭니다.
 * @details 게임 스레드는 정체성(열쇠)만 싣고 기록은 렌더 쪽이 가진다 — 패킷은 그리지 않고 버려질 수 있어(docs/agents/CppStyle.md "Thread and GPU resource ownership" 의 패킷 차분 · 알림)
 *          게임 스레드가 "지난 프레임 행렬" 을 싣으면 버려진 프레임 뒤에는 그 값이 실제로 그린 프레임의 것이 아니다. 렌더 쪽은 실제로 그린 프레임마다 한 번
 *          바꾸므로 지난 뷰-투영(`FrameViewTarget::_prevViewProj` — "지난번 이 뷰를 그린")과 같은 뜻이다.
 *          언리얼은 프리미티브 프록시가 PreviousLocalToWorld 를 들고 GPUScene 이 그것을 올리고, 유니티는 렌더러마다 지난 행렬을 엔진이 들고 있다 — 둘 다 렌더 쪽 기록이다.
 *          물체가 다른 칸으로 옮길 수 있으므로 칸이 바뀐 물체는 열쇠로 찾는다.
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Renderer/Scene/GPUSceneDirtySlots.h"
#include "Engine/Renderer/Scene/GPUSceneSnapshot.h"

namespace sw
{
    /**
     * @class GPUInstanceMotionHistory
     * @brief 칸마다의 지난번 그린 월드 행렬 기록입니다. 프레임마다 `update` 한 번으로 바뀐 칸의 지난 행렬을 내고 기록을 이번 것으로 바꿉니다.
     * @details **일은 바뀐 칸에 비례한다** — 이번에 바뀐 칸의 지난 행렬을 갱신하고, 지난번에 바뀌었다가 멈춘 칸은 지난 행렬을 이번 것으로 맞춘다(그 밖의 칸은 이미 맞다). 인스턴스마다 열쇠를 찾고
     *          버퍼 전체를 다시 올리면 하나만 움직여도 인스턴스 수만큼 든다(언리얼 GPUScene 도 바뀐 프리미티브만 지난 행렬과 함께 올린다).
     *          기록이 없는 인스턴스(열쇠 0 · 처음 본 열쇠 · 순간이동으로 바뀐 열쇠 · GPU 가 행렬을 고쳐 쓰는 회전 인스턴스)는 "기록 없음" 행렬(모두 0 — `_44` 가 0)을 낸다.
     *          셰이더는 그 칸을 이번 월드 행렬로 읽는다(binding.hlsli `InstanceData::loadPreviousWorld`) — 화면 속도가 카메라 움직임뿐이다.
     *          GPU 가 고쳐 쓰는 행렬(instance_animation · VFX 메시 입자)은 CPU 가 지난 값을 모르므로 같은 처리다.
     */
    class SW_API GPUInstanceMotionHistory
    {
    public:
        /** @brief "기록 없음" 행렬인지 반환합니다(아핀 행렬의 `_44` 는 늘 1 이라 0 이 표시가 된다). */
        static bool isNoHistory( const float4x4& previousWorld ) { return previousWorld._44 == 0.0f; }

        /**
         * @brief 칸 순 인스턴스 @p listInstance 의 지난 월드 행렬을 맞추고, 기록을 이번 행렬로 바꿉니다. 실제로 그리는 프레임마다 한 번 부른다(스냅샷이 그대로면 @p listChangedSlot 이 비었다).
         * @param listChangedSlot 지난 `update` 뒤 내용이 바뀐 칸입니다(`GPUSceneSlotBuffer::takeChangedSlots` — 되풀이돼도 된다). 늘어난 칸은 여기 없어도 바뀐 칸으로 본다.
         * @return 기록이 있는 인스턴스 가운데 지난 행렬이 이번 것과 비트로 다른 것이 하나라도 있으면 true(물체가 움직였다)입니다.
         */
        [[nodiscard]] bool update( const vector<GPUInstance>& listInstance, const vector<uint32>& listChangedSlot );
        /** @brief 칸 순 지난 월드 행렬입니다(기록 없음은 `isNoHistory`). */
        const vector<float4x4>& getPreviousWorlds() const { return _listPreviousWorld; }
        /** @brief 지난 월드 행렬의 올리지 않은 바뀐 칸입니다(올린 쪽이 `clear` 한다). */
        GPUSceneDirtySlots& getDirtySlots() { return _dirty; }

        /** @brief 기록을 모두 버립니다. 다음 `update` 의 모든 인스턴스가 기록 없음입니다(씬을 통째로 바꿀 때). */
        void reset();

    private:
        /** @brief 칸이 들고 있던 열쇠 · 행렬을 이번에 떠나는 것으로 적습니다(다른 칸이 같은 열쇠를 받으면 이어받는다). */
        void recordReleasedKey( uint32 slot );
        /** @brief 칸의 지난 행렬을 적습니다. 다르면 올릴 칸으로 적는다. */
        void writePrevious( uint32 slot, const float4x4& previousWorld );

    private:
        vector<uint64>                  _listKey;           ///< 칸의 열쇠(0 = 기록하지 않는 인스턴스)
        vector<float4x4>                _listWorld;         ///< 칸의 마지막으로 그린 월드 행렬
        vector<float4x4>                _listPreviousWorld; ///< 셰이더가 읽는 지난 월드 행렬
        GPUSceneDirtySlots              _dirty;
        vector<uint32>                  _listLastChangedSlot; ///< 지난 `update` 에서 바뀐 칸 — 이번에 그대로면 지난 행렬을 이번 것으로 맞춘다
        vector<uint32>                  _listChangedScratch;  ///< 이번에 바뀐 칸(오름차순 · 중복 없음)
        unordered_map<uint64, float4x4> _mapReleasedWorld;    ///< 이번에 칸을 떠난 열쇠 → 마지막 행렬
    };
} // namespace sw

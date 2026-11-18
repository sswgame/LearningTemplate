/**
 * @file VFXGPUMeshReadback.h
 * @brief GPU 메시 입자의 살아 있는 수와 주 시점이 그린 인스턴스 수를 몇 프레임 늦게 되읽는 고리(`VFXGPUMeshReadback`)입니다. 자동화 감시 값(`VFX.GPUMesh*`)이 읽습니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/array.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
#include "Engine/Renderer/VFX/VFXGPUSimulation.h"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;
    struct GPUInstance;

    /**
     * @class VFXGPUMeshReadback
     * @brief 프레임 끝에 GPU 메시 방출기마다 살아 있는 수(인자 버퍼의 `SW_VFX_ARGS_WORD_ALIVE`)와 그 배치의 주 시점 간접 인자 인스턴스 수를 작은 버퍼로 모아 되읽습니다.
     * @details 둘 다 GPU 에만 있는 값이다 — 빈 칸 스택(dead list)이 맞으면 그린 수는 살아 있는(보이는) 수를 넘지 않고, 모두 죽으면 0 이다(vfx_mesh_instance.hlsl · gpu_cull.hlsl).
     *          GPU 타이밍 · 메시지 링처럼 렌더 스레드가 기록하고(`requestReadback`) 프레임이 끝난 뒤 도착한 사본을 원자값으로 낸다(`poll`) — 게임 스레드의 감시 값은 원자값만 읽는다.
     *          `FrameRenderState` 가 갖는다. 같은 배치를 여러 방출기가 나누면 배치의 수를 한 번만 센다. 배치에 방출기 밖의 인스턴스가 함께 들면(같은 메시 · 머티리얼) 그 수도 든다.
     */
    class SW_API VFXGPUMeshReadback
    {
    public:
        /** @brief 한 프레임에 되읽는 방출기의 상한입니다. 넘는 방출기는 세지 않는다(시나리오 무대는 몇 개뿐이다). */
        static constexpr uint32 kMaxEmitters = 16;

        VFXGPUMeshReadback();
        ~VFXGPUMeshReadback();
        VFXGPUMeshReadback( const VFXGPUMeshReadback& )            = delete;
        VFXGPUMeshReadback& operator=( const VFXGPUMeshReadback& ) = delete;

        /**
         * @brief 이번 프레임의 방출기 @p listSource 마다 살아 있는 수와 배치의 인스턴스 수를 모으는 복사를 넣고 되읽기를 요청합니다. 렌더 패스 밖, 컬링 · 그리기가 끝난 뒤 부릅니다.
         * @param listInstance        칸 순 인스턴스(`GPUScene::getInstances`) — 방출기 칸의 배치 번호를 읽는다
         * @param mainIndirectArgs    주 시점의 간접 인자 버퍼(배치마다 `RHIDrawIndexedIndirectCommand`)
         * @param bGPUCullingActive   이번 프레임에 컬링 컴퓨트가 돌았다 — 아니면 인자는 CPU 가 채운 배치 전체 수라 되읽지 않는다
         * @details 방출기가 없으면 복사 없이 0 을 바로 낸다(재생이 끝난 방출기는 배치를 숨긴다 — `VFXComponent`).
         */
        void requestReadback( IRHIDevice& device, IRHICommandList& cmd, const vector<VFXGPUSimulation::MeshProbeSource>& listSource, const vector<GPUInstance>& listInstance,
                              RHIBufferHandle mainIndirectArgs, bool bGPUCullingActive );
        /** @brief 도착한 가장 최근 사본을 원자값으로 냅니다. 프레임 앞에서 부릅니다. */
        void poll();
        /** @brief 버퍼와 고리를 놓습니다. 여러 번 불러도 됩니다. */
        void release( IRHIDevice* pDevice );

        /** @brief 값이 한 번이라도 나왔으면 true 입니다(되읽기는 몇 프레임 늦다). 어느 스레드에서나 읽는다. */
        bool hasValue() const { return _bHasValue.load() != 0; }
        /** @brief 마지막 사본의 방출기 수입니다. */
        uint32 getEmitterCount() const { return _emitterCount.load(); }
        /**
         * @brief 마지막 사본의 (살아 있는 입자 수의 합, 배치마다 주 시점이 그린 인스턴스 수의 합 — 같은 배치는 한 번)입니다.
         * @details 한 원자값으로 함께 읽는다 — 둘을 견주는 감시 값(`VFX.GPUMeshDrawnOverAlive`)이 서로 다른 사본의 값을 섞지 않는다.
         */
        void readCounts( uint32& outAlive, uint32& outDrawn ) const
        {
            const uint64 packed = _packedCounts.load();
            outAlive            = static_cast<uint32>( packed );
            outDrawn            = static_cast<uint32>( packed >> 32 );
        }

    private:
        /** @brief 요청 하나의 방출기 수와 방출기마다의 배치 번호입니다(사본이 도착하면 이 표로 단어를 읽는다). */
        struct ReadbackMeta
        {
            uint32                      _emitterCount{ 0 };
            array<uint32, kMaxEmitters> _arrBatch{};
        };
        static constexpr uint32 kReadbackMetaCount = 8;

        /** @brief 값을 한꺼번에 냅니다. */
        void publish( uint32 emitterCount, uint32 aliveCount, uint32 drawnCount );

    private:
        RHIStructuredBuffer                     _probeBuffer; ///< 방출기마다 (살아 있는 수, 그린 수) 두 단어
        AsyncBufferReadback                     _readback;
        array<ReadbackMeta, kReadbackMetaCount> _arrMeta;
        uint64                                  _readbackSerial;
        uint64                                  _minAcceptedSerial; ///< 방출기가 없어 0 을 바로 낸 뒤로는 그 앞 요청의 사본을 받지 않는다
        atomic<uint32>                          _emitterCount;
        atomic<uint64>                          _packedCounts; ///< 아래 32 비트 살아 있는 수, 위 32 비트 그린 수(`readCounts`)
        atomic<uint8>                           _bHasValue;
    };
} // namespace sw

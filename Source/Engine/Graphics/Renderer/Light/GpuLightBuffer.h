/**
 * @file GpuLightBuffer.h
 * @brief 씬의 라이트를 GPU 구조버퍼 하나로 모읍니다. 언리얼의 라이트 데이터 버퍼가 있는 자리입니다.
 *
 * [왜 상수버퍼가 아닌가]
 * 라이트 수는 씬마다 다릅니다. 상수버퍼에 고정 배열로 넣으면 상한이 곧 비용이 되고(안 쓰는 자리도
 * 매 프레임 올라갑니다), 상한을 넘기는 순간 조용히 잘립니다. 이 엔진은 인스턴스 · 머티리얼 · 가시 목록이
 * 이미 모두 구조버퍼이고 셰이더는 **인덱스로 읽습니다**. 라이트도 같은 결로 둡니다.
 *
 * [왜 드로우마다 걸지 않는가]
 * 규약이 "드로우 사이에 바인딩이 바뀌지 않는다" 이기 때문입니다. 라이트 버퍼는 **패스당 한 번**
 * 걸리고 포워드 · 디퍼드가 같은 버퍼를 읽습니다.
 *
 * [그림자]
 * 그림자 맵이 하나뿐이라 그림자를 드리우는 빛도 하나입니다. 어느 빛인지는 원소의 `_params.x` 가
 * 말합니다. 점광 그림자는 큐브맵이 필요한데 이 엔진에는 큐브맵 자원이 없습니다. 없는 것을 있는 척하지 않습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class IRHIDevice;
    class Scene;

    /**
     * @struct GpuLight
     * @brief 라이트 하나입니다. 셰이더의 `SwLightData`(lighting.hlsli)와 레이아웃이 같아야 합니다.
     * @details **`float4` 넷입니다.** 원소를 `float3` 과 섞으면 std430 은 vec4 를 16 바이트 경계에
     *          맞추는 반면 DX · Vulkan 은 DXC 가 명시 오프셋을 적어 넘어가, **OpenGL 에서만** 값이
     *          어긋납니다(모프 정점 풀에서 실제로 그랬습니다). 증상이 "빛 하나가 조용히 엉뚱한 자리에
     *          있다" 라 찾기 어렵습니다. `float4` 는 그 자체가 16 바이트 정렬 단위라 이 함정이 없습니다.
     */
    struct GpuLight
    {
        float4 _positionRadius{}; ///< xyz 월드 위치, w 반경 (점광 · 스폿광. 방향광은 0)
        float4 _colorIntensity{}; ///< rgb 빛 색, a 세기
        float4 _directionType{};  ///< xyz 빛이 나아가는 방향(방향광 · 스폿광), w 타입 (shaderslot::kLightType*)
        float4 _params{};         ///< x 그림자 플래그(그림자 맵의 빛이면 1), y 스폿 바깥 원뿔 cos, z 스폿 안쪽 원뿔 cos, w 예약
    };

    static_assert( sizeof( GpuLight ) == 64, "GpuLight 는 float4 넷(64바이트)이어야 한다 — 셰이더 SwLightData 와 같은 크기" );

    /**
     * @brief 씬의 활성 라이트를 GPU 원소로 모읍니다(게임 스레드).
     * @details 등록부(`LightRegistry`)만 봅니다. 예전 `findActiveDirectionalLight` 처럼 모든
     *          GameObject 를 도는 경로를 다시 만들지 않습니다(큐브 20,000 개에서 그 한 줄이 2.9ms 였습니다).
     *          활성 판정은 여기서 합니다(등록부는 "무엇이 있나" 만 압니다).
     * @note 방향광 중 **그림자를 드리우는 첫 빛**만 그림자 플래그를 받습니다. 그림자 맵이 하나라서입니다.
     * @param outList 기존 내용을 지우고 채웁니다. 부르는 쪽이 프레임마다 재사용하는 버퍼여야 합니다(할당 회피).
     */
    SW_API void collectSceneLights( const Scene* pScene, vector<GpuLight>& outList );

    /**
     * @class GpuLightBuffer
     * @brief 프레임 라이트 목록을 구조버퍼에 올리고 SRV 를 들고 있습니다. 렌더 스레드가 소유합니다.
     */
    class SW_API GpuLightBuffer
    {
    public:
        GpuLightBuffer()                                   = default;
        ~GpuLightBuffer()                                  = default;
        GpuLightBuffer( const GpuLightBuffer& )            = delete;
        GpuLightBuffer& operator=( const GpuLightBuffer& ) = delete;

        /**
         * @brief 이번 프레임의 라이트를 올립니다.
         * @details 용량은 `ensureCapacity` 가 필요할 때만 다시 잡습니다. 라이트 수가 그대로면
         *          버퍼도 그대로이고 업로드만 합니다. 상한(`shaderslot::kMaxFrameLight`)을 넘으면 잘라
         *          보내고 **한 번만** 경고합니다(매 프레임 찍으면 로그가 그것으로 덮입니다).
         */
        void update( IRHIDevice* pDevice, const vector<GpuLight>& listLight );

        /** @brief 라이트 버퍼입니다. 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _buffer; }
        /** @brief GPU 에 올라간 라이트 수입니다. 셰이더 루프가 이 수만큼 돕니다. */
        uint32 getCount() const { return _count; }
        /** @brief 셰이더에 걸 수 있는 상태인지 확인합니다. */
        bool isBindable() const { return _count > 0 && _buffer._buffer != 0 && _buffer._srv != kInvalidDescriptorIndex; }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHIStructuredBufferSlot _buffer;
        uint32                  _count{ 0 };
        /// @brief 상한 초과 경고를 한 번만 찍기 위한 표시입니다.
        uint8 _bWarnedOverflow{ SW_FALSE };
    };
} // namespace sw

/**
 * @file GPULightBuffer.h
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
 * 그림자 맵은 아틀라스 하나입니다(`ShadowSetup.h`). 그림자를 받는 빛은 원소의 `_params.x` 가 말하고, 방향광은 PassCB 의
 * 캐스케이드 상수를, 점광 · 스폿은 **빛 뒤에 붙은 타일 원소**(`_params.w` 가 첫 타일의 상대 번호)를 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    class IRHIDevice;
    class Scene;

    /**
     * @brief 씬의 활성 라이트를 GPU 원소로 모읍니다(게임 스레드).
     * @details 레지스트리(`ComponentRegistry`)만 봅니다. 주의: 모든 GameObject 를 도는 경로를 만들지 않습니다
     *          (큐브 20,000 개에서 그 훑기 한 번이 2.9ms 입니다).
     *          활성 판정은 여기서 합니다(레지스트리는 "무엇이 있나" 만 압니다).
     * @note 방향광의 그림자 플래그는 `Scene::findShadowCastingDirectionalLight` 가 고른 빛 하나만 받습니다 — 그림자 행렬도 같은 함수에서
     *       나옵니다. 그림자를 켠 점광 · 스폿은 번호만 @p pOutListShadowLightIndex 에 적고, 아틀라스 칸은 `ShadowSetupUtil::populateFrame` 이 나눠 줍니다.
     * @param outList 기존 내용을 지우고 채웁니다. 부르는 쪽이 프레임마다 재사용하는 버퍼여야 합니다(할당 회피).
     * @param pOutListShadowLightIndex 비지 않으면 그림자를 켠 점광 · 스폿 원소의 번호를 지우고 채웁니다.
     */
    SW_API void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex = nullptr );

    /**
     * @class GPULightBuffer
     * @brief 프레임 라이트 목록을 구조버퍼에 올리고 SRV 를 들고 있습니다. 렌더 스레드가 소유합니다.
     */
    class SW_API GPULightBuffer
    {
    public:
        GPULightBuffer()                                   = default;
        ~GPULightBuffer()                                  = default;
        GPULightBuffer( const GPULightBuffer& )            = delete;
        GPULightBuffer& operator=( const GPULightBuffer& ) = delete;

        /**
         * @brief 이번 프레임의 라이트를 올립니다.
         * @details 용량은 `ensureCapacity` 가 필요할 때만 다시 잡습니다. 라이트 수가 그대로면
         *          버퍼도 그대로이고 업로드만 합니다. 상한(`shaderslot::kMaxFrameLight`)을 넘으면 잘라
         *          보내고 **한 번만** 경고합니다(매 프레임 찍으면 로그가 그것으로 덮입니다).
         *          @p listShadowTileElement(점광 · 스폿 그림자 타일)는 잘린 빛 바로 뒤에 붙고 `getCount` 에 들지 않습니다.
         */
        void update( IRHIDevice* pDevice, const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement );

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
        vector<GPULight>        _listUploadScratch; ///< 빛 + 그림자 타일을 이어 붙여 한 번에 올리는 자리(프레임마다 다시 쓴다)
        uint32                  _count{ 0 };
        /// @brief 상한 초과 경고를 한 번만 찍기 위한 표시입니다.
        uint8 _bWarnedOverflow{ SW_FALSE };
    };
} // namespace sw

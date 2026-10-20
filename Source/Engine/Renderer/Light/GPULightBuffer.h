/**
 * @file GPULightBuffer.h
 * @brief 씬의 라이트를 GPU 구조 버퍼 하나로 모읍니다. 언리얼의 라이트 데이터 버퍼에 해당합니다.
 *
 * [왜 상수 버퍼가 아닌가]
 * 라이트 수는 씬마다 다릅니다. 상수 버퍼에 고정 배열로 넣으면 상한이 곧 비용이 되고(안 쓰는 슬롯도
 * 매 프레임 업로드됩니다), 상한을 넘기는 순간 조용히 잘립니다. 이 엔진은 인스턴스, 머티리얼, 가시 목록이
 * 이미 모두 구조 버퍼이고 셰이더는 **인덱스로 읽습니다**. 라이트도 같은 방식으로 둡니다.
 *
 * [왜 드로우마다 바인딩하지 않는가]
 * 규약이 "드로우 사이에 바인딩이 바뀌지 않는다"이기 때문입니다. 라이트 버퍼는 **패스당 한 번**
 * 바인딩되고 포워드와 디퍼드가 같은 버퍼를 읽습니다.
 *
 * [그림자]
 * 그림자 맵은 아틀라스 하나입니다(`ShadowSetup.h`). 그림자를 받는 빛은 원소의 `_params.x` 가 알려 주고, 방향광은 PassCB 의
 * 캐스케이드 상수를, 점광과 스폿은 **빛 뒤에 붙은 타일 원소**(`_params.w` 가 첫 타일의 상대 번호)를 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Light/LightGrid.h"

namespace sw
{
    class IRHIDevice;
    class Scene;

    /**
     * @brief 씬의 활성 라이트를 GPU 원소로 모읍니다(게임 스레드).
     * @details 레지스트리(`ComponentRegistry`)만 봅니다. 주의: 모든 GameObject 를 도는 경로를 만들지 않습니다
     *          (큐브 20,000 개에서 그 훑기 한 번이 2.9ms 입니다).
     *          활성 판정은 여기서 합니다(레지스트리는 "무엇이 있나"만 압니다).
     * @note 방향광의 그림자 플래그는 `Scene::findShadowCastingDirectionalLight` 가 고른 빛 하나만 받습니다. 그림자 행렬도 같은 함수에서
     *       나옵니다. 그림자를 켠 점광과 스폿은 번호만 @p pOutListShadowLightIndex 에 적고, 아틀라스 슬롯은 `ShadowSetupUtil::populateFrame` 이 나눠 줍니다.
     * @param outList 기존 내용을 지우고 채웁니다. 부르는 쪽이 프레임마다 재사용하는 버퍼여야 합니다(할당 회피).
     * @param pOutListShadowLightIndex 비지 않으면 그림자를 켠 점광과 스폿 원소의 번호를 지우고 채웁니다.
     */
    SW_API void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex = nullptr );

    /**
     * @enum LightGridProducer
     * @brief 라이트 그리드를 누가 만드는지입니다. 개발 빌드는 `gv_lightCulling` 이 고르고 Shipping 은 GPU 로 고정합니다(결정 기록 5-12 D2-13).
     */
    enum class LightGridProducer : uint8
    {
        None, ///< 그리드 없이 목록 전체를 돈다(비교 기준)
        CPU,  ///< `LightGrid::populate` 가 만들어 동적 버퍼로 올린다
        GPU,  ///< 컴퓨트 lightgrid.hlsl 이 기본 힙 버퍼에 만든다(`FrameRenderer::dispatchLightGrid`)
    };

    /**
     * @class GPULightBuffer
     * @brief 프레임 라이트 목록을 구조 버퍼에 업로드하고 SRV 를 보관합니다. 뷰마다의 라이트 그리드 버퍼(t12)도 함께 소유합니다. 렌더 스레드가 소유합니다.
     */
    class SW_API GPULightBuffer
    {
    public:
        GPULightBuffer()                                   = default;
        ~GPULightBuffer()                                  = default;
        GPULightBuffer( const GPULightBuffer& )            = delete;
        GPULightBuffer& operator=( const GPULightBuffer& ) = delete;

        /**
         * @brief 이번 프레임의 라이트와 그림자 타일 원소를 업로드 목록에 담습니다. 업로드는 뷰가 정해진 뒤 `update` 가 합니다.
         * @details 상한(`shaderslot::kMaxFrameLight`)을 넘으면 잘라 담고 **한 번만** 경고합니다(매 프레임 찍으면 로그가 그것으로 덮입니다).
         *          @p listShadowTileElement(점광과 스폿 그림자 타일)는 잘린 라이트 바로 뒤에 붙고 `getCount` 에 들지 않습니다.
         */
        void populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement );

        /**
         * @brief 라이트 버퍼를 올리고, 뷰마다 라이트 그리드를 준비합니다.
         * @param listGridViewProj 그리드를 만들 뷰의 뷰-투영(셰이더의 `g_ViewProj` 와 같은 값). 순서가 그리드 번호입니다.
         * @param producer CPU 면 여기서 만들어 그리드 버퍼(t12)로 올리고, GPU 면 뷰 정보만 올리고 그리드 버퍼(UAV)를 잡습니다 — 채우기는 `FrameRenderer::dispatchLightGrid`.
         *                 None 이면 그리드 없이 올립니다.
         * @details 용량은 `ensureCapacity` 가 필요할 때만 다시 잡습니다. 그리드를 만들지 못한 뷰(투영이 깨졌다)와 GPU 그리드 버퍼를 만들지 못한 프레임은
         *          오류를 한 번 남기고 그리드 없이 그립니다(셰이더는 목록 전체를 돈다 — 그림은 같고 비용만 다르다).
         */
        void update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj, LightGridProducer producer );

        /** @brief 라이트 버퍼입니다. 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _buffer; }
        /** @brief GPU 에 올라간 라이트 수입니다. 셰이더 루프가 이 수만큼 돕니다. */
        uint32 getCount() const { return _count; }
        /** @brief `update` 에 준 뷰 @p gridIndex 의 그리드 첫 단어입니다(PassCB `g_SwLightGridBase`). 그리드가 없으면 `invalid_index::kUint32` 입니다. */
        uint32 getGridBase( uint32 gridIndex ) const { return gridIndex < _listGridBase.size() ? _listGridBase[gridIndex] : invalid_index::kUint32; }
        /** @brief 이번 프레임의 그리드 버퍼(t12)입니다. 생산자가 CPU 면 동적 버퍼, GPU 면 기본 힙 버퍼입니다. 그리드가 없으면 nullptr 입니다. */
        const RHIStructuredBufferSlot* findGridBuffer() const;
        /** @brief 셰이더에 걸 수 있는 상태인지 확인합니다. */
        bool isBindable() const { return _count > 0 && _buffer._buffer != 0 && _buffer._srv != kInvalidDescriptorIndex; }

        /** @brief GPU 생산자가 이번 프레임에 채울 뷰 수입니다. 0 이면 디스패치할 것이 없습니다. */
        uint32 getGPUGridViewCount() const { return _gpuGridViewCount; }
        /** @brief GPU 생산자의 뷰 정보 버퍼(컴퓨트 t1)입니다. */
        const RHIStructuredBufferSlot& getGPUGridViewBuffer() const { return _gpuGridViewBuffer; }
        /** @brief GPU 생산자가 쓰는 그리드 버퍼(컴퓨트 u0, 그래픽스 t12)입니다. */
        const RHIStructuredBufferSlot& getGPUGridBuffer() const { return _gpuGridBuffer; }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        /** @brief CPU 생산자 — 뷰마다 `LightGrid` 를 채워 단어를 이어 붙여 올립니다. */
        void updateCPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj );
        /** @brief GPU 생산자 — 뷰마다 뷰 정보를 올리고 그리드 버퍼를 잡습니다. */
        void updateGPUGrid( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj );
        /** @brief 그리드를 만들지 못한 이유를 한 번만 남깁니다. */
        void reportGridFailure( const utf8* pReason, uint32 viewIndex );

        RHIStructuredBufferSlot _buffer;
        RHIStructuredBufferSlot _cpuGridBuffer;       ///< CPU 생산자의 그리드 단어(동적, t12)
        RHIStructuredBufferSlot _gpuGridBuffer;       ///< GPU 생산자의 그리드 단어(기본 힙 UAV, t12 · 컴퓨트 u0)
        RHIStructuredBufferSlot _gpuGridViewBuffer;   ///< GPU 생산자의 뷰 정보(float4 × SW_LIGHT_GRID_VIEW_FLOAT4_COUNT, 컴퓨트 t1)
        vector<GPULight>        _listUploadScratch;   ///< 라이트와 그림자 타일을 이어 붙여 한 번에 업로드하는 임시 버퍼(프레임마다 다시 쓴다)
        vector<LightGrid>       _listGrid;            ///< 뷰마다의 라이트 그리드(CPU 생산자, 저장소를 프레임마다 다시 쓴다)
        vector<uint32>          _listGridWordScratch; ///< 뷰마다의 그리드 단어를 이어 붙인 업로드 임시 버퍼(CPU 생산자)
        vector<float4>          _listGridViewScratch; ///< 뷰마다의 뷰 정보(GPU 생산자)
        vector<uint32>          _listGridBase;        ///< 뷰마다 그리드의 첫 단어(없으면 invalid_index::kUint32)
        uint32                  _count{ 0 };
        uint32                  _gpuGridViewCount{ 0 };                   ///< GPU 생산자가 이번 프레임에 채울 뷰 수
        LightGridProducer       _gridProducer{ LightGridProducer::None }; ///< 이번 프레임에 그리드 버퍼를 채운 쪽
        /// @brief 상한 초과 경고를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedOverflow{ SW_FALSE };
        /// @brief 그리드를 만들지 못한 뷰의 오류를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedGrid{ SW_FALSE };
    };
} // namespace sw

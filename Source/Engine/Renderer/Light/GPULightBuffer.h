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
     * @class GPULightBuffer
     * @brief 프레임 라이트 목록을 구조 버퍼에 업로드하고 SRV 를 보관합니다. 렌더 스레드가 소유합니다.
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
         * @brief 뷰마다 라이트 그리드(`LightGrid`)를 만들어 타일 원소 뒤에 붙이고 버퍼를 올립니다.
         * @param listGridViewProj 그리드를 만들 뷰의 뷰-투영(셰이더의 `g_ViewProj` 와 같은 값). 비면 그리드 없이 올립니다(`gv_lightCulling` 0).
         * @details 용량은 `ensureCapacity` 가 필요할 때만 다시 잡습니다. 그리드를 만들지 못한 뷰(투영이 깨졌다)는 오류를 한 번 남기고 그리드 없이 그립니다.
         */
        void update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj );

        /** @brief 라이트 버퍼입니다. 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _buffer; }
        /** @brief GPU 에 올라간 라이트 수입니다. 셰이더 루프가 이 수만큼 돕니다. */
        uint32 getCount() const { return _count; }
        /** @brief `update` 에 준 뷰 @p gridIndex 의 그리드 첫 원소입니다(PassCB `g_SwLightGridBase`). 그리드가 없으면 `invalid_index::kUint32` 입니다. */
        uint32 getGridBase( uint32 gridIndex ) const { return gridIndex < _listGridBase.size() ? _listGridBase[gridIndex] : invalid_index::kUint32; }
        /** @brief 셰이더에 걸 수 있는 상태인지 확인합니다. */
        bool isBindable() const { return _count > 0 && _buffer._buffer != 0 && _buffer._srv != kInvalidDescriptorIndex; }

        /** @brief 버퍼를 놓습니다. 디바이스가 바뀌거나 내려갈 때 부릅니다. */
        void release( IRHIDevice* pDevice );

    private:
        RHIStructuredBufferSlot _buffer;
        vector<GPULight>        _listUploadScratch; ///< 라이트, 그림자 타일, 뷰마다의 그리드를 이어 붙여 한 번에 업로드하는 임시 버퍼(프레임마다 다시 쓴다)
        vector<LightGrid>       _listGrid;          ///< 뷰마다의 라이트 그리드(저장소를 프레임마다 다시 쓴다)
        vector<uint32>          _listGridBase;      ///< 뷰마다 그리드의 첫 원소(없으면 invalid_index::kUint32)
        uint32                  _count{ 0 };
        uint32                  _populatedCount{ 0 }; ///< `populate` 가 담은 라이트와 타일 원소 수(그리드는 이 뒤에 붙는다)
        /// @brief 상한 초과 경고를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedOverflow{ SW_FALSE };
        /// @brief 그리드를 만들지 못한 뷰의 오류를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedGrid{ SW_FALSE };
    };
} // namespace sw

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
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Lighting/SceneVoxelGrid.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Renderer/Light/LightGrid.h"

namespace sw
{
    class IRHIDevice;
    class Scene;

    /** @brief 쿠키(빛 텍스처)를 쓰는 빛 하나입니다 — 라이트 목록의 원소 번호와 텍스처 경로입니다. 렌더러가 칸을 나눠 그 원소의 `_extra.w` 에 적습니다(`Light2DCookieAtlas`). */
    struct GPULightCookie
    {
        uint32        _lightIndex{ 0 };
        hashed_string _texturePath{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 실시간으로 갱신할 라이트 프로브 볼륨 하나의 요청입니다(`collectSceneLights` 가 만들고 렌더 스레드의 `RealtimeProbeUpdater` 가 읽는다).
     * @details 볼륨 원소(`_volumeElement`)가 격자 모양(시작점 · 간격 · 수)과 프로브 버퍼 안의 자리를 이미 담고 있어 여기에는 갱신에만 필요한 값을 둔다.
     *          복셀 격자는 불변 공유 값이다 — 컴포넌트의 캐시와 같은 것을 가리키고, 렌더 스레드는 포인터가 바뀐 프레임에만 다시 올린다.
     */
    struct RealtimeProbeRequest
    {
        GPULight                         _volumeElement{};      ///< 라이트 목록에 넣은 볼륨 원소의 사본 - 격자 모양(시작점 `_colorIntensity.xyz` · 간격 `_directionType.xyz` · 수 `_params.xyz`)과 프로브 버퍼 안의 첫 프로브 번호(`_params.w`)
        uint64                           _volumeID{ 0 };        ///< 컴포넌트 번호 — 프레임을 넘는 기록(history)의 열쇠
        uint32                           _probeCount{ 0 };      ///< 볼륨의 프로브 수
        uint32                           _rayCount{ 0 };        ///< 프로브 하나를 한 번 갱신할 때 쏘는 광선 수
        uint32                           _updatePeriod{ 1 };    ///< 모든 프로브가 한 번씩 갱신되는 프레임 수
        float32                          _blend{ 1.0f };        ///< 새 값이 기록에 섞이는 비율
        float32                          _bounceAlbedo{ 0.5f }; ///< 닿은 면의 반사율
        shared_ptr<const SceneVoxelGrid> _pVoxelGrid;           ///< 광선이 걷는 점유 복셀
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 씬의 활성 라이트를 GPU 원소로 모읍니다(게임 스레드).
     * @details 레지스트리(`ComponentRegistry`)만 봅니다. 주의: 모든 GameObject 를 도는 경로를 만들지 않습니다
     *          (큐브 20,000 개에서 그 훑기 한 번이 2.9ms 입니다).
     *          활성 판정은 여기서 합니다(레지스트리는 "무엇이 있나"만 압니다).
     * @note 방향광의 그림자 플래그는 `Scene::findShadowCastingDirectionalLight` 가 고른 빛 하나만 받습니다. 그림자 행렬도 같은 함수에서
     *       나옵니다. 그림자를 켠 점광과 스폿은 번호만 @p pOutListShadowLightIndex 에 적고, 아틀라스 슬롯은 `ShadowSetupUtil::populateFrame` 이 나눠 줍니다.
     * @param outList 기존 내용을 지우고 채웁니다. 부르는 쪽이 프레임마다 재사용하는 버퍼여야 합니다(할당 회피).
     * @param pOutListShadowLightIndex 비지 않으면 그림자를 켠 점광과 스폿 원소의 번호를 지우고 채웁니다.
     * @param pOutListProbeElement 비지 않으면 쓸 수 있는 베이크를 가진 라이트 프로브 볼륨마다 볼륨 원소(`kLightTypeProbeVolume`, 3D 라이트 뒤)를 목록에 넣고
     *                             그 프로브들의 SH9 원소(프로브 하나가 float4 `kLightProbeFloat4Count` 개)를 지우고 채웁니다. 반사 프로브도 같은 길이다 — 볼륨 뒤에
     *                             반사 프로브 원소(`kLightTypeReflectionProbe`)를 넣고 그 옥타헤드럴 텍셀(텍셀 하나가 float4 하나)을 같은 목록에 붙입니다.
     *                             nullptr 이면 볼륨도 반사 프로브도 모으지 않습니다.
     * @param pOutListCookie 비지 않으면 쿠키 텍스처를 쓰는 빛(`LightComponent::getCookieTexturePath`)마다 원소 번호와 경로를 지우고 채웁니다. nullptr 이면
     *                       그 빛은 쿠키 없이 그려진다 — 아틀라스를 만들지 않는 경로(라이트 프로브 베이크)만 넘긴다.
     * @param pOutListRealtimeProbe 비지 않으면 실시간 갱신 볼륨(`LightProbeVolumeComponent::isRealtime`)마다 볼륨 원소(자리는 0 으로 채운 프로브 원소)를 넣고 갱신 요청을 지우고 채웁니다.
     *                              nullptr 이면 실시간 볼륨은 모으지 않습니다(갱신을 돌릴 수 없는 경로 — 미리보기와 베이크).
     */
    SW_API void collectSceneLights( const Scene* pScene, vector<GPULight>& outList, vector<uint32>* pOutListShadowLightIndex = nullptr,
                                    vector<float4>* pOutListProbeElement = nullptr, vector<GPULightCookie>* pOutListCookie = nullptr,
                                    vector<RealtimeProbeRequest>* pOutListRealtimeProbe = nullptr );

    /**
     * @enum LightGridProducer
     * @brief 라이트 그리드를 누가 만드는지입니다. 개발 빌드는 `gv_lightCulling` 이 고르고 Shipping 은 GPU 로 고정합니다.
     * @details 리플렉션 enum 이라 명령줄이 이름으로 고른다(`-gv_lightCulling=CPU`). 숫자는 받지 않는다.
     */
    ENUM()
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
         * @brief GPU 구간(`getGpuLightBase` 부터 `getGpuLightCount` 개)의 상한입니다 — 프레임 라이트 상한의 4분의 1입니다(측정 뒤 조정).
         * @details 라이트 버퍼는 [CPU 라이트][GPU 구간][그림자 타일] 순서입니다. GPU 구간은 컴퓨트(vfxsimulate.hlsl)가 파티클 라이트를 쓰는 자리이고, CPU 라이트 바로 뒤라서
         *          셰이더가 보는 라이트 수(`g_SwLightCount`)에 든다 — 라이트 그리드 생산자(lightgrid.hlsl)와 조명 커서가 구간을 따로 알 필요가 없다.
         *          CPU 라이트 수는 이 구간만큼 줄여 CPU + GPU 가 `shaderslot::kMaxFrameLight` 를 넘지 않게 한다.
         */
        static constexpr uint32 kMaxGPULight = shaderslot::kMaxFrameLight / 4;
        static_assert( 0 < kMaxGPULight && kMaxGPULight < shaderslot::kMaxFrameLight, "GPU 구간은 프레임 라이트 상한 안의 일부여야 한다" );

        /**
         * @brief GPU 구간으로 받을 수 있는 슬롯 수를 정합니다. **GPU 생산자만 받습니다.**
         * @details GPU 구간은 GPU 에서 채워져 CPU 가 내용을 모른다. CPU 생산자(`LightGrid::populate`)는 CPU 가 가진 목록만 그리드에 넣고, 그리드 없는 경로는
         *          구간의 죽은 슬롯까지 도므로, 둘 다 GPU 라이트를 제대로 그리지 못한다. 그래서 조용히 생산자를 바꾸지 않고 0 을 돌려준다(`update` 가 오류를 남기고 그리지 않는다).
         * @param requestedSlotCount 방출기들이 요청한 슬롯의 합(`VfxGPUSimulation::assignLightRanges`, 이미 `kMaxGpuLight` 안이다).
         */
        [[nodiscard]] static uint32 selectGPULightSlotCount( LightGridProducer producer, uint32 requestedSlotCount );

        /**
         * @brief 이번 프레임의 라이트와 그림자 타일 원소를 업로드 목록에 담습니다. 업로드는 뷰가 정해진 뒤 `update` 가 합니다.
         * @details 상한 처리(`shaderslot::kMaxFrameLight` 에서 GPU 구간을 뺀 몫)는 GPU 구간 수를 아는 `update` 가 합니다.
         *          @p listShadowTileElement(점광과 스폿 그림자 타일)는 라이트 뒤에 붙고 `getCount` 에 들지 않습니다.
         *          @p listProbeElement(라이트 프로브 SH9, `collectSceneLights`)는 따로 프로브 버퍼(t13)로 올라갑니다.
         * @param bProbeWritable 실시간 프로브 볼륨이 있어 컴퓨트(`probeupdate.hlsl`)가 프로브 버퍼를 쓴다 — 프로브 버퍼가 UAV 가 있는 기본 힙 버퍼가 된다.
         */
        void populate( const vector<GPULight>& listLight, const vector<GPULight>& listShadowTileElement, const vector<float4>& listProbeElement, bool bProbeWritable );

        /**
         * @brief 라이트 버퍼를 올리고, 뷰마다 라이트 그리드를 준비합니다.
         * @param listGridViewProj 그리드를 만들 뷰의 뷰-투영(셰이더의 `g_ViewProj` 와 같은 값). 순서가 그리드 번호입니다.
         * @param producer CPU 면 여기서 만들어 그리드 버퍼(t12)로 올리고, GPU 면 뷰 정보만 올리고 그리드 버퍼(UAV)를 잡습니다 — 채우기는 `FrameRenderer::dispatchLightGrid`.
         *                 None 이면 그리드 없이 올립니다.
         * @param requestedGpuLightSlotCount GPU 방출기들이 요청한 파티클 라이트 슬롯의 합. 생산자가 GPU 가 아니면 오류를 한 번 남기고 0 으로 받는다(`selectGpuLightSlotCount`).
         * @details 용량은 `ensureCapacity` 가 필요할 때만 다시 잡습니다. 그리드를 만들지 못한 뷰(투영이 깨졌다)와 GPU 그리드 버퍼를 만들지 못한 프레임은
         *          오류를 한 번 남기고 그리드 없이 그립니다(셰이더는 목록 전체를 돈다 — 그림은 같고 비용만 다르다).
         *          GPU 구간이 있으면 라이트 버퍼는 UAV 가 있는 기본 힙 버퍼이고, 구간은 **죽은 라이트**(점광, 반경 0 — 그리드에 들지 않는다)로 채워 올린다.
         *          컴퓨트가 쓰지 못한 슬롯(건너뛴 방출기)이 지난 프레임의 낡은 라이트로 남지 않게 한다.
         */
        void update( IRHIDevice* pDevice, const vector<float4x4>& listGridViewProj, LightGridProducer producer, uint32 requestedGPULightSlotCount );

        /** @brief 라이트 버퍼입니다. 셰이더가 SRV 로 읽습니다. */
        const RHIStructuredBufferSlot& getBuffer() const { return _buffer; }
        /** @brief GPU 에 올라간 라이트 수(CPU 라이트 + GPU 구간)입니다. 셰이더 루프가 이 수만큼 돕니다. */
        uint32 getCount() const { return _count; }
        /** @brief GPU 구간의 첫 라이트 번호입니다(= 올라간 CPU 라이트 수). 방출기의 슬롯 구간은 여기서부터 이어집니다. */
        uint32 getGPULightBase() const { return _cpuCount; }
        /** @brief 이번 프레임에 받은 GPU 구간의 슬롯 수입니다. 0 이면 GPU 라이트를 쓰지 않는 프레임(또는 생산자가 GPU 가 아니라 거절한 프레임)입니다. */
        uint32 getGPULightCount() const { return _gpuLightCount; }
        /** @brief GPU 구간을 쓰는 컴퓨트의 UAV 입니다. 구간이 없거나 UAV 를 만들지 못했으면 `kInvalidDescriptorIndex` 입니다. */
        RHIDescriptorIndex getGPULightUav() const { return _gpuLightCount > 0 ? _buffer._uav : kInvalidDescriptorIndex; }
        /** @brief `update` 에 준 뷰 @p gridIndex 의 그리드 첫 단어입니다(PassCB `g_SwLightGridBase`). 그리드가 없으면 `invalid_index::kUint32` 입니다. */
        uint32 getGridBase( uint32 gridIndex ) const { return gridIndex < _listGridBase.size() ? _listGridBase[gridIndex] : invalid_index::kUint32; }
        /** @brief 이번 프레임의 그리드 버퍼(t12)입니다. 생산자가 CPU 면 동적 버퍼, GPU 면 기본 힙 버퍼입니다. 그리드가 없으면 nullptr 입니다. */
        const RHIStructuredBufferSlot* findGridBuffer() const;
        /** @brief 셰이더에 걸 수 있는 상태인지 확인합니다. */
        bool isBindable() const { return _count > 0 && _buffer._buffer != 0 && _buffer._srv != kInvalidDescriptorIndex; }
        /** @brief 라이트 프로브 SH9 버퍼(t13)입니다. 이번 프레임에 쓸 프로브가 없으면 nullptr 입니다(볼륨 원소도 목록에 없다). */
        const RHIStructuredBufferSlot* findProbeBuffer() const;
        /** @brief 프로브 갱신 컴퓨트가 프로브 버퍼를 쓰는 UAV 입니다. 이번 프레임에 쓸 수 없으면(실시간 볼륨이 없다 · 버퍼가 없다 · 백엔드가 UAV 를 거절했다) `kInvalidDescriptorIndex` 입니다. */
        RHIDescriptorIndex getProbeUav() const { return ( _bProbeWritable == SW_TRUE && _probeElementCount > 0 ) ? _probeBuffer._uav : kInvalidDescriptorIndex; }

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
        /** @brief GPU 구간을 거절한 이유(생산자가 GPU 가 아니다)를 한 번만 남깁니다. */
        void reportGPULightRefused( uint32 requestedSlotCount );

        RHIStructuredBufferSlot _buffer;
        RHIStructuredBufferSlot _cpuGridBuffer;                           ///< CPU 생산자의 그리드 단어(동적, t12)
        RHIStructuredBufferSlot _gpuGridBuffer;                           ///< GPU 생산자의 그리드 단어(기본 힙 UAV, t12 · 컴퓨트 u0)
        RHIStructuredBufferSlot _gpuGridViewBuffer;                       ///< GPU 생산자의 뷰 정보(float4 × SW_LIGHT_GRID_VIEW_FLOAT4_COUNT, 컴퓨트 t1)
        RHIStructuredBufferSlot _probeBuffer;                             ///< 라이트 프로브 SH9 · 반사 프로브 텍셀(float4, t13)
        vector<float4>          _listProbeScratch;                        ///< 이번 프레임의 프로브 원소(`populate` 가 담고 `update` 가 올린다)
        uint32                  _probeElementCount{ 0 };                  ///< 이번 프레임에 올린 프로브 원소 수(0 이면 프로브 버퍼를 걸지 않는다)
        uint8                   _bProbeWritable{ SW_FALSE };              ///< 이번 프레임에 컴퓨트가 프로브 버퍼를 쓴다(`populate`)
        vector<GPULight>        _listLightScratch;                        ///< 이번 프레임의 CPU 라이트(`populate` 가 담고 `update` 가 상한을 맞춰 올린다)
        vector<GPULight>        _listTileScratch;                         ///< 이번 프레임의 그림자 타일 원소(`populate` 가 담는다)
        vector<GPULight>        _listUploadScratch;                       ///< CPU 라이트 · GPU 구간(죽은 라이트) · 그림자 타일을 이어 붙여 한 번에 업로드하는 임시 버퍼(프레임마다 다시 쓴다)
        vector<LightGrid>       _listGrid;                                ///< 뷰마다의 라이트 그리드(CPU 생산자, 저장소를 프레임마다 다시 쓴다)
        vector<uint32>          _listGridWordScratch;                     ///< 뷰마다의 그리드 단어를 이어 붙인 업로드 임시 버퍼(CPU 생산자)
        vector<float4>          _listGridViewScratch;                     ///< 뷰마다의 뷰 정보(GPU 생산자)
        vector<uint32>          _listGridBase;                            ///< 뷰마다 그리드의 첫 단어(없으면 invalid_index::kUint32)
        uint32                  _count{ 0 };                              ///< 셰이더가 보는 라이트 수 = CPU 라이트 + GPU 구간
        uint32                  _cpuCount{ 0 };                           ///< 올라간 CPU 라이트 수(= GPU 구간의 첫 번호)
        uint32                  _gpuLightCount{ 0 };                      ///< 받은 GPU 구간의 슬롯 수
        uint32                  _gpuGridViewCount{ 0 };                   ///< GPU 생산자가 이번 프레임에 채울 뷰 수
        LightGridProducer       _gridProducer{ LightGridProducer::None }; ///< 이번 프레임에 그리드 버퍼를 채운 쪽
        /// @brief 상한 초과 경고를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedOverflow{ SW_FALSE };
        /// @brief 그리드를 만들지 못한 뷰의 오류를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedGrid{ SW_FALSE };
        /// @brief GPU 구간을 거절한 오류를 한 번만 남기기 위한 표시입니다.
        uint8 _bWarnedGPULightRefused{ SW_FALSE };
        /// @brief 백엔드가 라이트 버퍼의 UAV 를 거절했습니다(다시 잡아 봐야 같다 — 오류도 한 번만).
        uint8 _bUavDenied{ SW_FALSE };
        uint8 _bProbeUavDenied{ SW_FALSE }; ///< 프로브 버퍼의 UAV 를 백엔드가 거절했다(한 번만 알리고 다시 잡지 않는다)
    };
} // namespace sw

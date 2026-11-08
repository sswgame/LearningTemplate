/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h
 * @brief 씬 하나의 내용을 패킷에 싣습니다(게임 스레드). 주 광원 · 라이트 · 하늘 · 안개 · 그림자 배치, 애니메이션 LOD · 메시 LOD 의 뷰, GPUScene 스냅샷입니다.
 * @details 씬마다 하나씩 둔다 — 주 화면은 `RenderPacketBuilder`, 미리보기 씬은 `RenderPacketScenePreviews`, 화면 밖 캡처는 `SceneCapture` 가 소유한다(스냅샷 빌더의 재구축 판단 캐시가 씬별이다).
 *          렌더 스레드는 씬을 보지 못하므로(패킷이 유일한 통로) 씬에서 읽을 것은 여기서 패킷에 담습니다. 언리얼 `FScene` 의 게임 스레드 쪽 갱신에 해당합니다.
 *          예전에는 미리보기 · 캡처의 채우기(`RenderPacketSceneFill`)가 같은 단계를 따로 적어 서로 갈라질 수 있었다 — 단계는 여기 한 벌이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Scene/AnimationLOD.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Frame/View/RenderView.h"
#include "Engine/Renderer/Lighting/GPULightBuffer.h"
#include "Engine/Renderer/Lighting/LensFlareSpriteBuffer.h"

namespace sw
{
    struct RenderFramePacket;

    class CameraComponent;
    class DirectionalLightComponent;
    class GPUSceneBuilder;
    class GPUUploadQueue;
    class IRHIDevice;
    class Scene;
    class UISystem;

    /** @brief 씬 패킷 하나가 보는 시점과 출력입니다(미리보기 · 화면 밖 캡처 — `RenderPacketSceneCollector::fillSceneView`). */
    struct RenderSceneViewDesc
    {
        RenderViewSettings _settings{};
        float4x4           _viewProj{};
        float3             _cameraPos{};
        RHITextureHandle   _outputRenderTarget{ 0 }; ///< 0 이면 렌더러의 기본 출력(백버퍼 · Present 캡처)
        uint32             _width{ 0 };
        uint32             _height{ 0 };
    };

    /** @brief 씬에서 고른 방향광 둘입니다 — 키라이트(방향 · 색 · 앰비언트의 출처)와 그림자 맵을 가져가는 빛(`DirectionalLightComponent::findShadowCasting`). */
    struct RenderPacketKeyLights
    {
        DirectionalLightComponent* _pLight{ nullptr };
        DirectionalLightComponent* _pShadowLight{ nullptr };

        /** @brief 그림자 배치에 넘길 빛입니다. 키라이트가 없으면 그림자 빛도 없다(그림자 빛만으로 볼륨을 만들지 않는다). */
        const DirectionalLightComponent* getShadowCaster() const { return _pLight != nullptr ? _pShadowLight : nullptr; }
    };

    /**
     * @class RenderPacketSceneCollector
     * @brief 활성 씬의 GT 쪽 스냅샷 빌더와 씬 내용 채우기입니다. 빌더는 `populateFromScene` 의 재구축 판단 캐시가 프레임을 넘어 유지되도록 여기서 소유합니다.
     * @details 프레임마다 CPU 스냅샷만 `exportCPUSnapshot` 으로 뽑아 패킷에 담아 렌더 스레드로 넘깁니다. 빌더는 힙에 둡니다(값으로 들면 Graphics 의 씬 스냅샷 헤더들을 App 까지 끌고 간다).
     */
    class SW_API RenderPacketSceneCollector
    {
    public:
        RenderPacketSceneCollector();
        ~RenderPacketSceneCollector();

        RenderPacketSceneCollector( const RenderPacketSceneCollector& )            = delete;
        RenderPacketSceneCollector& operator=( const RenderPacketSceneCollector& ) = delete;

        /** @brief 스냅샷 빌더가 없으면 만들고, 디바이스에 매인 설정(배치 합치기, GPU 인스턴스 LOD 능력)을 @p device 로 정합니다. */
        void initialize( IRHIDevice& device );
        /** @brief 스냅샷 빌더가 보관한 머티리얼과 인스턴스를 놓습니다. **디바이스가 살아 있을 때** 부릅니다. */
        void shutdown();
        /** @brief 스냅샷 빌더를 없앱니다. */
        void destroy();

        /** @brief 스냅샷 빌더가 있는지(기동 단계가 섰는지) 반환합니다. */
        bool isCreated() const { return _gpuSceneBuilder != nullptr; }
        /** @brief 스냅샷 빌더가 머티리얼이 달라도 배치를 합치는지 반환합니다(없으면 false). */
        bool isMergeBatchesAcrossMaterials() const;

        /**
         * @brief 씬의 키라이트를 패킷의 주 광원(방향 · 세기 · 색 · 앰비언트)으로 싣고 고른 방향광 둘을 돌려줍니다. 주 패킷과 미리보기 패킷이 같은 규칙으로 부릅니다.
         * @details 주광은 씬이 갖고, 렌더 스레드는 패킷으로만 받는다(렌더 스레드에서 씬을 조회할 수 없다).
         */
        static RenderPacketKeyLights writeKeyLight( RenderFramePacket& packet, Scene& scene );
        /** @brief 키라이트와 씬의 **모든** 라이트(방향광 · 점광 · 스폿, 프로브 · 쿠키 · 실시간 프로브 요청), 하늘을 싣습니다. 그림자를 켠 점광 · 스폿의 번호는 그림자 배치를 위해 남깁니다. */
        RenderPacketKeyLights collectLights( RenderFramePacket& packet, Scene& activeScene );
        /** @brief 주 시점 카메라에 매인 씬 내용입니다 — 안개(카메라가 물속이면 물의 안개)와 투명 정렬 축(render2d.xml). */
        void applyMainCamera( RenderFramePacket& packet, Scene& activeScene, CameraComponent& camera );
        /**
         * @brief 렌즈 플레어를 단 3D 빛(`LightComponent::getLensFlarePath`)마다 요청을 패킷에 싣습니다. 플레어 파일은 경로마다 한 번 읽어 둔다(`_listLensFlareAsset`).
         * @details 못 읽은 파일은 오류를 한 번 남기고 그 빛은 플레어가 없다 — 다른 플레어로 대신하지 않는다. shortcut: 파일을 고쳐도 다시 읽지 않는다 — 핫 리로드가 필요해지면 경로의 수정 세대를 열쇠에 더한다.
         */
        void collectLensFlares( RenderFramePacket& packet, const Scene& activeScene );
        /** @brief 켜진 데칼(`DecalComponent`)의 그리기 값을 그리는 차례(`_sortOrder`)로 패킷에 싣습니다. */
        static void collectDecals( RenderFramePacket& packet, const Scene& activeScene );
        /**
         * @brief 그림자 배치(아틀라스 칸 · 캐스케이드 · 점광 · 스폿 타일)를 만듭니다. 카메라가 정해진 **뒤에** 부릅니다 — `_shadowViewDistance` 를 준 빛은 볼륨을 카메라가 보는 곳에 맞춘다.
         * @details 텍셀 스냅 · 바이어스는 렌더 스레드가 그릴 캐스케이드 칸과 같은 해상도로 측정한다.
         */
        void populateShadows( RenderFramePacket& packet, const RenderPacketKeyLights& lights );
        /** @brief 이번 프레임에 그리는 추가 뷰마다 자기 카메라에 맞춘 방향광 그림자를 만듭니다(주 시점 볼륨을 쓰면 다른 곳을 보는 CCTV · 분할 화면은 그림자가 빠진다). */
        void populateViewShadows( RenderFramePacket& packet, const RenderPacketKeyLights& lights ) const;
        /** @brief 애니메이션 LOD · 메시 LOD 가 다음 프레임에 볼 뷰(주 시점과 이번 프레임에 그리는 추가 뷰)를 씬의 애니메이션 시스템과 메시 LOD 고르기에 넘깁니다. */
        void updateLODViews( const RenderFramePacket& packet, Scene& activeScene );
        /** @brief GPUScene 을 씬에서 채우고 추가 뷰의 투명 순서 · GPU 업로드 요청(@p pUploadQueue 가 있으면)을 만든 뒤 CPU 스냅샷을 패킷에 내보냅니다. */
        void populateGPUSceneSnapshot( RenderFramePacket& packet, Scene* pActiveScene, GPUUploadQueue* pUploadQueue );
        /**
         * @brief @p scene 을 @p view 하나로 본 패킷을 채웁니다(미리보기 씬 · 화면 밖 캡처) — 주 화면과 같은 단계(빛 · 안개 · 그림자 · LOD 뷰 · 스냅샷)를 차례로 부른다.
         * @details 패킷을 비우지 않는다(부르는 쪽이 `resetForFrame` 하고 씬 키 · 프레임 번호를 적는다). 추가 뷰 · 캔버스는 주 화면에만 있다.
         *          GPU 인스턴스 LOD 는 끈다 — 단계 전환의 크로스페이드가 시간을 흘려야 끝나는데, 화면 밖 캡처는 시간을 흘리지 않는다.
         */
        void fillSceneView( RenderFramePacket& outPacket, Scene& scene, const RenderSceneViewDesc& view, GPUUploadQueue* pUploadQueue );
        /**
         * @brief 패킷의 GPU 투영 마커(`CanvasFrameData::_listMarker`) 중 활성 씬의 메시에 붙은 것에 GPUScene 인스턴스 칸을 풉니다(스냅샷 · 캔버스를 채운 뒤).
         * @details 마커가 속한 관리자가 활성 씬의 것이 아니거나 칸을 못 찾으면 기록은 월드 점을 쓴다(`_instanceSlot` 이 비어 있다).
         */
        void resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const;

    private:
        /** @brief 스냅샷을 짓고(추가 뷰의 투명 순서 포함) 업로드를 요청한 뒤 패킷에 내보냅니다. 인스턴스 LOD 설정은 부르는 쪽이 먼저 정한다. */
        void buildSnapshot( RenderFramePacket& packet, Scene* pScene, GPUUploadQueue* pUploadQueue );

    private:
        /** @brief GT 쪽 씬 스냅샷 빌더입니다. populateFromScene 의 재구축 판단 캐시가 프레임을 넘어 유지되도록 여기서 소유합니다. */
        unique_ptr<GPUSceneBuilder> _gpuSceneBuilder;
        /** @brief 애니메이션 LOD · 메시 LOD 에 넘기는 이번 프레임의 뷰(주 시점 + 그리는 추가 뷰)입니다. 프레임마다 재사용합니다. */
        vector<LODView> _listLODView;
        /** @brief 그림자를 켠 점광과 스폿의 라이트 원소 번호입니다(`collectSceneLights` → `FrameShadowData::populateFrame`). 프레임마다 재사용합니다. */
        vector<uint32> _listShadowLightIndex;
        /** @brief 프로브 버퍼 원소 묶음입니다(`collectSceneLights`). 내용이 그대로면 지난 묶음을 다시 주도록 프레임을 넘어 유지합니다. */
        LightProbeElementBuilder _lightProbeElement;
        /** @brief 읽은 렌즈 플레어 파일(경로 → 데이터, 못 읽은 경로는 nullptr)입니다. 프레임을 넘어 유지한다. */
        vector<pair<string, shared_ptr<const LensFlareAsset>>> _listLensFlareAsset;
        /** @brief 디바이스가 GPU 컬링 컴퓨트를 돌릴 수 있어 인스턴스 LOD 를 GPU 가 고를 수 있는지입니다(`initialize` 가 정한다). `fillSceneView` 는 쓰지 않는다. */
        uint8 _bGPUInstanceLODCapable : 1;
    };
} // namespace sw

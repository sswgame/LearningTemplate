/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h
 * @brief 활성 씬의 내용을 패킷에 싣습니다(게임 스레드). 주 광원 · 라이트 · 하늘 · 안개 · 그림자 배치, 애니메이션 LOD · 메시 LOD 의 뷰, GPUScene 스냅샷입니다.
 * @details `RenderPacketBuilder` 가 값으로 소유합니다. 렌더 스레드는 씬을 보지 못하므로(패킷이 유일한 통로) 씬에서 읽을 것은 여기서 패킷에 담습니다.
 *          언리얼 `FScene` 의 게임 스레드 쪽 갱신(프리미티브 · 라이트 등록을 렌더 스레드에 넘기기)에 해당합니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Scene/AnimationLOD.h"

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

    /** @brief 씬에서 고른 방향광 둘입니다 — 키라이트(방향 · 색 · 앰비언트의 출처)와 그림자 맵을 가져가는 빛(`Scene::findShadowCastingDirectionalLight`). */
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

        /**
         * @brief 스냅샷 빌더가 없으면 만들고, 디바이스에 매인 설정(배치 합치기, GPU 인스턴스 LOD 능력)을 @p device 로 정합니다.
         * @return 머티리얼이 달라도 배치를 합치는지입니다(미리보기 빌더에도 같은 값을 준다).
         */
        bool initialize( IRHIDevice& device );
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
         * @brief 패킷의 GPU 투영 마커(`CanvasFrameData::_listMarker`) 중 활성 씬의 메시에 붙은 것에 GPUScene 인스턴스 칸을 풉니다(스냅샷 · 캔버스를 채운 뒤).
         * @details 마커가 속한 관리자가 활성 씬의 것이 아니거나 칸을 못 찾으면 기록은 월드 점을 쓴다(`_instanceSlot` 이 비어 있다).
         */
        void resolveMarkerInstanceSlots( RenderFramePacket& packet, const UISystem* pUISystem, const Scene* pActiveScene ) const;

    private:
        /** @brief GT 쪽 씬 스냅샷 빌더입니다. populateFromScene 의 재구축 판단 캐시가 프레임을 넘어 유지되도록 여기서 소유합니다. */
        unique_ptr<GPUSceneBuilder> _gpuSceneBuilder;
        /** @brief 애니메이션 LOD · 메시 LOD 에 넘기는 이번 프레임의 뷰(주 시점 + 그리는 추가 뷰)입니다. 프레임마다 재사용합니다. */
        vector<LODView> _listLODView;
        /** @brief 그림자를 켠 점광과 스폿의 라이트 원소 번호입니다(`collectSceneLights` → `ShadowSetupUtil::populateFrame`). 프레임마다 재사용합니다. */
        vector<uint32> _listShadowLightIndex;
        /** @brief 디바이스가 GPU 컬링 컴퓨트를 돌릴 수 있어 인스턴스 LOD 를 GPU 가 고를 수 있는지입니다(`initialize` 가 정한다). 미리보기 빌더는 쓰지 않는다. */
        bool _bGPUInstanceLODCapable;
    };
} // namespace sw

/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketScenePreviews.h
 * @brief 호스트가 요청한 미리보기 씬 뷰(에디터 툴 창)마다 미리보기 패킷을 채웁니다(게임 스레드).
 * @details `RenderPacketBuilder` 가 값으로 소유합니다. 씬마다 스냅샷 빌더를 따로 둔다(빌더의 재구축 판단 캐시가 씬별이기 때문이다) — 언리얼 에디터 미리보기 씬(`FPreviewScene`)이 자기 `FScene` 을 갖는 것과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Animation/Scene/AnimationLOD.h"

namespace sw
{
    struct HostViewTargets;
    struct RenderFramePacket;

    class GPUSceneBuilder;
    class GPUUploadQueue;
    class SceneManager;

    /**
     * @class RenderPacketScenePreviews
     * @brief 미리보기 씬마다의 GT 쪽 스냅샷 빌더와 미리보기 패킷 채우기입니다. 요청이 끊긴 씬의 빌더는 그 프레임에 해제합니다.
     */
    class SW_API RenderPacketScenePreviews
    {
    public:
        RenderPacketScenePreviews();
        ~RenderPacketScenePreviews();

        RenderPacketScenePreviews( const RenderPacketScenePreviews& )            = delete;
        RenderPacketScenePreviews& operator=( const RenderPacketScenePreviews& ) = delete;

        /** @brief 머티리얼이 달라도 배치를 합치는지를 있는 빌더와 앞으로 만들 빌더에 정합니다(주 빌더와 같은 값, 디바이스가 정한다). */
        void setMergeBatchesAcrossMaterials( bool bMerge );
        /** @brief 빌더를 모두 내립니다 — 머티리얼 · 인스턴스 소유를 놓으므로 디바이스가 살아 있을 때 부릅니다. */
        void clear();

        /**
         * @brief 호스트가 요청한 미리보기 씬 뷰(`HostViewTargets::_arrSceneView`)마다 미리보기 패킷을 하나씩 채워 @p packet 에 붙입니다.
         * @details 미리보기 패킷에는 주 광원, 라이트, 그림자, 카메라, 씬 스냅샷만 싣는다. 추가 뷰, 캔버스, 스크린샷은 주 패킷에만 있다.
         * @param sceneManager 씬 키로 씬을 찾는 곳입니다(`SceneManager::findPreviewScene`). 없는 키는 건너뜁니다.
         * @param pUploadQueue GPU 업로드 요청을 받을 큐입니다(없으면 요청하지 않는다).
         */
        void populate( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager, GPUUploadQueue* pUploadQueue );

    private:
        /** @brief 미리보기 씬 하나의 GT 쪽 스냅샷 빌더입니다. 씬 키로 찾습니다. */
        struct ScenePreviewBuilder
        {
            unique_ptr<GPUSceneBuilder> _builder;
            uint32                      _sceneKey{ 0 };
            uint8                       _bSeen{ SW_FALSE }; ///< 이번 프레임에 요청이 있었다
        };
        /** @brief 씬 키의 빌더 항목을 찾고, 없으면 만듭니다. */
        ScenePreviewBuilder& getOrCreateBuilder( uint32 sceneKey );

        /** @brief 미리보기 씬마다의 스냅샷 빌더입니다. 요청이 끊긴 씬의 것은 해제합니다. */
        vector<ScenePreviewBuilder> _listBuilder;
        /** @brief 미리보기 씬 하나의 애니메이션 LOD · 메시 LOD 뷰입니다. 프레임마다 재사용합니다. */
        vector<LODView> _listLODView;
        /** @brief 그림자를 켠 점광과 스폿의 라이트 원소 번호입니다(`collectSceneLights` → `ShadowSetupUtil::populateFrame`). 프레임마다 재사용합니다. */
        vector<uint32> _listShadowLightIndex;
        /** @brief 새 빌더에 줄 배치 합치기 값입니다(`setMergeBatchesAcrossMaterials`). */
        bool _bMergeBatchesAcrossMaterials;
    };
} // namespace sw

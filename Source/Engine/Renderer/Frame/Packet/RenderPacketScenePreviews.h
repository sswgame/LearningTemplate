/**
 * @file Engine/Renderer/Frame/Packet/RenderPacketScenePreviews.h
 * @brief 호스트가 요청한 미리보기 씬 뷰(에디터 툴 창)마다 미리보기 패킷을 채웁니다(게임 스레드).
 * @details `RenderPacketBuilder` 가 값으로 소유합니다. 씬마다 씬 채우기(`RenderPacketSceneCollector` — 주 화면과 같은 객체)를 따로 둔다(스냅샷 빌더의 재구축 판단 캐시가 씬별이다) —
 *          언리얼 에디터 미리보기 씬(`FPreviewScene`)이 자기 `FScene` 을 갖는 것과 같다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Renderer/Frame/Packet/RenderPacketSceneCollector.h"

namespace sw
{
    struct HostViewTargets;
    struct RenderFramePacket;

    class GPUUploadQueue;
    class IRHIDevice;
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

        /** @brief 씬 채우기를 만들 디바이스를 정합니다(배치 합치기 등 디바이스에 매인 설정 — 주 화면과 같은 규칙). 있는 채우기도 다시 맞춘다. */
        void initialize( IRHIDevice& device );
        /** @brief 씬 채우기를 모두 내립니다 — 머티리얼 · 인스턴스 소유를 놓으므로 디바이스가 살아 있을 때 부릅니다. */
        void clear();

        /**
         * @brief 호스트가 요청한 미리보기 씬 뷰(`HostViewTargets::_arrSceneView`)마다 미리보기 패킷을 하나씩 채워 @p packet 에 붙입니다.
         * @details 미리보기 패킷에는 씬 하나의 채우기(`RenderPacketSceneCollector::fillSceneView` — 주 광원, 라이트, 그림자, 카메라, 씬 스냅샷)만 싣는다. 추가 뷰, 캔버스, 스크린샷은 주 패킷에만 있다.
         *          사라진 씬(매니저가 더는 찾지 못한다)의 채우기는 내리고 그 키를 `RenderFramePacket::_listRetiredSceneKey` 에 싣는다 — 요청만 끊긴 씬(창을 가렸다)은 남긴다.
         * @param sceneManager 씬 키로 씬을 찾는 곳입니다(`SceneManager::findPreviewScene`). 없는 키는 건너뜁니다.
         * @param pUploadQueue GPU 업로드 요청을 받을 큐입니다(없으면 요청하지 않는다).
         */
        void populate( RenderFramePacket& packet, const HostViewTargets& views, const SceneManager& sceneManager, GPUUploadQueue* pUploadQueue );

    private:
        /** @brief 미리보기 씬 하나의 채우기입니다. 씬 키로 찾습니다. */
        struct ScenePreviewEntry
        {
            unique_ptr<RenderPacketSceneCollector> _collector;
            uint32                                 _sceneKey{ 0 };
        };
        /** @brief 씬 키의 항목을 찾고, 없으면 만듭니다. */
        ScenePreviewEntry& getOrCreateEntry( uint32 sceneKey );

        /** @brief 미리보기 씬마다의 채우기입니다. 요청이 끊긴 씬의 것은 해제합니다. */
        vector<ScenePreviewEntry> _listEntry;
        /** @brief 새 채우기를 맞출 디바이스입니다(`initialize`). 없으면 미리보기를 채우지 않는다. */
        IRHIDevice* _pDevice;
    };
} // namespace sw

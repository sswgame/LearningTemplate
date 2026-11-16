/**
 * @file Engine/Renderer/Frame/OffscreenSceneRenderers.h
 * @brief 화면 밖 씬(에디터 미리보기 씬 · 씬 캡처)마다 하나씩 둔 렌더러를 씬 키로 소유합니다.
 * @details 언리얼에서 미리보기 월드 · 씬 캡처가 자기 `FScene` 을 갖는 자리다 — 렌더러의 수명은 **씬의 수명**이다. 예전에는 주 렌더러가 하위 렌더러 목록을 들고
 *          "이번 프레임 패킷에 없으면 내린다" 로 수명을 정해, 창을 가리거나 닫기만 해도 파이프라인 · 그래프 · 트랜지언트를 처음부터 다시 지었다.
 *          이제 미리보기 씬이 파괴되면(패킷의 `_listRetiredSceneKey`) · 캡처가 끝나면(`SceneCapture::shutdown`) 내린다.
 *          렌더 스레드 객체다 — 렌더 스레드(또는 렌더 스레드를 재운 뒤의 호출)만 만진다. 수(`getCount`)만 다른 스레드가 읽는다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ServiceSlot.h"

namespace sw
{
    class FrameRenderer;
    class IRHIDevice;
    struct RenderFramePacket;

    /**
     * @class OffscreenSceneRenderers
     * @brief (씬 키, 파이프라인)마다 화면 밖 렌더러 하나를 두고 씬이 사라질 때 내립니다.
     */
    class SW_API OffscreenSceneRenderers
    {
    public:
        /** @brief 캡처 씬 키의 표시 비트입니다. 미리보기 씬 키(`SceneManager::getPreviewSceneKey` — 1 부터 센다)와 겹치지 않는 공간이다. */
        static constexpr uint32 kCaptureSceneKeyBit = 0x80000000u;

        OffscreenSceneRenderers();
        ~OffscreenSceneRenderers();

        OffscreenSceneRenderers( const OffscreenSceneRenderers& )            = delete;
        OffscreenSceneRenderers& operator=( const OffscreenSceneRenderers& ) = delete;

        /**
         * @brief 화면 밖 패킷을 그 (씬 키, 파이프라인)의 렌더러로 패킷의 출력 타깃(`_outputRenderTarget`)에 그립니다(렌더 스레드).
         * @details 출력 타깃을 열고(@p clearColor 로 지운다) 그린 뒤 셰이더 읽기 상태로 둔다 — 미리보기와 캡처가 이 한 길을 쓴다. 렌더러가 없으면 @p mainRenderer 를 본떠 만든다
         *          (@p pipelinePath 가 비면 주 렌더러의 파이프라인이고 PSO 저장소를 함께 쓴다).
         * @return 그린 렌더러입니다(캡처가 그 첨부를 읽는다). 그리지 못하면 nullptr 입니다(이유는 로그에 남는다).
         */
        FrameRenderer* execute( FrameRenderer& mainRenderer, IRHIDevice* pDevice, RenderFramePacket& packet, const float4& clearColor, string_view pipelinePath = {} );
        /**
         * @brief 씬 캡처 하나에 새 씬 키를 냅니다(하위 렌더러를 캡처끼리 나누지 않는다 — 씬이 다르다). 어느 스레드에서 불러도 된다.
         * @details 씬 키의 주인은 키로 렌더러를 드는 이 표다(프로세스 전역 발급기를 두지 않는다 — 사용자 지적 2026-11-08). 언리얼 씬 캡처도 자기 `FScene` 을 렌더러 모듈이 든다.
         */
        [[nodiscard]] uint32 allocateCaptureSceneKey();
        /** @brief 씬 @p sceneKey 의 렌더러를 모두 내립니다(그 씬이 사라졌다). 디바이스의 해제는 GPU 완료 뒤로 미뤄진다 — 이미 낸 프레임이 쓰는 자원을 지금 파괴하지 않는다. */
        void retire( uint32 sceneKey );
        /** @brief 셰이더 핫 리로드를 렌더러마다 알립니다. 주 렌더러가 PSO 캐시를 다시 채운 **뒤에** 부른다(함께 쓰는 캐시의 새 PSO 를 집는다). */
        void onShaderRecompiled( const vector<string>& listShaderPath );
        /** @brief 렌더러를 모두 내립니다(디바이스가 살아 있을 때, 주 렌더러보다 먼저 — 주 렌더러의 PSO 캐시를 빌려 쓴다). */
        void shutdown();
        /** @brief 살아 있는 렌더러 수입니다(진단 · 자동화 감시 값 `Editor.ScenePreviewRenderers`). 어느 스레드에서 읽어도 된다. */
        uint32 getCount() const { return _count.load(); }

    private:
        /** @brief 화면 밖 씬 하나를 그리는 렌더러입니다. */
        struct Entry
        {
            unique_ptr<FrameRenderer> _renderer;
            uint32                    _sceneKey{ 0 };
            string                    _pipelinePath; ///< 비면 주 렌더러의 파이프라인(PSO 저장소를 함께 쓴다)
        };
        /** @brief (씬 키, 파이프라인)의 렌더러를 찾고 없으면 만듭니다. 만들지 못하면 nullptr 입니다. */
        FrameRenderer* findOrCreate( FrameRenderer& mainRenderer, IRHIDevice* pDevice, uint32 sceneKey, string_view pipelinePath );

    private:
        vector<Entry>  _listEntry;
        atomic<uint32> _count;          ///< `_listEntry` 의 크기를 렌더 스레드가 공개한 값
        atomic<uint32> _nextCaptureKey; ///< `allocateCaptureSceneKey` 의 다음 번호(표시 비트 밖)

        SW_SERVICE_SLOT( OffscreenSceneRenderers, Optional );
    };
} // namespace sw

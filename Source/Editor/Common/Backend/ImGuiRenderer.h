/**
 * @file ImGuiRenderer.h
 * @brief 에디터 UI(Dear ImGui)를 RHI 로 그리는 렌더러입니다. 네 백엔드가 같은 코드(셰이더 `editor/shaders/imgui.hlsl`)를 지납니다.
 * @details 언리얼 Slate 가 제 RHI 렌더러(`FSlateRHIRenderer`)로, Godot 가 제 캔버스 렌더러로 UI 를 그리는 자리다. ImGui 의 렌더러 백엔드(`imgui_impl_dx11` …)를 쓰지 않는다 —
 *          그 넷은 백엔드마다 제 스왑체인 · 동기화 · 텍스처 등록을 따로 지어 같은 결함이 사본마다 숨었다(Vulkan 보조 창의 세마포어 재사용). 여기서는
 *          - 그리기: 프레임의 모든 그리기 목록(메인 · 캡처 · 보조 창)을 uint 구조 버퍼 하나에 한 번 올리고(`prepareFrame`), 명령마다 가위 · 루트 상수 · `draw` 를 기록한다(`render`).
 *          - 텍스처: ImGui 텍스처 ID 는 이 렌더러의 칸 번호다. 칸이 RHI 텍스처와 bindless 번호를 든다 — 번호 등록 · 반환은 렌더 스레드가 패스 기록 밖(present 훅)에서 한다
 *            (UI 스레드의 등록이 병렬 패스 기록과 겹치면 bindless 표가 바뀐다 — `requireRegistryMutableNow`). DX11 · GL 은 그 번호를 머티리얼 슬롯 서수로 건다(캔버스와 같다).
 *          - 보조 창: ImGui 플랫폼 창마다 RHI 스왑체인(`IRHIDevice::createSwapChain`)을 하나 두고, 렌더 스레드가 그 백버퍼에 그린다(`renderPlatformWindows`). 표시는 `endFrame` 이 한다.
 *          스레드: 텍스처 갱신과 그리기는 렌더 스레드(present 훅)에서 한다. UI 스레드는 그동안 다음 UI 프레임을 열지 않으므로(`DrawSnapshotHandoff`) ImGui 상태가 멈춰 있다.
 *          보조 창의 생성 · 크기 · 파괴는 ImGui 가 UI 스레드에서 부르고, 디바이스가 렌더 스레드와 GPU 를 비운 뒤에 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Backend/Model/EditorDrawReleaseQueue.h"

#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    class IRHIDevice;
} // namespace sw

struct ImDrawData;
struct ImDrawList;
struct ImTextureData;
struct ImGuiViewport;
struct ImVec2;

namespace sw::editor
{
    using RHITextureHandle = uint64;

    /**
     * @class ImGuiRenderer
     * @brief ImGui 그리기 목록 · 텍스처 · 보조 창을 RHI 로 다룹니다. `ImGuiEditor` 가 하나를 갖습니다.
     */
    class ImGuiRenderer
    {
    public:
        ImGuiRenderer();
        ~ImGuiRenderer();
        ImGuiRenderer( const ImGuiRenderer& )            = delete;
        ImGuiRenderer& operator=( const ImGuiRenderer& ) = delete;

        /**
         * @brief 디바이스에 붙고 PSO(백버퍼 포맷)를 만들며 ImGui 렌더러 칸(`BackendRendererUserData` · 텍스처 · 뷰포트 능력과 창 훅)을 겁니다. 렌더 스레드가 프레임을 기록하기 전에 부릅니다.
         * @return 셰이더 · PSO 를 만들지 못하면 이유를 남기고 false 입니다(다른 경로로 대신 그리지 않는다).
         */
        [[nodiscard]] bool initialize( IRHIDevice* pRHIDevice );
        /** @brief 보조 창을 부수고, 렌더 스레드와 GPU 를 기다려 미뤄 둔 해제를 끝낸 뒤 텍스처 · 버퍼 · PSO 를 놓습니다. */
        void shutdown();

        /**
         * @brief 이번 프레임에 그릴 그리기 목록들을 버퍼 하나에 올리고, 텍스처 칸(bindless 번호)과 ImGui 동적 텍스처(생성 · 갱신 · 파괴)를 처리합니다.
         * @details 렌더 스레드에서, 이 프레임의 `render` 앞에 한 번 부른다. 버퍼는 프레임에 한 번만 올린다(같은 프레임에 같은 버퍼를 두 번 올리지 않는다 — 캔버스와 같은 규칙).
         *          nullptr 항목은 건너뛴다.
         */
        void prepareFrame( IRHIDevice& device, const vector<ImDrawData*>& listDrawData );
        /** @brief @p drawData 를 지금 열린 렌더 패스에 그립니다. `prepareFrame` 이 이번 프레임에 올린 목록만 그린다(올리지 않은 목록은 오류를 남기고 건너뛴다). */
        void render( IRHIDevice& device, const ImDrawData& drawData );
        /** @brief 보조 창들의 그리기 목록을 더합니다(`prepareFrame` 에 메인 목록과 함께 넘긴다). 최소화했거나 스왑체인이 없는 창은 빠진다. */
        void collectPlatformWindowDrawData( vector<ImDrawData*>& inoutListDrawData ) const;
        /**
         * @brief 보조 창(메인 밖으로 끌어낸 패널)마다 그 창의 스왑체인 백버퍼에 그립니다. 렌더 스레드에서, 메인 그리기 뒤에 부릅니다.
         * @details 열린 패스를 닫고 창마다 패스를 열어 그린 뒤 닫는다. 이번 프레임에 그릴 수 없는 창(`IRHIDevice::acquireSwapChainImage` 가 false)은 건너뛴다.
         */
        void renderPlatformWindows( IRHIDevice& device );

        /** @brief 보조 창에 그린 누적 횟수입니다(시나리오 감시 값). */
        uint32 getPlatformWindowFrames() const { return _platformWindowFrames.load( std::memory_order_relaxed ); }

        /** @brief RHI 텍스처를 ImGui 텍스처 ID(칸 번호)로 등록합니다. UI 스레드에서 부릅니다. 번호 등록은 다음 `prepareFrame` 이 한다. 핸들이 0 이면 nullptr 입니다. */
        void* registerTexture( RHITextureHandle texture );
        /**
         * @brief `registerTexture` 로 발급한 ID 를 해제합니다. UI 스레드에서 부릅니다.
         * @details 이미 낸 draw 스냅샷이 그 ID 를 그릴 수 있으므로 칸 반환은 `getDrawReleaseQueue()` 에 맡긴다. ID 는 호출한 즉시 무효다(같은 UI 프레임에서 다시 그리지 않는다).
         */
        void unregisterTexture( void* pTextureID );

        /**
         * @brief UI 스레드에서 해제한 리소스(ImGui 텍스처와 그것이 가리키는 RHI 텍스처)의 해제 큐입니다.
         * @details 해제는 그 리소스를 그렸을 수 있는 마지막 프레임의 GPU 작업이 끝난 뒤에 실행된다(`EditorDrawReleaseQueue`). `ImGuiEditor` 가 스냅샷을 낼 때
         *          번호를 알리고, 렌더 스레드가 스냅샷을 그린 프레임에서 디바이스로 넘긴다.
         */
        EditorDrawReleaseQueue& getDrawReleaseQueue() { return _drawReleaseQueue; }

    private:
        /** @brief 그리기 목록 하나가 버퍼에서 시작하는 칸(uint 번호)입니다. */
        struct ListBase
        {
            uint32 _vertexWord{ 0 };
            uint32 _indexWord{ 0 };
        };
        /** @brief 텍스처 칸 하나입니다. ImGui 텍스처 ID 는 칸 번호 + 1 이다(0 은 ImGui 의 무효 ID). */
        struct TextureSlot
        {
            RHITextureHandle   _texture{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex }; ///< 렌더 스레드가 등록한 bindless 번호(아직이면 무효)
            bool               _bInUse{ false };
            bool               _bOwned{ false }; ///< ImGui 동적 텍스처라 RHI 텍스처도 이 렌더러가 만들고 놓는다
        };

        /** @brief 빈 칸을 잡습니다(`_textureMutex` 안). */
        uint32 acquireSlot( RHITextureHandle texture, bool bOwned );
        /** @brief 칸의 bindless 번호를 반환하고(만들었으면 텍스처도 놓고) 칸을 비웁니다. 그 칸을 그렸을 수 있는 프레임이 GPU 에서 끝난 뒤에 부릅니다. */
        void releaseSlot( IRHIDevice& device, uint32 slotIndex );
        /** @brief ImGui 1.92 동적 텍스처 하나를 처리합니다(생성 · 구간 갱신 · 미뤄 파괴). 렌더 스레드에서 부릅니다. */
        void updateTexture( IRHIDevice& device, ImTextureData& texture );
        /** @brief 그리기 명령의 텍스처 ID 를 이번 프레임의 bindless 번호로 풉니다. 아직 없으면 무효입니다. */
        RHIDescriptorIndex resolveTexture( uint64 textureID ) const;

        // ImGui 플랫폼 창 훅(UI 스레드) — 창마다 RHI 스왑체인 하나(`ImGuiViewport::RendererUserData`).
        static void createWindowSwapChain( ImGuiViewport* pViewport );
        static void destroyWindowSwapChain( ImGuiViewport* pViewport );
        static void resizeWindowSwapChain( ImGuiViewport* pViewport, ImVec2 size );

        IRHIDevice*                                _pDevice;
        RHIPipelineStateHandle                     _pso;                       ///< 백버퍼 포맷의 PSO(캡처 텍스처 · 보조 창도 같은 포맷이다)
        RHIStructuredBufferSlot                    _wordBuffer;                ///< 이번 프레임의 정점 · 인덱스(uint 칸)
        vector<uint32>                             _listWord;                  ///< 올릴 칸 모으기(프레임마다 비운다)
        unordered_map<const ImDrawList*, ListBase> _mapListBase;               ///< 이번 프레임에 올린 목록의 시작 칸
        vector<RHIDescriptorIndex>                 _listFrameSRV;              ///< 이번 프레임의 칸별 bindless 번호(그리기가 잠금 없이 읽는다 — `prepareFrame` 이 채운다)
        bool                                       _bNativeBindless;           ///< DX12 · Vulkan 이면 bindless 번호로, 아니면 서수로 텍스처를 건다
        atomic<uint32>                             _platformWindowFrames{ 0 }; ///< 보조 창에 그린 횟수(감시 값 `Editor.PlatformWindowFrames`) — 렌더 스레드가 올리고 게임 스레드가 읽는다

        mutable mutex                        _textureMutex; ///< 칸 표 — UI 스레드(등록 · 해제 예약)와 렌더 스레드(번호 등록 · 반환)가 함께 쓴다
        vector<TextureSlot> _listTextureSlot SW_GUARDED_BY( _textureMutex );
        vector<uint32> _listFreeSlot         SW_GUARDED_BY( _textureMutex );
        EditorDrawReleaseQueue               _drawReleaseQueue;
    };
} // namespace sw::editor

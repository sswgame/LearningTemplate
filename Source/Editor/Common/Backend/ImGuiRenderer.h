/**
 * @file ImGuiRenderer.h
 * @brief 에디터 UI(Dear ImGui)를 RHI 로 그리는 렌더러입니다. 네 백엔드가 같은 코드(셰이더 `editor/shaders/imgui.hlsl`)를 지납니다.
 * @details 언리얼 Slate 가 제 RHI 렌더러(`FSlateRHIRenderer`)로, Godot 가 제 캔버스 렌더러로 UI 를 그리는 자리다. ImGui 의 렌더러 백엔드(`imgui_impl_dx11` …)를 쓰지 않는다 —
 *          그 넷은 백엔드마다 제 스왑체인 · 동기화 · 텍스처 등록을 따로 지어 같은 결함이 사본마다 숨었다(Vulkan 보조 창의 세마포어 재사용). 여기서는
 *          - 그리기: 프레임의 모든 그리기 목록(메인 · 캡처 · 보조 창)을 uint 구조 버퍼 하나에 한 번 올리고(`prepareFrame`), 명령마다 가위 · 루트 상수 · `draw` 를 기록한다(`render`).
 *          - 텍스처: ImGui 텍스처 ID 는 이 렌더러의 칸 번호다. 칸이 RHI 텍스처와 bindless 번호를 든다 — 번호 등록 · 반환은 렌더 스레드가 패스 기록 밖(present 훅)에서 한다
 *            (UI 스레드의 등록이 병렬 패스 기록과 겹치면 bindless 표가 바뀐다 — `requireRegistryMutableNow`). DX11 · GL 은 그 번호를 머티리얼 슬롯 서수로 건다(캔버스와 같다).
 *          - 보조 창: ImGui 플랫폼 창마다 RHI 스왑체인(`IRHIDevice::createSwapChain`)을 하나 두고, 렌더 스레드가 그 백버퍼에 그린다(`renderPlatformWindows`). 표시는 `endFrame` 이 한다.
 *          스레드: 렌더 스레드는 살아 있는 ImGui 상태를 읽지 않는다 — UI 스레드가 다음 UI 프레임을 만드는 동안 그린다(`DrawSnapshotHandoff`). 그리기 목록 · 보조 창은 스냅샷
 *          (`EditorDrawDataSnapshot`)으로, ImGui 동적 텍스처(글꼴 아틀라스)의 생성 · 갱신 · 파괴는 UI 스레드가 픽셀을 복사해 낸 연산(`queueTextureUpdates`)으로 받는다.
 *          보조 창의 생성 · 크기 · 파괴는 ImGui 가 UI 스레드에서 부르고, 낸 스냅샷이 다 그려진 뒤(`DrawSnapshotHandoff::waitForIdle`) 디바이스가 렌더 스레드와 GPU 를 비우고 한다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Backend/Model/EditorDrawReleaseQueue.h"

#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"
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
    class DrawSnapshotHandoff;
    class EditorDrawDataSnapshot;

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
         * @param drawHandoff 스냅샷을 렌더 스레드에 넘기는 약속입니다. 보조 창의 스왑체인을 부수거나 바꾸기 전에 낸 스냅샷이 다 그려지기를 기다린다(렌더러보다 오래 산다).
         * @return 셰이더 · PSO 를 만들지 못하면 이유를 남기고 false 입니다(다른 경로로 대신 그리지 않는다).
         */
        [[nodiscard]] bool initialize( IRHIDevice* pRHIDevice, const DrawSnapshotHandoff& drawHandoff );
        /** @brief 보조 창을 부수고, 렌더 스레드와 GPU 를 기다려 미뤄 둔 해제를 끝낸 뒤 텍스처 · 버퍼 · PSO 를 놓습니다. */
        void shutdown();

        /**
         * @brief ImGui 동적 텍스처(글꼴 아틀라스)의 생성 · 갱신 · 파괴 요청을 연산으로 옮겨 담고 ImGui 쪽 상태를 끝냅니다(UI 스레드, `ImGui::Render` 뒤 스냅샷을 내기 전).
         * @details 픽셀은 지금 복사한다 — 다음 UI 프레임이 아틀라스를 키우거나 고치는 동안 렌더 스레드가 같은 버퍼를 읽지 않게. 새 텍스처의 칸 번호(ImGui 텍스처 ID)도 지금 정한다.
         *          그 ID 를 그리는 스냅샷은 이 뒤에 나가므로 렌더 스레드는 연산을 먼저 적용한 뒤 그린다(`prepareFrame`). 파괴는 그 텍스처를 그렸을 수 있는 스냅샷이 다 지나간 뒤에 낸다.
         */
        void queueTextureUpdates();
        /**
         * @brief 이번 프레임에 그릴 그리기 목록들을 버퍼 하나에 올리고, UI 스레드가 낸 텍스처 연산과 텍스처 칸(bindless 번호)을 처리합니다.
         * @details 렌더 스레드에서, **렌더 패스 밖**(`IEditor::prepareRender`)에서 이 프레임의 `render` 앞에 한 번 부른다 — 열린 패스 안에서 올리면 Vulkan 이 패스를 닫아
         *          그 뒤의 그리기가 버려진다. 버퍼는 프레임에 한 번만 올린다(같은 프레임에 같은 버퍼를 두 번 올리지 않는다 — 캔버스와 같은 규칙). nullptr 항목은 건너뛴다.
         */
        void prepareFrame( IRHIDevice& device, const vector<ImDrawData*>& listDrawData );
        /** @brief @p drawData 를 지금 열린 렌더 패스에 그립니다. `prepareFrame` 이 이번 프레임에 올린 목록만 그린다(올리지 않은 목록은 오류를 남기고 건너뛴다). */
        void render( IRHIDevice& device, const ImDrawData& drawData );
        /**
         * @brief 스냅샷의 보조 창(메인 밖으로 끌어낸 패널)마다 그 창의 스왑체인 백버퍼에 그립니다. 렌더 스레드에서, 메인 그리기 뒤에 부릅니다.
         * @details 열린 패스를 닫고 창마다 패스를 열어 그린 뒤 닫는다. 이번 프레임에 그릴 수 없는 창(`IRHIDevice::acquireSwapChainImage` 가 false)은 건너뛴다.
         */
        void renderPlatformWindows( IRHIDevice& device, EditorDrawDataSnapshot& snapshot );

        /** @brief 보조 창에 그린 누적 횟수입니다(시나리오 감시 값). */
        uint32 getPlatformWindowFrames() const { return _platformWindowFrames.load( std::memory_order_relaxed ); }

        /** @brief RHI 텍스처를 ImGui 텍스처 ID(칸 번호)로 등록합니다. UI 스레드에서 부릅니다. 번호 등록은 다음 `prepareFrame` 이 한다. 핸들이 0 이면 nullptr 입니다. */
        void* registerTexture( RHITextureHandle texture );
        /**
         * @brief `registerTexture` 로 발급한 ID 를 해제합니다. UI 스레드에서 부릅니다.
         * @details 이미 낸 draw 스냅샷이 그 ID 를 그릴 수 있으므로 칸 반환은 `getDrawReleaseQueue()` 에 맡긴다 — 바로 비우면 그 칸을 새 등록이 받아 앞 스냅샷이 다른 텍스처를 그린다.
         *          ID 는 호출한 즉시 무효다(같은 UI 프레임에서 다시 그리지 않는다).
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
            uint32 _indexHalf{ 0 }; ///< 첫 인덱스의 16비트 칸 번호(uint 칸 번호 × 2)
        };
        /** @brief 텍스처 칸 하나입니다. ImGui 텍스처 ID 는 칸 번호 + 1 이다(0 은 ImGui 의 무효 ID). */
        struct TextureSlot
        {
            RHITextureHandle   _texture{ 0 };
            RHIDescriptorIndex _srv{ kInvalidDescriptorIndex }; ///< 렌더 스레드가 등록한 bindless 번호(아직이면 무효)
            bool               _bInUse{ false };
            bool               _bOwned{ false }; ///< ImGui 동적 텍스처라 RHI 텍스처도 이 렌더러가 만들고 놓는다
        };
        /** @brief UI 스레드가 낸 ImGui 동적 텍스처 연산 하나입니다(`queueTextureUpdates`). 픽셀은 RGBA8 로 넓혀 복사해 두었다. */
        struct TextureOp
        {
            enum class Kind : uint8
            {
                Create,  ///< 칸 `_slot` 에 `_width` × `_height` 텍스처를 만들어 `_listByte` 를 올린다
                Update,  ///< 칸 `_slot` 텍스처의 (`_x`, `_y`) 구간에 `_listByte` 를 올린다
                Destroy, ///< 칸 `_slot` 을 비운다(텍스처 · 번호는 GPU 사용 뒤 놓인다)
            };
            Kind          _kind{ Kind::Create };
            uint32        _slot{ 0 };
            uint32        _x{ 0 };
            uint32        _y{ 0 };
            uint32        _width{ 0 };
            uint32        _height{ 0 };
            vector<uint8> _listByte;
        };

        /** @brief 빈 칸을 잡습니다(`_textureMutex` 안). */
        uint32 acquireSlot( RHITextureHandle texture, bool bOwned ) SW_REQUIRES_LOCK( _textureMutex );
        /** @brief 칸을 비웁니다. 이 렌더러가 만든 텍스처면 번호와 텍스처도 놓습니다(렌더 스레드). 패널 텍스처의 번호는 텍스처 주인의 파괴가 놓는다. */
        void releaseSlot( IRHIDevice& device, uint32 slotIndex );
        /** @brief 텍스처 연산 하나를 적용합니다(렌더 스레드, 패스 기록 밖). */
        void applyTextureOp( IRHIDevice& device, TextureOp& op );
        /** @brief 보조 창의 스왑체인을 만들거나 바꾸거나 부수기 전에 낸 스냅샷이 다 그려지기를 기다립니다(UI 스레드, ImGui 창 훅). */
        void waitDrawnBeforeWindowChange() const;
        /** @brief 그리기 명령의 텍스처 ID 를 이번 프레임의 bindless 번호로 풉니다. 아직 없으면 무효입니다. */
        RHIDescriptorIndex resolveTexture( uint64 textureID ) const;

        // ImGui 플랫폼 창 훅(UI 스레드) — 창마다 RHI 스왑체인 하나(`ImGuiViewport::RendererUserData`).
        static void createWindowSwapChain( ImGuiViewport* pViewport );
        static void destroyWindowSwapChain( ImGuiViewport* pViewport );
        static void resizeWindowSwapChain( ImGuiViewport* pViewport, ImVec2 size );

    private:
        IRHIDevice*                                _pDevice;
        const DrawSnapshotHandoff*                 _pDrawHandoff;    ///< 보조 창 스왑체인을 바꾸기 전에 낸 스냅샷이 다 그려지기를 기다린다
        RHIPipelineStateHandle                     _pso;             ///< 백버퍼 포맷의 PSO(캡처 텍스처 · 보조 창도 같은 포맷이다)
        RHIStructuredBuffer                        _wordBuffer;      ///< 이번 프레임의 정점 · 인덱스(uint 칸)
        vector<uint32>                             _listWord;        ///< 올릴 칸 모으기(프레임마다 비운다)
        unordered_map<const ImDrawList*, ListBase> _mapListBase;     ///< 이번 프레임에 올린 목록의 시작 칸
        vector<RHIDescriptorIndex>                 _listFrameSRV;    ///< 이번 프레임의 칸별 bindless 번호(그리기가 잠금 없이 읽는다 — `prepareFrame` 이 채운다)
        bool                                       _bNativeBindless; ///< DX12 · Vulkan 이면 bindless 번호로, 아니면 서수로 텍스처를 건다
        /// @brief 패널이 등록한(이 렌더러가 만들지 않은) 텍스처마다 한 번 받은 bindless 번호입니다(렌더 스레드 전용). 번호는 텍스처 주인의 파괴가 놓는다 — 이 렌더러는
        ///        해제하지 않는다(늦은 해제가 다시 발급된 번호를 놓지 않게). 핸들은 세대가 있어 다시 쓰이지 않으므로 사라진 텍스처의 항목은 읽히지 않는다.
        unordered_map<RHITextureHandle, RHIDescriptorIndex> _mapForeignSRV;
        atomic<uint32>                                      _platformWindowFrames{ 0 }; ///< 보조 창에 그린 횟수(감시 값 `Editor.PlatformWindowFrames`) — 렌더 스레드가 올리고 게임 스레드가 읽는다

        mutable mutex                        _textureMutex; ///< 칸 표 — UI 스레드(등록 · 해제 예약)와 렌더 스레드(번호 등록 · 반환)가 함께 쓴다
        vector<TextureSlot> _listTextureSlot SW_GUARDED_BY( _textureMutex );
        vector<uint32> _listFreeSlot         SW_GUARDED_BY( _textureMutex );
        vector<TextureOp> _listTextureOp     SW_GUARDED_BY( _textureMutex ); ///< UI 스레드가 낸 텍스처 연산(낸 순서) — 렌더 스레드의 `prepareFrame` 이 비운다
        vector<TextureOp>                    _listApplyingOp;                ///< 렌더 스레드가 적용 중인 연산(잠금 밖에서 쓰려고 바꿔 받는다 — 저장소를 다시 쓴다)
        EditorDrawReleaseQueue               _drawReleaseQueue;
    };
} // namespace sw::editor

/**
 * @file RenderDebuggerViewModel.h
 * @brief 렌더 디버거 창의 ViewModel — 프레임 디버거(이벤트 목록 · 멈춤 지점 · 바인딩), 렌더 그래프 그림(사본 · 배치 · 패스 GPU 시간), 버퍼 보기(목록 · 읽기 · 표 배치),
 *        가림 · 스트리밍(타일 요약 · 스트리밍 카운터)을 모읍니다. ImGui 를 모릅니다(`docs/agents/EditorUI.md` MVVM 계약).
 * @details 값은 `RenderDebugRegistry` 의 공개 번호가 바뀔 때만 다시 복사한다. 원천(우편함 · 프로파일러 · 가림 사본)은 `RenderDebuggerSources` 로 받아 단위 테스트가 가짜 원천을 넘긴다.
 *          언리얼 RDG Insights · 유니티 Frame Debugger 와 Rendering Debugger 를 한 창의 탭으로 모은 자리다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Panels/RenderDebugger/Model/BufferViewLayout.h"
#include "Editor/Panels/RenderDebugger/Model/RenderGraphLayout.h"

#include "Engine/Renderer/Debug/OcclusionDebugStats.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"

namespace sw
{
    class FrameProfiler;
} // namespace sw

namespace sw::editor
{
    /** @brief 창의 탭입니다. */
    enum class RenderDebuggerTab : uint8
    {
        FrameDebugger,
        RenderGraph,
        Buffers,
        Occlusion,
        Count,
    };

    /** @brief ViewModel 이 읽는 원천입니다. 없는 것은 nullptr(그 탭은 빈 상태로 보인다). */
    struct RenderDebuggerSources
    {
        RenderDebugRegistry*                    _pRegistry{ nullptr };
        FrameProfiler*                          _pProfiler{ nullptr }; ///< 패스 GPU 시간(`GPU.<패스>`) · 스트리밍 카운터
        shared_ptr<const OcclusionTileSnapshot> _pOcclusion{};         ///< 가림 타일 CPU 사본(`FrameRenderer::getOcclusionSnapshot`)
    };

    /** @brief 텍스처 스트리밍 카운터(`RT.TextureStreaming.*` · `RT.TextureUpload.waiting`)의 마지막 프레임 값입니다. */
    struct RenderStreamingCounters
    {
        uint64 _textureCount{ 0 };     ///< 스트리밍하는 텍스처
        uint64 _belowWantedCount{ 0 }; ///< 원하는 밉보다 거친 밉만 올라온 텍스처
        uint64 _missingMipCount{ 0 };  ///< 그 밉 차의 합
        uint64 _uploadsWaiting{ 0 };   ///< 프레임 상한에 밀린 업로드
        uint64 _residentBytes{ 0 };
        uint64 _poolBytes{ 0 };
        uint64 _bytesInFlight{ 0 };
    };

    /** @brief 창이 한 프레임 그릴 값입니다. */
    struct RenderDebuggerState
    {
        // 프레임 디버거
        FrameDebuggerSettings       _frameDebugger{};
        vector<FrameDebugDrawEvent> _listEvent{};
        optional<uint32>            _stopEvent{}; ///< 멈춤 지점의 기록 번호(없으면 전체 프레임)
        // 렌더 그래프
        vector<RenderGraphSnapshot> _listGraph{};
        RenderGraphLayout           _graphLayout{};          ///< 고른 그래프의 배치
        vector<float32>             _listPassMilliseconds{}; ///< 고른 그래프의 패스마다 GPU 시간(재지 않았으면 음수)
        uint32                      _graphIndex{ 0 };
        uint32                      _layoutSerial{ 0 }; ///< 배치를 다시 만들 때마다 는다 — 창이 노드를 다시 놓을지 안다
        // 버퍼
        vector<RenderBufferEntry> _listBuffer{};
        RenderBufferContent       _bufferContent{};
        BufferViewLayout          _bufferLayout{};
        optional<uint32>          _selectedBuffer{};
        uint8                     _bBufferReading{ SW_FALSE }; ///< 고른 버퍼를 읽는 중이다
        // 가림 · 스트리밍
        OcclusionTileStats                      _occlusion{};
        shared_ptr<const OcclusionTileSnapshot> _pOcclusion{};
        RenderStreamingCounters                 _streaming{};
        RenderDebuggerTab                       _tab{ RenderDebuggerTab::FrameDebugger };
    };

    /** @brief 렌더 디버거 창의 ViewModel 입니다. */
    class RenderDebuggerViewModel
    {
    public:
        /** @brief 에디터가 든 원천(우편함 · 프로파일러 · 렌더러의 가림 사본)을 찾습니다. 창(View)은 모델 · 렌더러 헤더를 넣지 않으므로 여기서 찾는다. */
        static RenderDebuggerSources findEditorSources();
        /**
         * @brief 원천에서 값을 맞추고 창이 그릴 값을 돌려줍니다(프레임마다 한 번). 명령은 이때 받은 우편함으로 간다.
         * @details 목록(기록 · 그래프 · 버퍼)은 공개 번호가 바뀐 프레임에만 다시 복사하므로 사본을 넘기지 않고 이 객체가 든 값을 읽게 한다 — 다음 `gather` 까지 유효하다.
         */
        const RenderDebuggerState& gather( const RenderDebuggerSources& sources );

        /** @brief 탭을 고릅니다(그래프 탭을 처음 열면 사본을, 버퍼 탭을 열면 목록을 요청한다). */
        void selectTab( RenderDebuggerTab tab );
        /** @brief 프레임 디버거를 켜거나 끕니다. 켜면 전체 프레임을 기록한다(멈춤 없음). */
        void setFrameDebuggerEnabled( bool bEnabled );
        /** @brief 멈춤 지점을 기록 안에서 @p delta 만큼 옮깁니다(`RenderDebugRegistry::stepStop`). */
        void stepFrameDebugger( int32 delta );
        /** @brief 기록 @p eventIndex 를 멈춤 지점으로 고릅니다. */
        void selectEvent( uint32 eventIndex );
        /** @brief 그래프 사본을 다시 요청합니다. */
        void requestGraphCapture();
        /** @brief 그릴 그래프(뷰 · 프레임)를 고릅니다. */
        void selectGraph( uint32 graphIndex );
        /** @brief 버퍼 목록을 다시 요청합니다. */
        void requestBufferList();
        /** @brief 목록의 @p bufferIndex 버퍼를 고르고 읽기를 요청합니다. */
        void selectBuffer( uint32 bufferIndex );
        /** @brief 고른 버퍼를 다시 읽습니다. */
        void refreshBuffer();
        /** @brief 칸 해석을 정합니다. */
        void setBufferScalar( BufferViewScalar scalar );

    private:
        /** @brief 우편함의 번호가 바뀐 것만 다시 복사합니다. */
        void syncRegistry();
        /** @brief 고른 그래프의 배치와 패스 GPU 시간을 다시 만듭니다. */
        void rebuildGraphLayout();

    private:
        /** @brief 읽은 버퍼의 원소 멤버입니다(목록 항목 — 모르면 비어 있다). */
        vector<RenderBufferEntry::Member> findSelectedMembers() const;

    private:
        RenderDebuggerSources _sources{};
        RenderDebuggerState   _state{};
        uint64                _eventSerial{ 0 };
        uint64                _graphSerial{ 0 };
        uint64                _bufferListSerial{ 0 };
        uint32                _pendingBufferSerial{ 0 }; ///< 기다리는 읽기 요청 번호(0 이면 없음)
        BufferViewScalar      _scalar{ BufferViewScalar::Shader };
        uint8                 _bGraphRequested{ SW_FALSE };
        uint8                 _bBufferListRequested{ SW_FALSE };
    };
} // namespace sw::editor

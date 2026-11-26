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
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"

#include "Engine/Renderer/Debug/OcclusionDebugStats.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Renderer/Graph/RenderGraphSnapshot.h"

// 렌더 그래프 사본(`RenderGraphSnapshot`)을 노드 그래프 배치로 옮깁니다 — 레벨 = 열, 같은 레벨의 패스 = 행, 쓴 패스 → 읽는 패스 = 링크(언리얼 RDG Insights 의 패스 그림). ImGui 를 모른다(MVVM — 창은 이 값으로 `EditorNodeGraph` 에 노드 · 핀 · 링크를 그린다). 노드 id · 핀 id 는 이 배치가 정해 링크와 어긋나지 않는다.
// 구조 버퍼 바이트를 표 행으로 푸는 규칙입니다 — 원소 보폭(등록 크기 `RenderBufferEntry::_elementSize`)마다 한 행, 4 바이트 칸마다 한 열(언리얼 RDG Insights 의 버퍼 보기 · RenderDoc Buffer Viewer 의 형식 지정 자리). 칸의 해석은 버퍼를 거는 컴퓨트 셰이더의 멤버(`RenderBufferEntry::_listMember`)이고, 모르거나 다르게 보려면 사람이 고른다(실수 · 부호 없는 정수 · 정수 · 16 진수).
namespace sw::editor
{
    /** @brief 4 바이트 칸 하나를 어떻게 읽을지입니다. */
    enum class BufferViewScalar : uint8
    {
        Shader, ///< 그 버퍼를 거는 컴퓨트 셰이더의 멤버 형식(모르는 칸은 16 진수)
        Float,  ///< float32(HZB 깊이 · 가림 타일)
        Uint,   ///< uint32(간접 인자 · 번호)
        Int,    ///< int32
        Hex,    ///< 날 비트(16 진수)
        Count,
    };

    /**
     * @struct BufferViewLayout
     * @brief 파일 머리말 참고.
     */
    struct BufferViewLayout
    {
        static constexpr uint32 kCellBytes      = 4;    ///< 칸 하나의 바이트
        static constexpr uint32 kMaxColumnCount = 16;   ///< 표가 보이는 칸 수의 위 한계(보폭 64 B) — 넘는 칸은 자른다
        static constexpr uint32 kMaxRowCount    = 4096; ///< 한 번에 표로 만드는 행 수의 위 한계(큰 피라미드를 통째로 글로 만들지 않는다)

        uint32                   _stride{ 0 };      ///< 원소 바이트(0 이면 풀 수 없다)
        uint32                   _columnCount{ 0 }; ///< 보이는 칸 수
        BufferViewScalar         _scalar{ BufferViewScalar::Shader };
        vector<string>           _listColumnName;   ///< 칸마다의 이름 — 멤버를 알면 `멤버.x`, 모르면 바이트 오프셋(`+4`)
        vector<BufferViewScalar> _listColumnScalar; ///< `Shader` 로 볼 때 칸마다의 해석

        /** @brief 원소 @p elementSize 바이트 · 칸 해석 @p scalar 의 배치입니다. 4 의 배수가 아닌 끝 바이트는 칸이 되지 못한다. */
        static BufferViewLayout make( uint32 elementSize, BufferViewScalar scalar, const vector<RenderBufferEntry::Member>& listMember = {} );
        /** @brief @p byteCount 바이트에 든 행 수입니다(`kMaxRowCount` 에서 자른다). */
        uint32 getRowCount( size_t byteCount ) const;
        /** @brief 행 @p row 의 칸을 글로 @p outListCell 에 채웁니다(버퍼를 다시 쓴다). 행이 바이트 밖이면 false 입니다. */
        [[nodiscard]] bool formatRow( const vector<uint8>& byte, uint32 row, vector<string>& outListCell ) const;
        /** @brief 칸 해석의 표시 이름입니다("float" · "uint" · "int" · "hex"). */
        static const utf8* getScalarLabel( BufferViewScalar scalar );
    };
} // namespace sw::editor

namespace sw::editor
{
    /** @brief 패스 노드 하나입니다. */
    struct RenderGraphLayoutNode
    {
        float2 _position{};  ///< 캔버스 좌표(왼쪽 위)
        uint32 _pass{ 0 };   ///< `RenderGraphSnapshot::_listPass` 번호
        uint32 _column{ 0 }; ///< 레벨
        uint32 _row{ 0 };    ///< 레벨 안 차례
    };

    /** @brief 링크 하나입니다 — 쓴 패스에서 그 자원을 다음에 읽는 패스로. 같은 두 패스 사이는 한 번만 잇는다(첫 자원을 이름으로 단다). */
    struct RenderGraphLayoutLink
    {
        uint32 _fromPass{ 0 };
        uint32 _toPass{ 0 };
        uint32 _resource{ 0 }; ///< `RenderGraphSnapshot::_listResource` 번호
    };

    /**
     * @struct RenderGraphLayout
     * @brief 파일 머리말 참고.
     */
    struct RenderGraphLayout
    {
        static constexpr float32 kColumnSpacing = 280.0f; ///< 열 사이(캔버스 px) — 노드 폭(패스 이름 · 전이 · GPU 시간 줄)보다 넓게
        static constexpr float32 kRowSpacing    = 110.0f; ///< 행 사이
        static constexpr int32   kNodeIDBase    = 100000; ///< 노드 id 의 시작 — 링크 id(1 부터)와 겹치지 않게
        static constexpr int32   kIDsPerNode    = 4;      ///< 노드 하나가 쓰는 id 수(노드 · 입력 핀 · 출력 핀)

        vector<RenderGraphLayoutNode> _listNode; ///< 실행 순서(패스 번호 순)
        vector<RenderGraphLayoutLink> _listLink;
        uint32                        _columnCount{ 0 };

        /** @brief @p snapshot 의 배치를 만듭니다. */
        static RenderGraphLayout make( const RenderGraphSnapshot& snapshot );

        /** @brief 패스 @p pass 노드의 id 입니다. */
        static int32 getNodeID( uint32 pass ) { return kNodeIDBase + static_cast<int32>( pass ) * kIDsPerNode; }
        /** @brief 패스 @p pass 의 입력 핀 id 입니다. */
        static int32 getInputPinID( uint32 pass ) { return getNodeID( pass ) + 1; }
        /** @brief 패스 @p pass 의 출력 핀 id 입니다. */
        static int32 getOutputPinID( uint32 pass ) { return getNodeID( pass ) + 2; }
        /** @brief 링크 @p link 번호의 id 입니다(1 부터). */
        static int32 getLinkID( uint32 link ) { return static_cast<int32>( link ) + 1; }
    };
} // namespace sw::editor

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

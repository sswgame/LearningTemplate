/**
 * @file RenderDebugRegistry.h
 * @brief 렌더링 런타임 디버그의 우편함입니다 — UI(에디터 렌더 디버거 창 · 개발 명령 · HUD)가 요청하고 렌더 스레드가 프레임 경계에서 처리해 값으로 공개합니다.
 * @details 넷을 나른다: 렌더 그래프 사본(`RenderGraphSnapshot`), 프레임 디버거(멈춤 지점 설정 · 드로우 기록 — 유니티 Frame Debugger), 프레임 통계(드로우 · 삼각형 상한),
 *          그래프 밖 버퍼 읽기(`FrameBufferTable` 등록분 — `RenderBufferInspector` 가 렌더 쪽에서 읽는다). `RenderTargetRegistry` 와 같은 꼴이다: 값만 복사해 넘기고
 *          공개 번호(`get…Serial`)로 다시 읽을지 안다. 렌더러는 호스트(`FrameRendererHost::_pRenderDebug`)로만 이것을 받아 화면 밖 렌더러는 기록하지 않는다.
 *          렌더러 쪽 훅은 배포 구성(`SW_SHIPPING`)에서 빠진다 — 그때 이 객체는 아무도 채우지 않는다.
 *          설계: DesignBacklog "렌더링 런타임 디버그(설계 2026-11-20)".
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Renderer/Graph/RenderGraphSnapshot.h"

namespace sw
{
    class ShaderBindingLayout;

    /** @brief 프레임 디버거의 멈춤 지점입니다. 주 시점의 씬 드로우에만 듣는다(렌더 타깃 창이 보는 풀과 같다). */
    struct FrameDebuggerSettings
    {
        string _stopPass;             ///< 멈출 패스(주 시점 뷰 그래프의 패스 이름). 그래프에 없으면 아무것도 건너뛰지 않는다
        uint32 _stopDraw{ 0 };        ///< 그 패스 안에서 이 번호(0 부터)의 드로우까지 그린다
        uint8  _bEnabled{ SW_FALSE }; ///< 꺼져 있으면 기록도 건너뛰기도 없다
    };

    /** @brief 드로우 하나의 기록입니다(값만 — 다른 스레드로 복사한다). */
    struct FrameDebugDrawEvent
    {
        string         _passName;
        vector<string> _listBinding;          ///< PSO 의 바인딩(리플렉션 이름 · 레지스터 · 종류 · 구조 보폭 · CB 멤버) 한 줄씩
        uint64         _pso{ 0 };             ///< 건 PSO 핸들
        uint64         _triangleCount{ 0 };   ///< 삼각형 상한(인덱스 / 3 × 가시 칸 — GPU 컬링 전의 값)
        uint32         _drawIndex{ 0 };       ///< 그 패스 안 번호
        uint32         _passOrder{ 0 };       ///< 그 패스의 실행 순서(주 시점 뷰 그래프에 없는 패스는 `MathUtil::kMaxUInt32` — 목록 끝)
        uint32         _indexCount{ 0 };      ///< 묶음 머리 배치의 인덱스 수
        uint32         _groupCount{ 0 };      ///< 멀티 드로우 한 번에 묶인 배치 수
        uint8          _bSkipped{ SW_FALSE }; ///< 멈춤 지점 뒤라 그리지 않았다
    };

    /** @brief 주 시점 씬 드로우의 프레임 통계입니다. */
    struct RenderFrameStats
    {
        uint64 _drawCount{ 0 };     ///< 낸 드로우(멀티 드로우는 한 번)
        uint64 _triangleCount{ 0 }; ///< 삼각형 상한의 합
        uint64 _serial{ 0 };        ///< 공개 번호(0 이면 아직 없다)
    };

    /** @brief 그래프 밖 버퍼 하나입니다(`FrameBufferTable` 등록 — 늘 크기가 있다). */
    struct RenderBufferEntry
    {
        string _name;
        uint32 _slot{ 0 };         ///< 뷰 칸(주 시점 0)
        uint32 _elementSize{ 0 };  ///< 원소 바이트(표는 크기 있는 등록만 받는다)
        uint32 _elementCount{ 0 }; ///< 원소 수
    };

    /** @brief 버퍼 하나를 CPU 로 읽은 결과입니다. */
    struct RenderBufferContent
    {
        RenderBufferEntry _entry{};
        vector<uint8>     _byte{};
        uint32            _serial{ 0 }; ///< 요청 번호(`requestBufferRead` 가 돌려준 값)
    };

    /**
     * @class RenderDebugRegistry
     * @brief 파일 머리말 참고. 요청 · 설정은 아무 스레드, `take…` · `publish…` · `beginFrame` · `allowDraw` · `endFrame` 은 렌더 스레드입니다.
     */
    class SW_API RenderDebugRegistry : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName      = "RenderDebugRegistry";
        static constexpr MemoryTag   kSubsystemMemoryTag = MemoryTag::RenderCPU;
        /** @brief HUD 가 통계를 한 번 원하면 렌더러가 이만큼의 프레임 동안 센다(HUD 는 4 Hz 로 다시 원한다). */
        static constexpr uint32 kStatsWantedFrames = 60;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        RenderDebugRegistry()                                        = default;
        ~RenderDebugRegistry() override                              = default;
        RenderDebugRegistry( const RenderDebugRegistry& )            = delete;
        RenderDebugRegistry& operator=( const RenderDebugRegistry& ) = delete;

        // ------------------------------------------------------------------------------
        // 1) 렌더 그래프 사본
        // ------------------------------------------------------------------------------
        /** @brief 다음 프레임에 그래프 사본을 만들어 달라고 요청합니다. */
        void requestGraphCapture() { _bGraphCaptureRequested.store( SW_TRUE, std::memory_order_release ); }
        /** @brief 요청이 있으면 꺼내고 true 입니다(렌더 스레드). */
        [[nodiscard]] bool takeGraphCaptureRequest() { return _bGraphCaptureRequested.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE; }
        /** @brief 사본을 공개합니다(렌더 스레드). */
        void publishGraphs( vector<RenderGraphSnapshot> listGraph );
        /** @brief 공개할 때마다 느는 번호입니다(0 이면 아직 없다). */
        uint64 getGraphSerial() const { return _graphSerial.load( std::memory_order_acquire ); }
        /** @brief 공개한 사본을 복사합니다. */
        void copyGraphs( vector<RenderGraphSnapshot>& outList ) const;

        // ------------------------------------------------------------------------------
        // 2) 프레임 디버거 · 프레임 통계
        // ------------------------------------------------------------------------------
        /** @brief 멈춤 지점을 정합니다(다음 프레임부터). */
        void setFrameDebugger( const FrameDebuggerSettings& settings );
        /** @brief 지금 멈춤 지점의 사본입니다. */
        FrameDebuggerSettings getFrameDebugger() const;
        /** @brief 드로우 기록을 공개할 때마다 느는 번호입니다. */
        uint64 getDrawEventSerial() const { return _drawEventSerial.load( std::memory_order_acquire ); }
        /** @brief 마지막으로 공개한 한 프레임의 드로우 기록을 복사합니다(패스 실행 순서 · 패스 안 번호 순). */
        void copyDrawEvents( vector<FrameDebugDrawEvent>& outList ) const;
        /** @brief 통계를 원합니다 — 렌더러가 `kStatsWantedFrames` 프레임 동안 센다. */
        void requestStats() { _statsWantedFrames.store( kStatsWantedFrames, std::memory_order_release ); }
        /** @brief 마지막으로 공개한 통계입니다. */
        RenderFrameStats getStats() const;

        /**
         * @brief 프레임을 시작합니다(렌더 스레드, 주 시점의 씬 기록 앞). @p listPassOrder 는 주 시점 뷰 그래프의 실행 순서다 — 멈춤 패스 뒤의 패스를 가른다.
         * @details 설정을 이 프레임의 사본으로 옮기고 패스마다의 드로우 번호를 비운다. 기록할 일이 없으면(꺼짐 · 통계를 원하지 않음) `isRecording` 이 거짓이다.
         */
        void beginFrame( const vector<hashed_string>& listPassOrder );
        /** @brief 이 프레임에 드로우를 기록하는가(렌더 스레드). 거짓이면 `allowDraw` 를 부를 까닭이 없다. */
        bool isRecording() const { return _bRecording.load( std::memory_order_relaxed ) != SW_FALSE; }
        /**
         * @brief 주 시점 씬 드로우 하나를 기록하고 그려도 되는지 답합니다(렌더 스레드 — 병렬 기록 워커에서도 부른다. 패스 하나는 한 스레드가 기록한다).
         * @return 멈춤 지점 뒤면 false — 부르는 쪽은 그 드로우를 내지 않는다.
         */
        [[nodiscard]] bool allowDraw( hashed_string passName, uint64 pso, const ShaderBindingLayout* pLayout, uint32 indexCount, uint32 groupCount, uint64 triangleCount );
        /** @brief 프레임을 끝내고 기록 · 통계를 공개합니다(렌더 스레드). */
        void endFrame();

        /**
         * @brief 드로우 @p drawIndex(패스 @p passOrder 번째)가 멈춤 지점(@p stopOrder 번째 패스의 @p stopDraw 번)을 넘었는지 답합니다 — `allowDraw` 의 규칙(단위 테스트).
         * @details 멈춤 패스 앞의 패스는 모두 그리고, 멈춤 패스는 그 번호까지, 뒤의 패스는 그리지 않는다. 그래프에 없는 패스(@p passOrder 없음)는 그린다.
         */
        static bool isPastStop( optional<uint32> passOrder, uint32 drawIndex, uint32 stopOrder, uint32 stopDraw );
        /**
         * @brief 기록 @p listEvent 안에서 멈춤 지점을 @p delta 만큼(앞 +, 뒤 -) 옮긴 설정을 돌려줍니다 — 창의 다음 · 이전 단추와 `framedebug next|prev` 가 같은 규칙을 쓴다.
         * @details 지금 지점이 기록에 없으면(처음 · 전체 프레임) 앞으로는 첫 드로우, 뒤로는 마지막 드로우다. 끝을 넘지 않는다. 기록이 비면 @p current 그대로다.
         */
        static FrameDebuggerSettings stepStop( const vector<FrameDebugDrawEvent>& listEvent, const FrameDebuggerSettings& current, int32 delta );
        /** @brief 기록에서 멈춤 지점의 번호입니다. 없으면 nullopt 입니다. */
        static optional<uint32> findStopEvent( const vector<FrameDebugDrawEvent>& listEvent, const FrameDebuggerSettings& settings );
        /** @brief PSO 바인딩 레이아웃을 한 줄씩 글로 옮깁니다(`FrameDebugDrawEvent::_listBinding`). */
        static void describeBindings( const ShaderBindingLayout& layout, vector<string>& outList );

        // ------------------------------------------------------------------------------
        // 3) 그래프 밖 버퍼
        // ------------------------------------------------------------------------------
        /** @brief 다음 프레임에 버퍼 목록을 공개해 달라고 요청합니다. */
        void requestBufferList() { _bBufferListRequested.store( SW_TRUE, std::memory_order_release ); }
        /** @brief 목록 요청이 있으면 꺼내고 true 입니다(렌더 스레드). */
        [[nodiscard]] bool takeBufferListRequest() { return _bBufferListRequested.exchange( SW_FALSE, std::memory_order_acq_rel ) != SW_FALSE; }
        /** @brief 목록을 공개합니다(렌더 스레드). */
        void publishBufferList( vector<RenderBufferEntry> listEntry );
        /** @brief 목록을 공개할 때마다 느는 번호입니다. */
        uint64 getBufferListSerial() const { return _bufferListSerial.load( std::memory_order_acquire ); }
        /** @brief 공개한 목록을 복사합니다. */
        void copyBufferList( vector<RenderBufferEntry>& outList ) const;

        /** @brief 이름 @p name 칸 @p slot 버퍼를 읽어 달라고 요청합니다. 아직 처리하지 않은 요청은 이것으로 바뀝니다. @return 요청 번호입니다. */
        uint32 requestBufferRead( const string& name, uint32 slot );
        /** @brief 처리할 읽기 요청을 꺼냅니다(렌더 스레드). */
        [[nodiscard]] bool takeBufferReadRequest( string& outName, uint32& outSlot, uint32& outSerial );
        /** @brief 읽은 결과를 공개합니다(렌더 스레드). */
        void publishBufferContent( RenderBufferContent content );
        /** @brief 마지막 결과의 요청 번호입니다(0 이면 없다). */
        uint32 getBufferContentSerial() const { return _bufferContentSerial.load( std::memory_order_acquire ); }
        /** @brief 마지막 결과를 복사합니다. 없으면 false 입니다. */
        [[nodiscard]] bool copyBufferContent( RenderBufferContent& outContent ) const;

    private:
        mutable mutex                          _mutex;
        vector<RenderGraphSnapshot> _listGraph SW_GUARDED_BY( _mutex );
        FrameDebuggerSettings _settings        SW_GUARDED_BY( _mutex );
        vector<FrameDebugDrawEvent> _listEvent SW_GUARDED_BY( _mutex ); ///< 공개한 한 프레임의 기록
        RenderFrameStats _stats                SW_GUARDED_BY( _mutex );
        vector<RenderBufferEntry> _listBuffer  SW_GUARDED_BY( _mutex );
        RenderBufferContent _bufferContent     SW_GUARDED_BY( _mutex );
        string _pendingBufferName              SW_GUARDED_BY( _mutex );
        uint32 _pendingBufferSlot              SW_GUARDED_BY( _mutex ){ 0 };
        uint32 _pendingBufferSerial            SW_GUARDED_BY( _mutex ){ 0 }; ///< 0 이면 요청 없음
        uint32 _lastBufferRequestSerial        SW_GUARDED_BY( _mutex ){ 0 };

        // 렌더 스레드의 이 프레임 상태 — 병렬 기록 워커도 `allowDraw` 로 오므로 `_frameMutex` 로 지킨다.
        mutable mutex                                        _frameMutex;
        FrameDebuggerSettings _frameSettings                 SW_GUARDED_BY( _frameMutex );
        unordered_map<hashed_string, uint32> _mapPassOrder   SW_GUARDED_BY( _frameMutex ); ///< 주 시점 뷰 그래프 실행 순서
        unordered_map<hashed_string, uint32> _mapPassDraw    SW_GUARDED_BY( _frameMutex ); ///< 패스마다 이 프레임에 본 드로우 수
        unordered_map<uint64, vector<string>> _mapPSOBinding SW_GUARDED_BY( _frameMutex ); ///< PSO 마다 바인딩 글(한 프레임 안 재사용)
        vector<FrameDebugDrawEvent> _listFrameEvent          SW_GUARDED_BY( _frameMutex );
        optional<uint32> _stopOrder                          SW_GUARDED_BY( _frameMutex ); ///< 멈춤 패스의 실행 순서(없으면 건너뛰지 않는다)
        uint64 _frameDrawCount                               SW_GUARDED_BY( _frameMutex ){ 0 };
        uint64 _frameTriangleCount                           SW_GUARDED_BY( _frameMutex ){ 0 };

        atomic<uint64> _graphSerial{ 0 };
        atomic<uint64> _drawEventSerial{ 0 };
        atomic<uint64> _bufferListSerial{ 0 };
        atomic<uint32> _bufferContentSerial{ 0 };
        atomic<uint32> _statsWantedFrames{ 0 };
        atomic<uint8>  _bGraphCaptureRequested{ SW_FALSE };
        atomic<uint8>  _bBufferListRequested{ SW_FALSE };
        atomic<uint8>  _bRecording{ SW_FALSE };

        SW_SERVICE_SLOT( RenderDebugRegistry, Optional );
    };
} // namespace sw

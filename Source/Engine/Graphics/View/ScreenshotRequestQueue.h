/**
 * @file Engine/Graphics/View/ScreenshotRequestQueue.h
 * @brief 스크린샷 요청의 우편함입니다 — 게임 스레드 · 개발 명령 · 에디터가 요청을 넣고 렌더 스레드가 꺼내 쓴 뒤 결과를 알립니다.
 * @details 엔진 범위의 서비스(선택 행)입니다. 렌더 스레드(`RenderThread`)의 정적 창구(`requestScreenshot` · `findScreenshotResult` …)가 `engine::findSubsystem<ScreenshotRequestQueue>()`
 *          로 찾아 부릅니다. 우편함은 엔진 범위와 함께 서고 사라지므로 엔진을 다시 세우면 표 · 장부가 비어 시작하고, 우편함이 없는 호스트(전용 서버)에서는 요청을 오류로 알리고 버립니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/array.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Subsystem/ServiceSlot.h"
#include "Core/Subsystem/Subsystem.h"

#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Graphics/RHI/RHIHandles.h"
#include "Engine/Graphics/View/ScreenshotTicketState.h"

namespace sw
{
    /** @brief 요청한 사진을 어디로 내는지입니다. */
    enum class ScreenshotOutput : uint8
    {
        File,     ///< `_path` 에 이미지 파일(확장자가 `.png` 면 PNG, 아니면 PPM)로 쓴다
        PNGBytes, ///< PNG 바이트로 그 표에 둔다(`takePNGBytes`) — `_path` 는 로그에 남길 요청 이름이다. 파일을 거치지 않는다(언리얼 `ReadPixels` 처럼 픽셀을 메모리로 받는다)
    };

    /**
     * @struct ScreenshotRequest
     * @brief 렌더 스레드가 우편함에서 꺼낸 요청 하나입니다.
     */
    struct ScreenshotRequest
    {
        string           _path;                                ///< 쓸 파일 경로. 바이트 요청이면 로그에 남길 요청 이름
        RHITextureHandle _texture{ 0 };                        ///< 0 이면 화면에 나간 이미지(Present 결과), 아니면 그 렌더 타깃
        RHIFormat        _format{ RHIFormat::R8G8B8A8_UNORM }; ///< `_texture` 의 포맷
        uint32           _ticket{ 0 };                         ///< `put` · `putForPNGBytes` 가 돌려준 표 — 끝 알림(`reportCompleted` · `reportCompletedPNGBytes`)이 이 표로 결과를 남긴다
        ScreenshotOutput _output{ ScreenshotOutput::File };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScreenshotRequestQueue
     * @brief 스크린샷 요청 줄과 처리 결과(표마다의 결과 · 처리한 수)를 든 서비스입니다.
     * @details 요청은 받은 차례로 처리하고(덮어쓰지 않는다), 요청마다 표를 돌려줘 기다리는 쪽이 제 결과만 읽는다 — 개발 명령 · 에디터 버튼 · 시작 인자 사진이 한 실행에서
     *          함께 요청해도 남의 결과를 제 것으로 읽지 않는다. 모든 함수는 어느 스레드에서 불러도 됩니다.
     */
    class SW_API ScreenshotRequestQueue final : public Subsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화하거나 종료할 것이 없고, 수명만 컬렉션이 정합니다. */
        static constexpr const utf8* kSubsystemName = "ScreenshotRequestQueue";

        ScreenshotRequestQueue()                                           = default;
        ~ScreenshotRequestQueue() override                                 = default;
        ScreenshotRequestQueue( const ScreenshotRequestQueue& )            = delete;
        ScreenshotRequestQueue& operator=( const ScreenshotRequestQueue& ) = delete;

        SubsystemInitResult        initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                       shutdownSubsystem() override {}
        const utf8*                getSubsystemName() const override { return kSubsystemName; }
        static constexpr MemoryTag kSubsystemMemoryTag = MemoryTag::RenderCPU;

        /** @brief 처리를 기다릴 수 있는 요청 수입니다. 넘치면 새 요청을 받지 않고 실패로 남긴다(요청을 매 프레임 내는 결함이 메모리를 끝없이 늘리지 않게). */
        static constexpr uint32 kMaxPendingCount = 16;
        /** @brief 표마다의 결과를 기억하는 수입니다. 이보다 오래된 표는 `Unknown` 이다. */
        static constexpr uint32 kResultHistoryCount = 32;

        /**
         * @brief 요청을 줄 끝에 넣고 그 표(0 이 아닌 수)를 돌려줍니다. 줄이 차 있으면 오류를 남기고 그 표는 곧바로 `Failed` 다.
         * @details 경로는 `AbsolutePath` 라 빈 값일 수 없다 — 빈 칸이 "요청 없음" 이라, 빈 경로를 받으면 받은 수만 올라 `hasUnfinishedRequest` 가 영영 참이었다.
         */
        uint32 put( const AbsolutePath& filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat );
        /**
         * @brief PNG 바이트로 받을 요청을 줄 끝에 넣고 그 표를 돌려줍니다(파일 요청과 같은 줄 · 같은 표). 결과는 그 표로 `takePNGBytes` 해 받는다.
         * @details @p requestName 은 로그에 남길 이름이다(결과를 가리는 열쇠는 표다). 비었으면 오류를 남기고 0(표 없음 — `findResult` 가 `Unknown`)을 돌려준다.
         */
        [[nodiscard]] uint32 putForPNGBytes( string_view requestName, RHITextureHandle sourceTexture, RHIFormat sourceFormat );
        /** @brief 줄 맨 앞의 요청을 꺼냅니다. 요청이 없으면 false 입니다. */
        [[nodiscard]] bool take( ScreenshotRequest& outRequest );
        /** @brief 표 @p ticket 의 요청을 처리했음을 알립니다(성공과 실패 모두). 결과는 그 표로만 읽는다(`findResult`). */
        void reportCompleted( uint32 ticket, bool bWritten );
        /** @brief 바이트 요청 표 @p ticket 을 처리했음을 알립니다. @p bEncoded 이면 @p pngBytes 를 그 표에 둔다(`takePNGBytes`). */
        void reportCompletedPNGBytes( uint32 ticket, bool bEncoded, vector<uint8> pngBytes );
        /** @brief 표 @p ticket 의 지금 상태입니다. */
        [[nodiscard]] ScreenshotTicketState findResult( uint32 ticket ) const;
        /** @brief 바이트 요청 표 @p ticket 이 성공했으면 그 PNG 바이트를 @p outPNGBytes 로 옮기고 true 입니다(한 번만 받는다). 다른 요청의 결과는 표가 달라 읽히지 않는다. */
        [[nodiscard]] bool takePNGBytes( uint32 ticket, vector<uint8>& outPNGBytes );
        /** @brief 받았지만 아직 처리하지 않은 요청(`put` 으로 넣고 `reportCompleted` 전)이나 패킷에 싣지 않은 패킷 사진이 있으면 true 입니다. */
        [[nodiscard]] bool hasUnfinishedRequest() const
        {
            return _requestedSerial.load( std::memory_order_acquire ) != _completedSerial.load( std::memory_order_acquire ) || isPacketSlotFree() == false;
        }

        /**
         * @brief 다음에 짓는 렌더 패킷이 그린 이미지(그 게임 프레임)를 @p filePath 에 쓰도록 맡깁니다. 앞 요청이 아직 패킷에 실리지 않았으면 false 입니다(다음 프레임에 다시 맡긴다).
         * @details `put` 은 렌더 스레드가 다음에 처리하는 프레임을 찍어 어느 게임 프레임인지 정해지지 않는다 — 이 칸은 그 프레임을 정해야 하는 쪽(자동화 `<Screenshot>` · 사진 모드)이 쓴다.
         */
        [[nodiscard]] bool putForNextPacket( const AbsolutePath& filePath );
        /** @brief 패킷을 지을 때(엔진 게임 스레드) 맡긴 경로를 꺼내 @p outPath 에 담습니다. 맡긴 것이 없으면 false 이고 @p outPath 는 그대로입니다. */
        bool takeForPacket( string& outPath );
        /** @brief 맡긴 패킷 사진이 없으면(다음 것을 받을 수 있으면) true 입니다. */
        [[nodiscard]] bool isPacketSlotFree() const;
        /** @brief 패킷이 실은 사진 하나를 처리했음을 알립니다(쓰지 못했어도 센다 — 기다리는 쪽이 끝을 안다). 렌더 스레드가 부릅니다. */
        void reportPacketCaptureCompleted() { (void)_completedPacketSerial.fetch_add( 1, std::memory_order_acq_rel ); }
        /** @brief 처리한 패킷 사진 수입니다(이 우편함이 선 뒤 누적). */
        uint32 getCompletedPacketSerial() const { return _completedPacketSerial.load( std::memory_order_acquire ); }

    private:
        /** @brief 요청 @p request 를 줄 끝에 넣고 표를 매겨 돌려줍니다(`put` · `putForPNGBytes` 공통). */
        uint32 enqueue( ScreenshotRequest request ) SW_REQUIRES_LOCK( _mutex );

    private:
        /** @brief 처리한 표 하나의 결과입니다. */
        struct TicketResult
        {
            uint32        _ticket{ 0 };       ///< 0 이면 빈 칸(아직 처리하지 않았다)
            bool          _bWritten{ false }; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
            vector<uint8> _pngBytes;          ///< 바이트 요청의 PNG(`takePNGBytes` 가 옮겨 간다)
        };

        mutable mutex                                       _mutex;
        deque<ScreenshotRequest> _listPending               SW_GUARDED_BY( _mutex ); ///< 받은 차례의 요청 줄
        array<TicketResult, kResultHistoryCount> _arrResult SW_GUARDED_BY( _mutex ); ///< 표마다의 결과(고리 — 표 번호가 자리다)
        string _pendingPacketPath                           SW_GUARDED_BY( _mutex );
        uint32 _lastTicket                                  SW_GUARDED_BY( _mutex ){ 0 }; ///< 마지막으로 내준 표
        atomic<uint32>                                      _requestedSerial{ 0 };        ///< 받은 요청 수(처리 수와 견준다)
        atomic<uint32>                                      _completedSerial{ 0 };
        atomic<uint32>                                      _completedPacketSerial{ 0 };

        SW_SERVICE_SLOT( ScreenshotRequestQueue, Optional );
    };
} // namespace sw

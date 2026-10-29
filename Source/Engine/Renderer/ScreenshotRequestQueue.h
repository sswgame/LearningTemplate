/**
 * @file Engine/Renderer/ScreenshotRequestQueue.h
 * @brief 스크린샷 요청의 우편함입니다 — 게임 스레드 · 개발 명령 · 에디터가 요청을 넣고 렌더 스레드가 꺼내 쓴 뒤 결과를 알립니다.
 * @details 엔진 범위의 서비스(선택 행)입니다. 렌더 스레드(`RenderThread`)의 정적 창구(`requestScreenshot` · `getCompletedScreenshotSerial` …)가 `engine::findSubsystem<ScreenshotRequestQueue>()`
 *          로 찾아 부릅니다. 우편함은 엔진 범위와 함께 서고 사라지므로 엔진을 다시 세우면 마지막 경로 · 장부가 비어 시작하고, 우편함이 없는 호스트(전용 서버)에서는 요청을 오류로 알리고 버립니다.
 *          `-gv_screenshotQuit` 의 장부(요청한 종류 · 끝낸 종류 · 쓰지 못한 종류)도 여기 둡니다.
 */
#pragma once
#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct ScreenshotRequest
     * @brief 렌더 스레드가 우편함에서 꺼낸 요청 하나입니다.
     */
    struct ScreenshotRequest
    {
        string           _path;                                ///< 쓸 파일 경로
        RHITextureHandle _texture{ 0 };                        ///< 0 이면 화면에 나간 이미지(Present 결과), 아니면 그 렌더 타깃
        RHIFormat        _format{ RHIFormat::R8G8B8A8_UNORM }; ///< `_texture` 의 포맷
    };
} // namespace sw

namespace sw
{
    /**
     * @class ScreenshotRequestQueue
     * @brief 스크린샷 요청 한 칸과 처리 결과(마지막 경로 · 처리한 수)와 `-gv_screenshotQuit` 장부를 든 서비스입니다.
     * @details 요청은 한 칸이라 앞 요청이 아직 남아 있으면 덮어씁니다. 모든 함수는 어느 스레드에서 불러도 됩니다.
     */
    class SW_API ScreenshotRequestQueue final : public ISubsystem
    {
    public:
        /** @brief 서브시스템 이름입니다. 초기화하거나 종료할 것이 없고, 수명만 컬렉션이 정합니다. */
        static constexpr const utf8* kSubsystemName = "ScreenshotRequestQueue";

        ScreenshotRequestQueue()                                           = default;
        ~ScreenshotRequestQueue() override                                 = default;
        ScreenshotRequestQueue( const ScreenshotRequestQueue& )            = delete;
        ScreenshotRequestQueue& operator=( const ScreenshotRequestQueue& ) = delete;

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }
        MemoryTag           getSubsystemMemoryTag() const override { return MemoryTag::RenderCPU; }

        /** @brief 요청을 넣습니다. 아직 꺼내지 않은 앞 요청은 이 요청으로 바뀝니다. */
        void put( string_view filePath, RHITextureHandle sourceTexture, RHIFormat sourceFormat );
        /** @brief 넣어 둔 요청을 꺼냅니다(꺼낸 칸은 빈다). 요청이 없으면 false 입니다. */
        [[nodiscard]] bool take( ScreenshotRequest& outRequest );
        /** @brief 요청 하나를 처리했음을 알립니다(성공과 실패 모두). @p bWritten 이 true 면 @p writtenPath 가 마지막 경로가 되고, 아니면 마지막 경로가 비워집니다. */
        void reportCompleted( bool bWritten, const string& writtenPath );
        /** @brief 처리한 요청 수입니다(성공과 실패 모두 셉니다). */
        uint32 getCompletedSerial() const { return _completedSerial.load( std::memory_order_acquire ); }
        /** @brief 마지막으로 처리한 요청이 쓴 파일 경로입니다. 실패했으면 빈 문자열입니다. */
        string getLastPath() const;

        /**
         * @brief 다음에 짓는 렌더 패킷이 그린 이미지(그 게임 프레임)를 @p filePath 에 쓰도록 맡깁니다. 앞 요청이 아직 패킷에 실리지 않았으면 false 입니다(다음 프레임에 다시 맡긴다).
         * @details `put` 은 렌더 스레드가 다음에 처리하는 프레임을 찍어 어느 게임 프레임인지 정해지지 않는다 — 이 칸은 그 프레임을 정해야 하는 쪽(자동화 `<Screenshot>` · 사진 모드)이 쓴다.
         */
        [[nodiscard]] bool putForNextPacket( string_view filePath );
        /** @brief 패킷을 지을 때(엔진 게임 스레드) 맡긴 경로를 꺼내 @p outPath 에 담습니다. 맡긴 것이 없으면 false 이고 @p outPath 는 그대로입니다. */
        bool takeForPacket( string& outPath );
        /** @brief 맡긴 패킷 사진이 없으면(다음 것을 받을 수 있으면) true 입니다. */
        [[nodiscard]] bool isPacketSlotFree() const;
        /** @brief 패킷이 실은 사진 하나를 처리했음을 알립니다(쓰지 못했어도 센다 — 기다리는 쪽이 끝을 안다). 렌더 스레드가 부릅니다. */
        void reportPacketCaptureCompleted() { (void)_completedPacketSerial.fetch_add( 1, std::memory_order_acq_rel ); }
        /** @brief 처리한 패킷 사진 수입니다(이 우편함이 선 뒤 누적). */
        uint32 getCompletedPacketSerial() const { return _completedPacketSerial.load( std::memory_order_acquire ); }

        /** @brief 이 종류(`RenderThread::CaptureKind` 비트)의 사진을 찍겠다고 알립니다. */
        void expectCapture( uint32 kindBit ) { (void)_expectedCaptureMask.fetch_or( kindBit ); }
        /** @brief 이 종류의 사진 요청을 모두 처리했음을 알립니다(@p bWritten 이 false 면 파일을 쓰지 못했다). 실패를 먼저 적는다 — 끝났다고 보인 순간 실패도 보여야 한다. */
        void reportCaptureFinished( uint32 kindBit, bool bWritten );
        /**
         * @brief 요청한 종류가 모두 끝났는지 봅니다. 요청한 종류가 하나도 없거나 아직 끝나지 않았으면 false 입니다.
         * @param outBFailed 끝났을 때 쓰지 못한 종류가 하나라도 있으면 true 입니다.
         */
        [[nodiscard]] bool findAllCapturesFinished( bool& outBFailed ) const;

    private:
        mutable mutex                    _mutex;
        string _pendingPath              SW_GUARDED_BY( _mutex );
        string _lastPath                 SW_GUARDED_BY( _mutex );
        string _pendingPacketPath        SW_GUARDED_BY( _mutex );
        RHITextureHandle _pendingTexture SW_GUARDED_BY( _mutex ){ 0 };
        RHIFormat _pendingFormat         SW_GUARDED_BY( _mutex ){ RHIFormat::R8G8B8A8_UNORM };
        atomic<uint32>                   _completedSerial{ 0 };
        atomic<uint32>                   _completedPacketSerial{ 0 };
        atomic<uint32>                   _expectedCaptureMask{ 0 };
        atomic<uint32>                   _finishedCaptureMask{ 0 };
        atomic<uint32>                   _failedCaptureMask{ 0 };
    };
} // namespace sw

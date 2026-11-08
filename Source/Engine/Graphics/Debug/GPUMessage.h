/**
 * @file GPUMessage.h
 * @brief 셰이더 메시지 채널의 CPU 쪽입니다 — 셰이더가 UAV 링에 적은 기록(메시지 id · 픽셀 또는 스레드 · 인자 넷)을 몇 프레임 뒤 비동기 되읽기로 읽어, id 의 서식으로 풀어 로그로 냅니다.
 * @details 언리얼 ShaderPrint(`ShaderPrint::PrintSymbol` — 글자를 GPU 에서 그린다) · ShaderDebug, 유니티 셰이더 printf 선과 같은 자리다. 여기서는 글자를 GPU 에서 만들지 않고
 *          **id 와 인자 비트만** 적는다 — 서식 문자열은 CPU 표에 있다(`SW_GPU_MESSAGE_FORMAT`). 그래서 셰이더에 문자열 리터럴이 없고, 로그 · 시나리오 판정(`ExpectLog`)이 같은 줄을 본다.
 *          셰이더 쪽은 `Content/engine/shaders/gpu_message.hlsli`(`SW_GPU_PRINT` · `SW_GPU_LOG` · `SW_GPU_ASSERT` · `SW_GPU_ASSERT_ARGS`), 버퍼 배치와 id 는 `gpu_message_ids.hlsli` 하나다 —
 *          HLSL 과 이 파일이 같은 파일을 include 하므로 번호를 두 번 적지 않는다.
 *
 *          **퍼뮤테이션이다.** 켜는 스위치는 진단 정책(`RenderDiagnosticsPolicy::_bGPUMessages`, 개발 도구의 `-gv_gpuMessages=1`, 기본 끔)이고, 켜면 메시지를 가진 패스
 *          (표의 `RenderPassTraitFlag::kGPUMessageTarget`)가 셰이더에 `SW_GPU_MESSAGES_ENABLED` 를 붙여 컴파일되고(`selectRenderPassShader` — 쿠커와 같은 함수) 렌더러가 그 패스 앞에서
 *          이 버퍼를 u4 에 건다(컴퓨트 `bindCompute`, 그래픽스 `bindGraphics`). Shipping 은 `isRequested` 가 늘 거짓이라 define 도 PSO 변형도 바인딩도 생기지 않는다.
 *          필수 값(버퍼 · 컴퓨트 · 그래픽스 쓰기 능력)이 없으면 오류로 멈춘다 — 메시지가 켜진 PSO 를 버퍼 없이 돌리는 길은 없다.
 *
 *          버퍼는 상한 있는 링이다. 셰이더가 누적 카운터를 원자적으로 올려 순번을 받고 순번 % 용량 칸에 적는다(지우지 않는다 — 클리어 패스가 필요 없다). CPU 는 지난번 읽은 누적 수와의 차에서
 *          한 바퀴 넘어 덮인 수를 **버려진 수**로 세어 알린다. 출력을 고르는 값(픽셀 · 스레드 · 로그 예산)은 CPU 가 프레임마다 머리 칸에 올린다(`updateControl`).
 * @note 슬롯 u4(`SW_SLOT_GPU_MESSAGE_UAV`)를 컴퓨트 · 그래픽스 단계가 함께 쓴다. SRV t 띠는 꽉 차 있어 t 가 아니라 u 다. DX11 픽셀 셰이더는 렌더 타깃이 넷 이하인 패스에서만 걸린다(binding_slots.hlsli 3 절).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIStructuredBuffer.h"

#include "gpu_message_ids.hlsli"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /** @brief 메시지 한 건의 심각도입니다. 로그 수준이 된다(`Info` — `SW_GPU_PRINT` · `SW_GPU_LOG`, `Warning` · `Error` — 대개 `SW_GPU_ASSERT`). */
    enum class GPUMessageSeverity : uint8
    {
        Info,
        Warning,
        Error,
    };

    /** @brief 인자 칸 하나의 비트를 어떻게 읽는지입니다. `None` 은 쓰지 않는 칸이다(서식의 `%#` 수와 쓰는 칸 수가 같아야 등록된다). */
    enum class GPUMessageArg : uint8
    {
        None,
        Uint,
        Int,
        Float,
        Hex,
    };

    /** @brief 셰이더가 적은 기록 하나입니다(링 칸을 푼 값). */
    struct GPUMessageRecord
    {
        uint32 _id{ 0 };
        uint32 _x{ SW_GPU_MESSAGE_NO_COORD }; ///< 픽셀 x 또는 스레드
        uint32 _y{ SW_GPU_MESSAGE_NO_COORD }; ///< 픽셀 y — 스레드 기록이면 `SW_GPU_MESSAGE_NO_COORD`
        uint32 _arrArg[SW_GPU_MESSAGE_ARG_COUNT]{};

        /** @brief 픽셀 셰이더의 기록이면 true 입니다(아니면 스레드 기록이거나 좌표를 정하지 않았다). */
        bool isPixel() const { return _y != SW_GPU_MESSAGE_NO_COORD; }
    };

    /**
     * @struct GPUMessageFormat
     * @brief 메시지 id 하나의 서식 표 항목입니다(`gpu_message_ids.hlsli` 의 id 와 같은 번호). 엔진 표는 `GPUMessage.cpp`, 게임 · 키트는 자기 .cpp 에서 `SW_GPU_MESSAGE_FORMAT` 으로 더한다.
     * @details 서식은 영어 한 줄이고 쓰는 인자 칸마다 `%#` 하나를 칸 순서대로 둔다. 등록이 `%#` 수와 쓰는 칸(`None` 이 아닌 칸) 수를 맞춰 보고 다르면 거절한다(로그 줄이 인자를 잃거나 남기지 않는다).
     *          같은 id 가 둘이면 뒤의 것을 거절한다(셰이더 하나가 두 뜻으로 읽힌다).
     */
    struct SW_API GPUMessageFormat
    {
        uint32             _id;
        GPUMessageSeverity _severity;
        const utf8*        _pFormat; ///< 영어 한 줄 — 쓰는 인자 칸마다 `%#`
        GPUMessageArg      _arrArg[SW_GPU_MESSAGE_ARG_COUNT];
        const utf8*        _pSource; ///< 이 메시지를 적는 셰이더 파일(로그에 싣는다)

        /** @brief 서식의 `%#` 수와 쓰는 인자 칸 수가 같은지입니다(등록 검사). */
        [[nodiscard]] bool isConsistent() const;
        /** @brief 기록의 인자를 서식에 채워 @p pOut 에 씁니다(할당 없음, 넘치면 자른다). */
        void formatArgs( const GPUMessageRecord& record, utf8* pOut, uint32 capacity ) const;

        /** @brief 표에 올립니다. 같은 id 가 이미 있거나 서식이 인자 칸과 맞지 않으면 false 이고 오류를 남깁니다. */
        [[nodiscard]] static bool registerFormat( const GPUMessageFormat* pFormat );
        /** @brief 표에서 뺍니다(모듈 언로드). */
        static void unregisterFormat( const GPUMessageFormat* pFormat );
        /** @brief id 의 항목입니다. 없으면(셰이더와 표가 어긋남) nullptr 입니다. */
        [[nodiscard]] static const GPUMessageFormat* find( uint32 id );
        /** @brief 표의 항목 수입니다(검사 · 테스트용). */
        [[nodiscard]] static uint32 getRegisteredCount();
    };

    /** @brief `SW_GPU_MESSAGE_FORMAT` 이 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 거절된 등록은 떼지 않는다). */
    SW_DEFINE_STATIC_REGISTRAR( GPUMessageFormatRegistrar, const GPUMessageFormat*, &GPUMessageFormat::registerFormat, &GPUMessageFormat::unregisterFormat );

    /**
     * @struct GPUMessageRingRead
     * @brief 한 번 읽은 링에서 새로 찾은 기록과 버려진 수입니다. 링 버퍼 배치(`gpu_message_ids.hlsli`)를 푸는 `read` 가 만든다 — GPU 없이 테스트한다.
     */
    struct SW_API GPUMessageRingRead
    {
        static constexpr uint32 kRecordBase = SW_GPU_MESSAGE_HEADER_UINTS + SW_GPU_MESSAGE_BUDGET_SLOTS; ///< 첫 기록 칸의 uint 위치

        vector<GPUMessageRecord> _listRecord;
        uint32                   _counter{ 0 }; ///< 읽은 사본의 누적 카운터 — 다음 읽기의 `processedCounter`
        uint32                   _dropped{ 0 }; ///< 한 바퀴 넘어 덮였거나 쓰다 만 칸의 수

        /**
         * @brief 링 사본 @p pWords 에서 @p processedCounter 뒤의 새 기록을 순번 순서로 꺼냅니다.
         * @details 누적 카운터는 32 비트로 돈다 — 차를 부호 없는 뺄셈으로 구해 한 바퀴 돌아도 맞는다. 용량을 넘은 수와 순번이 맞지 않는 칸(쓰다 만 칸 · 더 뒤의 기록이 덮은 칸)은 버려진 수다.
         * @param wordCount `SW_GPU_MESSAGE_BUFFER_UINTS` 보다 작으면 아무것도 꺼내지 않고 카운터를 @p processedCounter 그대로 돌려준다.
         */
        [[nodiscard]] static GPUMessageRingRead read( const uint32* pWords, uint32 wordCount, uint32 processedCounter );
    };

    /** @brief `SW_GPU_PRINT` 가 고를 픽셀 · 스레드입니다(`gpu.inspect x y` · `gpu.thread n`). 고르지 않은 칸은 `SW_GPU_MESSAGE_NO_COORD` 다. */
    struct GPUMessageSelection
    {
        uint32 _pixelX{ SW_GPU_MESSAGE_NO_COORD };
        uint32 _pixelY{ SW_GPU_MESSAGE_NO_COORD };
        uint32 _thread{ SW_GPU_MESSAGE_NO_COORD };
    };

    static_assert( SW_GPU_MESSAGE_BUFFER_UINTS == SW_GPU_MESSAGE_HEADER_UINTS + SW_GPU_MESSAGE_BUDGET_SLOTS + SW_GPU_MESSAGE_RING_CAPACITY * SW_GPU_MESSAGE_RECORD_UINTS, "메시지 버퍼 크기가 배치와 다르다" );
    static_assert( SW_GPU_MESSAGE_CONTROL_UINTS == ( SW_GPU_MESSAGE_HEADER_UINTS - 1 ) + SW_GPU_MESSAGE_BUDGET_SLOTS, "제어 구간은 머리 [1..7] 과 예산 칸 전부다" );
    static_assert( SW_GPU_MESSAGE_RECORD_ARG0 + SW_GPU_MESSAGE_ARG_COUNT == SW_GPU_MESSAGE_RECORD_SEQUENCE && SW_GPU_MESSAGE_RECORD_SEQUENCE + 1 == SW_GPU_MESSAGE_RECORD_UINTS, "기록 칸 배치가 크기와 다르다" );

    /**
     * @class GPUMessageChannel
     * @brief 메시지 링 버퍼 하나와 그 되읽기 · 로그입니다. 렌더 스레드 하나가 쓴다(`FrameRenderState` 가 갖는다 — 프리패스의 컴퓨트와 그래프의 그래픽스 패스가 함께 쓴다).
     * @details 언리얼 `FShaderPrintData`(뷰가 갖는 출력 버퍼 + 다음 프레임 되읽기)와 같은 자리다.
     */
    class SW_API GPUMessageChannel
    {
    public:
        GPUMessageChannel();
        ~GPUMessageChannel();
        GPUMessageChannel( const GPUMessageChannel& )            = delete;
        GPUMessageChannel& operator=( const GPUMessageChannel& ) = delete;

        /** @brief 이 실행이 셰이더 메시지를 요구하는지입니다 — 진단 정책(`RenderDiagnostics::get`)이 켰을 때만 참입니다. Shipping 은 늘 거짓입니다. PSO define 이 이 값으로 정해집니다. */
        [[nodiscard]] static bool isRequested();

        /**
         * @brief 링 버퍼(0 으로 채운 UAV 구조 버퍼)와 되읽기 고리를 만듭니다. `isRequested` 가 거짓이면 아무것도 만들지 않고 true 입니다.
         * @return 요구됐는데 만들지 못하면(컴퓨트 · 그래픽스 셰이더 쓰기 능력이 없거나 버퍼 실패) false 이며 이유를 로그로 남깁니다 — 호출한 쪽은 초기화를 실패로 끝냅니다.
         */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice );
        /** @brief 버퍼와 고리를 놓습니다. 여러 번 불러도 안전합니다. */
        void shutdown();
        /** @brief 버퍼가 만들어져 있는지입니다. */
        bool isActive() const { return _buffer.isValid(); }

        /**
         * @brief 이번 프레임의 선택(픽셀 · 스레드)과 로그 예산을 머리 칸에 올리고 예산 칸을 0 으로 되돌립니다. 프레임 기록 **앞**(열린 렌더 패스가 없을 때) 한 번 부릅니다.
         * @details 누적 카운터(머리 [0])는 건드리지 않는다 — GPU 만 쓴다. 업로드는 프레임 순서로 큐에 들어가 앞 프레임의 기록 뒤 · 이번 프레임의 기록 앞에 놓인다.
         */
        void updateControl( const GPUMessageSelection& selection, uint32 logBudget );
        /** @brief 메시지가 켜진 컴퓨트 디스패치 **앞에서** 부릅니다. 버퍼를 UAV 상태로 두고 u4 에 겁니다. 꺼져 있으면 아무것도 하지 않습니다. */
        void bindCompute( IRHICommandList& commands );
        /** @brief 메시지가 켜진 그래픽스 드로우 **앞에서**(렌더 패스를 연 뒤) 부릅니다. u4 를 그래픽스 단계에 겁니다. 꺼져 있으면 아무것도 하지 않습니다. */
        void bindGraphics( IRHICommandList& commands );
        /** @brief 이 프레임의 메시지 디스패치가 모두 끝난 뒤 한 번 부릅니다. 링 전체를 읽기 버퍼로 복사하는 명령을 넣습니다(프레임 번호가 요청의 tag 다). */
        void requestReadback( IRHICommandList& commands );
        /** @brief 도착한 가장 최근 사본을 읽어 새 기록을 로그로 냅니다(메시지 id 별로 묶는다). 몇 프레임 늦게 도착한다. */
        void poll();

        /** @brief 지금까지 로그로 낸 기록의 총수입니다(검사 · 감시 값용). */
        uint64 getReportedCount() const { return _reportedCount; }
        /** @brief 링이 한 바퀴 넘어 버려진 기록의 총수입니다(검사용). */
        uint64 getDroppedCount() const { return _droppedCount; }

    private:
        IRHIDevice*         _pDevice;
        RHIStructuredBuffer _buffer;
        AsyncBufferReadback _readback;
        uint64              _frameSerial;
        uint32              _processedCounter; ///< 지난번까지 읽은 누적 기록 수(셰이더 카운터와 같은 축, 32 비트로 돈다)
        uint64              _reportedCount;
        uint64              _droppedCount;
    };
} // namespace sw

/**
 * @brief 메시지 id 하나의 서식을 표에 올립니다(.cpp 의 파일 범위, `namespace sw` 안). @p tag 는 정적 이름을 만드는 식별자입니다.
 * @details 예: `SW_GPU_MESSAGE_FORMAT( presentPixel, SW_GPU_MESSAGE_ID_PRESENT_PIXEL, GPUMessageSeverity::Info, "present pixel color %# %# %#",
 *          GPUMessageArg::Float, GPUMessageArg::Float, GPUMessageArg::Float, GPUMessageArg::None, "fullscreen_blit.hlsl" );`
 */
#define SW_GPU_MESSAGE_FORMAT( tag, id, severity, pFormat, arg0, arg1, arg2, arg3, pSource ) \
    SW_STATIC_REGISTRATION( gpuMessageFormat, ::sw::GPUMessageFormat, ::sw::GPUMessageFormatRegistrar, tag, id, severity, pFormat, { arg0, arg1, arg2, arg3 }, pSource )

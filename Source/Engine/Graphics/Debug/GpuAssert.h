/**
 * @file GpuAssert.h
 * @brief 셰이더 assert / printf 의 CPU 쪽입니다 — 컴퓨트 셰이더가 UAV 링에 적은 기록(조건 id · 값 · 스레드)을 몇 프레임 뒤 비동기 되읽기로 읽어 로그로 냅니다.
 * @details 언리얼 ShaderPrint · ShaderDebug, 유니티 셰이더 printf 선과 같은 자리다. 셰이더 쪽은 `Resource/engine/shaders/gpuassert.hlsli`(`SW_GPU_ASSERT( 조건, id, 값 )`),
 *          조건 id 표는 `gpuassertids.hlsli` 하나다 — HLSL 과 이 파일이 같은 파일을 include 하므로 번호를 두 번 적지 않는다. id 별 심각도 · 메시지는 `GpuAssert.cpp` 의 표가 정한다.
 *
 *          **퍼뮤테이션이다.** 켜는 스위치는 `-gv_gpuAssert=1`(기본 끔)이고, 켜면 assert 를 가진 컴퓨트 패스(표의 `RenderPassTraitFlag::kGpuAssertTarget`)가 셰이더에
 *          `SW_GPU_ASSERT_ENABLED` 를 붙여 컴파일되고(`selectRenderPassShader` — 쿠커와 같은 함수) 렌더러가 그 패스 앞에서 이 버퍼를 u4 에 건다. Shipping 은 `isRequested` 가 늘 거짓이라
 *          define 도 PSO 변형도 바인딩도 생기지 않는다. 필수 값(버퍼)을 만들지 못하면 오류로 멈춘다 — assert 가 켜진 PSO 를 버퍼 없이 돌리는 길은 없다.
 *
 *          버퍼는 상한 있는 링이다. 셰이더가 누적 카운터를 원자적으로 올려 순번을 받고 순번 % 용량 칸에 적는다(지우지 않는다 — 클리어 패스가 필요 없다). CPU 는 지난번 읽은 누적 수와의 차에서
 *          한 바퀴 넘어 덮인 수를 **버려진 수**로 세어 알린다.
 * @note 네 백엔드 모두 컴퓨트 UAV 구조 버퍼 슬롯 u4 로 바인딩된다(DX11 포함 — 컴퓨트 UAV 는 DX11 에서도 된다). 컴퓨트 능력(`RHICapabilities::_bCompute`)이 없는 백엔드는 이 기능을 쓰지 않는다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Graphics/RHI/AsyncBufferReadback.h"
#include "Engine/Graphics/RHI/RHIStructuredBufferSlot.h"

#include "gpuassertids.hlsli"

namespace sw
{
    class IRHICommandList;
    class IRHIDevice;

    /** @brief assert 한 건의 심각도입니다. 로그 수준(`Warning` · `Error`)이 된다. */
    enum class GpuAssertSeverity : uint8
    {
        Warning,
        Error,
    };

    /** @brief 기록의 값 비트를 어떻게 읽는지입니다. */
    enum class GpuAssertValueKind : uint8
    {
        Uint,
        Float,
    };

    /** @brief 조건 id 하나의 표 항목입니다(`gpuassertids.hlsli` 의 `SW_GPU_ASSERT_ID_*` 와 같은 번호). */
    struct GpuAssertInfo
    {
        uint32             _id;
        GpuAssertSeverity  _severity;
        GpuAssertValueKind _valueKind;
        const utf8*        _pMessage; ///< 영어 한 줄 — 로그 문구
        const utf8*        _pPass;    ///< 이 조건을 검사하는 셰이더 파일(로그에 싣는다)
    };

    /** @brief id 의 표 항목입니다. 표에 없는 id(셰이더와 표가 어긋남)는 nullptr 입니다. */
    SW_API const GpuAssertInfo* findGpuAssertInfo( uint32 conditionId );

    /** @brief 표의 항목 수입니다(검사 · 테스트용). */
    SW_API uint32 getGpuAssertInfoCount();

    static_assert( SW_GPU_ASSERT_BUFFER_UINTS == SW_GPU_ASSERT_HEADER_UINTS + SW_GPU_ASSERT_RING_CAPACITY * SW_GPU_ASSERT_RECORD_UINTS, "assert 버퍼 크기가 배치와 다르다" );

    /**
     * @class GpuAssertMonitor
     * @brief assert 링 버퍼 하나와 그 되읽기 · 로그입니다. 렌더 스레드 하나가 쓴다(`FrameRenderer` 가 갖는다).
     */
    class SW_API GpuAssertMonitor
    {
    public:
        GpuAssertMonitor();
        ~GpuAssertMonitor();
        GpuAssertMonitor( const GpuAssertMonitor& )            = delete;
        GpuAssertMonitor& operator=( const GpuAssertMonitor& ) = delete;

        /** @brief 이 실행이 셰이더 assert 를 요구하는지입니다 — 개발 빌드이고 `-gv_gpuAssert` 가 켜졌을 때만 참입니다. Shipping 은 늘 거짓입니다. PSO define 이 이 값으로 정해집니다. */
        [[nodiscard]] static bool isRequested();

        /**
         * @brief 링 버퍼(0 으로 채운 UAV 구조 버퍼)와 되읽기 고리를 만듭니다. `isRequested` 가 거짓이면 아무것도 만들지 않고 true 입니다.
         * @return 요구됐는데 만들지 못하면 false 이며 이유를 로그로 남깁니다 — 호출한 쪽은 초기화를 실패로 끝냅니다(assert 가 켜진 PSO 를 버퍼 없이 돌리지 않는다).
         */
        [[nodiscard]] bool initialize( IRHIDevice* pDevice );
        /** @brief 버퍼와 고리를 놓습니다. 여러 번 불러도 안전합니다. */
        void shutdown();
        /** @brief 버퍼가 만들어져 있는지입니다. */
        bool isActive() const { return _buffer.isValid(); }

        /** @brief assert 가 켜진 컴퓨트 디스패치 **앞에서** 부릅니다. 버퍼를 UAV 상태로 두고 u4 에 겁니다. 꺼져 있으면 아무것도 하지 않습니다. */
        void bind( IRHICommandList& commands );
        /** @brief 이 프레임의 assert 디스패치가 모두 끝난 뒤 한 번 부릅니다. 링 전체를 읽기 버퍼로 복사하는 명령을 넣습니다(프레임 번호가 요청의 tag 다). */
        void requestReadback( IRHICommandList& commands );
        /** @brief 도착한 가장 최근 사본을 읽어 새 기록을 로그로 냅니다(조건 id 별로 묶는다). 몇 프레임 늦게 도착한다. */
        void poll();

        /** @brief 지금까지 로그로 낸 기록의 총수입니다(검사용). */
        uint64 getReportedCount() const { return _reportedCount; }
        /** @brief 링이 한 바퀴 넘어 버려진 기록의 총수입니다(검사용). */
        uint64 getDroppedCount() const { return _droppedCount; }

    private:
        IRHIDevice*             _pDevice;
        RHIStructuredBufferSlot _buffer;
        AsyncBufferReadback     _readback;
        uint64                  _frameSerial;
        uint64                  _processedCount; ///< 지난번까지 읽은 누적 기록 수(셰이더 카운터와 같은 축)
        uint64                  _reportedCount;
        uint64                  _droppedCount;
    };
} // namespace sw

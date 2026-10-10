/**
 * @file IProfilerBackend.h
 * @brief 외부 프로파일러(타임라인 뷰어)로 계측을 내보내는 출력 하나의 계약입니다.
 *
 * [왜 따로 두는가]
 * 엔진 `FrameProfiler` 는 프로세스 **안에서** 구간을 접어 p50 · p99 표를 냅니다(성능 회귀 · 에디터 패널 · Shipping 오버레이).
 * 타임라인 · 스레드 · GPU 큐 · 메모리를 시간축으로 보는 일은 외부 뷰어(Tracy)가 합니다. 계측 지점(`SW_PROFILE_SCOPE`)은 하나이고,
 * 이 인터페이스가 두 번째 출력입니다. 라이브러리 헤더는 구현 폴더(`Profiling/Tracy/`) 밖으로 나오지 않습니다 — 이 파일은
 * 라이브러리 타입을 하나도 쓰지 않으므로 Tracy 를 다른 뷰어로 바꿔도 호출부는 그대로입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct ProfileZoneSite
     * @brief 계측 지점 하나(이름 · 함수 · 파일 · 줄 · 색)입니다.
     * @details 출력은 이 주소를 **프로세스 끝까지** 들 수 있습니다(Tracy 는 포인터만 보내고 문자열은 뷰어가 물을 때 읽는다).
     *          그래서 지점은 `ProfilerBackend::registerZoneSite` 만 만들고, 문자열은 intern 한 사본입니다 — 핫 리로드로 내려간
     *          모듈의 문자열 상수를 가리키지 않습니다. 필드 배치는 Tracy 의 소스 위치 구조체와 같습니다(구현 파일이 정적 검사).
     */
    struct ProfileZoneSite
    {
        const utf8* _pName;     ///< 구간 이름(뷰어에 보이는 이름)
        const utf8* _pFunction; ///< 함수 이름
        const utf8* _pFile;     ///< 소스 파일
        uint32      _line;      ///< 줄 번호
        uint32      _color;     ///< 0xRRGGBB, 0 이면 뷰어 기본색
    };
} // namespace sw

namespace sw
{
    /**
     * @enum ProfilerGraphicsAPI
     * @brief GPU 구간을 낸 그래픽스 API 입니다. 뷰어가 큐 이름 옆에 보여 줍니다.
     */
    enum class ProfilerGraphicsAPI : uint8
    {
        Direct3D11,
        Direct3D12,
        Vulkan,
        OpenGl,
    };
} // namespace sw

namespace sw
{
    /**
     * @class IProfilerBackend
     * @brief 외부 프로파일러 출력입니다. 모든 함수는 어느 스레드에서나 부를 수 있습니다(아래 GPU 함수는 예외).
     * @details 구간은 RAII(`ScopedFrameProfile`)가 열고 닫으므로 같은 스레드에서 짝이 맞습니다. `beginZone` 이 돌려준 값은
     *          `endZone` 에 그대로 돌려줍니다.
     *
     *          GPU 함수(`createGPUContext` · `syncGPUClock` · `beginGPUZone` · `endGPUZone`)는 **한 스레드**(렌더 스레드)가
     *          부릅니다. GPU 시각은 이미 끝난 프레임의 타임스탬프를 읽은 뒤에 내므로 begin · end 는 그 자리에서 짝을 맞춥니다.
     */
    class SW_API IProfilerBackend
    {
    public:
        /** @brief GPU 컨텍스트를 만들지 못했을 때의 값입니다. */
        static constexpr uint32 kInvalidGPUContext = invalid_index::kUint32;

        IProfilerBackend()          = default;
        virtual ~IProfilerBackend() = default;

        IProfilerBackend( const IProfilerBackend& )            = delete;
        IProfilerBackend& operator=( const IProfilerBackend& ) = delete;

        /** @brief 출력 이름입니다("Tracy"). 로그 · 에디터 표시용입니다. */
        virtual const utf8* getBackendName() const = 0;
        /** @brief 뷰어가 지금 붙어 있으면 true 입니다. */
        virtual bool isViewerConnected() const = 0;

        /** @brief 이 스레드에서 구간을 엽니다. 반환값은 `endZone` 에 넘깁니다. */
        virtual uint64 beginZone( const ProfileZoneSite& site ) = 0;
        /** @brief `beginZone` 이 연 구간을 닫습니다. */
        virtual void endZone( uint64 zoneToken ) = 0;

        /** @brief 프레임 경계를 찍습니다. @p pFrameName 이 nullptr 이면 주 프레임, 아니면 그 이름의 보조 프레임입니다(이름은 프로세스 수명 문자열). */
        virtual void markFrame( const utf8* pFrameName ) = 0;
        /** @brief 이름 붙은 값 그래프에 한 점을 더합니다(프레임당 드로우 수 등). 이름은 프로세스 수명 문자열입니다. */
        virtual void plotValue( const utf8* pPlotName, float64 value ) = 0;

        /** @brief 할당 한 건을 알립니다. @p pPoolName 은 프로세스 수명 문자열(메모리 태그 이름)입니다. */
        virtual void onAllocate( const void* pPtr, size_t size, const utf8* pPoolName ) = 0;
        /** @brief `onAllocate` 로 알린 블록의 해제를 알립니다. */
        virtual void onFree( const void* pPtr, const utf8* pPoolName ) = 0;

        /**
         * @brief GPU 큐 하나의 타임라인을 엽니다. @p gpuNanos 는 **지금** GPU 시계(타임스탬프와 같은 영역의 나노초)입니다.
         * @return 컨텍스트 번호. 만들 수 없으면 `kInvalidGPUContext`.
         */
        virtual uint32 createGPUContext( ProfilerGraphicsAPI api, const utf8* pName, int64 gpuNanos ) = 0;
        /** @brief GPU 시계와 CPU 시계를 다시 맞춥니다. @p gpuNanos 는 **지금** GPU 시계입니다(시계가 서로 흐르는 만큼을 지웁니다). */
        virtual void syncGPUClock( uint32 gpuContext, int64 gpuNanos ) = 0;
        /** @brief GPU 구간을 엽니다. 안쪽 구간은 바깥 구간이 닫히기 전에 열고 닫습니다(트리). */
        virtual void beginGPUZone( uint32 gpuContext, const ProfileZoneSite& site, int64 gpuBeginNanos ) = 0;
        /** @brief 가장 안쪽의 열린 GPU 구간을 닫습니다. */
        virtual void endGPUZone( uint32 gpuContext, int64 gpuEndNanos ) = 0;
    };
} // namespace sw

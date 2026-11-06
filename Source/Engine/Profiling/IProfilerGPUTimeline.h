/**
 * @file IProfilerGPUTimeline.h
 * @brief 외부 프로파일러의 GPU 타임라인 쪽 계약입니다. 계측 지점(`ProfileZoneSite`)과 GPU API 종류도 여기 있습니다.
 * @details CPU 구간과 스레드 규칙이 달라 인터페이스를 가른다 — CPU 구간은 어느 스레드에서나 열지만 GPU 함수는 **한 스레드**(렌더 스레드)가 부른다.
 *          GPU 시각은 이미 끝난 프레임의 타임스탬프를 읽은 뒤에 내므로 begin · end 는 그 자리에서 짝을 맞춘다. 내보내는 쪽(`GPUTimelineExporter`)은 이것만 받는다.
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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
    /** @class IProfilerGPUTimeline @brief GPU 큐의 타임라인을 냅니다. 파일 머리말 참고. */
    class IProfilerGPUTimeline
    {
    public:
        /** @brief GPU 컨텍스트를 만들지 못했을 때의 값입니다. */
        static constexpr uint32 kInvalidGPUContext = constant::invalid_index::kUint32;

        virtual ~IProfilerGPUTimeline() = default;

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

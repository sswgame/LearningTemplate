/**
 * @file HardwareDetection.h
 * @brief 첫 실행에 그래픽 품질 프리셋을 고르려고 읽는 하드웨어 사양입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /**
     * @brief 프리셋 자동 선택(`ScalabilityAutoDetectRule`)이 보는 사양입니다.
     * @details CPU 논리 코어 수, 시스템 메모리, 기본 그래픽 어댑터(RHI 가 쓰는 어댑터 0)의 이름과 전용 메모리를 본다.
     *          규칙은 "이 사양이면 이 프리셋에서 시작" 하는 출발점일 뿐이고, 플레이어가 메뉴에서 바꾼다.
     */
    struct HardwareDetectionResult
    {
        uint32 _logicalCoreCount{ 0 }; ///< 논리 코어 수(하이퍼스레딩 포함)
        uint32 _systemMemoryMb{ 0 };   ///< 물리 메모리(MB). 못 읽으면 0 입니다.
        uint32 _gpuMemoryMb{ 0 };      ///< 기본 어댑터의 전용 비디오 메모리(MB). 못 읽거나 소프트웨어 어댑터면 0 입니다.
        string _gpuName{};             ///< 기본 어댑터 이름. 못 읽으면 비어 있습니다.
    };
} // namespace sw

namespace sw
{
    /** @brief 지금 기계의 사양을 읽습니다. */
    struct SW_API HardwareDetection
    {
        /** @brief 코어 수와 메모리를 읽습니다. 읽지 못한 필드는 0 입니다. 수 마이크로초가 걸립니다. */
        static HardwareDetectionResult probe();

        /**
         * @brief 기본 모니터가 내는 해상도 목록을 @p outListResolution 에 채웁니다(작은 것부터, 중복 없음). 읽지 못하면 비워 둡니다.
         * @details 옵션 메뉴의 해상도 선택지가 쓴다. Windows 는 `EnumDisplaySettings` 로 읽고, Linux 는 아직 읽지 않아 스키마의 고정 목록이 쓰인다.
         */
        static void probeDisplayResolutions( vector<int2>& outListResolution );
    };
} // namespace sw

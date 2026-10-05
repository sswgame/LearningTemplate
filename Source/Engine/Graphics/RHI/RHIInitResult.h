/**
 * @file RHIInitResult.h
 * @brief RHI 를 세운 결과와, "이 기계에서 그 백엔드를 못 돌린다" 를 프로세스 밖(시험 · 스크립트)으로 알리는 종료 코드입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @brief RHI 를 세운 결과입니다. 실패가 환경 탓(이 빌드 · 드라이버에 없다)인지, 결함일 수 있는지를 가릅니다.
     * @details `AppSmokeTest` · `Scripts/common/AppRun.py` 는 로그 문구가 아니라 이 값에서 나온 종료 코드(`kRhiUnusableHereExitCode`)로 판을 건너뜁니다.
     */
    enum class RHIInitResult : uint8
    {
        NotStarted,        ///< 아직 세우지 않았습니다(헤드리스 작업 · RHI 앞 단계의 실패)
        Succeeded,         ///< 디바이스가 섰습니다
        BackendNotBuilt,   ///< 이 빌드 · 플랫폼에 그 백엔드가 없습니다(리눅스의 DX12 · DX11, Shipping 이 링크하지 않은 백엔드) — 환경
        DriverUnsupported, ///< 드라이버가 백엔드에 필요한 기능을 주지 않습니다(GL 의 `GL_ARB_gl_spirv` — WSLg Mesa) — 환경
        Failed,            ///< 그 밖의 실패입니다 — 결함일 수 있습니다
    };

    /** @brief App 이 "이 기계에서 그 백엔드를 못 돌린다" 로 끝날 때의 종료 코드입니다(automake · CTest `SKIP_RETURN_CODE` 의 건너뜀 관례). */
    inline constexpr int32 kRhiUnusableHereExitCode = 77;

    /**
     * @struct RHIInitResultUtil
     * @brief `RHIInitResult` 에 대한 질문을 한 자리에 둡니다.
     */
    struct RHIInitResultUtil
    {
        /** @brief 이 기계 · 빌드가 그 백엔드를 못 돌린다는(환경) 결과면 true 입니다. 결함일 수 있는 `Failed` 는 false 입니다. */
        static constexpr bool isUnusableHere( RHIInitResult result )
        {
            return result == RHIInitResult::BackendNotBuilt || result == RHIInitResult::DriverUnsupported;
        }
    };
} // namespace sw

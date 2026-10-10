/**
 * @file ScreenshotPathUtil.h
 * @brief 실행 중 스크린샷(`RenderThread::requestScreenshot`)의 기본 파일 경로와 개발 명령 `screenshot` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/EngineMinimal.h"

namespace sw
{
    /** @brief 경로에 쓰는 지역 시각입니다(달 1 ~ 12, 날 1 ~ 31). */
    struct ScreenshotLocalTime
    {
        int32 _year{ 0 };
        int32 _month{ 0 };
        int32 _day{ 0 };
        int32 _hour{ 0 };
        int32 _minute{ 0 };
        int32 _second{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ScreenshotPathUtil
     * @brief 스크린샷 기본 경로 `Saved/Screenshots/<yyyyMMdd-HHmmss>.png` 를 만듭니다(언리얼 HighResShot · 유니티 Game view 스크린샷과 같은 PNG).
     * @details 작업 폴더 기준 상대 경로입니다. 시험용 `-gv_screenshot` 은 PPM 계약 그대로입니다.
     */
    struct SW_API ScreenshotPathUtil
    {
        /** @brief 저장 폴더(`path::kSavedFolder`) 아래 스크린샷 폴더 이름입니다. */
        static constexpr const utf8* kScreenshotFolderName = "Screenshots";

        /** @brief @p localTime 의 기본 경로입니다. */
        static string makeDefaultPath( const ScreenshotLocalTime& localTime );
        /** @brief 지금 지역 시각의 기본 경로입니다. */
        static string makeDefaultPathNow();
        /** @brief 지금 지역 시각입니다(BugIt 폴더 이름도 같은 꼴을 쓴다). */
        static ScreenshotLocalTime getLocalTimeNow();
    };
} // namespace sw

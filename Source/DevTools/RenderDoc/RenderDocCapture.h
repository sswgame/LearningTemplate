/**
 * @file RenderDocCapture.h
 * @brief RenderDoc in-app API 로 프레임 캡처를 코드에서 시작합니다(네 백엔드 모두). 개발 도구 `DevTools` 라 배포 빌드에는 없습니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "DevTools/DevToolsAPI.h"

namespace sw
{
    /**
     * @struct RenderDocCapture
     * @brief RenderDoc 이 이 프로세스에 붙어 있으면 캡처와 재생 UI 를 부릅니다(언리얼 RenderDoc 플러그인, 유니티 Frame Capture 버튼에 해당합니다).
     * @details 붙어 있으려면 RenderDoc UI 로 띄웠거나(`renderdoc.dll` 이 이미 로드되어 있습니다), `-renderdoc` 로 **RHI 디바이스를 만들기 전에** 로드했어야 합니다
     *          (RenderDoc 은 그래픽 API 를 만들 때 끼어듭니다). 캡처 파일은 `Saved/RenderDoc/capture_frame<N>.rdc` 이고 화면 글자(오버레이)는 끕니다.
     */
    struct SW_DEVTOOLS_API RenderDocCapture
    {
        /**
         * @brief 이미 로드된 RenderDoc 을 찾고, 없고 @p bLoadIfMissing 이면 설치 경로에서 로드합니다. RHI 디바이스를 만들기 전에 부릅니다(개발 도구의 기동 확장 — RHI 초기화 앞).
         * @details 두 번째 부르면 아무것도 하지 않습니다. 찾지 못해도 오류가 아닙니다. `isAvailable` 이 false 일 뿐입니다.
         */
        static void initialize( bool bLoadIfMissing );
        /** @brief API 를 얻었으면 true 입니다. */
        static bool isAvailable();
        /** @brief 다음 프레임(Present 하나)을 캡처합니다. 쓸 수 없으면 아무것도 하지 않습니다. */
        static void triggerCapture();
        /** @brief RenderDoc 재생 UI 를 띄워 이 프로세스에 붙입니다(이미 붙어 있으면 앞으로). 쓸 수 없거나 실패하면 false 입니다. */
        static bool launchReplayUI();
    };
} // namespace sw

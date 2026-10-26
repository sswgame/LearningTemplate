/**
 * @file SystemExternalBrowser.h
 * @brief `IExternalBrowser` 의 OS 구현입니다. 시스템 기본 브라우저로 주소를 엽니다(`Process::launchURL` — Windows `ShellExecuteW`, 리눅스 `xdg-open`).
 * @details - 받는 주소는 `http://` 와 `https://` 뿐입니다. 다른 스킴(`file:`, `javascript:`, 사용자 정의 프로토콜)은 OS 가 다른 프로그램을 띄우므로 거절합니다.
 *          - RFC 3986 의 글자(비예약, 예약, `%`) 가운데 따옴표, 백슬래시, `$`, 백틱을 뺀 것만 받습니다(주소는 인자 하나로 넘어가 셸을 거치지 않지만, 잘못 만든 주소를 일찍 거른다).
 *            외부 로그인 주소는 쿼리를 퍼센트 인코딩하므로(`HTTPUtil::encodePercent`) 이 규칙에 걸리지 않습니다.
 *          - 기다리지 않습니다. 알 수 있는 실패는 "열지 못했다" 뿐입니다.
 */
#pragma once
#include "Core/Container/string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Account/Shared/API/PlatformLoginClient.h"

namespace sw
{
    /**
     * @class SystemExternalBrowser
     * @brief 시스템 기본 브라우저로 주소를 여는 구현입니다. 상태가 없으므로 어느 스레드에서나 부를 수 있습니다.
     */
    class SW_KIT_API SystemExternalBrowser final : public IExternalBrowser
    {
    public:
        [[nodiscard]] bool openURL( string_view url ) override;

        /** @brief 명령 줄에 실어도 되는 `http(s)://` 주소면 true 입니다. */
        static bool isOpenableURL( string_view url );
    };
} // namespace sw

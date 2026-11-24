#include "pch.h"

#include "GameFramework/Runtime/Foundation/Flow/IContentConfiguration.h"

// 준비 단계 증표의 테스트 입구 — `Tests/` 아래라 배포 구성은 짓지 않는다(이 정의가 없어 부르는 코드가 링크되지 않는다). 감사 K-9.

namespace sw
{
    const ContentPreparationToken& getContentPreparationTokenForTest()
    {
        static const ContentPreparationToken s_token{};
        return s_token;
    }
} // namespace sw

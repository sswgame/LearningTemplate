#include "pch.h"

#include "GameFramework/Runtime/Online/Service/Tests/OnlineHostTestUtil.h"

namespace sw::test
{
    namespace
    {
        // 이름 공간 범위 객체다 — DLL 을 올릴 때 레지스트리에 오르고 내릴 때 빠진다(키트 항목과 같은 수명). 함수 안 정적 객체면 처음 쓸 때 올라 실행 중에 온라인 스키마 해시가 바뀐다.
        const OnlineProtocolEntry kTestLoginProtocol{ "OnlineHostTestUtil.Login", TestEmptyMethodTable<OnlineMethodRange::kGame>{}, OnlineMessageTypes<>{} };
    } // namespace

    const OnlineProtocolEntry& getTestLoginProtocol()
    {
        return kTestLoginProtocol;
    }
} // namespace sw::test

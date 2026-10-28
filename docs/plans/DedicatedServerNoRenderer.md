# 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인

> 상태: 구현중 · 선행: 없음 · 영역: network · 진행: 1/2

전용 서버 빌드에서 렌더러 폴더를 짓지 않아 서버의 군살을 줄이고, 렌더러를 부르는 남은 코드를 컴파일러가 찾게 한다. 렌더러 제외(S1)와 Debug-Server 확인은 끝났고, Shipping-Server 는 구성만 있고 **빌드하지 않았다**.

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 렌더러 폴더를 Server 타깃에서 뺀다 | 끝(Debug-Server 빌드) |
| S2 | Debug · Shipping 서버 빌드로 링크 오류를 잡고 테스트를 돌리고 크기 · 링크 시간을 잰다 | Debug-Server 끝, Shipping-Server 빌드 남음 |

## 설계

- 정본은 목록 하나 — `cmake/Engine/BuildLayout.cmake` 의 `SW_UNBUILT_ENGINE_FOLDERS`. Server 타깃은 `Renderer` 를 넣는다.
- Engine 은 그 폴더의 .cpp 를 빌드와 리플렉션 코드젠에서 뺀다. 헤더는 남긴다(`Engine.h` · `ModuleHost.h` 가 값 타입을 읽는다).
- 그 폴더를 부르는 본문은 `SW_WITH_CLIENT_CODE` 로 가른다(`Engine.cpp` · `HeadlessSubsystem.cpp` · `ModuleHost.cpp`). GameFramework · Games · Server 는 렌더러 헤더를 include 하지 않는다.
- 폴더 밖 .cpp 도 렌더러 헤더는 `SW_WITH_CLIENT_CODE` 안에서만 include 한다. 내보낸 클래스의 인라인 멤버는 include 한 TU 마다 만들어져, 쓰지 않아도 include 만으로 서버 링크가 깨지기 때문이다.
- 테스트는 그 폴더의 헤더를 include 하는 파일을 뺀다. `ServerBootTest` 는 서버에서 렌더 단계가 돌지 않음을 본다.
- RHI 앞단(`Engine/Graphics/RHI`)은 남긴다. 메시 · 텍스처 · 머티리얼이 `IRHIDevice` 를 들고 서버는 디바이스 없이 그 객체를 만든다.

## 남은 일

1. **첫 일** — `Ninja-Shipping-Server` 를 실제로 짓는다. 프리셋 · 빌드 구성 분기는 이미 있다(서버 배포본은 Info 로그, RHI 백엔드 링크 없음, 쿠킹 호스트가 Server). 선택 항목인 "전" 크기 · 링크 시간은 앞 커밋으로 한 번 더 지어야 해서 하지 않았다.
2. S3: `CheckX11Isolation` 4번 규칙은 서버 빌드에 들어가는 폴더의 .cpp 가 X11 · Wayland · xkbcommon · xcb 헤더를 include 하면 `SW_WITH_CLIENT_CODE` 로 감싸게 한다. 서버는 X11 을 링크하지 않아 감싸지 않은 .cpp 가 생기면 Windows 에서는 안 보이고 리눅스 서버 빌드만 깨진다. 현재 소스는 모두 통과한다.
3. 함정: 서버용 크래시 보고 전송 동의(운영 설정)는 `OperationsAndDeployment` 쪽이다.

# 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-2 · 진행: 1/3

게임별 기동 확인은 ServerTest `EveryBuiltGameStartsAsADedicatedServer`(Debug 9 게임 77 s)가 있다. 이 문서는 서버 빌드의 군살(결정 기록 `D7-2.1`)과 리눅스 확인을 다룬다.

## 설계

- 정본은 목록 하나다 — `cmake/Engine/BuildLayout.cmake` 의 `SW_UNBUILT_ENGINE_FOLDERS`. Server 타깃(`SW_TARGET_TYPE=Server`, Debug · Shipping 모두)은 `Renderer` 를 넣는다.
- Engine(`Source/Engine/CMakeLists.txt`)은 그 폴더의 .cpp 를 빼고(`sw_excludeUnbuiltSources`) 리플렉션 코드젠에서도 뺀다(`EXCLUDE_REGEX`). 헤더는 남긴다(`Engine.h` · `ModuleHost.h` 가 값 타입 `HostViewTargets` · 델리게이트를 읽는다).
- 그 폴더를 부르는 본문은 `SW_WITH_CLIENT_CODE` 로 가른다: `Engine.cpp`(렌더 단계 추가 · 패킷 제출 · 초상화 · 사진 종료 코드 · 백엔드 교체 · Present 훅), `HeadlessSubsystem.cpp`(셰이더 쿠킹), `ModuleHost.cpp`(렌더 스레드 비우기). GameFramework · Games · Server 는 렌더러 헤더를 include 하지 않는다(전수 grep).
- 폴더 밖 .cpp 는 렌더러 헤더를 `SW_WITH_CLIENT_CODE` 안에서만 include 한다. 내보낸(SW_ENGINE_API) 클래스의 인라인 멤버는 include 한 번역 단위마다 만들어지므로 쓰지 않아도 include 만으로 Server 링크가 진다(Engine.cpp 가 `GPUScene` · `GPUSceneBuilder` · `VfxParticleRenderer` 의 인라인 멤버로 그랬다).
- 테스트는 그 폴더의 헤더를 include 하는 파일을 뺀다(`sw_excludeSourcesIncludingUnbuiltEngineFolders`). `ServerBootTest` 는 Server 타깃에서 렌더 단계 이름이 "돌지 않은 단계" 줄에 없음을 본다.
- RHI 앞단(`Engine/Graphics/RHI`)은 남긴다 — 메시 · 텍스처 · 머티리얼이 `IRHIDevice` 를 들고 서버는 디바이스 없이 그 객체를 만든다. 백엔드 장치 소스는 이미 배포 서버에서 빠진다.

## 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 위 설계대로 렌더러 폴더를 Server 타깃에서 뺀다 | 완료(Debug-Server 빌드) |
| S2 | `Ninja-Debug-Server` · `Ninja-Shipping-Server` 빌드로 링크 오류를 잡고 `ServerTest` · `EngineTest_NoGPU` 를 돌리고 실행 파일 크기 · 링크 시간을 전후로 잰다 | Debug-Server 끝(2026-10-22): `all AllTests` 경고 0, ServerTest 16 / 16, EngineTest_NoGPU 다섯 샤드, 크기 Engine.dll 22.6 MB · GameFramework.dll 8.5 MB · Server.exe 1.2 MB. Shipping-Server 구성은 코드로 끝났다(빌드는 안 돌렸다) — 프리셋 `Ninja-Shipping-Server` · `WSL-Shipping-Server` · `CI-Shipping-Server`(`CMakePresets.json`, 각 빌드 프리셋 포함)와 빌드 구성 분기(`BuildLayout.cmake`: 서버 배포본은 Info 로그를 남긴다, `Source/Engine/CMakeLists.txt` 5절: RHI 백엔드를 하나도 링크하지 않는다, `AssetAndToolTargets.cmake`: 쿠킹 호스트가 Server, `Source/Server/CMakeLists.txt`)이 이미 있다. 남음: 그 구성을 실제로 짓는 일(CI `CI-Shipping-Server` 두 잡이 처음 짓는다)과 선택 항목 "전" 크기 · 링크 시간(S1 앞 커밋으로 Server 를 한 번 더 지어야 해서 하지 않았다) |
| S3 | 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로 libX11 없음 · `ServerTest` — CI 리눅스 잡 몫(로컬 WSL 빌드는 하지 않는다, 사용자 결정) | CI 쪽은 끝났다 — 잡 `Build Linux Dedicated Server Shipping (Clang)`(`CI-Shipping-Server`)이 짓고 `-L nogpu` 테스트(ServerTest 포함)를 돌리며 단계 `Dedicated server has no X11 dependency (Linux)`(처음이라 continue-on-error)가 `readelf -d` 로 `Bin` 의 Server · .so 의 libX11 · libxcb 요구를 본다. 첫 실행을 기다림(초록이 이어지면 단계의 `continue-on-error` 를 지운다). 2026-10-28: 작업 트리에서 `.github/` 가 지워진 상태(다른 작업자의 미커밋 삭제)라 이 문서는 HEAD 의 ci.yml 을 가리킨다 |

## 남은 것

- S2: 정적 검사로 닫은 것(렌더러 .cpp 의 `A::b(` 정의 가운데 렌더러 헤더에 없는 타입의 것 없음, 렌더러가 정의하고 폴더 밖이 `extern` · `SW_DECLARE_*GLOBAL*` 로 선언한 전역 변수 없음(`gv_rhiBackend` 를 쓰는 `Engine::applyPendingBackendChange` 는 갈랐다), 렌더러의 정적 등록은 개발 명령 다섯(`SW_DEV_COMMAND`)뿐이고 리플렉션 타입을 밖에서 이름으로 찾는 코드 없음)에서 빌드가 더 잡은 것은 include 로 생기는 인라인 멤버(Engine.cpp)와 렌더러의 개발 명령 `bugit` 을 기대한 테스트(`DevCommandRegistryTest`)였다.
- S3: `CheckX11Isolation` 의 4번 규칙 — 서버 빌드에 들어가는 폴더(`common/ServerBuild.py` 가 모듈 매니페스트 `_listTarget` 과 `Source/<X>` · `Test/<X>` CMakeLists 의 `SW_TARGET_TYPE` 서버 가드에서 읽는다 — 소속을 모르는 새 `Source/<X>` 폴더는 게이트가 스스로 실패, RHI 백엔드 폴더 제외, `CheckModuleTargets` 의 매니페스트 밖 폴더 손 표도 같은 맵으로 합쳤고 파일 단위 예외 한 줄만 `ServerBuild.py` 에 이유와 함께 둔다)의 .cpp 가 X11 · Wayland · xkbcommon · xcb 헤더를 include 하면 `SW_WITH_CLIENT_CODE` 로 감싸야 한다. 서버는 X11 을 링크하지 않아 감싸지 않은 .cpp 가 하나 생기면 Windows 빌드에서는 안 보이고 리눅스 서버 빌드만 선다. 현재 소스는 모두 통과한다. 런타임 `readelf` 확인은 CI 몫이다.
- 함정: 서버용 크래시 보고 전송 동의(운영 설정)는 `OperationsAndDeployment` 쪽이다.

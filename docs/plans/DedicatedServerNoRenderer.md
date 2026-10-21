# 전용 서버 타깃에서 렌더러 빼기와 리눅스 확인

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: D7-2 · 진행: 1/3

게임별 기동 확인은 ServerTest `EveryBuiltGameStartsAsADedicatedServer`(Debug 9 게임 77 s)가 있다. 이 문서는 서버 빌드의 군살(결정 기록 `D7-2.1`)과 리눅스 확인을 다룬다.

## 설계

- 정본은 목록 하나다 — `cmake/Engine/BuildLayout.cmake` 의 `SW_UNBUILT_ENGINE_FOLDERS`. Server 타깃(`SW_TARGET_TYPE=Server`, Debug · Shipping 모두)은 `Renderer` 를 넣는다.
- Engine(`Source/Engine/CMakeLists.txt`)은 그 폴더의 .cpp 를 빼고(`sw_excludeUnbuiltSources`) 리플렉션 코드젠에서도 뺀다(`EXCLUDE_REGEX`). 헤더는 남긴다 — `Engine.h` · `ModuleHost.h` 가 값 타입(`HostViewTargets` · 델리게이트)을 읽는다.
- 그 폴더를 부르는 본문은 `SW_WITH_CLIENT_CODE` 로 가른다: `Engine.cpp`(렌더 단계 추가 · 패킷 제출 · 초상화 · 사진 종료 코드 · 백엔드 교체 · Present 훅), `HeadlessSubsystem.cpp`(셰이더 쿠킹), `ModuleHost.cpp`(렌더 스레드 비우기). GameFramework · Games · Server 는 렌더러 헤더를 include 하지 않는다(전수 grep, 2026-10-22).
- 테스트는 그 폴더의 헤더를 include 하는 파일을 뺀다(`sw_excludeSourcesIncludingUnbuiltEngineFolders`, 뺀 .cpp 의 같은 이름 헤더를 include 하는 파일도 따라 빠진다). `ServerBootTest` 는 Server 타깃에서 렌더 단계 이름이 "돌지 않은 단계" 줄에 없음을 본다.
- RHI 앞단(`Engine/Graphics/RHI`)은 남긴다 — 메시 · 텍스처 · 머티리얼이 `IRHIDevice` 를 들고, 서버는 디바이스 없이 그 객체를 만든다. 백엔드 장치 소스는 이미 배포 서버에서 빠진다.

## 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| S1 | 위 설계대로 렌더러 폴더를 Server 타깃에서 뺀다 | 코드 완료(빌드 안 함) |
| S2 | `Ninja-Debug-Server` · `Ninja-Shipping-Server` 빌드로 링크 오류를 잡고, `ServerTest` · `EngineTest_NoGPU` 를 돌리고, 실행 파일 크기 · 링크 시간을 전후로 잰다 | 남음 |
| S3 | 리눅스 서버(`WSL-*-Server`) 빌드 · `readelf -d` 로 libX11 없음 · `ServerTest` — CI 리눅스 잡 몫(로컬 WSL 빌드는 하지 않는다, 사용자 결정) | 남음 |

## 남은 것

- S2: 확인 못 한 위험 — 렌더러 .cpp 에만 정의가 있는데 폴더 밖 헤더에 선언된 함수, 렌더러 폴더의 정적 등록(에셋 캐시 `RenderPipelineAssetCache` · 개발 명령 · 탐침)을 서버 기동이 찾는지. 링크 · 기동 로그로 본다.
- 함정: 서버용 크래시 보고 전송 동의(운영 설정)는 `OperationsAndDeployment` 쪽이다.

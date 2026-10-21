# SW Engine (게임 및 에디터 엔진 템플릿)

[![CI (main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml?query=branch%3Amain)
[![Windows](https://img.shields.io/badge/Windows-clang--cl%20(Debug%20%7C%20Shipping)-0078D6?logo=windows&logoColor=white)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![Linux](https://img.shields.io/badge/Linux-Clang%20(Debug%20%7C%20ASan%20%7C%20TSan%20%7C%20Shipping)-FCC624?logo=linux&logoColor=black)](https://github.com/sswgame/LearningTemplate/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/Language-C%2B%2B17-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)

C++17로 만든 학습용 게임 엔진과 에디터입니다. 상용 엔진이 안에서 하는 일을 작은 크기로 직접 구현해 두었기 때문에, 코드를 따라 읽으며 게임 엔진의 구조를 배울 수 있습니다.
이 엔진 위에 장르가 다른 테스트 게임 여러 개가 올라가 있어서, 엔진 기능이 실제 게임에서 어떻게 쓰이는지도 함께 볼 수 있습니다([게임 목록](Source/Games/README.md)).

## 지금 진행 상황

아래 표는 `docs/plans` 문서의 머리 줄과 [로드맵](docs/plans/Roadmap.md)에서 `py -3 -m Scripts status-board --write README.md` 로 만든 것입니다. 손으로 고치지 않습니다.

<!-- status-board:start -->
| 순위 | 업무(계획 문서) | 상태 | 진행 | 선행 | 영역 | 옛번호 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | [DataDriven](docs/plans/DataDriven.md) — 데이터 주도 구조 정리 계획 (2026-10-16) | 구현중 | 12/14 | 없음 | engine | C-30, C-36 |
| 2 | [Vfx](docs/plans/Vfx.md) — 파티클 · VFX 구현 단위 (`D2-11`) | 구현중 | 6/8 | 없음 | graphics | D2-11 |
| 3 | [CameraRendererIntegration](docs/plans/CameraRendererIntegration.md) — 카메라의 렌더러 쪽 연결과 다중 뷰 검증 | 구현중 | 1/4 | 없음 | gameplay | D4-5 |
| 5 | [SerializerSilentSkips](docs/plans/SerializerSilentSkips.md) — 직렬화가 조용히 넘기는 곳 (2026-10-21) | 구현중 | 4/4 | 없음 | engine | C-66 |
| 6 | [StructureSimplification](docs/plans/StructureSimplification.md) — 구조 단순화 계획 (2026-10-20) | 설계 | 3/21 | 없음 | engine | 없음 |
| 7 | [ScriptsStructure](docs/plans/ScriptsStructure.md) — Scripts 구조 — import 루트 하나 · 3층 · 층 게이트 (2026-10-21) | 측정대기 | 10/10 | 없음 | project | 없음 |
| 8 | [BuildSpeed](docs/plans/BuildSpeed.md) — 빌드 속도 계획 | 구현대기 | 6/13 | 다른 작업이 모두 끝난 뒤 혼자 | quality | C-2 |
| - | [AsyncResourceLoading](docs/plans/AsyncResourceLoading.md) — 리소스 비동기 로드와 디더 등장 계획 (2026-10-20) | 설계 | 1/6 | 없음 | engine | 없음 |
| - | [EditorCharacterEditor](docs/plans/EditorCharacterEditor.md) — 캐릭터 편집 창 구현 계획 (D4-8 ⑥) | 구현대기 | 0/6 | EditorMultiWorld | gameplay | D4-8 |
| - | [AudioEngineExtensions](docs/plans/AudioEngineExtensions.md) — 오디오 엔진 확장 계획 (2026-10-21) | 구현중 | 7/8 | AudioChannelLayout | audio-ai-localization | D5-1 |
| - | [ClothFollowups](docs/plans/ClothFollowups.md) — 천(`ClothComponent`)의 남은 것 | 구현중 | 1/3 | 없음 | engine | D1-4 |
| - | [ClusteredLighting](docs/plans/ClusteredLighting.md) — 클러스터드 라이팅 구현 단위 (`D2-13`) | 구현중 | 5/6 | 없음 | graphics | D2-13 |
| - | [EditorMultiWorld](docs/plans/EditorMultiWorld.md) — 다중 월드 · 에디터 툴 창 구현 계획 (D3-7) | 구현중 | 5/6 | 없음 | editor | D3-7 |
| - | [EnginePartition](docs/plans/EnginePartition.md) — 엔진 분할 계획 | 구현중 | 4/6 | 없음 | engine | C-1 |
| - | [GamepadVirtualCursor](docs/plans/GamepadVirtualCursor.md) — 패드 가상 커서의 남은 것 | 구현중 | 1/3 | 없음 | gameplay | D4-3 |
| - | [GpuVerification](docs/plans/GpuVerification.md) — Linux 와 GPU 검증 구조의 남은 일 (2026-10-18) | 구현중 | 1/2 | 없음 | quality | C-41 |
| - | [IndirectLighting](docs/plans/IndirectLighting.md) — 간접 조명 구현 단위 (`D2-12`) | 구현중 | 5/7 | ClusteredLighting | graphics | D2-12 |
| - | [LongFunctions](docs/plans/LongFunctions.md) — 긴 함수 정리 기준 (2026-10-18) | 구현중 | 해당없음 | 없음 | project | C-18 |
| - | [OnlineKits](docs/plans/OnlineKits.md) — 온라인 키트 마감 계획 (2026-10-21) | 구현중 | 22/34 | 없음 | network | D7-5 |
| - | [PairedAnimation](docs/plans/PairedAnimation.md) — 페어드 애니메이션(여러 개체가 한 타임라인으로 도는 애니메이션) | 구현중 | 6/6 | 없음 | gameplay | D4-6 |
| - | [RigEditing](docs/plans/RigEditing.md) — 후처리 리그 편집 저장과 기즈모 | 구현중 | 2/3 | 없음 | gameplay | D4-6 |
| - | [ServerDebug](docs/plans/ServerDebug.md) — 서버 디버깅 계획 — 에디터에서 서버 · 멀티플레이를 손쉽게 보는 길 | 구현중 | 4/15 | 없음 | network | D7-3 |
| - | [ShooterCharacterIntegration](docs/plans/ShooterCharacterIntegration.md) — Shooter3D 캐릭터 통합의 남은 것 | 구현중 | 1/4 | 없음 | gameplay | D4-10 |
| - | [StorageOwnership](docs/plans/StorageOwnership.md) — 저장소 소유 정리 — 종료 전용 해제 함수 없애기 | 구현중 | 0/6 | 없음 | engine | 없음 |
| - | [TerrainEditCost](docs/plans/TerrainEditCost.md) — 지형 LOD 교체 · 지형 쓰기 · 식생 다시 계산의 비용 (2026-10-21) | 구현중 | 2/4 | 없음 | graphics | D2-5.6, D2-5.7 |
| - | [WorldStreaming](docs/plans/WorldStreaming.md) — 월드 스트리밍(D1-7) — 설계와 구현 단위 | 구현중 | 5/7 | 없음 | engine | D1-7 |
| - | [AudioChannelLayout](docs/plans/AudioChannelLayout.md) — 오디오 채널 레이아웃 계획 (2026-10-20) | 측정대기 | 5/5 | 없음 | audio-ai-localization | D5-1 |
| - | [StructuralOptimization](docs/plans/StructuralOptimization.md) — 구조 최적화 측정 표 (A · B) | 측정대기 | 해당없음 | 없음 | engine | 없음 |
| - | [MotionMatching](docs/plans/MotionMatching.md) — 모션 매칭 (보류) | 보류 | 해당없음 | 없음 | gameplay | D4-16 |

후보 문서 9개 · 후보 절 131개
<!-- status-board:end -->

이 저장소에서 볼 수 있는 것은 다음과 같습니다.

- **핫 리로드.** 개발 빌드에서 게임, 에디터, 장르 키트, 렌더링 백엔드는 실행 중에 다시 로드되는 DLL입니다. 게임을 끄지 않고 코드를 고쳐 다시 빌드할 수 있습니다.
- **배포 빌드.** 배포 빌드(Shipping)는 에디터를 빼고 모든 모듈을 실행 파일 하나에 정적 링크합니다.
- **네 가지 그래픽 API.** DirectX 11, DirectX 12, Vulkan, OpenGL을 같은 렌더러 위에서 바꿔 가며 실행할 수 있습니다.
- **리플렉션.** 헤더의 매크로를 빌드할 때 읽어 타입 정보를 만듭니다. 씬 저장, 에디터 인스펙터, 핫 리로드가 이 정보를 씁니다.
- **기계가 지키는 규칙.** 이름 규칙, 엔진 계층, 문서 경로를 린트가 검사합니다. 같은 검사가 커밋 훅과 CI에서 돕니다.

Windows(clang-cl)와 Linux(Clang)를 지원합니다. macOS는 지원하지 않으며, Apple 컴퓨터에서는 CMake 구성 단계가 오류로 멈춥니다.

## 빌드

```powershell
py -3 Scripts/setup/SetupEnvironment.py       # LLVM, Ninja 받기
py -3 Scripts/setup/SetupVcpkg.py --install   # vcpkg 라이브러리 설치
py -3 Scripts/setup/InstallGitHooks.py        # 커밋 전에 규칙과 포맷을 검사하는 훅 설치
cmake --preset Ninja-Debug
cmake --build --preset Ninja-Debug
```

필요한 도구, 프리셋 고르기, Linux와 WSL에서의 준비는 시작하기에 있습니다.

## 실행

```powershell
./build/Ninja-Debug/Bin/App.exe                   # 게임만
./build/Ninja-Debug/Bin/App.exe -EnableEditor     # 에디터까지
./build/Ninja-Debug/Bin/App.exe -vk               # 그래픽 API 고르기: -dx11, -dx12, -vk, -gl
ctest --test-dir build/Ninja-Debug -L nogpu --output-on-failure   # CI와 같은 테스트
```

그래픽 API를 고르지 않으면 Windows에서는 DirectX 12로 실행됩니다.
CI는 GPU 없이 도는 `nogpu` 테스트만 돌립니다. GPU나 창이 필요한 `hostgpu` 테스트는 GPU가 있는 PC에서 직접 돌려야 합니다. 방법은 [Test 문서](Test/README.md)에 있습니다.
CI가 무엇을 빌드하고 어떤 테스트를 돌리는지는 `.github/workflows/ci.yml` 이 원본입니다.

## 폴더

| 폴더 | 내용 |
| :--- | :--- |
| `Source/Core` | 로그, 메모리, 문자열, 태스크, 파일 같은 기반 라이브러리 |
| `Source/Engine` | 렌더링, 오브젝트와 씬, 리플렉션, 물리, 오디오, 입력 |
| `Source/RuntimeAPI` | 실행 파일과 모듈 사이의 C 인터페이스(헤더만 있음) |
| `Source/App` | 실행 파일. 프레임 순서와 모듈 로드, 핫 리로드를 맡습니다 |
| `Source/Server` | 창과 GPU 없이 게임을 돌리는 전용 서버 실행 파일 |
| `Source/Editor` | 개발 빌드에만 있는 ImGui 에디터 모듈 |
| `Source/GameFramework` | 여러 장르가 같이 쓰는 기반과 장르별 키트 |
| `Source/Games` | 테스트 게임. 빌드할 게임은 프리셋으로 고릅니다 |
| `Resource/` | 런타임 에셋. 파일과 폴더 이름은 모두 소문자입니다 |
| `Config/` | 엔진, 에디터, 게임, 서버 설정 |
| `Scripts/` | 파이썬 도구와 린트 |
| `cmake/` | CMake 모듈 |
| `Tools/` | ReflectionParser, 부하 테스트 봇, 내려받은 툴체인 |
| `Test/` | 테스트 실행 파일과 자체 테스트 프레임워크 |

## 문서

처음이라면 시작하기부터 읽으세요. 빌드부터 첫 게임 오브젝트를 만들고 핫 리로드하는 데까지 안내합니다.

- [문서 지도](docs/02_DocumentMap.md)는 문서마다 다루는 주제와 모든 모듈 README의 목록입니다.
- [AGENTS.md](AGENTS.md)는 코드 규칙의 원본입니다. [코딩 규칙 예시](docs/04_CodingGuidelines.md)는 같은 규칙을 예시와 함께 한국어로 풀어 씁니다.
- [설정](docs/07_Configuration.md)은 설정 값을 어느 파일에 두는지 설명합니다. 필드별 표는 생성 문서 [docs/Config](docs/Config/README.md)에 있습니다.
- [작업 방식](docs/11_Workflow.md) 6절은 할 일을 다루는 방식입니다. 남은 일은 `docs/plans/` 의 계획 문서와 `Candidates-<영역>.md` 에 있고, 목록은 `py -3 -m Scripts plans` 로 봅니다. 작업을 시작하기 전에 읽습니다.
- [검증과 측정](docs/08_Verification.md)은 작업을 끝내기 전에 돌릴 것과 측정 방법입니다. [결정 기록](docs/09_Decisions.md)은 정한 방향과 기각한 안입니다.
- [문서 쓰기 지침](docs/10_WritingDocs.md)은 문서를 쓰거나 고칠 때 따르는 규칙입니다.

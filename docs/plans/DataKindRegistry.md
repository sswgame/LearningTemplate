# 엔진 데이터 종류의 레지스트리 이전

> 상태: 구현중 · 선행: 없음 · 영역: editor · 옛번호: D3-1.2 · 진행: 1/5

엔진 데이터 종류(장면 · 머티리얼 · 파이프라인 …)는 아직 `ResourceDataSchemaTest` 의 표(`kArrDataKind`, `Test/EngineTest/Resource/TestResourceDataSchema.cpp`)에 있습니다.
로더가 `ResourceDataKindUtil::loadCatalog<T>` 인 줄은 판정 함수가 접미 하나뿐이라 기계적으로 `SW_RESOURCE_DATA_KIND` 로 옮길 수 있습니다.
옮기기로 한 까닭과 걸림돌 셋은 [결정 기록](../09_Decisions.md) 5-11 "엔진 데이터 종류의 레지스트리 이전과 Engine 열거자 이름 표 통합은 미룹니다" 에 있습니다.
`EnumNameTable` 의 Core 이전은 끝났습니다(`Source/Core/String/EnumNameTable.h`).

## 1. 옮기는 방법

- 등록 줄은 그 타입의 .cpp 끝(`SW_ASSET_TYPE` 옆)에 둡니다(예: `Source/Engine/Audio/AudioMixerDesc.cpp`). 접미 글자는 테스트 표의 판정 함수와 똑같이 씁니다.
- 같은 커밋에서 테스트 표의 줄 · 판정 함수 · 쓰지 않게 된 include 를 지웁니다.
- 레지스트리는 "표 + 레지스트리" 와 "레지스트리끼리" 겹침을 오류로 봅니다(`findKind`). 표에서 앞 줄이 먼저 맞아 가려지던 겹침이 드러날 수 있으니, 단위마다 `ResourceDataSchemaTest` 가 초록인지 본다.
- 테스트 전용 로더가 필요한 종류(scene · prefab · uidocument · animgraph …)는 표에 남깁니다.

## 2. 단위

| 단위 | 내용 | 선행 |
|---|---|---|
| DK1 | 오디오 셋(`audiomixer` · `audioevents` · `audiomusic`)을 각 타입 .cpp 로 옮김(빌드 · `ResourceDataSchemaTest` 확인 대기) | 없음 |
| DK2 | 애니메이션 셋(`referencepose` · `bodyshape` · `animationlod` · `animationcrowd` · `vertexanimation` · `lipsync` · `visemes` · `facial` · `socketkinds`) | DK1 |
| DK3 | UI 셋(`fontcatalog` · `uiscale` · `uithemes`) | DK1 |
| DK4 | 나머지 카탈로그(`navmeshsettings` · `telemetryschema` · `surfacechannels` · `destruction`) | DK1 |
| DK5 | 카탈로그가 아닌 로더 줄(함수 판정 · 테스트 전용 판단이 든 줄)을 줄마다 옮길지 표에 남길지 정함 | DK2 ~ DK4 |

## 3. 함정

층 규칙은 걸림돌이 아닙니다(레지스트리는 티어 5). 에디터 영역 패널의 자리 — 엔진 영역은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`, 키트 영역은 `GF_EditorPlugIn_<이름>`.
에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않습니다.

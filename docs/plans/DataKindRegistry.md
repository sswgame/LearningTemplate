# 엔진 데이터 종류의 레지스트리 이전

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-1.2 · 진행: 5/5

**남은 확인(통합에서 확인):** 매니페스트 id `resource-data-schema-test`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

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
| DK1 | 오디오 셋(`audiomixer` · `audioevents` · `audiomusic`)을 각 타입 .cpp 로 옮김(확인은 머리의 매니페스트 id) | 없음 |
| DK2 | 애니메이션 셋(`referencepose` · `bodyshape` · `animationlod` · `animationcrowd` · `vertexanimation` · `lipsync` · `visemes` · `facial` · `socketkinds`) — 옮김(확인은 머리의 매니페스트 id) | DK1 |
| DK3 | UI 셋(`fontcatalog` · `uiscale` · `uithemes`) — 옮김(같은 확인) | DK1 |
| DK4 | 나머지 카탈로그(`navmeshsettings` · `telemetryschema` · `surfacechannels` · `destruction`) — 옮김(같은 확인) | DK1 |
| DK5 | 카탈로그가 아닌 로더 줄(함수 판정 · 테스트 전용 판단이 든 줄)을 줄마다 옮길지 표에 남길지 정함 — 정함(4절). 순수 `loadFromResource` 이고 전용 편집기가 없는 `physicssettings` · `render2d` 둘만 더 옮김 | DK2 ~ DK4 |

## 3. DK5 결정 — 표에 남기는 줄과 까닭

레지스트리는 카탈로그 편집기(`CatalogPanel`)의 파일 목록이기도 하다. 그래서 "순수 로더인가" 만으로 옮기지 않고, 올렸을 때 같은 파일을 두 편집기가 쥐지 않는지를 함께 본다. 테스트 표(`kArrDataKind`)에 남긴 줄의 까닭은 넷 중 하나이고 표의 주석에도 적었다.

- 테스트 전용 판단을 든다: `scene` · `prefab` · `uidocument` · `uistylesheet` · `animgraph` · `sequence` · `tilemap` · `vrmavatar` · `usersettings`.
- 전용 편집기가 있다(올리면 두 편집기가 쥔다): `tileset` · `dialoguegraph` · `animgraph` · `bonemask` · `rig` · `inputmap` · `material` · `spriteclip` · `physicsasset` · `pairedanimation` · `skeleton`.
- 표 편집으로 고치는 카탈로그가 아니다: `clipdata` · `sourcemanifest`(JSON) · `scenario` · `automationlogs` · `enginedefaultassets` · `pipeline` · `renderpass`(렌더 구성 에셋) · 로컬라이제이션 문서 다섯(전용 현지화 도구가 쥔다).
- 읽는 데 다른 표가 먼저 필요하다: `sockets` · `fittables` · `partfit` · `notifytable` · `retargetprofile` · `bonelod` · `livelinkface*`.

새 종류가 늘면 이 넷에 안 드는 한 등록 줄로 올린다. 실행 확인은 통합이 한다: 빌드 뒤 `EngineTest --test_filter=ResourceDataSchemaTest.*`(옮긴 종류 열여덟이 모두 읽히고 한 파일에 둘 이상 맞는 겹침이 한 파일에 둘 이상 맞는 겹침이 없는지)와 `EditorTest` 의 카탈로그 패널 테스트.

## 4. 함정

층 규칙은 걸림돌이 아닙니다(레지스트리는 티어 5). 에디터 영역 패널의 자리 — 엔진 영역은 EditorModule, 기반 GameFramework 영역은 `GF_EditorPlugIn_Base`, 키트 영역은 `GF_EditorPlugIn_<이름>`.
에디터에 이미 있는 것(전역 변수 표, 커맨드 팔레트, 핫 리로드, Undo, Play 와 Step, InputReplay, 기즈모, RenderTargetPanel, 프로파일러와 Tracy)은 다시 만들지 않습니다.

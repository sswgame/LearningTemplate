# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 진행: 해당없음

에디터 패널 · 플러그인 · 시나리오에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 이미 계획 문서가 있는 UX 결함은 [EditorUX](EditorUX.md)에 있다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| PreviewWindowsRemaining | 미리보기 틀을 쓰는 나머지 툴 창(장르 도구) | 그 창의 편집 대상이 디자이너 일이 될 때 |
| MeshModifierCpuPreviewCost | 메시 모디파이어 CPU 미리보기 비용 | 측정 없이 착수 가능 |

## 기능

### PreviewWindowsRemaining
제목: 미리보기 씬 틀(EditorPreviewPanel)을 쓰는 나머지 툴 창
왜 필요한가: 머티리얼 · 프리팹 · VFX · 캐릭터 · 페어드 애니메이션 · 메시 · 애니메이션 클립 · 카메라 프리셋(`CameraPresetPanel`, 보기 전용, 뷰포트에 자유 카메라 `setFreeCamera` 를 더했다) · 래그돌(`RagdollPreviewPanel`, `.physics.xml` 보기 + 쓰러뜨리기 · 충격 · 부분 · 리셋) 창은 올랐다. 같은 모양(문서 패널 + 미리보기 씬 + 창 Undo)에 얹을 창이 남았다: 장르 도구. 창마다 독립이라 하나씩 계획 문서로 옮겨 착수한다.
착수 조건: 그 창의 편집 대상(예: `meadow.cameras.xml` 프리셋)이 디자이너의 일이 되어 인스펙터 · 콘텐츠 브라우저 썸네일로 모자랄 때.
남은 첫 일: 창마다 `EditorPreviewPanel` 파생 + 자산 종류 등록(`EditorAssetType` 표) + 미리보기 씬 스폰. `VfxPreviewPanel`(보기 전용, 약 230 줄)이 가장 작은 본보기다.
- 카메라 프리셋 창: 올랐다(보기 전용 — 프리셋 편집 · 저장은 카탈로그 편집기). 상태 있는 모드(입력 · 암 · 프레이밍 · 흔들림)는 `evaluatePreset` 이 안 풀어 정적 자리만 보인다.
- 래그돌 창 시나리오: 시나리오 파일(`Resource/**/automation`)을 걷어낸 뒤라 못 썼다. 탐침 `Editor.RagdollPreview*` 는 올려 두었다. 물리 에셋이 든 스켈레탈 프리팹(`shooter3d/prefabs/skeleton.prefab.xml` + `skeleton_warrior.physics.xml`)으로 처음 띄워 볼 때 바디가 서는지(`Editor.RagdollPreviewBuiltBodies`) 확인한다.
- 클립 창 시나리오: Empty 게임에 스켈레탈 애니메이터 프리팹이 없어 못 썼다.
함정: 같은 파일을 두 편집기가 쥐지 않는다(카탈로그 편집기가 쥔 종류는 보기 전용). 닫기 확인 · 창 Undo 는 틀이 맡는다.

### MeshModifierCpuPreviewCost
제목: 메시 모디파이어 CPU 미리보기의 비용 줄이기
왜 필요한가: GPU 프로그램 표에 없는 모디파이어가 스택에 있으면 미리보기가 매번 CPU 로 스택을 적용하고 새 메시(탄젠트 + 정점 풀 재업로드)를 만든다. 정점 갱신 API 는 렌더 스냅샷 수명 때문에 이 비용을 못 줄여 더하지 않았다.
착수 조건: 측정 없이 착수 가능. 베지어 끌개(`bezierattractor`)와 Rubber(`rubber`)는 올렸다. 곡선 매개변수(`Curve`)는 `packGPUParams` 기본 배치가 [0,1] 32 개 균등 표본(float4 8 개)으로 싣고 셰이더 `ModifierContext::curve` 가 선형 보간한다(키 목록을 싣는 것보다 기록 길이가 고정이라 단순하다. Constant 보간 곡선의 계단은 32 등분으로 무뎌진다). 남은 것은 스플라인 참조(`CurveDeform` · `CurveSculpt` · `RopeDeform`) · 텍스처 · 격자 · 충돌 같은 외부 입력을 읽는 모디파이어다 — 입력을 GPU 버퍼로 올리는 길(프로그램 옆 보조 버퍼)이 먼저 필요하다.
남은 첫 일: `Apply` 가 크면 CPU 로 내려가는 모디파이어를 GPU 프로그램 표(`MeshModifierGPUProgram`)에 올린다. `CpuMesh` 가 크면 탄젠트 재계산을 건너뛰는 옵션(`Mesh::setVerticesWithTangents`)을 본다.
함정: 메시를 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를 쥔다.

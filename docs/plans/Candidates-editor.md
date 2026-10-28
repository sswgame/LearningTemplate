# 에디터 후보

> 상태: 후보 · 선행: 없음 · 영역: editor · 진행: 해당없음

에디터 패널 · 플러그인 · 시나리오에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다. 이미 계획 문서가 있는 UX 결함은 [EditorUX](EditorUX.md)에 있다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| PopupWindowCapture | 캡처 영역을 팝업 OS 창까지 넓히기 | 팝업을 캡처로 확인할 시나리오가 생길 때 |
| PreviewWindowsRemaining | 미리보기 틀을 쓰는 나머지 툴 창 | 그 창의 편집 대상이 디자이너 일이 될 때 |
| PreviewVertexPoolSharing | 미리보기 렌더러의 정점 풀 공유 | 메모리 · 재오픈 시간이 눈에 띌 때 |
| MeshModifierCpuPreviewCost | 메시 모디파이어 CPU 미리보기 비용 | 10 만 정점 끌기가 프레임을 16 ms 넘게 늘릴 때 |

## 도구

### PopupWindowCapture
제목: 캡처 영역을 플랫폼 창 합집합으로 넓히기
왜 필요한가: 뷰포트 옵션 팝업은 `ViewportsEnable` 때문에 OS 창이 되는데 `CaptureWindow`(`AutomationWindowSteps.cpp`)는 주 창 클라이언트 사각형만 BitBlt 해서 팝업이 잘린다. 팝업은 의도된 동작이라 빈틈은 캡처 도구에 있다.
착수 조건: 팝업을 캡처로 확인하는 시나리오가 생길 때.
남은 첫 일: `CaptureWindow` 가 팝업 창 사각형까지 합쳐 찍게 한다.

## 기능

### PreviewWindowsRemaining
제목: 미리보기 씬 틀(EditorPreviewPanel)을 쓰는 나머지 툴 창
왜 필요한가: 머티리얼 · 프리팹 · VFX · 캐릭터 · 페어드 애니메이션 · 메시 · 애니메이션 클립 창은 올랐다. 같은 모양(문서 패널 + 미리보기 씬 + 창 Undo)에 얹을 창이 남았다: 카메라 프리셋 · 래그돌 · 장르 도구. 창마다 독립이라 하나씩 계획 문서로 옮겨 착수한다.
착수 조건: 그 창의 편집 대상(예: `meadow.cameras.xml` 프리셋)이 디자이너의 일이 되어 인스펙터 · 콘텐츠 브라우저 썸네일로 모자랄 때.
남은 첫 일: 창마다 `EditorPreviewPanel` 파생 + 자산 종류 등록(`EditorAssetType` 표) + 미리보기 씬 스폰. `VfxPreviewPanel`(보기 전용, 약 230 줄)이 가장 작은 본보기다.
- 카메라 프리셋 창: `EditorPreviewViewport` 가 오빗 카메라뿐이라 Fixed · Follow · Dolly 를 보이려면 뷰포트에 자유 카메라가 먼저 필요하다.
- 래그돌 창: 미리보기 정책의 물리(`_bPhysics`)와 `RagdollComponent` 스켈레톤 바인딩을 쓴다.
- 클립 창 시나리오: Empty 게임에 스켈레탈 애니메이터 프리팹이 없어 못 썼다.
함정: 같은 파일을 두 편집기가 쥐지 않는다(카탈로그 편집기가 쥔 종류는 보기 전용). 닫기 확인 · 창 Undo 는 틀이 맡는다.

### PreviewVertexPoolSharing
제목: 미리보기 렌더러의 정점 풀 · 머티리얼 버퍼 공유
왜 필요한가: 미리보기 씬마다 `FrameRenderer` 하나를 두고 PSO 캐시만 주 렌더러와 공유해 정점 풀 · 머티리얼 버퍼가 렌더러마다 따로다. 풀을 나누면 bindless 인덱스 공간이 갈라져 값을 따져 보아야 한다.
착수 조건: 미리보기 창 여럿의 GPU 메모리나 창 재오픈 첫 프레임 시간이 눈에 띌 때.
남은 첫 일: 공유 전 커밋과 견주어 정점 풀까지 나눌 값이 있는지 정한다(탐침 `Editor.ScenePreviewRenderers`).

### MeshModifierCpuPreviewCost
제목: 메시 모디파이어 CPU 미리보기의 비용 줄이기
왜 필요한가: GPU 프로그램 표에 없는 모디파이어가 스택에 있으면 미리보기가 매번 CPU 로 스택을 적용하고 새 메시(탄젠트 + 정점 풀 재업로드)를 만든다. 정점 갱신 API 는 렌더 스냅샷 수명 때문에 이 비용을 못 줄여 더하지 않았다.
착수 조건: 큰 메시(10 만 정점) 끌기가 Release 프로파일러(`Editor.MeshModifiers.*`)에서 프레임을 16 ms 넘게 늘릴 때.
남은 첫 일: `Apply` 가 크면 CPU 로 내려가는 모디파이어를 GPU 프로그램 표(`MeshModifierGPUProgram`)에 올린다. `CpuMesh` 가 크면 탄젠트 재계산을 건너뛰는 옵션(`Mesh::setVerticesWithTangents`)을 본다.
함정: 메시를 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를 쥔다.

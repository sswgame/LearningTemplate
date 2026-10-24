# 메시 모디파이어 미리보기 비용

> 상태: 측정대기 · 선행: 없음 · 영역: editor · 옛번호: D3-10 · 진행: 3/3

**남은 확인(통합에서 확인):** 매니페스트 id `mesh-modifiers-captures`, 매니페스트 id `mesh-modifier-preview-cost`(`Test/QA/VerifyBatch.json`). 확인이 끝나면 통합 담당이 이 문서를 닫는다.

`GF_EditorPlugIn_MeshModifiers` 는 56 id 를 등록했습니다. 구조 · 추가 방법 · 알려진 근사는 [모듈 README](../../Source/GameFramework/PlugIn/Editor/MeshModifiers/README.md), 구조는 같은 문서의 "셰이딩 미리보기" 에 있습니다.
남은 것은 측정이 먼저인 비용 문제와 아직 눈으로 보지 않은 캡처입니다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| MM1 | 패널 캡처 확인: 시나리오 `editor.meshmodifiers` 가 찍는 `meshmodifiers/panel.png` · `gizmo.png` · `nested.png` 를 돌려 눈으로 본다 | 끝남(매니페스트 id `mesh-modifiers-captures` — 통합이 돌려 PNG 세 장을 본다) |
| MM2 | 셰이딩 미리보기 비용 측정: 큰 메시(10 만 정점 이상)에서 매개변수 끌기 · 시간 재생 중 프레임 시간을 Release 로 잰다(매개변수가 바뀌면 고른 모디파이어부터 CPU 로 한 번 돌고, CPU 로 내려간 스택은 바뀔 때마다 정점 버퍼를 새로 만든다). 구간 이름을 프로파일러에 심었다: `Editor.MeshModifiers.Refresh`(전체) · `.Apply`(CPU 스택 적용) · `.CpuMesh`(CPU 결과 메시 만들기 — 정점 수는 카운터 `.CpuMeshVertices`) | 끝남(구간 이름 심음 — 매니페스트 id `mesh-modifier-preview-cost` 가 값을 낸다) |
| MM3 | MM2 가 크면 엔진 `Mesh` 에 정점 갱신 API(렌더 스레드와 이중 버퍼)를 더해 매번 새로 만들지 않게 한다. 작으면 취소 | 결정함(2절) — 값이 문턱을 넘을 때만 [Candidates-editor](Candidates-editor.md) `MeshModifierCpuPreviewCost` 를 연다 |

## 2. MM3 결정 — 정점 갱신 API 를 지금 더하지 않는 까닭

- **이중 버퍼는 지금의 "새 메시로 바꿔 끼우기" 와 같은 비용 구조다.** 렌더 스냅샷이 옛 메시를 쥐고 있어(`Mesh::setDynamic` 머리 주석: 정점은 제자리에서 바꾸지 않는다) 게임 스레드가 정점을 제자리에서 바꾸면 렌더 스레드가 읽는 사본과 겹친다.
  제자리 갱신을 안전하게 하려면 "렌더 스레드가 놓을 때까지 옛 버퍼를 쥐는" 이중 버퍼가 필요하고, 그 수명 관리가 `shared_ptr<Mesh>` 를 렌더 패킷이 쥐는 지금의 규칙과 같은 일을 한다.
- **API 가 줄이는 것은 Mesh 껍데기 할당뿐이다.** 비용의 본체는 (1) CPU 스택 적용(`.Apply`) (2) 탄젠트 재계산(`Mesh::setVertices` → `computeTangents`) (3) 정점 풀 재업로드(정점 수에 비례)다. 셋 모두 API 로 사라지지 않는다.
- **줄이는 길은 따로 있다:** GPU 프로그램 표를 넓혀 CPU 로 내려가는 모디파이어를 줄이는 것(CPU 로 내려가는 순간 위 셋이 모두 돈다). 어떤 모디파이어가 내려가는지는 `MeshModifierGPUProgram::canMake` 가 알려 준다(`_gpuBlockingIndex`).
- MM2 의 값이 문턱(10 만 정점 끌기에서 프레임이 16 ms 를 넘는다)을 넘으면 `.Apply` 와 `.CpuMesh` 중 어느 쪽이 큰지로 갈린다: `.Apply` 가 크면 GPU 표 확장, `.CpuMesh` 가 크면 탄젠트 생략 옵션(미리보기는 노멀 맵을 안 쓴다) 부터 본다. 그때 후보를 연다.

## 3. 받아들인 것

- 텍스처 표본기는 8 비트 원본도 float4 로 보관한다(4 배). 캐시 상한(512 MB, 1 초 넘게 안 쓴 것부터 내림)이 이미 있어 이 크기를 받아들인다. 파일 변경 감지는 1 초 지연이다.

## 4. 함정

clang-format 20.1.8 로 줄 길이를 맞출 것(이 PC 는 21.1.1).

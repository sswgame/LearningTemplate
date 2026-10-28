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
제목: 메시 모디파이어 CPU 미리보기의 남은 비용
왜 필요한가: GPU 프로그램 표에 없는 모디파이어가 스택에 있으면 미리보기가 매번 CPU 로 스택을 적용하고 새 메시(탄젠트 + 정점 풀 재업로드)를 만든다. 정점 갱신 API 는 렌더 스냅샷 수명 때문에 이 비용을 못 줄여 더하지 않았다. 점 단위 모디파이어는 곡선 · 스플라인(`CurveDeform` · `CurveSculpt` · `CurveSculptLayered` · `RopeDeform` · `PathDeform` · `WorldPathDeform`) · 텍스처(`DisplaceRenderTarget` · `DisplaceWebCam`) · 격자(`Lattice` · `FFD2/3/4` · `FFD2D`) · 충돌(`CollisionDeform` · `HitDeform`)까지 모두 GPU 표에 올랐다. 입력은 새 바인딩이 아니라 기록의 매개변수 뒤에 인라인으로 싣는다(`MeshModifier::packGPUInputs` — 스플라인은 128 구간 위치 · 접선 · 단면 축 표본, 텍스처는 한 채널을 64² 격자로 누른 값과 프로그램 끝의 정점 UV 표, 격자는 제어점 버퍼, 충돌 · 충돌점은 수 목록 평탄 배치). 근사: 스플라인은 128 구간 선형 보간이라 곡률이 큰 곳에서 CPU 정확 표본과 구간 간격 제곱 정도 어긋나고, 텍스처는 64² 보다 큰 원본이 무뎌진다(해상도는 `kGPUTextureResolution` 과 셰이더 매크로를 함께 바꾼다).
착수 조건: 측정 없이 착수 가능.
남은 첫 일: 아직 CPU 로만 도는 것을 입력 모양별로 올린다 — `Conform` · `ConformMulti`(다른 메시의 삼각형 BVH 와 광선 투영: 삼각형 · 노드 버퍼가 정점 수에 비례해 커서 인라인이 아닌 별도 버퍼가 필요하다), `PointCache` · `VertexAnim`(프레임 × 정점 데이터라 같은 이유로 별도 버퍼), `Paint`(색 채널은 모프 풀이 위치 · 노멀 · 탄젠트만 가진다), `WorldWarp`(`applyToMesh` 재정의), `Deformable`(묶음). `CpuMesh` 가 크면 탄젠트 재계산을 건너뛰는 옵션(`Mesh::setVerticesWithTangents`)도 본다.
함정: 메시를 제자리에서 바꾸지 않는다 — 렌더 스냅샷이 옛 메시를 쥔다. 정점 UV 표는 칸 표 y(풀 안 구간 시작)를 빼서 읽는다 — 풀이 구간을 다시 짜면 세대 표를 비워 칸 표가 다시 올라가야 한다(`GPUMeshMorphPool::updateDeformPrograms`). 수 목록은 고정 매개변수 뒤에 싣는 규약이라 새 모디파이어가 매개변수를 더하면 셰이더 번호도 함께 바꾼다. 셰이더 출구는 하나, HLSL 예약어 `point` 는 변수 이름으로 쓰지 않는다.

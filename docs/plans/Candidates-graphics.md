# 그래픽스 후보

> 상태: 후보 · 선행: 없음 · 영역: graphics · 진행: 해당없음

그래픽스 · RHI · 셰이더 영역에서 아직 안 한 일의 목록이다. 착수하면 절을 계획 문서로 옮기고 여기서 지운다.

## 한눈에 보기

| 절 | 한 줄 설명 | 착수 조건 |
|---|---|---|
| VfxMultiGroupCompaction | GPU 방출기 다중 그룹 압축 | 측정 없이 착수 가능 |
| SpanAllocatorVatSkinPools | 스킨 원본 · VAT 풀의 구간 할당기 | 측정 없이 착수 가능 |

## 조건부

### VfxMultiGroupCompaction
제목: GPU 방출기 다중 그룹 압축
왜 필요한가: GPU 시뮬레이션은 방출기 하나를 그룹 하나(256 스레드)가 돌아 풀이 수만 개면 느리다. 풀이 크면 디스패치 셋으로 나눈다: (1) 그룹마다 생성 · 적분 · 생존 표시와 개수, (2) 개수의 접두사 합(그룹 하나), (3) 그룹마다 자기 시작 위치로 안정 압축. 단계마다 셋이라 여러 단계를 한 디스패치에 못 넣는다.
착수 조건: 측정 없이 착수 가능.
남은 첫 일: 세 디스패치 경로를 만들고, 풀 크기로 두 경로를 고를지 정한다(같은 결과여야 한다).

### SpanAllocatorVatSkinPools
제목: 스킨 원본 · VAT 풀의 구간 할당기
왜 필요한가: 스킨 원본 전체 다시 올리기(`RT.Deform.rebuildSkinSources`, 집합이 바뀐 프레임에만 찍힘)와 VAT 테이블 풀(`RT.Deform.rebuildVertexAnimationPool`)은 `GPUMeshVertexPool` · `GPUMeshMorphPool` 과 같은 모양의 `SpanAllocator` 구간 갱신이 아니다. 구간은 이미 들어갔다.
착수 조건: 측정 없이 착수 가능.
남은 첫 일: 두 풀을 `SpanAllocator` 구간 갱신으로 옮기는 단위를 계획 문서로 올린다.

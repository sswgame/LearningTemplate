# 저장 전용 압축 타입

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 진행: 6/6

정점 · 인스턴스 · 버퍼의 크기를 줄이는 압축 타입(half · unorm · snorm · 옥타헤드럴 법선 · RGB10A2)을 엔진이 갖고, 새 포맷은 아래 규칙으로 적용한다. 타입 · half 복사본 통합 · 리플렉션 연결 · G버퍼 법선 · `.mesh` 파일 정점은 코드까지 들어갔다. G버퍼 법선(P4)과 `.mesh` 정점(P5)은 빌드 · 셰이더 재쿠킹 · 네 백엔드 확인 전이고, VAT 위치(P6)는 코드까지 들어갔고 같은 확인 전이다.

## 진행

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 저장 전용 타입 일곱 가지와 테스트(`Core/Math`) | 끝 |
| P2 | `half` → `float32` 풀이 복사본 세 곳을 `half::toFloat` 등으로 통합 | 끝 |
| P3 | 리플렉션 연결(`ReflectBuiltins.xxx` 내장 타입 열다섯 가지) | 코드 끝, 컴파일 확인 전 |
| P4 | G버퍼 법선을 `R16G16_SNORM` 옥타헤드럴로(8 B → 4 B) | 코드 끝, 셰이더 재쿠킹 · GPU 확인 전 |
| P5 | `.mesh` 버전 4 — 파일 정점 64 B → 32 B | 코드 끝, 빌드 · 네 백엔드 확인 전 |
| P6 | VAT 위치를 경계 정규화 `snorm16` 으로(16 B → 8 B) | 코드 끝, 빌드 · 셰이더 재쿠킹 · VAT 재쿠킹 · GPU 확인 전 |

첫 일: 빌드 → 셰이더 재쿠킹 → P4 · P5 의 네 백엔드 스크린샷 확인. 메모리 · GPU 정점의 압축은 `RHIVertex` 를 쓰는 76 개 파일 전체의 변경이라 P5 뒤 후보다.

## 1. 타입 (P1)

`Core/Math` 헤더만으로 `constexpr` 이고, 계산은 `float32` 로 풀어서 하며 연산자와 암시적 변환이 없다. 크기 · 정렬은 `static_assert` 가 고정한다(묶음의 정렬은 성분 하나의 정렬).

| 타입 | 파일 | 크기 | 쓰임 |
|---|---|---|---|
| `half` · `half2` · `half3` · `half4` | `Half.h` | 2 · 4 · 6 · 8 B | 0 근처의 작은 값, 속도, 가중치, HDR 색 |
| `unorm8` · `unorm16` · `unorm8x4` · `unorm16x2` | `NormalizedInt.h` | 1 · 2 · 4 · 4 B | 색, 0..1 값, 경계 정규화 UV |
| `snorm8` · `snorm16` · `snorm8x4` · `snorm16x2` · `snorm16x4` | `NormalizedInt.h` | 1 · 2 · 4 · 4 · 8 B | 경계 정규화 위치, 부호 있는 가중치 |
| `OctahedralNormal` | `OctahedralNormal.h` | 4 B | 법선 · 탄젠트 방향 |
| `RGB10A2` | `RGB10A2.h` | 4 B | 알파가 네 단계면 되는 색 · 마스크 |

- `half` 변환은 비트 연산 직접 구현이며 65536 비트 패턴이 전수 왕복한다.
- `OctahedralNormal` 의 풀이는 `binding.hlsli` 의 `VertexAnimation::unpackNormal` 과 같은 식이고 인코딩은 `MeshVertexAnimation::packNormal` 과 같다.

## 2. 새 포맷 규칙

| 데이터 | 형식 | 이유 |
|---|---|---|
| 월드 좌표, 누적 시간, 큰 범위 값 | `float32` 이상. `half` 금지 | `half` 는 1024 이상에서 간격이 1 이다 |
| 메시 · VAT 위치 | 메시(VAT) 경계로 정규화한 `snorm16`, 경계는 헤더에 | 오차가 경계 크기의 1/65535 로 고르다 |
| 법선 · 탄젠트 | `OctahedralNormal` | 12 B → 4 B, 오차 1e-4 라디안 아래 |
| 색 | `unorm8x4`, 1 을 넘는 HDR 은 `half4` | `R8G8B8A8_UNORM` 과 같은 바이트 |
| UV | 경계 정규화 `unorm16x2`, 타일링이 큰 메시는 `float32` | `half` 는 1 근처 간격이 1/2048 이라 계단이 보인다 |
| 압축 풀이 | `binding.hlsli` 한 곳 + CPU 는 이 타입 | 두 쪽이 어긋나지 않게 정의를 하나만 둔다 |
| 포맷이 바뀔 때 | `.mesh` · `.vat` · `SCN1` · `PFB2` 버전을 올리고 CPU 구조체 · HLSL 스트라이드를 `static_assert` 로 맞춘다 | 낡은 파일을 조용히 잘못 읽지 않게 |
| 압축 값끼리의 계산 | `toFloat` 로 푼 `float32` | 타입에 연산자가 없다 |

## 3. 리플렉션 접점 (P3)

`PROPERTY` · 인자 · 반환 · 직렬화 · 인스펙터에서 값으로 보인다. 줄을 더하는 쪽이 지킬 구조:

- 성분 규칙은 `Core/Math/PackedValueTraits.h` 의 `PackedValueTraits<T>`(`toFloats` · `fromFloats` · 범위) 한 곳이다. 특수화를 빠뜨리면 컴파일되지 않는다.
- 이진은 `sizeof( T )` 바이트 그대로, 글은 성분 실수의 쉼표 목록(`0.5,1,0.25`). 개수가 틀리거나 범위를 넘으면 거절하고 값을 그대로 둔다. `half` 의 무한대 · NaN 은 글로 못 간다.
- `OctahedralNormal` 의 글 · 인자 성분은 방향이 아니라 저장 좌표 (u, v) 다(방향으로 풀고 다시 담으면 비트가 흔들린다). 인스펙터만 방향 xyz 로 편집한다.
- 헤더: `PackedTypesFwd.h`(이름 · 별칭의 유일한 정의)와 `PackedTypes.h`. `PROPERTY` 의 타입은 별칭(`unorm8x4`)으로 적는다.
- 한계: `Transient` 가 아닌 압축 타입 `PROPERTY` 는 고정 배치형 코덱 표 밖이라 코드 생성기가 오류로 멈춘다(새 인코딩 종류를 더하는 별도 단계 후보). 인스펙터는 색 선택기 · `Min` · `Max` 를 읽지 않고 성분 드래그다.

## 4. P4 — G버퍼 법선 `R16G16_SNORM` 옥타헤드럴

1920 × 1080 에서 법선 첨부가 8 B → 4 B 라 프레임마다 쓰기 · 읽기가 각각 약 8 MB 줄고(계산값), 오차는 half 의 5e-4 에서 1e-4 이하가 된다.

**코드에 들어간 것:**
- 포맷 열거자 `R16G16_SNORM = 16`(DXGI · Vulkan · GL 표 · 에디터 렌더 타깃 뷰 · 캡처). G버퍼 고정 포맷 표와 `deferredpipeline.xml` 이 이 포맷을 쓴다(클리어 0,0,0,0).
- 셰이더: `Octahedral::encode · decode`, `sampleNormal` 이 `float3` 을 돌려준다. "그려졌다" 표시는 G버퍼 머티리얼 칸 알파(`loadGbufferMaterial` 의 `outDrawn`)로 옮겼다. 알베도 알파는 셰이딩 모델이라 쓸 수 없다.
- 렌더 타깃 지원 확인은 Vulkan 만 장치 생성에서 하고(없으면 오류), D3D 는 필수 포맷이며 **GL 은 확인하지 않았다**(프레임버퍼 완전성 오류로 드러남). `R16G16_FLOAT` 는 오차 1e-4 를 못 지켜 기각했다.

**남은 것:** 머티리얼 셰이더 전부와 `deferredlighting` · `ssao` 가 바뀌므로 셰이더 재쿠킹, 그 뒤 G버퍼 노멀 GPU 테스트(네 백엔드), 디퍼드 시나리오 스크린샷 전후 비교(양자화 오차 이내, 네 백엔드 같은 이미지), 의도한 차이면 골든 갱신. 포워드 파이프라인에는 이 첨부가 없다.

## 5. P5 — `.mesh` 버전 4

`RHIVertex` 는 64 B(위치 12 + 노멀 12 + UV 8 + 색 16 + 탄젠트 16)다. 파일의 정점을 32 B(위치 `float32` 12 + 법선 옥타헤드럴 4 + 탄젠트 옥타헤드럴 4 + UV `unorm16x2` 4 + 색 `unorm8x4` 4 + 플래그 4, 바이탄젠트 부호는 플래그 비트 0)로 줄였고 머리는 40 B(UV 경계 추가)다.

- 읽을 때 64 B `RHIVertex` 로 풀므로 메모리 · GPU 정점 · 입력 레이아웃 · 컴퓨트는 그대로다. 옛 형식 리더는 없고 쿠킹된 `.mesh` 는 재임포트해야 읽힌다.
- 색은 0..1 밖이 잘리고 UV 는 큰 범위일수록 오차가 크다(`float32` 유지 선택지는 아직 없음).
- 재임포트하면 커밋된 `.mesh` 가 모두 바뀌므로 [PreloadAndLod](PreloadAndLod.md) 의 `.mesh` 변경과 같은 묶음에 넣는다.
- CPU 쪽 정점(물리 · 내비 · 피킹, LOD 0)은 `float32` 사본을 유지한다. 압축 위치는 기준 포즈에만 쓴다(스킨 · 모프 결과는 컴퓨트가 `float32` 로 계산).
- `snorm16` 의 단위 오차는 경계의 1/65535 다(1 km 메시면 약 1.5 cm). 오차 상한이 넘는 메시는 `float3` 위치를 유지하는 선택지를 임포트 규칙에 둔다.

## 6. P6 — VAT 위치

`.vat` 버전 2 로 원소를 16 B(`float4`)에서 8 B(`MeshVertexAnimationElement`: 위치 `snorm16` 셋 + 옥타헤드럴 법선 `snorm8` 둘)로 줄였다. 위치는 모든 프레임을 아우르는 경계 상자(중심 · 반 크기, 파일 머리 52 B)로 정규화한다.

- CPU 표(`_listFrameVertex`)도 같은 8 B 원소이고 베이커가 float 위치 · 법선을 모아 `MeshVertexAnimation::quantize` 로 한 번에 압축한다. `packNormal` · `unpackNormal(float)` · `SW_VERTEX_ANIMATION_NORMAL_STEPS` 는 없앴고 법선 풀이는 `OctahedralNormal::decodeOctant`(셰이더 `Octahedral::decode`)다.
- GPU 표 `g_SwVertexAnimation` 은 `uint` 단어 배열이다(라이트 그리드와 같은 모양, `float4` 가 아니다). 메시마다 머리 단어 10 개(프레임 수 · 프레임율 · 정점 수 · 반복 · 경계 중심 · 반 크기) 뒤에 원소가 단어 둘씩 프레임 우선으로 이어진다. 배치의 `_vertexAnimationBase` 와 `g_SwVertexAnimationCount` 는 단어 번호 · 단어 수다.
- 위치 풀이(`VertexAnimation::computePosition`)는 정규화한 두 원소를 보간한 뒤 경계로 푼다(`precise`, 한 함수라 패스 사이 비트가 같다). `.vat` 쿠커 버전을 2 로 올려 옛 파일은 다시 구워진다.
- 단위 오차는 반 크기의 1/32767(1 m 메시 약 0.015 mm), 법선은 8 비트 옥타헤드럴(각 오차 약 0.5도 안쪽).

**남은 것:** 빌드, 셰이더 재쿠킹, VAT 재쿠킹, 군중 장면 GPU 확인(네 백엔드 같은 이미지). VAT 표 메모리와 정점 읽기 대역폭 전후 비교는 측정이라 하지 않았다(8 B 라 표 메모리는 절반, 오차는 위 값).

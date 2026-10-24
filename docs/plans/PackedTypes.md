# 저장 전용 압축 타입

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 1/6

> 질문: 정점 · 인스턴스 · 버퍼의 크기를 줄이는 압축 타입을 엔진이 어떻게 갖고, 어디부터 어떤 규칙으로 적용하는가?
> 독자: 정점 포맷 · G버퍼 · 리플렉션을 고치는 작업자와 통합 담당.
> 결론: 타입은 `Core/Math` 에 들어갔다(P1). 다음은 중복 해독 코드 통합(P2), 리플렉션 연결(P3), 첫 적용처 셋(G버퍼 법선 P4, 메시 정점 P5, VAT 위치 P6) 순서다. 새 포맷은 아래 2절 규칙을 따른다.

## 1. 들어간 것 (P1)

저장 전용 타입 일곱 가지와 테스트입니다. 모두 `Core/Math` 의 헤더만으로 되어 있고 `constexpr` 입니다. 계산은 `float32` 로 풀어서 하고, 연산자와 암시적 변환이 없습니다.

| 타입 | 파일 | 크기 | 쓰임 |
|---|---|---|---|
| `half` · `half2` · `half3` · `half4` | `Half.h` | 2 · 4 · 6 · 8 B | 0 근처의 작은 값, 속도, 가중치, HDR 색 |
| `unorm8` · `unorm16` · `unorm8x4` · `unorm16x2` | `NormalizedInt.h` | 1 · 2 · 4 · 4 B | 색, 0..1 에 갇힌 값, 경계 정규화 UV |
| `snorm8` · `snorm16` · `snorm8x4` · `snorm16x2` · `snorm16x4` | `NormalizedInt.h` | 1 · 2 · 4 · 4 · 8 B | 경계 정규화 위치, 부호 있는 가중치 |
| `OctahedralNormal` | `OctahedralNormal.h` | 4 B | 법선 · 탄젠트 방향 |
| `RGB10A2` | `RGB10A2.h` | 4 B | 알파가 네 단계면 되는 색 · 마스크 |

- `half` 변환은 비트 연산으로 직접 구현했고(가장 가까운 값으로 반올림, 가운데는 짝수, 65520 이상은 무한대, 비정규수와 NaN 처리) 65536 개 비트 패턴이 전수 왕복합니다.
- 크기와 정렬은 헤더의 `static_assert` 가 고정합니다. 묶음의 정렬은 성분 하나의 정렬이라 `unorm8x4` 는 1 B 정렬이고, 4 B 정렬이 필요한 구조체는 쓰는 쪽이 맞춥니다.
- `OctahedralNormal` 의 풀이는 `binding.hlsli` 의 `VertexAnimation::unpackNormal` 과 같은 식이고, 테스트 `OctahedralNormalTest.DecodeMatchesShaderFormula` 가 셰이더 본문을 옮겨 적은 식과 비교합니다. 인코딩은 `MeshVertexAnimation::packNormal` 과 같은 식입니다. 해상도는 VAT 가 12 + 12 비트 정수를 실수 하나에 담고, 이 타입은 `snorm16` 두 개입니다.
- 이 저장소의 `C++` 표준 옵션은 17 이상입니다(`SW_CPP_STANDARD`). 그래서 `std::bit_cast` 대신 `BitUtil::floatToBits` · `bitsToFloat`(Clang `__builtin_bit_cast`)를 씁니다.
- 테스트는 `Test/CoreTest/Math` 의 `TestHalf` · `TestNormalizedInt` · `TestOctahedralNormal` · `TestRGB10A2` 입니다. 매니페스트 id `packed-types-test`(필터 `HalfTest.*,NormalizedIntTest.*,OctahedralNormalTest.*,RGB10A2Test.*`)가 통합에서 돕니다.

## 2. 새 포맷 규칙

정점 · 인스턴스 · 버퍼 포맷을 새로 정하거나 바꿀 때 지킵니다.

| 데이터 | 형식 | 이유 |
|---|---|---|
| 월드 좌표, 누적 시간, 큰 범위 값 | `float32` 이상. `half` 금지 | `half` 는 1024 이상에서 간격이 1 이다(`HalfTest.LargeValuesLoseSubUnitPrecision`) |
| 메시 · VAT 위치 | 메시(또는 VAT) 경계로 정규화한 `snorm16`. 경계는 헤더에 둔다 | 위치 오차가 경계 크기의 1/65535 로 고르게 나온다 |
| 법선 · 탄젠트 | `OctahedralNormal`(4 B) | 12 B 를 4 B 로 줄이고 오차가 1e-4 라디안 아래다 |
| 색 | `unorm8x4`. 1 을 넘는 HDR 은 `half4` | `R8G8B8A8_UNORM` 과 같은 바이트 |
| UV | 메시 경계로 정규화한 `unorm16x2`. 타일링이 큰 메시는 `float32` 유지 | `half` 는 1 근처에서 1/2048 간격이라 큰 텍스처에서 계단이 보인다 |
| 압축 풀이 | `binding.hlsli` 한 곳, CPU 는 이 타입 | 두 쪽이 어긋나지 않게 한 정의만 둔다 |
| 포맷이 바뀔 때 | `.mesh` · `.vat` · `SCN1` · `PFB2` 버전을 올리고, CPU 구조체와 HLSL 스트라이드를 `static_assert` 로 맞춘다 | 낡은 파일을 새 코드가 조용히 잘못 읽지 않게 한다 |
| 압축 값끼리의 계산 | `toFloat` 로 푼 `float32` 로 한다 | 타입에 연산자가 없다 |

## 3. 단위

### P2 half 해독 복사본 통합

같은 `half` → `float32` 풀이가 세 곳에 따로 있습니다. `Editor/Panels/RenderTarget/RenderTargetViewUtil.cpp` 의 `halfToFloat`, `Engine/Renderer/Capture/ReflectionProbeBaker.cpp` 의 `decodeHalf`, `Engine/Renderer/Frame/FrameRendererUtil.h` 의 `halfToUnorm8` 입니다.
`half::fromBits( bits ).toFloat()` 와 `unorm8::fromFloat` 로 바꾸고 복사본을 지웁니다. 렌더 타깃 뷰와 캡처 파일(`RenderTargetImageFile.cpp`)의 기존 테스트가 같은 값을 내는지 봅니다.
읽을 문서: 이 문서 1절. 고칠 문서: 이 문서 단위 절과 `진행`.

### P3 직렬화 · 리플렉션 연결

타입을 `PROPERTY` 로 받으려면 건드릴 곳이 정해져 있습니다(코드로 확인했습니다).

- `Engine/Reflection/ReflectBuiltins.xxx` 에 타입마다 `SW_REFLECT_BUILTIN_TYPE( half, sw::half, none, sw, _ )` 한 줄을 더합니다. 이 표를 리플렉션 파서(`Tools/ReflectionParser/ReflectBuiltinsLoader.cpp`)가 읽어 `ReflectBuiltins.gen.cpp` 의 타입 정보를 만들고, `SerializeContext::makeDefault`(이진 처리기), `ReflectValue.cpp`, 에디터 인스펙터(`InspectorBuiltinValue.cpp`), UI 바인딩(`UIBindingValue.cpp`), RPC(`SerializeReflectionRPC.cpp`)가 같은 표를 include 합니다.
- 이진 처리기는 표의 줄마다 자동 등록되어 `sizeof( T )` 바이트를 그대로 읽고 씁니다. 그래서 모든 타입이 비트 복사 가능하고 패딩이 없어야 하며, 헤더의 `static_assert` 가 이 조건을 이미 고정합니다. `half3` 처럼 정렬이 2 인 타입은 읽기 쓰기가 정렬되지 않은 주소에서도 되는지 확인합니다.
- 글 처리기는 `TextConv` 를 `none` 으로 둔 줄마다 따로 등록해야 합니다(`SerializeContext.cpp` 의 `registerFloatVectorTextHandlers` 와 같은 자리). 형식은 성분 실수의 쉼표 목록(`0.5,1,0.25`)으로 정하고, 쓸 때는 `toFloat` 로 푼 값이 왕복에 안전한 자릿수로 나가게 합니다. 읽을 때는 `fromFloat` 로 담으므로 한 번 푼 값은 다시 같은 비트가 됩니다.
- `ReflectValue.cpp` 의 `convertSource` · `formatBuiltin` 에 분기를 더합니다(지금은 `float2/3/4` 만 벡터로 읽습니다).
- 인스펙터는 압축 타입을 `float` 위젯으로 편집하고 값을 `fromFloat` 로 다시 담도록 정합니다. 편집이 양자화 한 칸 안에서 되돌려지는 것을 사용자에게 보입니다.
- 스키마 해시에 들어가는 타입 이름이 새로 생기므로 `ReflectionFormats` 문서의 해시 규칙([ReflectionFormats](ReflectionFormats.md))을 먼저 읽습니다.

첫 속성이 생길 때(P4 이후)까지 서둘러 하지 않아도 됩니다. P5 가 `.mesh` 를 직접 읽고 쓰면 P3 은 컴포넌트 속성에 압축 값을 두려는 때에만 필요합니다. 확인은 왕복 테스트(`SerializeContext` 이진 · 글)와 리플렉션 파서 재생성입니다.

### P4 G버퍼 법선을 R16G16 옥타헤드럴로

G버퍼의 법선 첨부(`GBufferNormal`, 지금 `R16G16B16A16_FLOAT` 로 알고 있으며 렌더 패스 선언 데이터에서 확인한다)를 `R16G16_SNORM` 으로 바꿉니다. 알파에 다른 값이 실려 있는지 먼저 봅니다. 1920 × 1080 에서 첨부 하나가 8 B 에서 4 B 가 되어 프레임마다 쓰기와 읽기가 각각 약 8 MB 줄어듭니다(계산값, 측정 필요).

- `RHIFormat`(`RHITypes.h`)에 `R16G16_SNORM` 을 더합니다. 번호는 기존 값을 바꾸지 않고 뒤(16)에 잇습니다. `R10G10B10A2_UNORM`(17)도 이때 같이 더할지 정합니다. 네 백엔드(DX11 · DX12 · Vulkan · GL)의 포맷 변환 표와 텍스처 업로드 크기 계산이 모두 같은 줄을 받아야 합니다.
- 셰이더: `gbuffer.hlsl` 이 법선을 옥타헤드럴로 인코딩해 쓰고, `deferredlighting.hlsl` 과 법선을 읽는 다른 패스(TAA · 툰 등)가 `binding.hlsli` 의 풀이 함수 하나로 읽습니다. 풀이 식은 `OctahedralNormal::decodeOctant` 와 같은 식입니다.
- 셰이더를 바꾸므로 `App.exe --cook-shaders` 와 낡은 바이너리 삭제가 따라옵니다([작업자 규칙](WorkerRules.md) 6절).
- 확인은 GPU 이미지 비교입니다(G버퍼를 쓰는 기존 시나리오의 스크린샷 전후, 라이팅 차이가 양자화 오차 이내인지). 네 백엔드가 같은 이미지를 내야 합니다.

### P5 메시 정점 포맷 (`.mesh` 버전 4)

`RHIVertex`(`RHITypes.h`)는 위치 12 + 노멀 12 + UV 8 + 색 16 + 탄젠트 16 = 64 B 입니다. 위치 `float3`(12) + 법선 `OctahedralNormal`(4) + 탄젠트 `OctahedralNormal`(4) + UV `unorm16x2`(4) + 색 `unorm8x4`(4) = 28 B 가 됩니다. 탄젠트의 바이탄젠트 부호(±1)는 한 비트라서 색 알파나 탄젠트 좌표의 한 비트로 나눠 쓸지 이 단위에서 정합니다. 어느 쪽이든 28 ~ 32 B 입니다.

- `MeshAssetFormat::kVersion` 을 3 에서 4 로 올리고 임포터 · 로더가 새 레이아웃을 읽고 씁니다. 재임포트하면 저장소에 커밋된 `.mesh` 산출물이 모두 바뀌므로 큰 diff 가 나옵니다. 판 올림을 [PreloadAndLod](PreloadAndLod.md) 의 `.mesh` 변경과 같은 커밋 묶음에 넣습니다.
- 입력 레이아웃: `RHIVertexAttribute` 표(`_format` 확장)가 네 백엔드의 입력 레이아웃의 원본이므로 표 한 곳을 고치고 네 백엔드가 같은 표를 읽는지 확인합니다. 정점 셰이더가 `binding.hlsli` 의 풀이 함수로 풉니다.
- CPU 쪽 정점(물리 · 내비 · 피킹, LOD 0)은 `float32` 사본을 유지합니다. 스킨 정점(`LSKN`)과 모프도 새 위치 경계를 쓰는지 확인하고, 경계 밖으로 움직이는 모프 · 스킨 결과는 컴퓨트가 `float32` 로 계산하므로 압축 위치는 기준 포즈에만 씁니다.
- 위치 경계 정규화 `snorm16` 은 큰 메시에서 경계의 1/65535 가 단위 오차입니다(1 km 메시면 약 1.5 cm). 오차 상한을 메시별로 계산해 넘으면 `float3` 위치를 유지하는 선택지를 임포트 규칙에 둡니다.
- 확인은 메시 에셋 테스트(`MeshAssetTest`)와 정점 레이아웃을 쓰는 전 시나리오의 네 백엔드 스크린샷입니다.

### P6 VAT 위치를 경계 정규화 snorm16 으로

`.vat` 파일이 생긴 뒤에 합니다(지금 VAT 는 실행 중에 구워서 `float4`(16 B) 원소로 올립니다). 원소를 위치 `snorm16` 세 개(6 B) + 법선 `snorm8x2`(2 B) = 8 B 로 줄이고 위치 경계를 VAT 머리에 둡니다. `.vat` 버전을 올리고 `MeshVertexAnimation::packNormal` 의 12 + 12 비트 정수 형식을 `OctahedralNormal` 로 바꾸면 `binding.hlsli` 의 `VertexAnimation::unpackNormal` 이 P4 의 풀이 함수와 합쳐집니다.
측정: VAT 표 메모리와 군중 장면의 정점 셰이더 읽기 대역폭을 전후로 비교합니다. 이득이 오차(위치 단계 1/65535)를 정당화하지 못하면 되돌립니다.

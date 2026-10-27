# 저장 전용 압축 타입

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 3/6

> 질문: 정점 · 인스턴스 · 버퍼의 크기를 줄이는 압축 타입을 엔진이 어떻게 갖고, 어디부터 어떤 규칙으로 적용하는가?
> 독자: 정점 포맷 · G버퍼 · 리플렉션을 고치는 작업자와 통합 담당.
> 결론: 타입(P1), half 해독 복사본 통합(P2), 리플렉션 연결(P3, 빌드 확인은 통합)은 들어갔다. 다음은 첫 적용처 셋(G버퍼 법선 P4, 메시 정점 P5, VAT 위치 P6)이고 P4 는 GPU 확인과 재쿠킹을 하는 통합이 코드를 쓴다. 새 포맷은 2절 규칙을 따른다.

## 1. 들어간 것 (P1)

저장 전용 타입 일곱 가지와 테스트입니다. `Core/Math` 의 헤더만으로 `constexpr` 이고, 계산은 `float32` 로 풀어서 하며 연산자와 암시적 변환이 없습니다.

| 타입 | 파일 | 크기 | 쓰임 |
|---|---|---|---|
| `half` · `half2` · `half3` · `half4` | `Half.h` | 2 · 4 · 6 · 8 B | 0 근처의 작은 값, 속도, 가중치, HDR 색 |
| `unorm8` · `unorm16` · `unorm8x4` · `unorm16x2` | `NormalizedInt.h` | 1 · 2 · 4 · 4 B | 색, 0..1 에 갇힌 값, 경계 정규화 UV |
| `snorm8` · `snorm16` · `snorm8x4` · `snorm16x2` · `snorm16x4` | `NormalizedInt.h` | 1 · 2 · 4 · 4 · 8 B | 경계 정규화 위치, 부호 있는 가중치 |
| `OctahedralNormal` | `OctahedralNormal.h` | 4 B | 법선 · 탄젠트 방향 |
| `RGB10A2` | `RGB10A2.h` | 4 B | 알파가 네 단계면 되는 색 · 마스크 |

- `half` 변환은 비트 연산 직접 구현(가까운 값 반올림, 가운데는 짝수, 65520 이상은 무한대)이고 65536 비트 패턴이 전수 왕복합니다. `std::bit_cast` 대신 `BitUtil::floatToBits` · `bitsToFloat` 를 씁니다(C++17 이상 옵션).
- 크기와 정렬은 헤더의 `static_assert` 가 고정합니다. 묶음의 정렬은 성분 하나의 정렬이라 `unorm8x4` 는 1 B 정렬이고 4 B 정렬이 필요한 구조체는 쓰는 쪽이 맞춥니다.
- `OctahedralNormal` 의 풀이는 `binding.hlsli` 의 `VertexAnimation::unpackNormal` 과 같은 식이고(`OctahedralNormalTest.DecodeMatchesShaderFormula`), 인코딩은 `MeshVertexAnimation::packNormal` 과 같습니다. VAT 는 12 + 12 비트 정수를 실수 하나에 담고 이 타입은 `snorm16` 두 개입니다.
- 테스트는 `Test/CoreTest/Math` 의 `TestHalf` · `TestNormalizedInt` · `TestOctahedralNormal` · `TestRGB10A2`, 매니페스트 id `packed-types-test`.

### 리플렉션 접점 (P3)

열다섯 가지 타입이 `Engine/Reflection/ReflectBuiltins.xxx` 의 내장 타입 줄이 되어 `PROPERTY` · 인자 · 반환 · 직렬화 · 인스펙터에서 값으로 보입니다. 줄을 더하는 쪽이 지킬 구조:

- **성분 규칙은 한 곳입니다.** `Core/Math/PackedValueTraits.h` 의 `PackedValueTraits<T>` 가 `toFloats` · `fromFloats` · 범위를 갖고 글 처리기 · 인자 변환 · 인스펙터가 이것만 부릅니다. 특수화를 빠뜨리면 `TextConv` `packed` 줄의 등록이 컴파일되지 않습니다. 왕복은 비트까지 같고 예외는 `snorm` 정수 최솟값과 `half` 신호 NaN 둘입니다.
- **이진은 `sizeof( T )` 바이트 그대로**(바이트 복사라 정렬되지 않은 주소에서 안전), **글은 성분 실수의 쉼표 목록**(`0.5,1,0.25`). 개수가 틀리거나 범위를 넘으면(`unorm8` 에 `5`) 거절하고 값을 그대로 둡니다. `half` 의 무한대 · NaN 은 글로 못 갑니다.
- `OctahedralNormal` 의 글 · 인자 성분은 방향이 아니라 저장된 좌표 (u, v) 입니다(방향으로 풀고 다시 담으면 비트가 흔들려 글 왕복이 불안정). 인스펙터만 방향 xyz 로 편집합니다(`InspectorValueWidget::Direction`).
- 인자 변환(`ReflectValue`)은 성분이 하나면 숫자도 받고 범위 밖은 거절, 여럿이면 글만 받습니다.
- 헤더: `Core/Math/PackedTypesFwd.h`(이름과 별칭의 유일한 정의)와 `PackedTypes.h`(완전한 정의 묶음). 표의 C++ 타입 칸에는 별칭만 씁니다(템플릿 이름의 쉼표가 매크로 인자를 나눔).
- 한계: (1) `Transient` 가 아닌 압축 타입 `PROPERTY` 는 고정 배치형 코덱의 인코딩 표 밖이라 코드 생성기가 **생성 오류로 멈춥니다**([결정 기록 5-4](../decisions/5-4_DataConfig.md) 3.1 · 3.4). 압축 타입을 B 코덱에 넣는 일은 새 인코딩 종류를 더하는 별도 단계 후보입니다. (2) 인스펙터는 색 선택기 · `Min` · `Max` 메타를 읽지 않고 성분 드래그입니다. (3) UI 바인딩과 스키마 이주는 내장 타입 표의 갈래(`ReflectBuiltinClass`)로 숫자를 가릅니다. (4) `PROPERTY` 의 타입은 별칭(`unorm8x4`)으로 적습니다(풀어 쓴 `NormalizedVector<unorm8, 4>` 는 모르는 이름).
- 확인: `PackedValueTraitsTest`(CoreTest) · `PackedValueSerializationTest`(EngineTest) · `InspectorBuiltinValueTest`(EditorTest), 매니페스트 id `packed-types-test`.

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

### P2 half 해독 복사본 통합 (끝)

세 곳의 `half` → `float32` 풀이를 `half::toFloat` · `unorm8::fromFloat` 로 모았다. `ReflectionProbeBaker::decodeHalf`(Inf · NaN 을 부호 있는 `kMaxFloat` 로 — 거르기가 NaN 을 퍼뜨리지 않게)은 `half::toFiniteFloat` 로, `FrameRendererUtil::halfToUnorm8` 은 부호 · 비유한 분기만 남겼다. 옛 함수 사본과 65536 비트 패턴을 견주는 테스트가 있다(`ReadPixelHalfMatchesLegacyDecode`, `HalfToUnorm8MatchesLegacyDecode`, `HalfTest.ToFiniteFloatClampsOnlyNonFinite`). HLSL 쪽 f16 해독은 저장소에 없다(첨부 읽기는 CPU 에서만).

### P4 G버퍼 법선을 R16G16 옥타헤드럴로

**설계만 있고 코드는 없습니다.** GPU 확인과 셰이더 재쿠킹이 필요해 통합이 아래 목록으로 쓰고 검증합니다. 1920 × 1080 에서 법선 첨부가 8 B(`R16G16B16A16_FLOAT`)에서 4 B 가 되어 프레임마다 쓰기와 읽기가 각각 약 8 MB 줄어듭니다(계산값, 측정 필요). 지금 half 의 `n * 0.5 + 0.5` 는 1 근처 간격 1/2048 로 오차가 최대 약 5e-4 이고, 옥타헤드럴은 1e-4 이하입니다.

**지금 모양.**

| 항목 | 위치 | 내용 |
|---|---|---|
| 선언 | `Resource/engine/pipeline/deferredpipeline.xml` | `GBufferNormal` `_format="R16G16B16A16_FLOAT"` `_clearColor="0.5,0.5,1,0"`. 다른 파이프라인 xml 은 쓰지 않음 |
| 쓰기 | `binding.hlsli` `SurfaceOutput::pack`(`SW_PASS_GBUFFER`) | `_normal = float4( saturate( normalize( n ) * 0.5 + 0.5 ), 1 )`, `float4 _normal : SV_TARGET1` |
| 읽기 | `PassTexture::sampleNormal` | 호출은 `deferredlighting.hlsl` 과 `ssao.hlsl` 둘뿐. 슬롯 t2 는 BloomBright · AutoExposure 와 같은 슬롯이라 슬롯 표는 안 바뀜 |
| 알파 | `deferredlighting.hlsl` | **알파가 "그려졌다" 표시를 싣는다**(클리어 0, 그려지면 1). `depth >= 1 && normalSample.a < 0.5` 이면 버린다(Overdraw 보기 모드가 깊이를 쓰지 않아 깊이만으론 안 된다). `TestRenderPassGPU.cpp` 의 `readMeanGBufferNormal` 도 알파 ≥ 128 로 센다 |

**바꿀 것.**

1. **포맷 열거자.** `RHITypes.h` `RHIFormat` 에 `R16G16_SNORM = 16` 을 뒤에 잇습니다(기존 번호 불변). 고칠 곳은 `-Wswitch` 가 알려 줍니다: `getRHIFormatBlockInfo`(`{1,1,4}`), `isRHIFormatRenderable`, `toDxgiFormat` · `fromDxgiFormat`, `toVulkanTextureFormat`(`VK_FORMAT_R16G16_SNORM`), GL `arrFormatRow`(`{ R16G16_SNORM, GL_RG16_SNORM, GL_RG, GL_SHORT }`), 에디터 렌더 타깃 뷰(`RenderTargetViewUtil.cpp` 세 switch)와 캡처(`RenderTargetImageFile.cpp`), 렌더 가능 포맷 목록을 단언하는 `TestRenderPass.cpp`. `R10G10B10A2_UNORM`(17)은 쓰는 단위가 생길 때 더합니다.
2. **렌더 타깃이 되는지 먼저 확인합니다(미확인 가정).** 16 비트 SNORM 은 Vulkan 필수 컬러 첨부 포맷이 아니고 GL 코어도 요구하지 않는 것으로 기억합니다(틀릴 수 있음). 통합은 네 백엔드에서 `R16G16_SNORM` 컬러 첨부 지원을 장치 생성 때 조회해 없으면 오류로 멈추게 하거나(폴백 없음), 고르지 않으면 `R16G16_UNORM`(좌표를 `* 0.5 + 0.5` 로 옮겨 적음)으로 바꿉니다. `R16G16_FLOAT` 는 오차 1e-4 를 못 지킵니다.
3. **셰이더 인코드 · 디코드.** `VertexAnimation::unpackNormal` 의 팔면체 → 단위 벡터 식을 `Octahedral::decode( float2 )` 로 뽑고, `Octahedral::encode( float3 ) : float2` 는 `OctahedralNormal::fromNormal` 과 같은 식(`sum = |x|+|y|+|z|`, `z < 0` 이면 접기, 0 은 양수 부호)으로 씁니다. `sampleNormal` 은 디코드한 단위 노멀 `float3` 을 돌려주고 호출 둘에서 `* 2 - 1` · `normalize` 를 지웁니다. `SurfaceOutput::_normal` 은 `float2 _normal : SV_TARGET1`.
4. **"그려졌다" 표시를 옮깁니다.** G버퍼 머티리얼 칸 알파로 옮깁니다(`encodeGbufferMaterial` 의 b · a 가 비어 있고 클리어 0 이라 같은 규칙). `deferredlighting.hlsl` 의 버림 조건은 `PassTexture::loadGbufferMaterial` 쪽으로. 알베도 알파는 셰이딩 모델(0 = Unlit)이라 쓸 수 없습니다. 노멀 클리어 값은 (0, 0)(+z).
5. **CPU 쪽.** `readMeanGBufferNormal` 이 `snorm16x2` 두 값을 읽어 `OctahedralNormal::toNormal` 로 풀고 표시는 머티리얼 첨부 알파에서 셉니다(`RHITestImage` 에 16 비트 두 채널 읽기가 없으면 더함). 에디터 렌더 타깃 패널은 방향 색으로 보입니다.
6. **재쿠킹.** 머티리얼 셰이더 전부(`SW_PASS_GBUFFER`)와 `deferredlighting` · `ssao` 가 바뀌므로 `App.exe --cook-shaders` 와 `ShaderCookRequestTest` 의 낡은 바이너리 삭제를 0 이 될 때까지 반복합니다([작업자 규칙](WorkerRules.md) 6절). 테스트 픽스처의 `GBufferNormal` 포맷 문자열(`TestRenderPass.cpp` · `TestShader.cpp`)도 맞춥니다.

영향 패스는 G버퍼(쓰기), Lighting · SSAO(읽기) 셋이고 다른 패스는 `sampleNormal` 을 부르지 않습니다. 포워드 파이프라인에는 이 첨부가 없습니다.
**골든과 확인.** 디퍼드 경로 골든(`-gv_deferred=1`)은 림 · 반사에서 작은 차이가 날 수 있습니다. 순서: 코드 → 재쿠킹 → `RenderPassGPUTest` 의 G버퍼 노멀 테스트(수직 노멀, 4 백엔드) → 디퍼드 시나리오 스크린샷 전후 비교(양자화 오차 이내, 네 백엔드 같은 이미지) → 의도한 차이면 `golden-record`. 항목은 구현할 때 `Test/QA/VerifyBatch.json` 에 id 로 더합니다.

### P5 메시 정점 포맷 (`.mesh` 버전 4)

`RHIVertex`(`RHITypes.h`)는 위치 12 + 노멀 12 + UV 8 + 색 16 + 탄젠트 16 = 64 B 입니다. 위치 `float3`(12) + 법선 `OctahedralNormal`(4) + 탄젠트 `OctahedralNormal`(4) + UV `unorm16x2`(4) + 색 `unorm8x4`(4) = 28 B 가 됩니다. 바이탄젠트 부호(±1)는 색 알파나 탄젠트 좌표의 한 비트로 나눠 쓸지 이 단위에서 정합니다(28 ~ 32 B).

- `MeshAssetFormat::kVersion` 3 → 4, 임포터 · 로더가 새 레이아웃을 읽고 씁니다. 재임포트하면 커밋된 `.mesh` 산출물이 모두 바뀌므로 [PreloadAndLod](PreloadAndLod.md) 의 `.mesh` 변경과 같은 커밋 묶음에 넣습니다.
- 입력 레이아웃은 `RHIVertexAttribute` 표(`_format` 확장)가 네 백엔드의 원본이므로 표 한 곳을 고치고 네 백엔드가 같은 표를 읽는지 확인합니다. 정점 셰이더는 `binding.hlsli` 의 풀이 함수로 풉니다.
- CPU 쪽 정점(물리 · 내비 · 피킹, LOD 0)은 `float32` 사본을 유지합니다. 스킨 정점(`LSKN`)과 모프도 새 위치 경계를 쓰는지 확인하고, 경계 밖으로 움직이는 결과는 컴퓨트가 `float32` 로 계산하므로 압축 위치는 기준 포즈에만 씁니다.
- `snorm16` 의 단위 오차는 경계의 1/65535 입니다(1 km 메시면 약 1.5 cm). 오차 상한을 메시별로 계산해 넘으면 `float3` 위치를 유지하는 선택지를 임포트 규칙에 둡니다.
- 확인: `MeshAssetTest` 와 정점 레이아웃을 쓰는 전 시나리오의 네 백엔드 스크린샷.

### P6 VAT 위치를 경계 정규화 snorm16 으로

`.vat` 파일이 생긴 뒤에 합니다(지금 VAT 는 실행 중에 구워서 `float4`(16 B) 원소로 올립니다). 원소를 위치 `snorm16` 세 개(6 B) + 법선 `snorm8x2`(2 B) = 8 B 로 줄이고 위치 경계를 VAT 머리에 둡니다. `.vat` 버전을 올리고 `MeshVertexAnimation::packNormal` 의 12 + 12 비트 정수 형식을 `OctahedralNormal` 로 바꾸면 `VertexAnimation::unpackNormal` 이 P4 의 풀이 함수와 합쳐집니다.
측정: VAT 표 메모리와 군중 장면의 정점 셰이더 읽기 대역폭을 전후로 비교하고, 이득이 오차(1/65535)를 정당화하지 못하면 되돌립니다.

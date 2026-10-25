# 저장 전용 압축 타입

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: 없음 · 진행: 3/6

> 질문: 정점 · 인스턴스 · 버퍼의 크기를 줄이는 압축 타입을 엔진이 어떻게 갖고, 어디부터 어떤 규칙으로 적용하는가?
> 독자: 정점 포맷 · G버퍼 · 리플렉션을 고치는 작업자와 통합 담당.
> 결론: 타입은 `Core/Math` 에 들어갔고(P1) half 해독 복사본은 통합했으며(P2) 리플렉션 연결도 코드로 들어갔다(P3, 빌드 확인은 통합). 다음은 첫 적용처 셋(G버퍼 법선 P4, 메시 정점 P5, VAT 위치 P6) 순서다. P4 는 설계가 아래에 있고 GPU 확인과 재쿠킹을 하는 통합이 코드를 쓴다. 새 포맷은 아래 2절 규칙을 따른다.

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

### 리플렉션 접점 (P3)

열다섯 가지 타입(`half`~`half4` 넷, `unorm8` · `unorm16` · `snorm8` · `snorm16` 과 묶음 다섯, `OctahedralNormal`, `RGB10A2`)이 `Engine/Reflection/ReflectBuiltins.xxx` 의 내장 타입 줄이 되어 `PROPERTY` · 인자 · 반환 · 직렬화 · 인스펙터에서 값으로 보입니다. 앞으로 줄을 더하는 쪽이 지킬 구조는 이렇습니다.

- **성분 규칙은 한 곳입니다.** `Core/Math/PackedValueTraits.h` 의 `PackedValueTraits<T>` 가 `toFloats` · `fromFloats` · 범위(`kRangeMin` · `kRangeMax`)를 갖고, 글 처리기(`SerializeContext`) · 인자 변환(`ReflectValue.cpp`) · 인스펙터가 이것만 부릅니다. 표에 줄을 더하고 특수화를 빠뜨리면 `TextConv` `packed` 줄의 글 처리기 등록이 컴파일되지 않습니다. `fromFloats( toFloats( v ) )` 는 비트까지 같습니다(`PackedValueTraitsTest`). 예외는 `snorm` 정수 최솟값(`-최대` 로 감)과 `half` 신호 NaN(조용한 NaN 이 됨) 둘입니다.
- **이진은 `sizeof( T )` 바이트 그대로**(표의 줄마다 자동 등록, 정렬 2 인 `half3` 도 바이트 복사라 정렬되지 않은 주소에서 안전), **글은 성분 실수의 쉼표 목록**(`0.5,1,0.25`)입니다. 읽을 때 개수가 틀리거나 범위를 넘으면(`unorm8` 에 `5`) 거절하고 값을 그대로 둡니다(정수 필드가 범위를 넘는 글을 거절하는 규칙과 같음). `half` 의 무한대 · NaN 은 글로 못 갑니다(실수 읽기가 유한값만 받음, 이진은 비트 그대로).
- `OctahedralNormal` 의 글 · 인자 성분은 방향이 아니라 저장된 좌표 (u, v) 입니다(방향으로 풀고 다시 담으면 한 칸 안에서 비트가 흔들려 글 왕복이 불안정). 인스펙터만 방향 xyz 로 편집합니다(`InspectorValueWidget::Direction`, 바뀐 프레임에만 다시 인코딩).
- 인자 변환(`ReflectValue`)은 성분이 하나면 숫자도 받고(`unorm8` 에 `0.5`) 읽을 때 숫자로 내보내며, 범위를 넘는 값은 거절합니다. 성분이 여럿이면 글만 받습니다.
- 헤더가 둘 늘었습니다. `Core/Math/PackedTypesFwd.h`(이름과 별칭 `unorm8x4` 등의 유일한 정의 — `ReflectValue.h` · `InspectorBuiltinValue.h` 가 이것만 읽음)와 `Core/Math/PackedTypes.h`(완전한 정의 묶음 — 값 크기가 필요한 번역 단위와 `BuiltinFileHeader.tpl` 이 읽음). 표의 C++ 타입 칸에는 별칭만 씁니다(템플릿 이름의 쉼표가 매크로 인자를 나눔).
- 한계: (1) `Transient` 가 아닌 압축 타입 `PROPERTY` 는 고정 배치형 코덱의 인코딩 표 밖이라 코드 생성기가 **생성 오류로 멈춥니다**(포인터 같은 적격이 아닌 타입과 달리 조용히 코덱 없이 등록되지 않습니다. 압축 타입 이름은 `ReflectBuiltins.xxx` 의 `packed` 줄에서 오므로 줄만 더하면 따라옵니다. [ReflectionFormats](ReflectionFormats.md) 3.1 · 3.4). 압축 타입을 B 코덱에 넣는 일은 표(`kArrMathType`)가 아니라 새 인코딩 종류를 더하는 별도 단계 후보입니다. (2) 인스펙터는 색 선택기 · `Min` · `Max` 같은 메타를 읽지 않고 성분 드래그 위젯입니다. (3) UI 바인딩과 스키마 이주는 타입 이름 목록이 아니라 내장 타입 표의 갈래(`ReflectBuiltinClass`, `ReflectBuiltinClass.h` 가 유일한 정의)로 숫자를 가르며, 성분이 하나인 압축 타입은 숫자 바인딩입니다. (4) 속성에 쓸 때 `PROPERTY` 의 타입은 별칭(`unorm8x4`)으로 적습니다 — 파서가 별칭 이름을 표에서 찾고, 풀어 쓴 `NormalizedVector<unorm8, 4>` 는 모르는 이름이 됩니다.
- 확인: `PackedValueTraitsTest`(CoreTest) · `PackedValueSerializationTest`(EngineTest — 표의 모든 줄에 이진 · 글 처리기가 있는지, half 65536 비트 글 왕복, 거절 규칙, 인자 변환) · `InspectorBuiltinValueTest`(EditorTest — 위젯 분기 · 반환 서식). 매니페스트 id `packed-types-test` 가 돕니다.

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

같은 `half` → `float32` 풀이가 세 곳에 따로 있습니다. `Editor/Panels/RenderTarget/Model/RenderTargetViewUtil.cpp` 의 `halfToFloat`, `Engine/Renderer/Capture/ReflectionProbeBaker.cpp` 의 `decodeHalf`, `Engine/Renderer/Frame/FrameRendererUtil.h` 의 `halfToUnorm8` 입니다.
`half::fromBits( bits ).toFloat()` 와 `unorm8::fromFloat` 로 바꾸고 복사본을 지웁니다. 렌더 타깃 뷰와 캡처 파일(`RenderTargetImageFile.cpp`)의 기존 테스트가 같은 값을 내는지 봅니다.
결과: `halfToFloat` 는 `half::toFloat` 로 바뀌었고 복사본은 지웠습니다. 나머지 둘은 옛 동작을 보존하는 얇은 함수만 남았습니다.

| 복사본 | 옛 동작 | `half::toFloat` 와 | 처리 |
|---|---|---|---|
| `RenderTargetViewUtil` `halfToFloat` | 부호 · 비정규수 · Inf · NaN 을 IEEE 대로 | 모든 비트 패턴에서 같음(NaN 은 페이로드만 다름) | 복사본 삭제, 호출처가 `half::fromBits( bits ).toFloat()` |
| `ReflectionProbeBaker` `decodeHalf` | Inf · NaN 은 부호 있는 `kMaxFloat`(거르기가 NaN 을 퍼뜨리지 않게) | 유한값은 같고 Inf · NaN 이 다름 | 유한값만 `toFloat`, 비유한은 옛 규칙 유지(숨은 동작 변경 없음) |
| `FrameRendererUtil` `halfToUnorm8` | 음수 · -0 · 음의 NaN 은 0, 양의 Inf · NaN 은 255, 그 외 `value*255+0.5` 버림 | `unorm8::fromFloat` 와 반올림 규칙이 같다. 다만 NaN 은 0 이 됨 | 부호 · 비유한 분기만 남기고 나머지는 `toFloat` + `unorm8::fromFloat` |

테스트는 두 곳에 옛 함수 사본과 65536 개 비트 패턴을 전부 견주는 단언을 더했습니다(`ReadPixelHalfMatchesLegacyDecode`, `HalfToUnorm8MatchesLegacyDecode`). `decodeHalf` 의 규칙은 `half::toFiniteFloat` 로 올려 `HalfTest.ToFiniteFloatClampsOnlyNonFinite` 가 65536 비트 패턴을 모두 견줍니다(유한값은 `toFloat` 와 같고 Inf · NaN 은 부호 있는 `kMaxFloat`). HLSL 쪽 f16 해독은 저장소에 없습니다(`f16tof32` · `min16float` 쓰임 0 — 첨부 읽기는 CPU 에서만 합니다).
읽을 문서: 이 문서 1절. 고칠 문서: 이 문서 단위 절과 `진행`.

### P4 G버퍼 법선을 R16G16 옥타헤드럴로

**설계만 있고 코드는 없습니다.** GPU 확인과 셰이더 재쿠킹이 필요해 통합이 아래 목록으로 쓰고 검증합니다. 1920 × 1080 에서 법선 첨부가 8 B(`R16G16B16A16_FLOAT`)에서 4 B 가 되어 프레임마다 쓰기와 읽기가 각각 약 8 MB 줄어듭니다(계산값, 측정 필요). 옥타헤드럴 오차 상한 1e-4 는 Debug nogpu 에서 통과했고(`OctahedralNormalTest`), 지금 half 에 `n * 0.5 + 0.5` 를 적는 방식은 1 근처 간격이 1/2048 이라 법선 성분 오차가 최대 약 5e-4 입니다(계산값). 해상도는 오히려 좋아집니다.

**지금 모양**(코드로 확인).

| 항목 | 위치 | 내용 |
|---|---|---|
| 선언 | `Resource/engine/pipeline/deferredpipeline.xml` 4행 | `GBufferNormal` `_format="R16G16B16A16_FLOAT"` `_clearColor="0.5,0.5,1,0"`. 다른 파이프라인 xml 은 이 첨부를 쓰지 않음 |
| 쓰기 | `binding.hlsli` `SurfaceOutput::pack`(`SW_PASS_GBUFFER`) | `_normal = float4( saturate( normalize( n ) * 0.5 + 0.5 ), 1 )`, 구조체 필드 `float4 _normal : SV_TARGET1`. 머티리얼 셰이더가 모두 이 한 곳을 지남 |
| 읽기 | `PassTexture::sampleNormal`(`binding.hlsli`) | 호출은 `deferredlighting.hlsl`(32행 근처)과 `ssao.hlsl`(22 · 31행) 둘뿐. 슬롯 t2 는 BloomBright · AutoExposure 와 같은 슬롯이라(`Bindless::sampleIndex` · `sampleIndexWith` 둘) 슬롯 표는 안 바뀜 |
| 알파 | `deferredlighting.hlsl` | **알파가 "그려졌다" 표시를 싣는다**(클리어 0, 그려지면 1). `depth >= 1 && normalSample.a < 0.5` 이면 버린다. Overdraw 보기 모드가 깊이를 쓰지 않아 깊이만으론 안 된다. `TestRenderPassGPU.cpp` 의 `readMeanGBufferNormal` 도 알파 ≥ 128 로 그려진 픽셀을 센다 |

**바꿀 것.**

1. **포맷 열거자.** `RHITypes.h` `RHIFormat` 에 `R16G16_SNORM = 16` 을 뒤에 잇습니다(기존 번호 불변). 함께 고칠 곳은 `-Wswitch` 가 알려 줍니다: `getRHIFormatBlockInfo`(`{1,1,4}`), `isRHIFormatRenderable`, `toDxgiFormat` · `fromDxgiFormat`(`DX/RHIDxgiFormat.h`, DX11 · DX12 공유), `toVulkanTextureFormat`(`Vulkan/VulkanRHIDeviceInternal.h`, `VK_FORMAT_R16G16_SNORM`), `arrFormatRow`(`GL/OpenGLRHIResourceFactory.cpp`, `{ R16G16_SNORM, GL_RG16_SNORM, GL_RG, GL_SHORT }`), 에디터 렌더 타깃 뷰(`RenderTargetViewUtil.cpp` 의 세 switch — 채널 수 2, 해독 가능, 값 읽기)와 캡처(`RenderTargetImageFile.cpp`). 렌더 패스 선언의 `_format` 문자열은 `ENUM()` 이름이라 열거자만 더하면 읽히고, 렌더 가능 포맷 목록을 단언하는 `TestRenderPass.cpp`(765행 근처)에 새 이름을 더합니다. `R10G10B10A2_UNORM`(17)은 쓰는 단위가 생길 때 더합니다(지금 `RGB10A2` 타입의 쓰임 없음).
2. **렌더 타깃이 되는지 먼저 확인합니다(미확인 가정).** `isRHIFormatRenderable` 은 "네 백엔드가 모두 보장하는 포맷만 true" 가 기준이고, 16 비트 SNORM 은 Vulkan 의 필수 컬러 첨부 포맷이 아니고 GL 코어도 SNORM 을 컬러 렌더러블로 요구하지 않는 것으로 알고 있습니다(문서 확인 없이 기억, 틀릴 수 있음). 통합은 네 백엔드에서 `R16G16_SNORM` 컬러 첨부 지원을 장치 생성 때 조회해 없으면 오류로 멈추게 하거나(폴백 없음), 지원이 고르지 않으면 `R16G16_UNORM`(좌표를 `* 0.5 + 0.5` 로 옮겨 적음)으로 바꿉니다. 바꾸면 변경은 인코드 · 디코드 함수 두 개와 CPU 쪽 되읽기 한 곳(아래 5)에 한정됩니다. 반정밀도(`R16G16_FLOAT`)는 모든 곳에서 렌더 가능하지만 1 근처 간격이 1/2048 이라 오차 1e-4 를 못 지킵니다.
3. **셰이더 인코드 · 디코드 함수.** `binding.hlsli` 의 `VertexAnimation::unpackNormal`(선언 114행 근처, 정의 425행 근처)에 있는 팔면체 → 단위 벡터 식을 `Octahedral::decode( float2 octant )` 로 뽑고 `unpackNormal` 이 정수 두 개를 [-1, 1] 로 푼 뒤 그것을 부릅니다(P6 의 `unpackNormal` 대체와 합쳐지는 자리). 짝이 되는 `Octahedral::encode( float3 normal ) : float2` 는 `OctahedralNormal::fromNormal` 과 같은 식(`sum = |x|+|y|+|z|`, `z < 0` 이면 접기, 0 은 양수로 보는 부호)으로 새로 씁니다. 읽는 쪽은 `PassTexture::sampleNormal` 이 `float3`(디코드한 단위 노멀)을 돌려주게 바꾸고, 호출 둘(`deferredlighting` · `ssao`)에서 `* 2 - 1` · `normalize` 를 지웁니다. 쓰는 쪽은 `SurfaceOutput::_normal` 을 `float2 _normal : SV_TARGET1` 로 바꾸고 `pack` 이 `Octahedral::encode` 를 부릅니다.
4. **"그려졌다" 표시를 옮깁니다.** 알파가 없어지므로 표시를 G버퍼 머티리얼 칸 알파로 옮깁니다(`SurfaceOutput::encodeGbufferMaterial` 의 b · a 가 지금 0 으로 비어 있고 클리어 0 이라 같은 규칙: 그려지면 a = 1). `deferredlighting.hlsl` 의 버림 조건은 머티리얼 칸 알파를 읽는 `PassTexture::loadGbufferMaterial` 쪽(또는 옆의 조회)으로 바꿉니다. 알베도 알파는 셰이딩 모델(0 = Unlit)이라 클리어와 구분되지 않아 쓸 수 없습니다. 노멀의 클리어 값은 (0, 0)(디코드하면 +z)으로 기존 `0.5,0.5,1` 과 같은 방향입니다.
5. **CPU 쪽.** `TestRenderPassGPU.cpp` 의 `readMeanGBufferNormal` 이 `Rgba8` 로 읽어 `n * 0.5 + 0.5` 를 풉니다. 새 형식은 `snorm16x2` 두 값을 읽어 `OctahedralNormal::toNormal` 로 풀고, 그려졌다 표시는 머티리얼 첨부 알파에서 셉니다(`RHITestImage` 가 16 비트 두 채널을 읽는 길이 있는지 확인하고 없으면 더합니다). 에디터 렌더 타깃 패널은 새 형식을 방향 색(`n * 0.5 + 0.5`)으로 보여 주도록 합니다.
6. **재쿠킹.** 머티리얼 셰이더 전부(`SW_PASS_GBUFFER` 퍼뮤테이션)와 `deferredlighting` · `ssao` 가 바뀌므로 `App.exe --cook-shaders` 와 `ShaderCookRequestTest` 가 알리는 낡은 바이너리 삭제를 0 이 될 때까지 반복합니다([작업자 규칙](WorkerRules.md) 6절). 테스트 픽스처의 `GBufferNormal` 포맷 문자열(`TestRenderPass.cpp` · `TestShader.cpp`)은 새 이름으로 맞춥니다.

**영향을 받는 패스.** G버퍼(쓰기), Lighting(읽기), SSAO(읽기) 셋입니다. 다른 패스(TAA · 툰 · 포그 · 필드 깊이)는 `sampleNormal` 을 부르지 않는 것을 확인했습니다(`Resource/engine/shaders` 전체 검색, 호출은 위 둘뿐). 포워드 계열 파이프라인에는 이 첨부가 없습니다.

**골든과 확인.** 법선 양자화가 바뀌므로 디퍼드 경로를 타는 골든(`Test/QA/Golden` 의 장면 가운데 `-gv_deferred=1` 로 찍은 것)은 림 · 반사에서 작은 차이가 날 수 있습니다. 순서는 (1) 코드 쓰기, (2) 재쿠킹, (3) `RenderPassGPUTest` 의 G버퍼 노멀 테스트(수직 노멀, 4 백엔드), (4) 디퍼드 시나리오 스크린샷 전후 비교(라이팅 차이가 양자화 오차 이내이고 네 백엔드가 같은 이미지), (5) 골든 비교 후 의도한 차이면 `golden-record`. 항목은 구현할 때 `Test/QA/VerifyBatch.json` 에 id 로 더합니다.

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

# 1 채널(R8) DDS 를 내는 임포트 규칙

> 상태: 구현중 · 선행: 없음 · 영역: graphics · 옛번호: G-12 · 진행: 1/2

RHI · 로더는 `R8_UNORM`(DXGI 61, DX10 머리)을 읽는데 `App --import-textures` 의 규칙 표(`TextureImportConfig.json`)에 R8 출력이 없었습니다(2026-10-22 사용자 결정으로 착수 조건 갈음).

## 1. 정한 것

- 형식 이름 표는 `TextureImportFormatUtil::parse`(`TextureImportConfig.h`) 하나입니다. 설정 로드가 모르는 `format` 을 거절하고, 임포터는 `TextureImportFormat` 을 DXGI 로 바꾸기만 합니다. 예전에는 모르는 이름이 조용히 BC7 이 됐습니다(폴백 제거).
- `R8_UNORM`(`r8`)은 원본의 R 채널만 남깁니다(DirectXTex `Convert`). sRGB 짝이 없어 `srgb` 와 무관하게 UNORM 이고 바이트를 곡선 없이 옮깁니다(BC4 · BC5 와 같음).
- R8 DDS 는 DX10 머리로 씁니다(`DDS_FLAGS_FORCE_DX10_EXT`). 엔진 `DDSLoader` 의 옛 머리 경로는 L8 을 모릅니다. 다른 형식은 옛 머리 그대로라 커밋된 DDS 바이트가 바뀌지 않습니다.
- 규칙 표에 `Base_Mask` 프리셋과 `Mask_Textures`(`*_mask.*`) 규칙을 더했습니다. 지금 맞는 원본은 없습니다.

## 2. 단계

| 단계 | 내용 | 상태 |
|---|---|---|
| R1 | 형식 표 · R8 출력 · DX10 머리 · 규칙 표 · 테스트 `EditorTexturePipelineTest.SingleChannelR8ImportRoundTrips` | 끝(빌드 없이 작성) |
| R2 | 빌드와 `EditorTest` 실행, 생성 문서 `docs/Config/TextureImportConfig.md` 갱신 확인 | 남음 |

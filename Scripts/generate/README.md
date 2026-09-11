# Scripts/generate (자동화 및 데이터/코드 생성 스크립트)

빌드 환경 구성이 아닌, **데이터 쿠킹(Cooking/Baking), C++ 코드 생성 및 워크플로우 자동화**를 목적으로 하는 스크립트들이 모여있습니다.

## 주요 역할 및 스크립트

| 스크립트 | 역할 | 출력/대상 |
|---|---|---|
| 스크립트 | 역할 | 출력/대상 |
|---|---|---|
| `CookAssets.py` | Prefab, Scene XML 파싱(PFB2, SCN1) 및 Resource 디렉터리 .pack 바이너리 패킹 | `Resource/**/*.bin`, `build/*/Bin/Packs/*.pack` |
| `BakeShippingHostDefaults.py` | 커밋된 런타임 JSON 설정을 읽어 Shipping 및 Fallback용 C++ 헤더로 베이킹 | `build/.../ShippingHostDefaults.h` |
| `GenerateDocs.py` | Doxygen을 구동하여 C++ API 레퍼런스 문서 생성 | `Docs/Doxygen/html/index.html` |

## 팩 압축 코덱 고르기

`Config/Engine/PackConfig.json` 의 `compression` 이 정합니다. 이름은 팩 포맷 계약
(`Config/Engine/PackFormat.json` 의 `compression.codecs`)에 있는 것만 씁니다.

```json
"compression": { "codec": "Zlib", "level": 0 }
```

| 코덱 | 쿠킹에 필요한 것 | 쓰는 자리 |
|---|---|---|
| `None` · `RLE` · `Zlib` | **없음** — 파이썬 표준 라이브러리 | 기본값은 `Zlib` |
| `LZ4` | `py -3 -m pip install lz4` | 해제 속도가 로딩 시간인 자리 |
| `Zstd` | `py -3 -m pip install zstandard` | 배포물 크기를 줄이고 싶을 때 |

> **모듈이 없으면 쿠킹이 그 자리에서 멈춥니다.** 설정은 LZ4 인데 zlib 으로 구워 버리면 설정과
> 산출물이 달라지고, 그 사실은 한참 뒤 배포본에서야 드러납니다.

**실측 — 이 프로젝트의 실제 팩에서는 코덱 차이가 거의 없습니다** (2026-09-11):

| 팩 | Zlib | LZ4 | Zstd |
|---|---|---|---|
| `engine.pack` | 254,558,515 | 327,207,121 (**+28%**) | 254,202,331 |
| `common.pack` | 87,267 | 91,826 | 87,315 |
| `game_empty.pack` | 20,812 | 20,944 | 20,851 |

내용이 이미 압축된 것(DDS 텍스처·셰이더 바이너리)이라 더 줄 여지가 없기 때문입니다. **LZ4 만
눈에 띄게 커집니다.** 합성 데이터(1MB 모사 버퍼)에서는 Zstd 가 Zlib 보다 훨씬 작았는데
(34.7% vs …), 실제 팩에서는 0.1% 차이입니다 — 벤치마크 표본이 곧 결론이 아니라는 예입니다.
압축되지 않은 자산(대량의 JSON·XML·오디오 PCM 등)이 팩에 들어오면 그때 다시 재 보십시오.

## 실행 방법

```bash
py -3 -m Scripts cook --all
# 또는 직접 실행:
py -3 Scripts/generate/CookAssets.py [--all] [--prefabs-only] [--scenes-only] [--packs-only]
py -3 Scripts/generate/BakeShippingHostDefaults.py <output_header_path>
py -3 Scripts/generate/GenerateDocs.py [--open]
```

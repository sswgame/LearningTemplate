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

## 실행 방법

```bash
py -3 -m Scripts cook --all
# 또는 직접 실행:
py -3 Scripts/generate/CookAssets.py [--all] [--prefabs-only] [--scenes-only] [--packs-only]
py -3 Scripts/generate/BakeShippingHostDefaults.py <output_header_path>
py -3 Scripts/generate/GenerateDocs.py [--open]
```

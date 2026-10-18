# Scripts 실행 방법 · 에셋 병합 드라이버

[목차](../README.md)에서 나눈 문서.

## 통합 CLI 인터페이스 (`python -m Scripts`)

모든 도구는 프로젝트 루트에서 파이썬 모듈 인터페이스를 통해 일관되게 실행할 수 있습니다:

```bash
# Windows: py -3 -m Scripts <command>  /  POSIX: python3 -m Scripts <command>
py -3 -m Scripts setup                # 개발 환경 및 도구체인 탐색/설정 (SetupEnvironment)
py -3 -m Scripts cook --all           # 프리팹, 씬, 리소스 팩 일괄 쿠킹 (CookAssets)
py -3 -m Scripts vcpkg                # vcpkg 탐색 및 부트스트랩 (SetupVcpkg)
py -3 -m Scripts llvm                 # LLVM/Clang 탐색 및 설정 (SetupLlvm)
py -3 -m Scripts format               # C++ 코드 clang-format 자동 포맷팅 (FormatClangFormat) — 파일을 고르면 `--files a.cpp b.h`(게이트와 같은 철자)
py -3 -m Scripts lint                 # Staged 파일 대상 사전 커밋 린트 검사 (PreCommitLint)
py -3 -m Scripts gate                 # lint/gate/ 의 이름 목록 — 폴더 명령 넷은 이름 표가 아니라 폴더를 훑는다
py -3 -m Scripts gate CheckEngineLayers --files Source/Engine/Scene/Scene.h   # 게이트 하나
py -3 -m Scripts fix FormatIncludeOrder --files Source/Engine/Scene/Scene.cpp # 픽서 하나(lint/fixer/)
py -3 -m Scripts report RunBuildScriptInventory   # 보고서 하나(lint/report/)
py -3 -m Scripts selftest CheckLintsAreAlive      # 셀프테스트 하나(lint/selftest/)
py -3 -m Scripts docs                 # Doxygen API 레퍼런스 문서 생성 (GenerateDocs)
py -3 -m Scripts test SceneTest.*     # 스위트 · 케이스 이름으로 테스트 실행 (RunTests)
py -3 -m Scripts validate-assets      # 에셋 검증 표 (ValidateAssets) — `--severity error` 는 게이트와 같은 판정
py -3 -m Scripts asset-merge diff a.scene.xml b.scene.xml   # XML 에셋 의미 비교 (AssetMerge)
py -3 -m Scripts golden --app build/Ninja-Debug/Bin/App.exe --game NileCity   # 골든 이미지 (GoldenImages)
py -3 -m Scripts soak --app <App> --minutes 10                # 장시간 실행 (Soak)
py -3 -m Scripts perf --app build/Ninja-Release/Bin/App.exe   # 성능 회귀 (PerfRegression)
py -3 -m Scripts symbols --preset Ninja-Shipping --store <저장소>   # 심볼 저장소 배치 (StoreSymbols)
py -3 -m Scripts stacks <pid> --samples 200                   # 스택 샘플러 (SampleStacks, Windows)
```

## 개별 스크립트 실행

```bash
py -3 Scripts/setup/SetupEnvironment.py
py -3 Scripts/generate/CookAssets.py --all
py -3 Scripts/lint/gate/CheckEngineLayers.py
py -3 Scripts/lint/fixer/FormatClangFormat.py
py -3 Scripts/generate/GenerateShippingHostDefaults.py build/Ninja-Shipping/generated/sw/config/ShippingHostDefaults.h  # 빌드가 읽는 자리(<빌드 폴더>/generated)
```

`SetupLlvm` / `SetupEnvironment` 는 최소 LLVM 키트에 `clang-format` 을 포함·보완합니다 (기존 키트에 없으면 캐시된 LLVM tar에서 bin만 추출).

## XML 에셋 병합 · 비교 드라이버 (git)

`Scripts/asset/AssetMerge.py` 는 씬 · 프리팹 · 머티리얼 · 카탈로그 XML 을 **엔티티 id · 컴포넌트(`_componentName`) · 속성** 단위로 비교하고
3-way 병합한다(유니티 Smart Merge 와 같은 자리). 줄 단위 병합이 충돌하는 두 갈래 — 같은 목록 끝에 서로 엔티티를 더한 것, 한 요소의 다른 속성을
고친 것 — 가 충돌 없이 합쳐진다. 같은 속성을 다르게 고치면 충돌이고, 결과 파일에 우리 쪽 값을 둔 채 `<!-- MERGE CONFLICT … -->` 주석을 남기고
1 로 끝난다(XML 은 그대로 읽힌다 — 줄 충돌 표식처럼 파일을 깨지 않는다). 출력은 엔진 저장기와 같은 서식이라 엔진이 다시 저장해도 줄이 바뀌지 않는다
(엔진이 쓴 파일은 읽고 다시 쓰면 바이트까지 같다 — `PythonTest_TestXmlAssetMerge`).

git 에 붙이는 것은 강제하지 않는다(PC 마다 한 번):

```bash
git config merge.swasset.name   "SW XML asset merge"
git config merge.swasset.driver "py -3 Scripts/asset/AssetMerge.py git-merge %O %A %B %P"
git config diff.swasset.command  "py -3 Scripts/asset/AssetMerge.py git-diff"
```

그리고 `.gitattributes`(또는 저장소에 남기지 않으려면 `.git/info/attributes`)에:

```
*.scene.xml   merge=swasset diff=swasset
*.prefab.xml  merge=swasset diff=swasset
*.material    merge=swasset diff=swasset
```

리눅스는 `py -3` 대신 `python3`. 병합기가 XML 로 읽지 못하면(2) git 은 그 파일을 충돌로 남긴다. `--prefer ours|theirs` 는 충돌을 그쪽으로 푼다
(`py -3 -m Scripts asset-merge merge base ours theirs --prefer theirs`).

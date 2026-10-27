# Scripts 실행 방법 · 에셋 병합 드라이버

[목차](../README.md)에서 나눈 문서.

## 통합 CLI (`py -3 -m Scripts`)

목록은 코드에서 만든다 — 손으로 적은 목록은 낡는다(`docs/decisions/5-2_Build.md`).

```bash
# Windows: py -3 -m Scripts <명령>  /  POSIX: python3 -m Scripts <명령>
py -3 -m Scripts --help               # 이름 있는 명령과 한 줄 설명(원본은 Scripts/__main__.py 의 _kSubcommand)
py -3 -m Scripts --list [폴더]        # 파일마다 docstring 첫 줄과 부르는 명령(예: --list lint/gate)
py -3 -m Scripts gate|fix|report|selftest <이름> [인자…]   # lint/ 의 폴더에서 이름으로 — 이름 없이 부르면 그 폴더의 목록
```

## 개별 스크립트 실행

```bash
py -3 Scripts/setup/SetupEnvironment.py
build/Ninja-Debug/Bin/App.exe --cook-all --cooked-dir=build/Ninja-Debug/Cooked --pack-dir=build/Ninja-Debug/Bin/Packs
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

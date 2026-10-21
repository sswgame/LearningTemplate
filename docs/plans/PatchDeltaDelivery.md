# 패치 · 델타 업데이트 배포 구조

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: G-16 · 진행: 3/6

출시 뒤 바뀐 리소스만 내려받게 하는 팩 단위 차분 배포와 빌드 버전 관리가 없었다. 크래시 컨텍스트와 텔레메트리 코드는 있다. 스토어 제공자 연동(`StoreProviderIntegration`, 후보)은 이 일에 들지 않는다 — 내려받기는 런처나 스토어가 하고, 엔진은 받은 패치를 올바른 순서로 얹는다.

## 1. 설계

- **패치 하나 = 팩 하나 + 목록 하나.** 팩(`patch_<빌드>.pack`)은 새로 생겼거나 내용이 바뀐 파일만 담는다(팩 형식은 그대로 — `Config/Engine/PackFormat.json` 을 바꾸지 않는다). 목록(`<이름>.packpatch.xml`)은 `<PackPatch from="1.4.0" to="1.4.1" pack="patch_1.4.1.pack"><Removed path="ui/old.ui.xml"/></PackPatch>` 이다.
- **차이 셈**: 두 빌드의 내용 목록(경로 + 압축 전 내용 해시, `PackContentEntry`)에서 `PackPatchUtil::computeDelta` 가 바뀐 경로 · 지운 경로를 사전순으로 낸다. 같은 입력이면 같은 패치다. 한 목록에 같은 경로가 둘이면 거절한다.
- **얹기**: 런타임은 패치 팩을 기본 팩보다 높은 우선순위로 `ResourcePakManager::mountPatchPack( 팩, 우선순위, 지움 목록 )` 한다. 바뀐 파일은 우선순위 스택이 덮고, 지운 경로는 그 패치보다 아래 팩에서 찾지 않는다(`hasFile` · `readFile` · 폴더 나열 모두). 패치를 내리면 그대로 되돌아간다. 낱개 파일은 가리지 않는다 — 패치는 팩만 읽는 배포 구성의 일이다.
- **버전 관리**: 빌드 번호는 글 그대로 비교한다(대소 비교 없음). 설치된 빌드에서 시작하는 패치가 정확히 하나씩 이어져 목표 빌드에 닿아야 한다(`PackPatchUtil::orderPatchChain`) — 끊김 · 갈림 · 고리는 오류로 멈춘다(폴백 없음). 사슬 순서대로 우선순위를 올려 마운트하면 뒤 패치가 앞 패치를 덮는다.
- **무결성**: 팩 항목 CRC32(`PackFlag::HasCrc32`)가 이미 있다. 목록 파일의 서명과 팩 전체 해시는 배포 경로(런처 · CDN)가 정해질 때 정한다(P6).

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 설계(이 문서) | 끝남 |
| P2 | `Engine/Resource/Pack/PackPatch.h` — `computeDelta` · `parseManifest` · `makeManifestText` · `orderPatchChain`. 테스트 `PackPatchTest.*` 셋 | 끝남(빌드 미확인) |
| P3 | `ResourcePakManager::mountPatchPack` — `MountedPack::_listRemovedPathHash` 로 아래 팩 가리기(찾기 두 단계와 폴더 나열). 테스트 `PackPatchTest.PatchPackOverridesAndHidesRemovedPaths` | 끝남(빌드 미확인) |
| P4 | 시작 마운트가 팩 폴더의 `*.packpatch.xml` 을 읽어 `orderPatchChain` 순서로 `mountPatchPack` 한다. 설치된 빌드 번호의 출처(프로젝트 설정의 칸 하나)를 정한다 | 남음 |
| P5 | 쿠커가 두 빌드의 내용 목록을 남기고(팩마다 경로 + 내용 해시), 그 둘에서 패치 팩과 목록을 만든다(`Scripts` 쿠커 또는 C++ 팩 쿠킹 — [StructureSimplification](StructureSimplification.md) PK 와 함께) | 남음 |
| P6 | 배포 경로(런처 · CDN · 스토어)와 목록 서명 · 팩 해시 확인 | 남음 |

## 남은 것

- P4 ~ P6. P4 의 시작 마운트는 `AssetManager::mountStartupPacks` 다 — 같은 파일을 다른 작업이 고치고 있지 않은지 먼저 본다.
- 빌드 · `EngineTest --test_filter=PackPatchTest.*:ResourcePakTest.*` 실행.

# 패치 · 델타 업데이트 배포 구조

> 상태: 구현중 · 선행: 없음 · 영역: network · 옛번호: G-16 · 진행: 7/7

출시 뒤 바뀐 리소스만 내려받게 하는 팩 단위 차분 배포와 빌드 버전 관리가 없었다. 크래시 컨텍스트와 텔레메트리 코드는 있다. 스토어 제공자 연동(`StoreProviderIntegration`, 후보)은 이 일에 들지 않는다 — 내려받기는 런처나 스토어가 하고, 엔진은 받은 패치를 올바른 순서로 얹는다.

**결정(2026-10-23, 사용자)**: 출시는 스팀 기준이다. 스팀은 SteamPipe 가 depot 업로드 · 브랜치 · 청크 단위 차이 받기 · 무결성을 맡는다. 그래서 이 문서의 배포 경로 · 서명(P6)은 스팀 전용이면 취소하고, 스팀 밖 채널이 생길 때 다시 연다. 시작 마운트(P4)와 패치 만들기(P5)는 이미 들어갔고 우선순위를 내려 스팀 밖 채널과 콘텐츠 핫픽스용으로 남긴다. 아래 "두 채널 공존" 설계와 P7(팩 레이아웃 안정성 점검)을 더했다.

## 1. 설계

- **패치 하나 = 팩 하나 + 목록 하나.** 팩(`patch_<빌드>.pack`)은 새로 생겼거나 내용이 바뀐 파일만 담는다(팩 형식은 그대로 — `Config/Engine/PackFormat.json` 을 바꾸지 않는다). 목록(`<이름>.packpatch.xml`)은 `<PackPatch from="1.4.0" to="1.4.1" pack="patch_1.4.1.pack"><Removed path="ui/old.ui.xml"/></PackPatch>` 이다.
- **차이 셈**: 두 빌드의 내용 목록(경로 + 압축 전 내용 해시, `PackContentEntry`)에서 `PackPatchUtil::computeDelta` 가 바뀐 경로 · 지운 경로를 사전순으로 낸다. 같은 입력이면 같은 패치다. 한 목록에 같은 경로가 둘이면 거절한다.
- **얹기**: 런타임은 패치 팩을 기본 팩보다 높은 우선순위로 `ResourcePakManager::mountPatchPack( 팩, 우선순위, 지움 목록 )` 한다. 바뀐 파일은 우선순위 스택이 덮고, 지운 경로는 그 패치보다 아래 팩에서 찾지 않는다(`hasFile` · `readFile` · 폴더 나열 모두). 패치를 내리면 그대로 되돌아간다. 낱개 파일은 가리지 않는다 — 패치는 팩만 읽는 배포 구성의 일이다.
- **버전 관리**: 빌드 번호는 글 그대로 비교한다(대소 비교 없음). 설치된 빌드에서 시작하는 패치가 정확히 하나씩 이어져 목표 빌드에 닿아야 한다(`PackPatchUtil::orderPatchChain`) — 끊김 · 갈림 · 고리는 오류로 멈춘다(폴백 없음). 사슬 순서대로 우선순위를 올려 마운트하면 뒤 패치가 앞 패치를 덮는다.
  시작 마운트는 목표 빌드를 따로 받지 않고 폴더의 패치가 이어지는 끝까지 얹는다(`orderPatchChainToLatest`). 설치된 빌드는 같은 폴더의 `base.packbuild.xml` 이 밝히고, 사슬 밖에 남은 패치가 있으면 조용히 버리지 않고 실패한다.
- **무결성**: 팩 항목 CRC32(`PackFlag::HasCrc32`)가 이미 있다. 목록 파일의 서명과 팩 전체 해시는 배포 경로(런처 · CDN)가 정해질 때 정한다(P6, 스팀 전용이면 취소 — 스팀이 청크 무결성을 확인한다).
- **두 채널 공존(2026-10-23 결정, 설계)**: 스팀 채널과 스팀 밖 채널이 한 코드로 돈다.
  - 런타임은 같다. 패치 팩은 우선순위가 높은 팩일 뿐이다. 스팀 채널은 SteamPipe 가 새 빌드의 팩을 통째로 받아 주고, 패치 사슬은 쓰지 않는다. 스팀 밖 채널과 콘텐츠 핫픽스만 위 패치 사슬을 쓴다.
  - 설치 빌드 번호의 원본은 프로젝트 설정의 필드 하나다. 스팀 빌드 ID 를 따로 읽지 않는다(두 값이 따로 있으면 어긋난다). 지금 P4 는 번호를 팩 폴더의 `base.packbuild.xml` 에서 읽는다 — 설정 필드로 옮기는 일은 P4 의 근거(팩을 마운트하는 시점에는 팩 안 설정을 읽을 수 없다)와 부딪히므로, 옮기기 전에 그 부딪힘을 풀 방법을 정한다(확인하지 못한 것).
  - 패치를 쓰지 않는 빌드는 빈 값이 아니라 명시적 값(예: `patching="none"`)으로 표시한다. 값이 없으면 오류로 멈춘다(폴백 금지).
  - 패치 목록이 있는데 사슬이 끊기면 지금처럼 오류로 멈춘다. `patching="none"` 인 빌드에 패치 목록이 있어도 오류다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 설계(이 문서) | 끝남 |
| P2 | `Engine/Resource/Pack/PackPatch.h` — `computeDelta` · `parseManifest` · `makeManifestText` · `orderPatchChain`. 테스트 `PackPatchTest.*` 셋 | 끝남(빌드 미확인) |
| P3 | `ResourcePakManager::mountPatchPack` — `MountedPack::_listRemovedPathHash` 로 아래 팩 가리기(찾기 두 단계와 폴더 나열). 테스트 `PackPatchTest.PatchPackOverridesAndHidesRemovedPaths` | 끝남(빌드 미확인) |
| P4 | 시작 마운트가 팩 폴더의 `*.packpatch.xml` 을 읽어 설치된 빌드부터 이어지는 패치를 사슬 순서로 `mountPatchPack` 한다. **설치된 빌드의 출처는 프로젝트 설정이 아니라 팩 폴더의 `base.packbuild.xml`**(쿠커가 팩과 함께 남기는 빌드 내용 목록)이다 — 설정 칸을 두면 팩과 빌드 번호가 따로 어긋나고, 팩을 마운트하는 시점에는 팩 안의 설정을 아직 읽을 수 없다. `ResourcePakManager::scanAndMountPacks` 가 패치 목록이 가리키는 팩을 기본 팩에서 빼고, 빌드 기록 없음 · 사슬 끊김 · 갈림 · 고리 · 닿지 않는 패치 · 패치 팩 없음이면 이 호출이 올린 팩을 모두 내리고 실패한다(`PackPatchUtil::orderPatchChainToLatest`). 테스트 `PackPatchTest.StartupMountAppliesThePatchChainOnTopOfTheBasePacks` · `StartupMountMountsNothingWhenAPatchCannotBeApplied` · `ChainToLatestFollowsEveryPatchAndRefusesOrphans` | 끝남(Debug 테스트 확인) |
| P5 | 팩 폴더에서 두 빌드의 내용 목록(`PackBuildRecord` — 전체 가상 경로 + 내용 해시)을 세고 그 차이로 패치 팩 · 패치 목록 · 새 빌드의 내용 목록을 만든다(`PackPatchBuilder`). 소스 트리가 아니라 **구운 팩**에서 센다 — 쿠킹 증분 기록과 상관없이 배포되는 바이트의 해시다. 명령줄: `-cook-packs`/`-cook-all` 에 `-pack-build=<빌드>` 를 주면 `base.packbuild.xml` 을 남기고, `-make-patch -pack-dir=<새 팩 폴더> -patch-base=<이전 빌드 목록> -pack-build=<새 빌드> -patch-dir=<출력>` 이 패치를 만든다. 테스트 `PackPatchTest.MadePatchOnTheOldBuildShowsTheNewBuildContent` · `MakePatchRefusesNothingToPatchAndUnusableInputs` · `BuildRecordRoundTripsAndRefusesBrokenOnes` | 끝남(Debug 테스트 · 명령줄 실행 확인) |
| P6 | 배포 경로(런처 · CDN · 스토어)와 목록 서명 · 팩 해시 확인. 2026-10-23 결정: 출시가 스팀이면 SteamPipe 가 업로드 · 차이 받기 · 무결성을 맡으므로 스팀 전용이면 취소한다. 스팀 밖 채널이 생길 때 다시 연다(그때 서명 키 관리와 배포 경로는 계정 · 비용이 드는 연동이라 사용자 결정이 먼저). 팩 항목의 CRC32(`PackFlag::HasCrc32`)와 헤더 기하 검사는 이미 있어 잘린 · 깨진 팩은 읽을 때 거절된다 | 취소(스팀 전용이면 — 스팀 밖 채널이 생기면 다시 연다) |
| P7 | **팩 레이아웃 안정성 점검**(2026-10-23, Debug · 합성 입력). 쓰기 정책은 이미 안정적이라 코드는 고치지 않고 회귀 테스트(`PackLayoutTest.*`: 결정성 · 항목 하나 변경 시 다른 바이트 유지 · 추가 · 삭제 시 남은 페이로드 유지)만 더했다. 측정과 한계는 아래 "P7 측정" | 끝남 |

## 남은 것

- P6. 스팀 전용이면 취소다. 스팀 밖 채널이 생겨 다시 열면: 패치 목록에 팩 해시와 서명을 더하고(`PackPatchManifest`), 시작 마운트가 마운트 전에 확인한다(실패하면 지금처럼 이 호출이 올린 팩을 모두 내린다).
- 낱개 파일로 배포하는 구성(`setAllowLooseFiles`)은 패치가 가리지 못한다 — 패치는 팩만 읽는 배포 구성의 일이다(1절).
- 확인함(2026-10-22, Debug): `App -game=Empty -cook-all -pack-build=1.0` 이 `base.packbuild.xml`(5220 파일)을 남기고, `Resource/game/empty/data` 에 파일 하나를 더해 `-pack-build=1.1` 로 다시 구운 뒤 `-make-patch` 가 `1 changed, 0 removed` 패치(팩 12 KB)를 만들었다(같은 입력의 나머지는 해시가 같아 빠짐). 확인 못 한 것: 그 패치를 실제 `Packs/` 폴더에 두고 App 이 시작할 때 적용하는 실행(테스트는 `scanAndMountPacks` 를 직접 부른다).
- 빌드 · `EngineTest --test_filter=PackPatchTest.*:ResourcePakTest.*` 실행.

## P7 측정 (2026-10-23)

입력은 **합성**이다: 파일 600개(200 B ~ 600 KB 로그 균등 분포, 합 47.9 MB, 절반 압축 가능한 본문), Debug · `ResourcePakWriter::makePackBytes` 직접 호출, 개발 PC 한 대. 작은 합성 입력의 결과는 이 규모의 결과일 뿐이다 — 실제 쿠킹 트리(Empty 5220 파일 등)와 스팀이 쓰는 실제 청크 경계로는 재지 않았다.

- **결정성**: 같은 입력을 두 번, 담는 순서를 뒤집어도 None · Zlib · LZ4 · Zstd 모두 바이트가 같다(항목은 경로 해시 순, 항목별 압축).
- **구조**: FAT 경로 해시 순 · 항목별 압축(전체 스트림 아님) · 페이로드와 FAT 와 스트링 풀이 4096 B 섹터 정렬. 압축 없는 팩 49.3 MB, Zstd 26.6 MB.
- **항목 하나 바꿈**(같은 크기 · +100 B, 섹터 안): 항목 페이로드와 FAT 칸 밖 바이트는 그대로, 뒤 항목 안 밀림. 고정 1 MB 청크 2/50 (4.0 %, None), 2/27 (7.4 %, Zstd) — 바뀐 항목이 청크 경계에 걸려 둘. 내용 기준 청크(gear 해시, 평균 64 KB) 0.5 ~ 0.6 %.
- **항목 하나 추가 · 삭제**: 해시 순으로 뒤에 오는 항목 37 ~ 45 %(221 · 272 / 600)의 오프셋이 섹터 단위로 밀린다. 고정 1 MB 청크는 46 ~ 56 % 가 달라지고, 내용 기준 청크는 0.9 ~ 1.3 %. 항목 크기가 섹터를 넘게 변해도 같다.
- **결정**: 팩 형식(`PackFormat.json`)을 바꾸지 않는다. 밀림은 항목이 이어 붙는 가변 길이 배치의 성질이고, 없애려면 항목마다 청크 크기 단위의 빈 자리(패딩)나 슬롯 배치가 필요해 형식 변경이다. 스팀이 내용 기준으로 청크를 자르면(확인 못 함 — SteamPipe 의 청크 경계 방식은 문서로 확인하지 않았다) 지금 배치로 충분하다. 고정 청크라면 추가 · 삭제가 잦은 빌드에서 불리하므로 첫 스팀 업로드 두 번(빌드 사이)의 실제 업데이트 크기를 재고, 나쁘면 형식 변경안(슬롯 배치)을 연다.

# 패치 · 델타 업데이트 배포 구조

> 상태: 측정대기 · 선행: 없음 · 영역: network · 옛번호: G-16 · 진행: 7/7

**남은 확인(통합에서 확인):** 매니페스트 id `pack-patch-tests`, `pack-patch-app-start`(아래 "남은 것"). P6 은 스팀 밖 채널이 생길 때만 다시 연다. 확인이 끝나면 통합 담당이 이 문서를 닫는다.

출시 뒤 바뀐 리소스만 내려받게 하는 팩 단위 차분 배포와 빌드 버전 관리입니다. 스토어 제공자 연동(`StoreProviderIntegration`, 후보)은 이 일에 들지 않는다 — 내려받기는 런처나 스토어가 하고 엔진은 받은 패치를 올바른 순서로 얹는다.

**결정(2026-10-23, 사용자)**: 출시는 스팀 기준이다. SteamPipe 가 depot 업로드 · 브랜치 · 청크 단위 차이 받기 · 무결성을 맡으므로 배포 경로 · 서명(P6)은 스팀 전용이면 취소하고 스팀 밖 채널이 생길 때 다시 연다. 시작 마운트(P4)와 패치 만들기(P5)는 스팀 밖 채널과 콘텐츠 핫픽스용으로 남긴다.

## 1. 설계

- **패치 하나 = 팩 하나 + 목록 하나.** 팩(`patch_<빌드>.pack`)은 새로 생겼거나 내용이 바뀐 파일만 담는다(팩 형식 `Config/Engine/PackFormat.json` 은 그대로). 목록(`<이름>.packpatch.xml`)은 `<PackPatch from="1.4.0" to="1.4.1" pack="patch_1.4.1.pack"><Removed path="ui/old.ui.xml"/></PackPatch>` 이다.
- **차이 셈**: 두 빌드의 내용 목록(경로 + 압축 전 내용 해시, `PackContentEntry`)에서 `PackPatchUtil::computeDelta` 가 바뀐 경로 · 지운 경로를 사전순으로 낸다(같은 입력이면 같은 패치, 한 목록에 같은 경로가 둘이면 거절).
- **얹기**: 런타임은 패치 팩을 기본 팩보다 높은 우선순위로 `ResourcePakManager::mountPatchPack( 팩, 우선순위, 지움 목록 )` 한다. 바뀐 파일은 우선순위 스택이 덮고 지운 경로는 그 패치보다 아래 팩에서 찾지 않는다(`hasFile` · `readFile` · 폴더 나열 모두). 낱개 파일은 가리지 않는다 — 패치는 팩만 읽는 배포 구성의 일이다.
- **버전 관리**: 빌드 번호는 글 그대로 비교한다(대소 비교 없음). 설치된 빌드에서 시작하는 패치가 정확히 하나씩 이어져 목표 빌드에 닿아야 한다(`PackPatchUtil::orderPatchChain`) — 끊김 · 갈림 · 고리는 오류로 멈춘다(폴백 없음). 시작 마운트는 폴더의 패치가 이어지는 끝까지 얹고(`orderPatchChainToLatest`) 사슬 밖에 남은 패치가 있으면 실패한다.
- **무결성**: 팩 항목 CRC32(`PackFlag::HasCrc32`)와 헤더 기하 검사가 이미 있어 깨진 팩은 읽을 때 거절된다. 목록 서명과 팩 전체 해시는 P6.
- **두 채널 공존(2026-10-23 결정)**: 런타임은 같다(패치 팩은 우선순위가 높은 팩일 뿐). 스팀 채널은 SteamPipe 가 새 빌드의 팩을 통째로 받아 주고 패치 사슬을 쓰지 않으며 스팀 밖 채널과 콘텐츠 핫픽스만 패치 사슬을 쓴다.
  - 설치 빌드 번호의 원본은 팩 폴더 옆 `base.packbuild.xml` 하나다(2026-10-24 결정, 결정 기록 5-9 — 마운트 전에 읽는 값은 팩 밖에 둔다). 프로젝트 설정 필드로 옮기지 않고 스팀 빌드 ID 를 따로 읽지 않는다(두 값이 따로 있으면 어긋난다).
  - 패치를 쓰지 않는 빌드는 그 기록에 명시적 값 `patching="none"` 을 쓴다(`PackBuildRecord::_patching` 이 `chain | none`, `-pack-build` 없는 쿠킹이 `PackPatchBuilder::writeNoPatchingRecord` 로 남긴다). `patching` 이 없는 옛 기록은 읽기가 거절한다(폴백 금지). 패치 목록이 있는데 사슬이 끊기거나 `none` 폴더에 패치 목록이 있으면 오류로 멈추고 none 기록에서 패치를 만들려 하면 `makePatch` 가 거절한다.

## 2. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| P1 | 설계 | 끝남 |
| P2 | `Engine/Resource/Pack/PackPatch.h` — `computeDelta` · `parseManifest` · `makeManifestText` · `orderPatchChain`, 테스트 `PackPatchTest.*` | 끝남(빌드 미확인) |
| P3 | `ResourcePakManager::mountPatchPack` — `MountedPack::_listRemovedPathHash` 로 아래 팩 가리기 | 끝남(빌드 미확인) |
| P4 | 시작 마운트가 팩 폴더의 `*.packpatch.xml` 을 읽어 설치된 빌드부터 이어지는 패치를 사슬 순서로 마운트한다. 설치 빌드의 출처는 프로젝트 설정이 아니라 `base.packbuild.xml`(팩을 마운트하는 시점에는 팩 안의 설정을 읽을 수 없고 설정 칸을 두면 어긋난다). `scanAndMountPacks` 가 패치 목록이 가리키는 팩을 기본 팩에서 빼고 빌드 기록 없음 · 사슬 끊김 · 갈림 · 고리 · 닿지 않는 패치 · 패치 팩 없음이면 올린 팩을 모두 내리고 실패한다 | 끝남(Debug 테스트 확인) |
| P5 | 팩 폴더에서 두 빌드의 내용 목록(`PackBuildRecord`)을 세고 그 차이로 패치 팩 · 목록 · 새 빌드의 내용 목록을 만든다(`PackPatchBuilder`). 소스 트리가 아니라 **구운 팩**에서 센다(배포되는 바이트의 해시). `-cook-packs`/`-cook-all` 에 `-pack-build=<빌드>` 를 주면 `base.packbuild.xml` 을 남기고 `-make-patch -pack-dir=<새 팩 폴더> -patch-base=<이전 빌드 목록> -pack-build=<새 빌드> -patch-dir=<출력>` 이 패치를 만든다 | 끝남(Debug 테스트 · 명령줄 실행 확인) |
| P6 | 배포 경로(런처 · CDN · 스토어)와 목록 서명 · 팩 해시 확인. 스팀 전용이면 취소, 스팀 밖 채널이 생기면 다시 연다(서명 키 관리와 배포 경로는 계정 · 비용이 드는 연동이라 사용자 결정이 먼저) | 취소 |
| P7 | 팩 레이아웃 안정성 점검(2026-10-23, Debug · 합성 입력). 쓰기 정책이 이미 안정적이라 코드는 고치지 않고 회귀 테스트(`PackLayoutTest.*`)만 더했다. 측정은 아래 | 끝남 |

## 남은 것

- P6: 다시 열면 패치 목록에 팩 해시와 서명을 더하고(`PackPatchManifest`) 시작 마운트가 마운트 전에 확인한다(실패하면 올린 팩을 모두 내린다).
- 낱개 파일 배포(`setAllowLooseFiles`)는 패치가 가리지 못한다.
- 확인함(2026-10-22, Debug): `App -game=Empty -cook-all -pack-build=1.0` 이 `base.packbuild.xml`(5220 파일)을 남기고 파일 하나를 더해 `-pack-build=1.1` 로 다시 구운 뒤 `-make-patch` 가 `1 changed, 0 removed` 패치(팩 12 KB)를 만들었다. 확인 못 한 것: 그 패치를 실제 `Packs/` 폴더에 두고 App 이 시작할 때 적용하는 실행(테스트는 `scanAndMountPacks` 를 직접 부른다) — 매니페스트 id `pack-patch-tests`(`PackPatchTest.*` · `ResourcePakTest.*` · `PackLayoutTest.*`), `pack-patch-app-start`(한 명령이 아직 없다).

## P7 측정 (2026-10-23)

입력은 **합성**이다: 파일 600개(200 B ~ 600 KB 로그 균등, 합 47.9 MB, 절반 압축 가능), Debug · `makePackBytes` 직접 호출, 개발 PC 한 대. 실제 쿠킹 트리(Empty 5220 파일 등)와 스팀의 실제 청크 경계로는 재지 않았다.

- **결정성**: 같은 입력을 두 번, 담는 순서를 뒤집어도 None · Zlib · LZ4 · Zstd 모두 바이트가 같다(항목은 경로 해시 순, 항목별 압축). FAT · 페이로드 · 스트링 풀은 4096 B 섹터 정렬. 압축 없는 팩 49.3 MB, Zstd 26.6 MB.
- **항목 하나 바꿈**(같은 크기 · +100 B): 항목 칸 밖 바이트는 그대로, 뒤 항목 안 밀림. 고정 1 MB 청크 4.0 % (None), 7.4 % (Zstd), 내용 기준 청크(gear 해시, 평균 64 KB) 0.5 ~ 0.6 %.
- **항목 하나 추가 · 삭제**: 해시 순으로 뒤에 오는 항목 37 ~ 45 %의 오프셋이 섹터 단위로 밀린다. 고정 1 MB 청크는 46 ~ 56 % 가 달라지고 내용 기준 청크는 0.9 ~ 1.3 %.
- **결정**: 팩 형식을 바꾸지 않는다. 밀림은 가변 길이 이어 붙임 배치의 성질이고 없애려면 항목마다 청크 단위 패딩이나 슬롯 배치(형식 변경)가 필요하다. 스팀이 내용 기준으로 청크를 자르면(문서로 확인 못 함) 지금 배치로 충분하다. 고정 청크라면 추가 · 삭제가 잦은 빌드에서 불리하므로 첫 스팀 업로드 두 번의 실제 업데이트 크기를 재고 나쁘면 슬롯 배치안을 연다.

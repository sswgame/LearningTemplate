# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: **남은 일**만 본다. 끝난 일은 git 기록에, 지킬 규칙은 CLAUDE.md · AGENTS.md · `docs/agents/` · 스킬에 있다 — 여기에 되풀이하지 않는다. 끝나면 줄을 지운다.

## 일하는 법

절차는 스킬이 정본이다: 묶음 끝 검증 · push `bundle-verify`, Linux `linux-verify`, 시나리오 실패 `scenario-debug`. 시나리오 체계 · 시간 기록은 `Test/README.md`. 사람이 보는 실행: `py -3 Scripts/dev/WatchScenarios.py [--preset] [--backend] [--stages a,b] [체인…]`.

## 상태 (2026-11-09)

- main 은 origin 보다 앞서 있다(검증 뒤 push). 묶음 검증 `80d60e162f`: Windows Debug 통과, Release 진행 중이었다 — 결과(`VERIFIED=<sha>`)를 보고 push.
- 병렬 작업자 W1 ~ W8 이 워크트리(`worktree-agent-*` 브랜치)에서 일한다(코드 → 린트 → 커밋만). 보고가 오면 통합 담당이 모아 빌드 · 테스트 · 검증한다.
- 브라우저 조종 MCP(`chrome-devtools`, `py -3 -m Scripts browser-mcp` 로 설치)는 새 세션에서 실린다.

## 남은 일

### 1. 작업자 통합 (보고가 오면)

| 작업자 | 범위 |
|---|---|
| W1 | GameFramework 편집 모델(CharacterEditor · GameplayTools) 87 자리 → `expected<bool>`, GF `find*Index` 41 → optional |
| W2a · W2b | 엔진 `find*Index` 약 20 종(호출 약 400) → optional — 애니메이션 · 리그 · 물리 · 착용 / 오디오 · 그래프 · 리플렉션 · 글꼴 · UI · 범용 · 팩 |
| W3 | 결과 형식 린트에 `string* pOut…` 포인터 출력 인자, `.cpp` 안 상태 타입을 Internal 이 고치는 모양 |
| W4 | MVVM 남은 View 66 개 |
| W5 | 틱 뒤 큐 미루기 남은 타입 → 프레임 시스템, 디버그 그림 겹치기(`CanvasOverlays`)를 개발 층으로(골든이 바뀔 수 있다), [FolderMoves](FolderMoves.md) 막힌 표 |
| W6 | [StructureRefactor](StructureRefactor.md) T8 · T9 · T10, [ResourceStructure](ResourceStructure.md) 남은 것 |
| W7 | 시퀀서 편집 조작 · VFX 저작 UX · 에셋 배치([DesignBacklog](DesignBacklog.md)) |
| W8 | 지역 효과 범용 구조 · 관성 이동 · 표면 변형 층([DesignBacklog](DesignBacklog.md) 통합 판단 절) |

통합 뒤: `Expected.h` 의 두 방향 임시 변환을 지운다(결과 형식 기준선이 0 이 되면). 작업자들이 고치는 문서(DesignBacklog · StructureRefactor · ResourceStructure · FolderMoves · TickPlacement · Errors)의 중복 · 끝난 줄 정리는 통합 뒤에 한다.

### 2. 통합 담당이 직접 (실행 · 측정이 필요)

- **에디터 첫 패널의 느린 프레임:** 새 패널이 처음 열리는 프레임이 약 1 초(`EditorWaitSettled has waited 2 s (2 frame(s))`). `-gv_traceSlowTickMs` 트레이스로 그 프레임의 동기 일(에셋 · 썸네일 · PSO)을 찾아 비동기로.
- **시나리오 대기:** boat · clothshowcase(골든 앞 구간이 물리 이력 — 줄이려면 골든을 다시 뜬다), waterplay(강체 속도 · 잠듦 감시 값이 있으면 조건 대기로). 전체 게임 시간 배율은 기각(Empty 체인 배율 4 — 프레임 11% 감소, 94 단계 중 17 실패).
- **유니티 배치 크기 측정:** Release 깨끗한 빌드 12 · 24 · 48.

### 3. Shooter3D 총 (사용자 지적)

- **소총 자세:** UBC 애니메이션(UAL1)에 소총 클립이 없어 권총 조준 클립으로 소총이 머리 옆에 들린다(11-04 UBC 교체 때부터, 골든에도 그 모습). 브랜치 `rifle-pose-wip`(5073ea9105)에 기반이 있다 — 셋 클립 줄 `retarget`(가져오기가 `models/_retargeted/` 에 굽는다), 상체 층 `OverrideMeshSpace`, `WeaponDef twoHanded`. KayKit 2h_ranged 는 UBC 에서 조준 자세로 옮겨지지 않았다(팔 사슬 4 뼈 ↔ 3 뼈 의심, 미확인). 다음: Quaternius **Toon Shooter Game Kit**(CC0, <https://quaternius.com/packs/toonshootergamekit.html> → Google Drive) 의 `Character_Soldier` 클립 `Idle` · `Idle_Shoot` · `Run_Gun` · `Run_Shoot` · `Walk_Shoot`(팔 사슬 UpperArm · LowerArm 두 뼈)을 들여 UBC 로 리타깃 → 몸 전체로 먼저 사진 확인 → 손 소켓(`ubc_humanoid.sockets.xml` 의 `Gun`) 회전을 사진으로 맞춤 → 골든 다시.
- **걸을 때 총이 떨린다:** 원인 미확인. `-gv_shooterMotionTrace` 기록에 총 위치를 더해 손 · 카메라와 프레임별로 대조한다.

### 4. 리소스

- **Sprite Fusion 타일셋**(상세 페이지마다 `License: CC0`): Castle · Tiny Swords(Pixel Frog), Dungeon(0x72), Four Seasons Platformer(Analog Studios), Retro Gameboy(Ben Leggett) — `https://www.spritefusion.com/downloads/tilesets/<이름>.zip`. 1Bit Platformer(Kenney)는 그 주소가 없어 Kenney 원본에서.
- **Sprite Fusion VFX 25 종**(MIT — 저작권 고지를 credits 에): 그림이 아니라 캔버스에 그리는 TypeScript 코드다 → 브라우저 MCP 로 재생해 스프라이트 시트로 굽는다. `https://www.spritefusion.com/downloads/pixel-art-vfx/<이름>.zip`.
- **PixelFX Piper**(무료 브라우저 효과 생성기): 이용 조건을 먼저 확인해 credits 에, 기본 효과를 시트로 내보내고, 층 쌓기 기법(섬광 → 궤적 · 충격 → 파편 · 먼지 → 잔광, 히트스톱 · 흔들림 · 데칼)으로 효과를 만든다. 붉은 사막의 연출은 기법만 배우고 그대로 본뜨지 않는다.
- `Content/common/credits.md` 머리의 "새 리소스는 CC0 묶음만" 을 아래 "리소스 규칙" 으로 고친다.

### 5. 그 뒤

- Empty 게임 이름을 TestBed 로 전수 개명(옛 이름 린트로 막기), S8 측정 6 건, dzn SPIR-V→DXIL 충돌.
- 웹 · Android 첫 빌드와 남은 플랫폼 일([PlatformTargets](PlatformTargets.md) "남은 일").
- 스캐폴드 `new-online-kit` 로 키트 하나를 실제로 지어 보기. 게임을 `-cooked-content` 로 띄워 경로를 조립해 읽는 에셋이 빠지지 않는지 보기.
- 남은 단위 테스트: `py -3 Scripts/dev/TestCoverage.py` 의 "없음" 폴더를 클래스 이름으로 테스트에서 찾아 하나씩 본다(테스트가 다른 폴더에 있어도 "없음" 으로 센다). 온라인 `AccountTicketConnect` 의 성공 · 만료 · 재시도 길.
- 흔들리는 게임 골든: 11-04 이후 지지 않았다. 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다(GL 컨텍스트 잃음은 `KHR_debug` 로 가린다).
- **Linux**(큰 일이 모두 끝난 뒤 — 사용자 결정): `LinuxScenarios.py all`, `.linux` 골든. 드라이버를 고르는 칸(dzn · Mesa d3d12 GPU)을 더한다. 에디터 체인에서 원인을 못 가린 단계 약 18 — `audiomixer` · `graphaddnode` · `graphviewtools` · `viewportselect` · `assetmanage` · `meshmodifiers` · `idle` · `previewswap` · `flowboundaries` · `selftests`(`dpi.monitorScaleFollows`). WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보인다. 그래픽 골든 크기 차이는 Windows 모니터 DPI 에 묶인 것으로 보인다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes` — 포맷 기능 확인 없이 블렌드를 켠다).

## 사용자에게 받을 것

영수증 · 푸시 · 외부 로그인의 실제 계정과 키(지금은 로컬 가짜 끝점까지 — `Config/Server/Online/Providers/`, `Scripts/net/OnlineLocalRun.py`). 새 의존성(인코더 라이브러리)의 라이선스.

## 꼭 알아야 할 결정

- **리소스 규칙(2026-11-09):** 받아 오는 리소스는 무료 · 공개 라이선스(CC0 우선, MIT 처럼 재배포가 되는 것은 고지와 함께)만 — 묶음 페이지의 라이선스를 확인해 credits 에 URL · 날짜. 출처 하나에 고집하지 않고 뼈대가 달라도 리타깃한다. 에이전트가 리소스를 직접 그리지 않는다. PixelFX Piper(무료)는 쓴다. Agent Sprite Forge 는 도입하지 않는다(유료 생성 API 필요). Quaternius 는 2026-08 QAL(원본 재배포 금지)로 바뀌었다 — 새 묶음은 그 묶음 페이지가 CC0 일 때만. 원본 바이너리는 Git LFS, 받아 둔 팩은 `/AssetLibrary/`(무시)에 두고 쓰는 것만 `common/models_raw · textures_raw/<출처>/<묶음>/` 로.
- 범용 스크립팅 언어는 도입하지 않는다(실제 요구가 확인될 때만 — [판단](DesignBacklog.md#핫-리로드와-스크립팅-언어-판단)). 런타임 아틀라스는 하지 않는다. 메모리 풀은 측정 뒤, GC 는 권하지 않는다.
- 최상위 `Mods/` 는 `Test/Mods/`(시연 모드는 테스트 자료). App 의 `Dev/` 는 남긴다(배포에 쓰지 않는 인터페이스를 두지 않는다).
- 줄끝: `.gitattributes` 가 `* text=auto eol=lf`.

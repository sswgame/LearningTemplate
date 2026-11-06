# 다음 세션 인계

> 질문: 무엇이 남았고 무엇부터 하는가?
> 독자: 이어서 일하는 세션(에이전트와 사람).
> 결론: 아래 **남은 일**만 본다 — 끝난 일은 git 기록과 계획 문서([StructureRefactor](StructureRefactor.md))에 있다. 순서는 7 → 1 → 2 → 0 → 5. 이 문서에는 끝난 일을 쌓지 않는다(끝나면 줄을 지운다).

## 일하는 법 (요약)

- 단계마다 코드 → `py -3 -m Scripts lint` → 커밋. 묶음 끝에 `bash Scripts/dev/VerifyBundle.sh` 한 번 — Windows Debug · Release · 서버 · 배포 구성이 모두 통과한 뒤에만 Linux(`Scripts/dev/LinuxScenarios.py all`)가 돈다. 끝 줄 `VERIFIED=<sha>` 면 push.
- **테스트마다 로그의 Error · Warning 을 모두 분류한다** — `build/Ninja-Debug/Testing/Logs/*.log`, id 없는 줄과 `[id:…]` 줄을 나눠 테스트가 선언한 것인지 본다. 통과만 보지 않는다.
- 느린 곳은 새로 재지 말고 로그의 `[Scenario] … timeline:` · `stage reset took` · `Backend swap took` 줄을 모은다(`Test/README.md` 의 시간 기록).
- 규칙: 구조로 푼다 · 폴백 없음 · 게이트는 최후 · 구성 차이는 크기대로(`docs/agents/BuildConfigSwitch.md`) · 용어는 `docs/agents/NamingCpp.md` 의 표. 시나리오 체계는 `Test/README.md` 가 정본.
- Windows 전용 서버(`Ninja-Debug-Server` — 빌드 · `Server.exe --test`)도 묶음 끝에 함께 돌린다. 키트는 짓지 않는다(`SW_BUILD_UNUSED_KITS` 는 Release 만) — 서버 키트 테스트는 `Ninja-Release` 의 `App --test` 가 돌린다.
- Linux 는 WSL 클론(`~/LT-linux`)에서 돈다 — 입구는 `py -3 Scripts/dev/LinuxScenarios.py`(명령은 파일 머리말, 절차는 `linux-verify` 스킬). 매니페스트에서 포트가 빠지면(2026-11-05 `vulkan`) 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다. 시나리오는 Linux-Release 를 Xvfb(`apt-get download` + `dpkg -x`, 창 관리자 없음 · 사용자 화면을 건드리지 않음, `-screen 0 2560x1440x24`) 안에서 돌린다 — Vulkan 은 `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`(lavapipe), GL 은 `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`(Mesa 26 은 4.5 문맥에 `ARB_gl_spirv` 를 내준다), 소프트웨어라 느리니 `-gv_scenarioWallScale=4`. `Scenario.<팩>` 명령에서 `-scenario-backend-split` 을 빼고 `-scenario-backends=vk` 로 돌린다. 임포트 결과는 Windows 에서 복사한다(Debug 임포트는 너무 느리다). 골든은 `<시나리오>.<이름>.<백엔드>.linux.ppm` 으로 따로 둔다. Xvfb 에는 창 관리자가 없어 `engine/automation/windowpresent` 의 최소화 단계는 건너뛴다. 메시 원본은 Git LFS 라 클론에 `git-lfs` 와 `.git/lfs/objects` 복사가 있어야 임포트된다. Xvfb 의 `xkbcomp` 경로는 실행 파일에 박혀 있다. 고정 화면 글꼴: 카탈로그가 찾는 파일을 `~/.local/share/fonts` 에 둔다(Noto CJK, LiberationSans).
  전용 서버 Debug 도 클론에서 함께 짓는다 — 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다: `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로** 넘긴다(상대 경로면 컴파일러 확인에서 진다), 실행은 `Bin/Server -gv_serverExitAfterTicks=60` 과 `Bin/Server --test`.
  Wayland 창 층(`SW_ENABLE_WAYLAND`)은 기본 클론 빌드에 없다 — `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(`apt-get download` + `dpkg -x`, .pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON` 로 짓는다. 실행은 `SW_WINDOW_BACKEND=wayland`.

## 남은 일

### 0. 사용자가 시킨 일 — 빠짐없이 (2026-11-01)

에이전트 없이 직접 한다. 이름만 보지 말고 내용을 읽고 판단한다. 빌드 통과는 판단 기준이 아니라 마지막 확인이다.

1. 코드 전체를 다시 훑어 SOLID · 프로젝트 구성(층 · 책임 · 의존 방향 · 폴더)을 전면적으로 고친다 — StructureRefactor 7 · 8 절의 "남김" · "다음" 줄 포함.
2. 확인한 상태(Debug 빌드 · 단위 · ctest, Release 빌드 · ctest, 오류 · 경고 분류)만 중간중간 origin main 에 푸시한다.
3. 아래 2 ~ 6 절(테스트 공백 · 흔들리는 골든 · T8 · T9 …)도 남은 일이다.
4. 로우폴리 에셋을 실제 게임 품질로(CC0 만, 원본 바이너리는 Git LFS — 사용자 결정). Quaternius 열 팩은 `/AssetLibrary/Quaternius/`(무시)에 받아 두었고, 게임에 쓰는 것만
   `common/models_raw · textures_raw/quaternius/<묶음>/` 로 옮긴다(credits.md). 농장 · 서부 · 놀이공원 · 투기장 · 나일 소품, HarvestValley 농부 · 마을 사람(외형 · 피팅 · UAL 클립), MeadowVillage 밭 칸은 했다.

### 2. 테스트 · 시나리오 공백

| 일 | 내용 |
|---|---|
| 흔들리는 게임 골든 | 2026-11-04 Debug · Release 전체 ctest 각 일곱 번과 Release Shooter3D 체인 세 번에서 한 번도 지지 않았다(killcam · cape · bodyviews · useroptions · GL 컨텍스트 잃음 모두). 다시 지면 그 로그의 단계 · 백엔드 · 직전 `Assets.Pending` 을 적는다 — GL 컨텍스트 잃음은 GL 디버그 출력(`KHR_debug`)으로 드라이버 리셋인지 VAT 인덱스 드로우인지 가린다 |
| 남은 단위 테스트 | 폴더 집계(`py -3 Scripts/dev/TestCoverage.py`)는 테스트가 다른 폴더에 있어도 "없음" 으로 센다 — 실제로 비었는지는 클래스 이름을 테스트에서 찾아 본다. 2026-11-05 에 채운 것: 파괴 상태 · 그래프, 외형 해석, 리그 IK, 턴 중계, RESP 저장소, 메시 쪼개기, 시퀀서 · 단축키 · 전역 변수 · 모듈 · 환경설정 · 테스트 실행기 · 문서 패널 · 테마 값 · Sync 탭 Model, UI 레이아웃 · 포커스 · 이벤트, 날씨 · 시계, 조종 시스템 · 의도 기록, 차량 좌석, 행동 트리, 기믹 회로, 어빌리티, 모듈 카탈로그, 프로파일러 · 타일맵 · 데이터 테이블 · 카탈로그 · 콘텐츠 브라우저 · UI 미리보기 Model, 인터랙터, 프리팹 덮어쓰기, 팩 리더, 크래시 보고, 립싱크, 카메라 모드. `UIPreviewModel` 은 화면이 있어야 해 시나리오 `editor/uipreview` · `uipreviewdesigner` 가 맡는다. 장르 키트 Rule 은 모두 채웠다 — 게임이 싣지 않는 키트는 Release `UnitTests`, 게임이 싣는 키트는 `UnitTests.<게임>`, 온라인 키트는 `UnitTests.OnlineKits` 가 돈다(에디터 케이스는 `Scenario.editor` 의 `editor.unitTests`). 크게 남은 것: 화면 없이 도는 패널 Model 밖의 큰 파일(`Editor/Common` · `Editor/Panels` 의 그리기 코드는 시나리오 몫이다). 온라인 키트는 싣는 게임이 없어 `UnitTests.OnlineKits`(OnlineLoadBot `-test`, Release — 봇이 온라인 키트를 모두 싣는다)가 돌린다 — 매칭 · 채팅 거르기 · 라이브 이벤트 · 친구 관계 · 거래 상태 기계 · 우편 · 경제 구매 · 순위 보드 정의 · GM 권한 · 지급은 채웠다, 계정 로그인 저장(가짜 암호 — 연동 · 마지막 로그인 수단 · 탈퇴 유예 포함)과 서버 목록 `ServerListModel`(실패 길 · 루프백 서버로 응답 길)도 채웠다. 2026-11-06 서비스 층도 채웠다 — 순위표 · 길드 · 매칭 대기열 · 파티 로비 · 채팅 · 친구 · 경제 영수증 · 거래 · 로그인 · 우편함 · 라이브 이벤트 · 서버 디렉터리 · GM, 그리고 외형 선택 · 퀘스트 일지 · 예약 작업 · 보존 쓸기 · 월드 플래그 · 록온 · 대화 러너 · UI 화면 서비스 · UI 애니메이션. 네트워크 하네스는 있다 — Core 엔드포인트는 `LoopbackCluster`, 온라인 키트 끝단은 `OnlineHostTestUtil.h`(`OnlineTestRig` — 호스트 + 클라이언트 묶음, 키트 DLL 이 include 하고 키트 서버 쪽은 같은 프로토콜 항목의 가짜로 올린다). 하네스는 있다(`AnimationTestUtil` · `RHIFakeDevice` · `FakeFontRasterizer`, 자식 프로세스는 `Core/Process` · 크래시 보고). 2026-11-06 에 더 채운 것: 모듈 의존 그래프 · 언로드 큐 · 섀도 복사본 거절, UI 문서, 가상 입력, 개발 명령, 스프라이트 클립, 캐릭터 데이터 읽개, UI 입력 소비 · 입력 방식, 포인터 잡기 취소, 게임 흐름의 화면 없는 규칙(팁 · UI 없는 로딩 화면 · 타이틀 없는 게임). 아직 단위 테스트가 없는 곳: `Programs/ReflectionParser`(별도 프로그램이라 빌드로만 확인), 섀도 복사본의 성공 길(실제 모듈 바이너리가 있어야 해 핫 리로드 시나리오가 맡는다), 화면이 뜨는 게임 흐름(시나리오 `loadingscreen` 이 맡는다). 집계의 나머지 "없음" 폴더는 위 안내대로 클래스 이름으로 하나씩 본다 |

### 7. 규칙을 형식 · 린트로 강제하고 전수 정리 (2026-11-05 사용자 지적)

규칙 문서만 있고 검사가 없어 어긋남이 퍼졌다. 검사를 먼저 세우고(린트 — 새 위반을 막는다) 기존 위반을 한 번에 고친다.

끝났다. 그대로 두는 정적 상태: 상태 없는 객체(SQL 드라이버 · 애니메이션 코덱), 정적 초기화 전에도 세는 `CacheStats`, 테스트 등록표, OS 진입점, 모듈 경계 창구, `FileDialog`(떼어 둔 스레드가 객체를 쓴다), 아이콘 표 발행(글꼴 설정이 에디터 범위보다 먼저 돈다), `EditorExtensionRegistry` 의 종류별 목록(등록 줄의 수명이 모듈 이미지와 같아 편집기 범위보다 먼저 선다 — 언리얼 모듈 등록과 같은 자리).

### 5. 그 뒤

- Linux 에디터 체인 나머지(WSL lavapipe, 140 단계 중 약 18 단계): 2026-11-06 처음 끝까지 돌았다. 원인을 못 가린 것 — `audiomixer`(M 단추를 눌러도 음소거 수가 그대로), `graphaddnode` · `graphviewtools`(노드 추가 · 정렬이 안 먹는다), `viewportselect`(Ctrl 클릭 선택), `assetmanage`(휴지통 `Asset.Recycled`), `meshmodifiers`, `idle`(창 초점 없는 프레임 상태), `previewswap` · `flowboundaries`(GL 없음으로 보인다), `selftests` 의 `dpi.monitorScaleFollows`(Xvfb 에서만). 믹서를 단독으로 돌리면 팔레트 입력부터 먹지 않는다 — WSLg · Xvfb 에서 창이 입력 초점을 받지 못하는 것이 공통 원인으로 보이니, 창 관리자와 초점이 있는 화면에서 먼저 다시 돌린다. 그래픽 골든(`screen` · `viewmodes` · `emptyscene` · `loadingscreen` · `sceneviewgameview` · `screenrectview` · `flowterrain` · `terrainbrush`)은 크기부터 다르다 — 골든을 만든 Windows 모니터 DPI 에 묶인 것으로 보이며, 어느 PC 에서나 같은 크기가 되게 하거나 Linux 골든을 따로 둔다. lavapipe 검증 레이어가 `blendEnable` + `R16G16_SNORM` 을 잡는다(`viewmodes`) — 포맷 기능 확인 없이 블렌드를 켠다. 시나리오 게임 체인은 Debug 텍스처 임포트가 너무 느려 돌려 보지 못했다.

---
name: linux-verify
description: "이 저장소의 Linux 검증 절차: WSL 클론 빌드, Xvfb 에서 시나리오 체인(Vulkan dzn · GL d3d12, --gpu software 는 lavapipe · llvmpipe), .linux 골든 기록과 비교, Linux Debug 단위 테스트. Linux · WSL · Xvfb · lavapipe · llvmpipe · .linux 골든 · Linux 시나리오 · X11 코드를 다룰 때, 또는 묶음 끝 Linux 검증을 할 때 쓴다."
---

# linux-verify

입구는 `py -3 Scripts/dev/LinuxScenarios.py <명령>` 하나다. Windows 에서 부르면 WSL 에서 다시 뜬다. 임시 스크립트를 새로 만들지 말고 여기에 더한다.

## 순서

평소에는 `bundle-verify` 의 한 명령이 Windows 가 모두 통과한 뒤 Linux 를 돌린다. Windows 가 지면 Linux 를 돌리지 않는다(사용자 결정). 아래는 Linux 결함만 따로 고칠 때다.

1. 처음 한 번: `setup`(Xvfb · 글꼴을 sudo 없이 `~/lt-deps` 에 푼다). 클론과 `build/Linux-Debug`(컴파일러 캐시) · `build/Linux-Server` 구성은 아래 "클론 환경" 을 따른다.
2. `all`: HEAD 로 Release 빌드 → 단위 · 서버 → 모든 체인(4 개씩 동시). 끝 줄 `LINUX_OK=<sha>`. 앞이 지면 긴 체인을 돌리지 않는다.
3. 영향 범위만: `build` → `chain <이름…>`(또는 `stages`).
4. 새니타이저(감사 P-8): 묶음마다 `sanitize`(ASan + UBSan, `Linux-Debug-ASan`) 한 번, 경쟁을 다룬 묶음 뒤에는 `sanitize thread`(TSan — `x64-linux-tsan` 트리플릿을 따로 깐다). 끝 줄 `SANITIZE_OK=<종류>`. 누수는 기준선을 만들기 전까지 끈다(`detect_leaks=0`).
5. 골든을 새로 뜰 때: `chain --record <이름…>` → `goldens`(본 저장소로 복사, 한 색 사진 경고) → 표본을 눈으로 본다 → 커밋 → `--record` 없이 다시 `chain`.

## 지킬 것

- 시나리오 드라이버는 기본이 WSL GPU 다(Vulkan 은 dzn, GL 은 d3d12). 처음 한 번 `setup-dzn`. 소프트웨어(lavapipe · llvmpipe)는 명령 앞에 `--gpu software`. `.linux` 골든은 dzn 으로 다시 뜬다 — 그 전에는 그림 차이를 참고만 한다.
- dzn 은 `vkCreateDevice` 에 `descriptorIndexing` 묶음 비트가 있어야 bindless 경로로 셰이더를 내린다(없으면 크기 없는 텍스처 배열을 쓰는 PSO 컴파일에서 죽는다). 드라이버 진단(`DZN_DEBUG=nir`)은 백엔드 자식 프로세스의 콘솔로 나와 체인 실행에서는 버려진다 — `Bin/App -EnableEditor -vk -scenario=…` 를 직접 띄워 본다.

- 기록 실행은 그림이 틀려도 통과한다. 기록 통과는 그림의 증거가 아니다.
- 이 명령은 클론당 하나만 돌린다(체인 병렬은 명령 안에서 한다).
- 클론은 `build` · `all` 때 `reset --hard` + `git clean -fd` 된다(무시 목록 밖의 추적 안 된 파일 — 커밋하지 않은 기록 골든도 — 이 지워진다). 기록한 골든은 다음 `build` 전에 `goldens` 로 옮긴다. 클론에서 고친 것은 사라진다. 고칠 것은 본 저장소에서 고치고 커밋한다.
- 멈출 때는 `stop` 을 쓴다. 게임 프로세스는 이름이 `GameThread` 로 바뀌어 `App` 으로 찾으면 놓치고, Xvfb 도 남는다.
- 긴 체인은 백그라운드로 돌리고 알림을 기다린다. 결과를 짐작해 말하지 않는다.
- 로그: `~/LT-linux/build/Linux-Release/Testing/Logs/Scenario.<이름>.vk.log`. 판정 경고 밖의 Error · Warning 분류는 로그를 직접 읽는다.
- X11 헤더 주의: `Core/Foundation/X11Headers.h` 는 `None` · `True` · `False` · `Success` 매크로를 지운다. Windows 빌드는 이 코드를 컴파일하지 않으니 X11 을 고치면 `build` 로 확인한다.

## 클론 환경 (WSL `~/LT-linux`)

- vcpkg: `build` 는 vcpkg.json 해시가 설치본 표시(`.sw-manifest.sha256`)와 다르면 Linux-Debug 를 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 다시 구성해 새 포트(예: dawn)를 깐다. 매니페스트에서 포트가 빠지면 공유 설치본 보호 장치가 설치를 건너뛰어 오버레이 패치(imgui-node-editor 의 `NavigateToRect` · `GetGroupSize`)가 들어가지 않는다 — 혼자 쓰는 클론이라 한 번 `-DSW_VCPKG_FORCE_INSTALL=ON` 으로 구성한다.
- Xvfb: `apt-get download` + `dpkg -x` 로 받고 창 관리자 없이 `-screen 0 2560x1440x24` 로 띄운다(사용자 화면을 건드리지 않는다). `xkbcomp` 경로는 실행 파일에 박혀 있다. 창 관리자가 없어 `engine/automation/windowpresent` 의 최소화 단계는 건너뛴다.
- 드라이버(`scenario_env`): 기본은 Vulkan `~/lt-deps/dzn` 의 dzn ICD · GL `GALLIUM_DRIVER=d3d12`(`/usr/lib/wsl/lib`). `--gpu software` 는 Vulkan `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`(lavapipe), GL `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe`(Mesa 26 은 4.5 문맥에 `ARB_gl_spirv`). 벽시계 시한은 `-gv_scenarioWallScale=4`. `Scenario.<팩>` 명령에서 `-scenario-backend-split` 을 빼고 `-scenario-backends=vk`.
- 콘텐츠: 임포트 결과는 Windows 에서 복사한다(Debug 임포트가 너무 느리다). 메시 원본은 Git LFS 라 `git-lfs` 와 `.git/lfs/objects` 복사가 있어야 임포트된다. 고정 화면 글꼴은 카탈로그가 찾는 파일을 `~/.local/share/fonts` 에(Noto CJK · LiberationSans). 골든은 `<시나리오>.<이름>.<백엔드>.linux.ppm`.
- 전용 서버 Debug: 렌더러를 빼는 구성이라 클라이언트 빌드가 못 잡는 층 위반이 여기서만 드러난다 — `cmake -S . -B build/Linux-Server -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSW_TARGET_TYPE=Server` 에 Linux-Debug 캐시의 컴파일러 · 툴체인 · `VCPKG_INSTALLED_DIR` 을 **절대 경로로**(상대 경로면 컴파일러 확인에서 진다). 실행 `Bin/Server -gv_serverExitAfterTicks=60` · `Bin/Server --test`.
- Wayland(`SW_ENABLE_WAYLAND`, 기본 빌드에 없음): `~/lt-deps/root` 에 sudo 없이 푼 `libxkbcommon-dev` · `wayland-protocols`(.pc 의 prefix 를 그 폴더로)를 `PKG_CONFIG_PATH` 로 주고 `cmake --preset Linux-Debug -B build/Linux-Wayland -DSW_ENABLE_WAYLAND=ON`. 실행은 `SW_WINDOW_BACKEND=wayland`.

단계가 지면 `scenario-debug` 스킬로 넘어간다.

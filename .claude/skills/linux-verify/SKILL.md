---
name: linux-verify
description: "이 저장소의 Linux 검증 절차: WSL 클론 빌드, Xvfb 에서 시나리오 체인(Vulkan lavapipe · GL llvmpipe), .linux 골든 기록과 비교, Linux Debug 단위 테스트. Linux · WSL · Xvfb · lavapipe · llvmpipe · .linux 골든 · Linux 시나리오 · X11 코드를 다룰 때, 또는 묶음 끝 Linux 검증을 할 때 쓴다."
---

# linux-verify

입구는 `py -3 Scripts/dev/LinuxScenarios.py <명령>` 하나다. Windows 에서 부르면 WSL 에서 다시 뜬다. 임시 스크립트를 새로 만들지 말고 여기에 더한다.

## 순서

평소에는 `bundle-verify` 의 한 명령이 Windows 가 모두 통과한 뒤 Linux 를 돌린다. Windows 가 지면 Linux 를 돌리지 않는다(사용자 결정). 아래는 Linux 결함만 따로 고칠 때다.

1. 처음 한 번: `setup`(Xvfb · 글꼴을 sudo 없이 `~/lt-deps` 에 푼다). 클론과 `build/Linux-Debug`(컴파일러 캐시) · `build/Linux-Server` 구성은 `docs/NEXT.md` 의 Linux 줄을 따른다.
2. `all`: HEAD 로 Release 빌드 → 단위 · 서버 → 모든 체인(4 개씩 동시). 끝 줄 `LINUX_OK=<sha>`. 앞이 지면 긴 체인을 돌리지 않는다.
3. 영향 범위만: `build` → `chain <이름…>`(또는 `stages`).
4. 골든을 새로 뜰 때: `chain --record <이름…>` → `goldens`(본 저장소로 복사, 한 색 사진 경고) → 표본을 눈으로 본다 → 커밋 → `--record` 없이 다시 `chain`.

## 지킬 것

- 기록 실행은 그림이 틀려도 통과한다. 기록 통과는 그림의 증거가 아니다.
- 이 명령은 클론당 하나만 돌린다(체인 병렬은 명령 안에서 한다).
- 클론은 `reset --hard` 된다. 클론에서 고친 것은 사라진다. 고칠 것은 본 저장소에서 고치고 커밋한다.
- 멈출 때는 `stop` 을 쓴다. 게임 프로세스는 이름이 `GameThread` 로 바뀌어 `App` 으로 찾으면 놓치고, Xvfb 도 남는다.
- 긴 체인은 백그라운드로 돌리고 알림을 기다린다. 결과를 짐작해 말하지 않는다.
- 로그: `~/LT-linux/build/Linux-Release/Testing/Logs/Scenario.<이름>.vk.log`. 판정 경고 밖의 Error · Warning 분류는 로그를 직접 읽는다.
- X11 헤더 주의: `Core/Foundation/X11Headers.h` 는 `None` · `True` · `False` · `Success` 매크로를 지운다. Windows 빌드는 이 코드를 컴파일하지 않으니 X11 을 고치면 `build` 로 확인한다.

단계가 지면 `scenario-debug` 스킬로 넘어간다.

# 함정

작업 중 부딪히기 쉬운 환경 · 도구 함정입니다. 해당하는 일을 할 때만 읽습니다. 지금 상태는 [NEXT.md](plans/NEXT.md) 입니다.

## 빌드 · 환경

- 빌드 로그를 bash `grep` 으로 세지 않는다: `Start-Process *>` 로그는 UTF-16 이라 항상 0 이다. `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 센다.
- 같은 PC 에서 빌드 · 테스트를 겹치면 시간 제한에 걸려 다시 돈다. 검증 중에는 다른 빌드를 하지 않는다. 비결정적 실패는 3~5 회 재실행해 재현율을 적는다.
- 검증 스크립트는 빌드 종료 코드와 `ctest` 가 읽는 바이너리의 갱신 시각을 함께 본다(구성 이름 오타로 옛 바이너리가 "통과"한 적이 있다).
- 서버 구성 빌드 폴더의 낡은 컴파일러 런처 캐시(없는 `sccache.exe`)는 `cmake --preset <이름> -UCMAKE_CXX_COMPILER_LAUNCHER -UCMAKE_C_COMPILER_LAUNCHER` 로 지우고 다시 구성한다.
- 병합 비용: 작업자 둘을 따로 통과시킨 뒤 합치면 의미 충돌이 난다. 병합마다 전체 빌드를 돌리고 관련 스위트를 Release 와 Debug 둘 다 돌린다. 빌드 전 컴파일러 프로세스 수를 본다.
- 새 .cpp 가 "incomplete type" 으로 서면 `Component.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 PCH 에 없으니 직접 include 한다.

## 워크트리 · 편집

- `worktree-remove` 는 브랜치도 지운다. 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지운다(커밋이 없는 변경이 있으면 건너뛴다 — 버려도 되면 `--force`).
- `D:\w\perfbase` · `perfbase2` 워크트리는 성능 기준선이라 남긴다.
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 본다.
- 에디터 시나리오가 저장하는 픽스처(`automation/editor/<시나리오>/` 아래)는 에디터가 LF 로 쓴다. `core.autocrlf` 체크아웃이 CRLF 로 만들면 시나리오만 돌려도 `git status` 가 더러워져서, `.gitattributes` 의 한 줄이 그 폴더 전체를 `eol=lf` 로 못박는다(새 폴더도 자동). 이 줄을 바꾼 뒤 기존 워크트리는 픽스처 파일을 지우고 `git checkout-index -f` 로 다시 받는다.

## 실행 · 코드

- 앱 창을 전경으로 올려 캡처하지 않는다. 게임 뷰는 `-gv_screenshot`, 에디터 UI 는 `-gv_screenshotEditor` · `<CaptureEditorUI>`([에디터 UX 기반](plans/EditorUX.md)).
- `-gv_screenshot` · `-gv_screenshotEditor` 만 주면 사진 뒤에도 앱이 끝나지 않는다(종료 코드 124 로 죽는다). 사진만 찍을 거면 `-gv_screenshotQuit=1`, 측정과 함께면 `-gv_profileFrames`([검증과 측정](08_Verification.md) 1절).
- `-scenario=` 는 리소스 ID(`engine/automation/x.scenario.xml`)도 받는다. 읽기 실패 로그는 `[read]`(파일을 못 찾음 — 리소스 루트 · 작업 폴더 · 낱개 파일 금지 구성이 함께 찍힌다) · `[xml]`(문법) · `[scenario]`(형식)로 막힌 단계를 가른다.
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨진다(Debug 는 조용). 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않는다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders`.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공한다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.
- `ImGui::BeginPopupModal` 은 창 이름마다 처음 한 번만(`FirstUseEver`) 가운데에 놓고, 다시 열면 옛 자리를 쓴다. 시나리오가 창 크기를 바꾼 뒤 모달이 작아진 창의 오른쪽 아래 가장자리에 붙어 글자가 잘렸다. 에디터의 모달은 `EditorWidgets::beginCenteredModal`(열릴 때마다 호스트 뷰포트 가운데)로 연다.
- ImGui 렌더러 백엔드(Vulkan · DX12)는 `RenderDrawData` 를 부를 때마다 뷰포트의 정점 · 인덱스 버퍼 링을 한 칸 넘기고, 모자란 칸은 GPU 를 기다리지 않고 바로 지운다. 한 프레임에 같은 뷰포트를 한 번 더 그리면(에디터 UI 캡처처럼) `kImGuiDrawsPerViewportPerFrame` 을 올린다 — 안 올리면 Vulkan 검증 레이어가 `vkDestroyBuffer ... in use by VkCommandBuffer`(VUID-vkDestroyBuffer-buffer-00922)를 낸다.
- OpenGL 은 프레임버퍼 묶임이 컨텍스트 상태라, 오프스크린 패스(`beginRenderPass`)를 연 채로 두면 이어서 패스 없이 직접 그리는 코드가 그 텍스처에 그린다. 에디터 UI 캡처가 그랬다 — 렌더 스레드가 이어 그리는 플로팅 창이 제 창이 아니라 캡처 텍스처의 원점에 한 번 더 그려져 캡처에 두 번 나왔다(2026-10-20 수정). 패스를 연 쪽이 `endRenderPass` 로 닫고 돌아온다.

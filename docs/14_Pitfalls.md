# 함정

작업 중 부딪히기 쉬운 환경 · 도구 함정입니다. 해당하는 일을 할 때만 읽습니다. 지금 상태는 [NEXT.md](plans/NEXT.md) 입니다.

## 빌드 · 환경

- 빌드 로그를 bash `grep` 으로 세지 않는다: `Start-Process *>` 로그는 UTF-16 이라 항상 0 이다. `Out-File -Encoding utf8` 로 쓰고 `Select-String | Measure-Object` 로 센다.
- 같은 PC 에서 빌드 · 테스트를 겹치면 시간 제한에 걸려 다시 돈다. 검증 중에는 다른 빌드를 하지 않는다. 비결정적 실패는 3~5 회 재실행해 재현율을 적는다.
- 검증 스크립트는 빌드 종료 코드와 `ctest` 가 읽는 바이너리의 갱신 시각을 함께 본다(구성 이름 오타로 옛 바이너리가 "통과"한 적이 있다).
- 서버 구성 빌드 폴더의 낡은 컴파일러 런처 캐시(없는 `sccache.exe`)는 `cmake --preset <이름> -UCMAKE_CXX_COMPILER_LAUNCHER -UCMAKE_C_COMPILER_LAUNCHER` 로 지우고 다시 구성한다.
- 병합 비용: 작업자 둘을 따로 통과시킨 뒤 합치면 의미 충돌이 난다. 병합마다 전체 빌드를 돌리고 관련 스위트를 Release 와 Debug 둘 다 돌린다. 빌드 전 컴파일러 프로세스 수를 본다.
- 새 .cpp 가 "incomplete type" 으로 서면 `Component.h` · `GameObject.h` · `Scene.h` · `SceneManager.h` · `RHITypes.h` 는 PCH 에 없으니 직접 include 한다.
- 에디터 모듈의 새 REFLECT 헤더는 `Source/Editor/CMakeLists.txt` 의 `REFLECTION_HEADERS` 에 넣는다(안 넣으면 `StaticType` 미정의 링크 오류).

## 워크트리 · 편집

- `worktree-remove` 는 브랜치도 지운다. 병합 뒤 `git merge-base --is-ancestor` 로 확인하고 지운다(커밋이 없는 변경이 있으면 건너뛴다 — 버려도 되면 `--force`).
- `D:\w\perfbase` · `perfbase2` 워크트리는 성능 기준선이라 남긴다.
- Edit 도구와 `sed -i` 가 CRLF 파일을 LF 로 바꾼 적이 있다. 수정 뒤 `git diff --stat` 로 파일 전체가 바뀌지 않았는지 본다.

## 실행 · 코드

- 앱 창을 전경으로 올려 캡처하지 않는다. 게임 뷰는 `-gv_screenshot`, 에디터 UI 는 `-gv_screenshotEditor` · `<CaptureEditorUI>`([에디터 UX 기반](plans/EditorUX.md)).
- 정렬 할당 객체를 기반 포인터로 지우면 배포본 힙이 깨진다(Debug 는 조용). 모듈 안 정적 객체의 소멸자에서 스레드를 join 하지 않는다.
- 무효 bindless 인덱스(`0xFFFFFFFF`)는 DX12 페이지 폴트, Vulkan DEVICE_LOST 다. 셰이더를 바꾼 뒤에는 `App.exe --cook-shaders`.
- `castTo<Component>( Component*, 타입 )` 은 항상 성공한다. 종류 거르기는 `getTypeInfo()->isDerivedFrom( 타입 )`.

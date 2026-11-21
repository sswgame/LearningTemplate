# Testing

> 결론: 테스트는 부하 · 앞선 실행과 무관하게 결과가 같아야 한다. 별도 테스트 프레임워크를 들이지 않고 `SW_TEST_CASE` · 시나리오 XML 틀을 그대로 쓴다. 골든은 혼자 돈 실행에서만 기록한다. 시나리오 실패 추적은 스킬 `scenario-debug`.

## 단위 테스트

- 틀(`SW_TEST_CASE` · 단언 매크로 · 시나리오 XML 형식 · 단계 등록)은 바꾸지 않는다. 같은 프로세스 병렬은 `TestExecutionContext` 를 케이스별로 가르기 전에는 하지 않는다.
- Tests never write to `Bin`, a fixed file name or a fixed port: per-run temporary path, sockets on port 0.
- 시간 · 부하에 기대지 않는다: 잠 대신 사건 동기화 · 조건 대기, 상태는 RAII 로 되돌린다, 시한 대신 샤드. "부하 탓" 으로 닫지 않는다. 흔들리는 시간 문턱은 CI 에 넣지 않는다.
- 벤치는 시간 문턱을 두지 않고 값을 로그로 남긴다 — 판정은 통합이 [NEXT](../NEXT.md) 측정 줄의 문턱으로 한다(`--test-run-disabled`, Release 5 번 중앙값).
- 에디터 ViewModel 명령은 헤드리스 테스트(`Panels/<Panel>/Model/Tests/`, [EditorUI](EditorUI.md) MVVM).
- 신뢰 경계 파서의 퍼저 크래시 입력은 `Tests/Snapshots/` + 단위 케이스로 남긴다.

## 시나리오

- 선언 밖 Warning 도 실패다.
- 골든 · `<Record>` 는 혼자 돈 실행에서만 기록하고 체인으로 검증한다. 기준 파일이 없으면 `MissingBaseline` — `changed` 단계만 없는 것을 쓴다.
- 기다림은 조건 대기(`<WaitUntil>` · `<Expect watch>`)로 — 짧은 고정 대기 · 옛 `<Wait>` 를 새로 쓰지 않는다. 시나리오 전체 `gv_timeScale` · 사진 수 줄이기 · 백엔드 함께 돌리기는 하지 않는다.
- 단계마다 프로세스 · 체인 순서 섞기 · 정지 시한에 걸린 자식 다시 띄우기는 하지 않는다. 시나리오 산출물 `{out}` · 체인 보고는 실행 뿌리로 옮기지 않는다(도구가 시나리오 이름으로 찾는다).
- 자동 플레이는 플레이어와 같은 입력 · 서비스를 쓴다(치트 경로 금지).
- 개발 그림(디버그 드로우 · HUD)은 스크린샷 · `<ExpectImage>` 골든에 들지 않는다 — 캔버스 겹치기 등록은 층(`CanvasOverlayLayer`)을 밝힌다.
- 에디터 UI 는 화소 골든을 두지 않는다(폰트 · DPI 가 머신마다 다르다) — 같은 머신에서 PNG 를 읽는다.
- 감시 값 · 로그 id 를 기대하는 테스트가 있으면 그 id 를 바꾸거나 지울 때 기대도 함께 고친다(린트 `feature-references` 강제, 표 `Config/Rules/Lint/FeatureRefs.json`).
- 그림을 바꾸는 변경은 `<ExpectImagesDiffer>` 로 확인한다. 기능이 없는 백엔드는 `<SkipUnless watch="…">`.
- 측정 장치(감시 값 · 구간)를 두는 시나리오는 판정 없이 값만 기록하고, 문턱은 NEXT 측정 줄에 둔다.

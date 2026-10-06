# Config (호스트 · 개발 설정)

무엇을 어디에 두는지 · 우선순위 · 배포본은 [`docs/07_Configuration.md`](../docs/07_Configuration.md), 파일마다의 칸 표는 생성 문서 [`docs/Config/`](../docs/Config/README.md) 에 있다.
파일에는 **기본값과 다른 값만** 적는다(기본값은 생성 문서에 있다).

| 폴더 | 무엇 | 커밋 |
|---|---|---|
| `Engine/` | 엔진 기동 설정 · 메모리 예산 · 쿠킹/팩 계약 | 함 |
| `Game/` | 게임마다 프리셋 하나(`SW_ACTIVE_GAME` 이 고른다) | 함 |
| `Editor/` | 사람이 정하는 에디터 도구 값 · 임포트 규칙 — 에디터가 쓰는 상태는 `Saved/Editor/`(git 무시) | 함 |
| `Environment/` | 개발 머신 툴체인 경로 — `*.defaults.json` 시드만 커밋, 로컬 `*.json` 은 생성 · git 무시 | 시드만 |
| `Server/` | 전용 서버 운영 설정(게임마다) — 비밀은 환경 변수 이름만 | 함 |

명칭 주의: `EngineConfig.json`(런타임 엔진 호스트) ≠ `Environment/toolchain_config.json`(개발 PC 컴파일러 · SDK 경로).

## 함정 · 계약

- **설정 표의 열쇠는 타입이다**(`ensureConfig<T>( path, generated )`). Shipping 은 디스크의 `Config/` 를 보지 않는다. 고정 스텝 상한은 `EngineConfig::_fixedDeltaTime` · `_maxFixedStepPerFrame`
  (넘친 잔액은 버린다). `ModuleFrameState` 래치 지점이 둘인 것은 의도다(옮기면 에디터 Step 한 칸이 틱 없이 소비된다).
  틀린 설정 파일(모르는 키 · 대소문자만 다른 키 · `Min`/`Max` 밖)은 키 이름과 함께 오류이고 기동을 멈춘다 — 없는 파일만 생성 JSON → C++ 기본값이다.
  `EngineConfig.json` 도 configure 의존이다(Shipping 에 굽는 값). 설정 파일에는 기본값과 다른 값만 적는다(언리얼 `Default*.ini` 와 같다) —
  `ConfigFileSchemaTest` 가 기본값을 다시 적은 키를 막는다(`ConfigManager::collectDefaultEchoKeys`, 앱이 통째로 쓰는 상태는 제외).

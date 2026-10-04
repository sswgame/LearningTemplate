# Telemetry — 텔레메트리

플레이 데이터(진행 · 성능 · 세션)를 사건으로 모아 디스크에 묶어 두고, **동의가 있을 때만** 올립니다. 참고: 언리얼 `FAnalytics` · `IAnalyticsProvider`
(공급자 교체 · 세션 · 사건 속성), Unity Analytics(사건 스키마 · 세션 표본 · 오프라인 큐).

| 파일 | 하는 일 |
|------|---------|
| `TelemetrySchema` | `*.telemetry.xml` — 사건 id · 분류 · 필드(이름 · 타입 · 필수) · 표본 비율, 파이프라인 설정. 모르는 이름은 로드 오류(파일째 거절) |
| `TelemetryEvent` | 사건 하나(id + 타입 붙은 필드 값) |
| `TelemetryService` | 엔진 서비스 — 동의 · 스키마 대조 · 표본 · 묶음 · 스풀(JSON lines) · 회전 · 상한 · 올리기 · 장면별 프레임 시간 요약 · 빵부스러기 |
| `TelemetryUploader` | `ITelemetryUploader` · `NullTelemetryUploader`(기본 — 보내지 않음) · `HttpTelemetryUploader`(`IHttpClient` 로 POST) |
| `CrashReportService` | 크래시 보고 — 다음 실행이 지난 크래시를 묶음 폴더로, 동의(local · ask · send)에 따라 두거나 묻거나 보고 프로세스로 올린다 |
| `CrashReportUploader` | `ICrashReportUploader` · `NullCrashReportUploader`(기본) · `HttpCrashReportUploader`(multipart, `upload_file_minidump`) |
| `HttpClient` | `IHttpClient` · `HttpRequest` · `HttpResponse` · `NullHttpClient`(기본 — 보내지 않고 거절). 이 저장소는 실제 네트워크 클라이언트를 싣지 않는다 |

## 동의 — 기본은 아무것도 하지 않는다

- 사용자 설정 `telemetry.enabled`(카테고리 `privacy`, `engine.settings.xml`, **기본 false**)의 **확정 값**이 동의입니다(`bindConsentSetting` — 적용 · 되돌리기 · 로드
  통보에서 다시 읽는다. 메뉴에서 보류 중인 값은 동의가 아니다).
- 동의가 없으면 `record` 는 `NoConsent` 로 버리고, 프레임 시간도 모으지 않고, 파일을 쓰지도 올리지도 않습니다.
- 동의를 거두면(그리고 동의 없이 시작할 때마다) 쓰지 않은 묶음과 스풀 파일을 **지웁니다** — 지난 실행이 남긴 것까지.
- 동의해도 기본 업로더는 보내지 않습니다(`Kept` — 파일은 상한 안에서 남는다). 실제로 보내려면 게임이 `setUploader( HttpTelemetryUploader( 자기 IHttpClient, 끝점, 키 ) )`.
- 빵부스러기(최근 사건 `breadcrumbs` 개)는 동의와 상관없이 **메모리에만** 둡니다 — 크래시 보고가 덤프 옆에 적고, 그 보고를 보내는 것은 크래시 보고 동의가 정합니다.

## 데이터

```xml
<TelemetrySchema version="1">
  <Pipeline batchEvents="32" flushSeconds="30" maxFileBytes="262144" maxFiles="16" maxTotalBytes="2097152" sessionSample="1" breadcrumbs="32"/>
  <Event id="progression.waveReached" category="progression" sample="1">
    <Field name="wave" type="int" required="true"/>
    <Field name="kills" type="int"/>
  </Event>
</TelemetrySchema>
```

- 엔진 스키마 `Resource/engine/telemetry/engine.telemetry.xml`(`EngineDefaultAssets::_telemetrySchema` — `session.start` · `session.end` · `perf.sceneSummary`),
  게임 스키마는 게임 프리셋 `_telemetrySchema`(팩 상대, `CheckGamePresets` 가 파일을 확인). 예: `Resource/game/shooter3d/data/shooter3d.telemetry.xml`.
- 필드 타입 `bool` · `int` · `float` · `string`. 실수 칸은 정수를 받는다. 필드 이름 `type` · `event` · `seq` · `t` · `sample` · `session` 은 예약.
- `ResourceDataSchemaTest` 가 `Resource/` 아래 모든 `*.telemetry.xml` 을 읽습니다.

## 스풀 — JSON lines

폴더는 사용자 설정 파일 옆의 `telemetry/`(`%LOCALAPPDATA%/SWEngine/<팩>/telemetry`), 자동화는 `-gv_telemetryFolder=<경로>`. 파일은
`telemetry_<세션>_<번호 4 자리>.jsonl` 이고 첫 줄이 문맥, 그 뒤가 사건입니다.

```
{"type":"context","schema":1,"session":"9c1f...","build":"Debug","platform":"Windows","buildId":"6B1E...1","game":"shooter3d","file":0}
{"type":"event","event":"perf.sceneSummary","seq":3,"t":61.233,"sample":1,"fields":{"scene":"game/shooter3d/maps/arena.scene.xml","frames":3600,"p50Ms":16.65,"p99Ms":21.35,...}}
```

- `t` 는 세션 시작 뒤 초(게임 스레드의 `update` 시계), `seq` 는 세션 안 순번, `sample` 은 그 사건의 표본 비율(받는 쪽이 1 / 비율로 무게를 준다).
- **묶음**: `batchEvents` 개가 모이거나 `flushSeconds` 가 지나면 지금 파일에 덧붙인다. **회전**: 줄을 더하면 `maxFileBytes` 를 넘을 때 파일을 닫고 다음 번호로.
  **상한**: 스풀 전체가 `maxFiles` · `maxTotalBytes` 를 넘으면 가장 오래된 닫힌 파일을 보내지 못한 채 지운다(`_droppedFiles`). **올리기**: 닫힌 파일을 오래된 순으로
  업로더에 넘기고 `Sent` 면 지운다. 실패하면 순서를 지켜 다음 flush 에. 종료 때 지금 파일도 닫아 올린다. 지난 실행이 남긴 파일은 다음 실행이 이어 받는다.
- **표본**은 결정적이다 — 세션은 세션 id 해시(`sessionSample`), 사건은 (세션 · 사건 · 그 사건의 번호) 해시. 같은 세션은 같은 사건을 고른다.
- **장면 요약**: `EngineLoop::tick` 이 `recordFrame( 장면 경로, dt )` 를 부른다. 장면이 바뀌거나 세션이 끝나면 `perf.sceneSummary`(프레임 수 · 시간 · 평균 · p50 · p99 ·
  최대). 히스토그램은 0.1 ms 칸 2500 개(250 ms 넘침 칸) — 백분위 오차 0.05 ms 안.
- `record` 는 아무 스레드에서나 부른다(잠금 하나 — 컴포넌트 틱 워커에서도 된다). 올리기는 flush 를 부른 스레드에서 막고 돈다 — 실제 HTTP 창구를 끼우면
  비동기 창구(작업 스레드에 넘기고 곧 돌아오는)로 구현해야 프레임이 서지 않는다.

## 게임이 부르는 것

```cpp
TelemetryService* pTelemetry = game::getService<TelemetryService>();
TelemetryEvent event( "progression.waveReached" );
event.setInt( "wave", wave ).setInt( "kills", kills ).setFloat( "seconds", seconds );
(void)pTelemetry->record( event );   // Recorded · NoConsent · SampledOut · UnknownEvent · InvalidField
```

Shooter3D 는 새 웨이브(`progression.waveReached`)와 쓰러짐(`progression.roundEnded` — 웨이브 · 처치 · 시간 · 명중률)을 남긴다.

## 크래시 보고

크래시 순간에는 아무것도 보내지 않는다 — 죽어 가는 프로세스는 할당 없이 파일만 쓴다(`Core/Process/CrashHandler`: `crash_<세션>.dmp` · `.txt`(컨텍스트) ·
`.stack.txt` · `.breadcrumbs.txt`, 로그 폴더 `Saved/Logs`). **다음 실행**의 기동 단계 `Telemetry` 가 `CrashReportService::collectNewCrashes` 로 지금 세션이 아닌
크래시를 `Saved/CrashReports/crash_<세션>/` 로 묶는다.

| 묶음 안 | 무엇 |
|---------|------|
| `crash.dmp` | 미니덤프(Windows) — 원래 파일을 옮긴다 |
| `crash.txt` | 컨텍스트 — 사유 · 주소 · 프로세스/스레드 · Build · Platform · RHI · GPU · `BuildId`(실행 파일) · `EngineBuildId`(Dev 의 Engine.dll) |
| `crash.stack.txt` | 심볼 변환한 콜 스택 |
| `crash.breadcrumbs.txt` | 최근 사건 32 줄(텔레메트리 사건마다 `CrashHandler::addBreadcrumb` — 동의와 상관없이 기계 안에만) |
| `last.log` | 그 세션의 로그 파일들을 이어 끝 512 KB(복사 — 로그 폴더의 것은 그대로) |
| `manifest.json` | 세션 · 상태 · 시도 수 · 사유 · 빌드 id · 백엔드 · GPU · 시스템(논리 코어 · 메모리 — 묶는 실행이 읽는다) · 컨텍스트 전부 · 파일 목록 |

- **동의** — 사용자 설정 `telemetry.crashReports`(기본 `local`): `local` 은 묶기만, `ask` 는 `AwaitingDecision` 으로 두고 게임 UI 가 `collectReports` 로 묻고
  `decide( 세션, 보냄? )`, `send` 는 `Queued`. `local` 로 바꾸면 아직 보내지 않은 묶음(기다림 · 보낼 줄)이 `local` 로 돌아온다(철회). `send` 로 바꿔도 이미
  `local` 로 모인 묶음은 소급해 보내지 않는다. 묶음은 10 개까지(오래된 것부터 지운다).
- **보고 프로세스** — 보낼 줄이 있으면 `App.exe --crash-reporter="<묶음 폴더>"` 를 기다리지 않고 띄운다(`Process::launchDetached`). `main` 이 엔진 · 게임 모듈을
  세우기 **전에** 이 인자를 보고 `runReporter` 만 돌고 끝낸다 — 크래시 난 게임 코드를 다시 올리지 않는다(언리얼 CrashReportClient · Crashpad 핸들러와 같은 자리,
  여기서는 별도 exe 대신 같은 exe 의 다른 모드). 올리기 결과가 `Sent` 면 상태 `sent` 와 덤프 삭제, 아니면 시도 수 + 1, 3 번이면 더 띄우지 않는다.
  띄울 실행 파일은 `setReporterExecutable` 로 정한 것뿐이다(EngineLoop 이 App 경로를 넣는다 — 시험 실행 파일은 자기를 다시 띄우지 않는다).
- **업로더** — `HttpCrashReportUploader` 는 `multipart/form-data`: `manifest`(JSON) + 파일마다 한 부분, 덤프는 `upload_file_minidump`(Breakpad · Crashpad ·
  Sentry 미니덤프 끝점이 받는 이름). 이 저장소의 보고 프로세스는 `NullCrashReportUploader`(보내지 않음)를 쓴다 — 실제로 올리려면 게임이 `IHttpClient` 와
  끝점을 넣어 `runReporterFromCommandLine` 자리의 업로더를 바꾼다.

### 심볼 · 빌드 id 짝짓기

- 덤프만으로는 함수 이름이 없다. 덤프의 모듈 목록은 모듈마다 빌드 id(Windows: PE CodeView `RSDS` 의 GUID + age, PDB 이름)를 담고, 심볼 서버는 그 열쇠로
  PDB 를 찾는다 — `symstore` 배치 `<pdb 이름>/<GUID 32 자리><age 16진>/<pdb 이름>`. `ModuleBuildId::find` 가 같은 열쇠를 만들어 컨텍스트 `BuildId` 에 적으므로,
  묶음의 `buildId` 로 "어느 빌드의 PDB 가 필요한가" 를 덤프를 열지 않고 안다.
- 배포할 때 할 일: 빌드마다 `Bin/*.pdb` 를 심볼 저장소에 넣는다(`symstore add /r /f build\Ninja-Shipping\Bin\*.pdb /s <저장소> /t SWEngine /v <버전>` 또는
  Sentry `sentry-cli debug-files upload`). 배포물에는 PDB 를 싣지 않는다. Dev 는 모듈(DLL)마다 PDB 가 따로라 `EngineBuildId` 도 적는다.
- 리눅스: ELF `NT_GNU_BUILD_ID` 노트(16진). 링커가 노트를 적어야 한다(`-Wl,--build-id` — 배포판 clang 은 기본으로 켠다, 없으면 `buildId` 가 빈다).
  디버그 정보는 `objcopy --only-keep-debug` 로 떼어 `.build-id/<앞 2 자리>/<나머지>.debug` 배치로 저장한다(gdb · Sentry 가 이 배치를 읽는다).

## 상용 엔진과 견주면

- 언리얼 Analytics: 공급자 인터페이스 · 세션 · 사건 속성 — 같은 자리(`ITelemetryUploader`). 언리얼은 속성이 자유 형식이고 여기는 스키마 대조가 있다(받는 쪽 테이블과
  어긋난 줄을 쓰지 않는다 — Unity 쪽). 언리얼의 내장 공급자(ET · 파일 · Flurry 등)에 해당하는 실제 HTTP 백엔드는 없다(`IHttpClient` 를 게임이 구현).
- Unity Analytics: 오프라인 큐 · 동의 게이트 · 세션 표본 · 사건 스키마 — 있다. 대시보드 · 퍼널 · 서버 쪽 처리는 범위 밖.
- 없는 것: 압축(gzip) 업로드, 재시도 백오프, 사건 단위 사용자 id(세션 id 뿐 — 개인 식별자를 일부러 싣지 않는다), 데이터 삭제 요청 API(GDPR — 서버 몫).
- 언리얼 CrashReportClient: 별도 프로세스 · 덤프 + 로그 + 컨텍스트 묶음 · "보낼까요?" — 같은 틀(여기는 같은 exe 의 보고 모드, 창은 게임 UI 몫). CRC 는 크래시 직후
  띄우고 여기는 다음 실행이 띄운다(죽어 가는 프로세스에서 아무것도 띄우지 않는다). Sentry · Backtrace: 미니덤프 multipart 업로드 · 빵부스러기 · 빌드 id
  심볼 매칭 — 업로드 모양과 열쇠는 같다. 서버 쪽 심볼 변환 · 묶어 보기(같은 크래시 묶기) · 덤프 압축 · 크래시 중 스크린샷은 없다.

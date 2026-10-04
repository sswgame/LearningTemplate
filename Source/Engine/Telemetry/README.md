# Telemetry — 텔레메트리

플레이 데이터(진행 · 성능 · 세션)를 사건으로 모아 디스크에 묶어 두고, **동의가 있을 때만** 올립니다. 참고: 언리얼 `FAnalytics` · `IAnalyticsProvider`
(공급자 교체 · 세션 · 사건 속성), Unity Analytics(사건 스키마 · 세션 표본 · 오프라인 큐).

| 파일 | 하는 일 |
|------|---------|
| `TelemetrySchema` | `*.telemetry.xml` — 사건 id · 분류 · 필드(이름 · 타입 · 필수) · 표본 비율, 파이프라인 설정. 모르는 이름은 로드 오류(파일째 거절) |
| `TelemetryEvent` | 사건 하나(id + 타입 붙은 필드 값) |
| `TelemetryService` | 엔진 서비스 — 동의 · 스키마 대조 · 표본 · 묶음 · 스풀(JSON lines) · 회전 · 상한 · 올리기 · 장면별 프레임 시간 요약 · 빵부스러기 |
| `TelemetryUploader` | `ITelemetryUploader` · `NullTelemetryUploader`(기본 — 보내지 않음) · `HttpTelemetryUploader`(`IHttpClient` 로 POST) |
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

## 상용 엔진과 견주면

- 언리얼 Analytics: 공급자 인터페이스 · 세션 · 사건 속성 — 같은 자리(`ITelemetryUploader`). 언리얼은 속성이 자유 형식이고 여기는 스키마 대조가 있다(받는 쪽 테이블과
  어긋난 줄을 쓰지 않는다 — Unity 쪽). 언리얼의 내장 공급자(ET · 파일 · Flurry 등)에 해당하는 실제 HTTP 백엔드는 없다(`IHttpClient` 를 게임이 구현).
- Unity Analytics: 오프라인 큐 · 동의 게이트 · 세션 표본 · 사건 스키마 — 있다. 대시보드 · 퍼널 · 서버 쪽 처리는 범위 밖.
- 없는 것: 압축(gzip) 업로드, 재시도 백오프, 사건 단위 사용자 id(세션 id 뿐 — 개인 식별자를 일부러 싣지 않는다), 데이터 삭제 요청 API(GDPR — 서버 몫).

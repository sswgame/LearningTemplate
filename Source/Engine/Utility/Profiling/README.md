# Profiling — 엔진 프로파일러의 두 번째 출력(Tracy)

계측 지점은 하나입니다. `SW_PROFILE_SCOPE( "GT.Scene.tick" )` 한 줄이 두 곳에 남습니다.

| 출력 | 무엇을 하는가 | 어디서 보는가 |
|---|---|---|
| `Debug/FrameProfiler` | 프로세스 **안**에서 구간을 프레임마다 접어 avg · p50 · p99 · 최대를 낸다 | `-gv_profileFrames` 표 · 성능 회귀(`Scripts/qa/PerfRegression.py`) · 에디터 프로파일러 패널 · Shipping 오버레이 |
| `Profiling/` (이 폴더) | 같은 구간을 **시간축**으로 외부 뷰어에 보낸다 — 스레드별 타임라인 · GPU 큐 · 프레임 · 그래프 · 메모리 | Tracy 뷰어(언리얼 Insights · 유니티 Profiler 의 타임라인 자리) |

## 구조

| 파일 | 역할 |
|---|---|
| `IProfilerBackend.h` | 출력 하나의 계약 — CPU 구간 · 프레임 표시 · 그래프 · 할당 · GPU 컨텍스트/구간. 라이브러리 타입이 없다 |
| `ProfilerBackend.h/.cpp` | 활성 출력 하나(정적), 계측 지점 저장소(프로세스 수명 사본), `-gv_tracy` · `-gv_tracyMemory` 해석, Core 할당 관찰자 연결 |
| `Tracy/TracyProfilerBackend.h/.cpp` | Tracy C API 호출부 — **저장소에서 Tracy 헤더를 include 하는 유일한 곳** (`CheckThirdPartyIsolation.py`) |

- **교체**: 다른 뷰어를 붙이려면 `IProfilerBackend` 구현 하나를 더하고 `ProfilerBackend` 가 고르게 합니다. 호출부(`SW_PROFILE_SCOPE`)는 그대로입니다.
- **Tracy 없는 빌드**: `SW_ENABLE_TRACY=OFF`(리눅스 기본) 이면 Tracy 를 링크하지 않고 `TracyProfilerBackend` 는 빈 함수입니다. 출력 자리는 있지만 비어 있습니다.
- **Shipping**: `SW_PROFILER_BACKEND_COMPILED` 가 0 — 매크로가 지점을 등록하지도 출력을 묻지도 않고, Tracy 는 링크하지 않습니다(TracyClient.dll 도 배포물에 없다).
- **클라이언트는 프로세스에 하나**: Engine.dll 만 Tracy 를 링크합니다. 모듈(게임 · 에디터 · GF 킷 · RHI 백엔드)은 `SW_PROFILE_SCOPE` → Engine 의 내보낸 함수로 들어오므로
  Tracy 를 모릅니다.

## 켜는 법 — 기본 꺼짐, 켜면 기동부터 기록

```powershell
build/Ninja-Release/Bin/App.exe -gv_tracy=1                    # 기동부터 Tracy 로 보낸다
build/Ninja-Release/Bin/App.exe -gv_tracy=1 -gv_tracyMemory=1  # + 할당 · 해제(메모리 태그별 풀) — 느리다
```

**왜 on-demand(뷰어가 붙을 때만 수집)가 아니라 "기본 꺼짐 + 인자로 켜기" 인가**

- Windows 는 TracyClient.dll 을 **지연 로드**합니다. 끈 채로는 DLL 이 올라오지 않아 수집 스레드 · 리슨 소켓 · 방화벽 창이 없습니다 — 개발 빌드를 돌리는 모든
  실행(시험 · 쿠킹 · 스모크)이 값을 내지 않습니다. on-demand 는 DLL 을 늘 올려 두고 스레드를 세운 채 기다리는 방식이라 이 이득이 없습니다.
- vcpkg 포트는 기능을 끄고(on-demand 아님) 빌드되어 있고, on-demand 는 클라이언트 라이브러리의 컴파일 정의라 바꾸려면 포트를 다시 지어야 합니다.
- 켜면 **기동부터** 기록합니다(뷰어가 나중에 붙어도 처음부터 받는다). 기동 히치를 보려면 이것이 필요합니다. 대신 뷰어 없이 오래 돌리면 Tracy 가 메모리에 쌓습니다.
- 한 번 켜면 프로세스 끝까지 켜져 있습니다(중간에 끊으면 뷰어 쪽 타임라인이 깨진다). 에디터 프로파일러 패널의 "Tracy 열기" 는 실행 중에 켜고 뷰어를 띄웁니다.

켜면 엔진 `FrameProfiler` 도 함께 켜집니다 — 카운터(`SW_PROFILE_COUNT`)가 프레임 합으로 Tracy 그래프에 나가는 길이 그 표입니다.

## 뷰어(Tracy 서버) — 같은 판 0.13.1 을 받아 쓴다

뷰어 바이너리는 저장소에 넣지 않습니다. **클라이언트와 같은 판(0.13.1)** 의 Windows 릴리스를 받아 씁니다(판이 다르면 프로토콜이 맞지 않아 붙지 않는다).

1. <https://github.com/wolfpld/tracy/releases/tag/v0.13.1> 에서 `windows-0.13.1.zip` 을 받아 풀고 `tracy-profiler.exe` 를 둡니다. 에디터 버튼이 찾는 자리는
   `Tools/Tracy/tracy-profiler.exe`(저장소 밖으로 무시됨) 또는 `-gv_tracyViewerPath=<경로>` 입니다.
2. 앱을 `-gv_tracy=1` 로 띄웁니다. 로그에 `Tracy profiler started (data port 8086 …)` 가 나옵니다.
3. 뷰어에서 `Connect`(주소 `127.0.0.1`) 를 누르거나 `tracy-profiler.exe -a 127.0.0.1` 로 띄웁니다. 포트는 8086 이고, 다른 프로세스가 쓰고 있으면 Tracy 가 다음 번호
   (최대 20 개)를 고릅니다 — 둘을 띄우면 뷰어의 발견 목록(같은 망 브로드캐스트)에서 고릅니다. `TRACY_PORT` 환경 변수로 고정할 수 있습니다.

## 무엇이 나가는가

| Tracy | 엔진 쪽 |
|---|---|
| CPU 구간 | 모든 `SW_PROFILE_SCOPE`(이름 · 함수 · `Source/` 부터의 파일 · 줄) |
| 스레드 이름 | OS 이름(`ThreadName` — `GameThread` · `RenderThread` · `Worker N` · `IO` · `IO.Iocp` · `Logger` · `Net`). Tracy 가 OS 에서 읽는다 |
| 프레임 | 주 프레임 = 게임 스레드(`EngineLoop::endFrame`), 보조 프레임 `Render` = 렌더 스레드 Present 뒤 |
| 그래프 | `SW_PROFILE_COUNT` 의 프레임 합(드로우 수 · `Mem.LiveKB` · 에셋 바이트 …) |
| 메모리 | `-gv_tracyMemory=1` 일 때 `Memory::allocate`/`free` 를 메모리 태그 이름의 풀로. Tracy 를 켜기 전에 잡힌 블록의 해제는 보내지 않는다(짝 없는 해제는 뷰어가 기록을 멈춘다) |

## 함정

- **지점 문자열은 프로세스 수명 사본이다.** Tracy 는 소스 위치를 포인터로 보내고 문자열은 뷰어가 물을 때 읽습니다. 모듈 상수(핫 리로드로 사라짐)나 이름 풀
  (`HashedStringPool`, 엔진 종료 때 내림)을 가리키면 그때 사라진 메모리를 읽습니다. `ProfilerBackend::registerZoneSite` · `internName` 이 정적 저장소에 복사합니다
  (지점 1024 개 · 64 KB — 넘치면 그 지점만 Tracy 에 안 나간다).
- **구간은 연 출력으로 닫는다.** 프레임 도중에 출력이 켜져도 짝이 어긋나지 않습니다(`ScopedFrameProfile` 이 연 출력을 든다).
- Tracy 를 켜면 Windows 방화벽이 리슨 소켓을 처음 한 번 묻습니다. 같은 PC 의 뷰어는 거절해도 붙습니다(localhost).

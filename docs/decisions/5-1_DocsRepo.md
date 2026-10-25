# 5-1. 문서와 저장소

> 질문: 5-1. 문서와 저장소 영역에서 이미 정한 것은 무엇이고 근거는 무엇인가?
> 독자: 같은 제안을 다시 하려는 사람, 정한 이유를 찾는 사람.
> 결론: 결정 기록 5 절의 한 영역이다. 번호 체계와 쓰는 법은 [결정 기록](../09_Decisions.md) 5절에 있다.

- **약어는 대문자로 쓴다**(사용자 결정 2026-10-10 — 이미 대문자인 `AABB` · `RHI` · `TagID` 와 철자가 하나가 되고, 함수 이름에서도 약어가 보인다). 목록은 `Scripts/lint/rules/AcronymRegistry.toml` 한 자리(읽기 · 철자 판정은 `Scripts/common/AcronymRegistry.py`), 대문자 약어가 이어 붙으면 낱말을 사이에 두거나 뜻이 같은 쪽을 뺀다(`RTSCommanderAI` · `HTTPAddress` · `kRTSCatalogXML` · `RHITimestamp` · `OpenSSLContext`). `SSL` 은 제품 이름표(`OpenSSL`)로 둔다 — 약어로 올리면 `_pSsl` 같은 OpenSSL 핸들 이름까지 바뀌고 이어 붙음은 이름 하나뿐이었다. 파이썬 · CMake 식별자는 대상이 아니다.
- **클래스 접미어는 하는 일로 고릅니다**(사용자 결정 2026-10-13 — 상용 엔진이 같은 접미어를 같은 뜻으로 쓴다). `…Manager` 는 런타임 오브젝트 집합의 수명과 생성 · 파괴 · 로드 정책을 소유하고(유니티 SceneManager, 언리얼 AssetManager),
  `…Registry` 는 등록으로 채우는 이름 · ID 조회 테이블이며 수명 정책이 없습니다(언리얼 AssetRegistry). `…System` 은 매 프레임 많은 오브젝트 · 컴포넌트에 같은 로직을 돌리고(유니티 DOTS System),
  `…Service` 는 서비스 바인딩이나 원격 엔드포인트로 닿는 교체 가능한 기능 창구(PlayFab · EOS · Nakama), `…Backend` 는 인터페이스의 플랫폼 · 라이브러리 구현(`JoltPhysicsBackend`, `XAudio2AudioBackend`),
  `…API` 는 C 함수 테이블(`GameAPI` · `EditorAPI` · `ModuleAPI`)입니다. 함께 정한 규칙: `Anim` 은 `Animation` 으로 풀어 씁니다(Anim 접두 식별자는 개명 중이며 새로 만들지 않고, 언리얼 `AnimInstance` 같은 외부 용어만 예외).
  불린 변수 · 멤버의 `b` 접두(`bVisible`)는 언리얼 관습으로 허용하고 불린을 돌려주는 함수만 is · has · can · should 규칙을 따릅니다.
  `Scene` 은 씬 하나의 데이터를 소유하고(언리얼 Level/World · 유니티 Scene), `GameObjectManager` 는 그 씬 오브젝트의 수명을, `GameObjectStore` 는 저장소를 맡습니다. 규칙 원문은 [docs/agents/NamingCpp.md](../agents/NamingCpp.md) "Class suffixes" 절입니다.
- **같은 기준으로 바꾼 이름**(2026-10-13): 온라인 호스트 쪽 인터페이스는 클라이언트 쪽과 짝을 맞춰 `IOnlineHostService` · `registerHostService`. 키트가 나눠 쓰는 공유 상태는 언리얼 GameState(월드 단위 복제 상태)와 뜻이 달라 `SessionStateComponent` · `SessionStateRefs`.
- **저장소에 LICENSE 파일을 두지 않습니다**(2026-10-06). 라이선스를 정하는 것은 사용자의 일이라, README 의 MIT 배지와 develop 배지도 뺐습니다.
- **코딩 규칙의 원본은 `AGENTS.md` 와 `docs/agents/` 입니다**(2026-10-06). [04 코딩 규칙 예시](../04_CodingGuidelines.md)는 규칙을 다시 적지 않는 예시 모음입니다(언리얼도 규칙 문서와 예시를 따로 둔다).
  `CLAUDE.md` 는 에이전트가 매번 읽는 요약이고, 원본이 따로 있는 규칙은 한 줄과 링크로만 가리킵니다.
- **모듈 폴더마다 README 를 강제하지 않습니다**(2026-10-06). 있는 README 는 모두 [문서 지도](../02_DocumentMap.md)에 있어야 하고, `CheckDocPaths` 가 확인합니다.
- **문서만 바뀐 커밋에서도 CI 린트 잡은 돌고, 빌드 잡만 건너뜁니다**(2026-10-06). 바뀐 파일은 git 으로 직접 판정하고, 기준 커밋을 얻지 못하면 안전하게 빌드합니다.
- **CI 린트와 빌드는 동시에 시작하고, 린트가 지면 main 이 아닌 실행은 빌드를 취소합니다**(2026-10-25). 빌드 열 개가 1 분짜리 린트를 기다리지 않게 `code` 판정을 가벼운 `changes` 잡으로 옮겼고, 취소는 린트 잡 끝의 `gh run cancel`(`actions: write`)입니다. main 은 취소하지 않습니다 — 돌던 실행을 끝까지 돌리는 규칙이고 취소된 실행은 세션 가드가 실패로 세지 않습니다. `ctest -j` 는 4 고정 대신 러너 코어 수입니다. [측정: 없음 · 설계 변경, 단축 폭은 다음 CI 실행으로 확인]
- **백로그는 할 일 목록만 둡니다**(2026-10-07). 끝낸 일의 교훈은 그 코드의 헤더 주석에, 결정과 기각한 안은 이 문서에, 검증 방법은 [검증과 측정](../08_Verification.md)에 둡니다.
- **게임 리소스는 상업적 사용 가능, 출처 표기 의무 없음, 재배포 허용(CC0 · 퍼블릭 도메인 급)만 넣습니다**(사용자 결정). 저장소가 GitHub 에 공개되기 때문입니다.
  CC BY 처럼 표기 의무가 있거나 재배포를 막는 리소스는 내려받기 스크립트로 저장소 밖에 두는 방식으로도 쓰지 않습니다. 후보는 라이선스 원문으로 확인하고, `credits.md` 에는 감사 표기만 합니다.
  쓸 수 있는 예는 Kenney, KayKit, VRoid 의 CC0 샘플입니다. Quaternius 는 쓰지 않습니다(5-12). 게임별 CC0 리소스 배치와 MechArena 는 "안정화된 뒤" 로 보류했습니다([후보 문서](../plans/Candidates-project.md) `H-1`).
- **문서는 사람이 읽는 순서로, 한국 개발자가 쓰는 말로 씁니다**(2026-10-07, 사용자 결정). 모듈 README 는 무엇이고 왜 있나, 머릿속 그림, 따라 해 보기, 작동 원리, 확장하는 법, 함정과 주의, 더 볼 곳 순서이고 폴더 트리와 클래스 표는 쓰지 않습니다.
  업계 용어(버전, 빌드, 베이크, 테이블, 슬롯, 레지스트리, 엔드포인트)를 그대로 쓰고 순우리말 조어를 만들지 않습니다. 전문은 [문서 쓰기 지침](../10_WritingDocs.md)이고 코드 주석과 커밋 메시지에도 같은 기준을 씁니다.
- **다음 이름 점검의 결론**(2026-10-24, 접미어 규칙은 위 "클래스 접미어" 항목). `Context` 타입 약 50 개는 호출 · 패스 · 프레임 하나 동안 넘기는 환경이거나 그래픽스 API 용어라 두고, 매니저가 들고 있는 상태만 `Record` 로 바꿨습니다(`LiveReloadManager::ModuleRecord`). 같은 이름의 타입이 따로 있는 `*Util` 일곱(`EditorViewTargetUtil` · `FrameRendererUtil` · `InspectorBuiltinValueUtil` · `MeshModifierUtil` · `ReflectUnitUtil` · `SceneNavigationUtil` · `ServiceRecordUtil`)은 그 타입의 인스턴스를 받는 함수가 아니라 기능에 딸린 상태 없는 도우미라 메서드로 옮기지 않습니다(옮기면 헤더 의존만 늘어납니다). `Unit` 의 세 뜻(게임 플레이 유닛 · `AnimationSystem` 의 스켈레탈 메시 컴포넌트 · 측정 단위)은 같은 파일에서 섞이지 않아 이름을 두고 `NamingCpp.md` 에 적었습니다. 단독 `value` 는 약 650 곳 중 이름이 곧 뜻인 곳(비트 · 해시 도우미 인자, `Wrapper<T>::_value`, `key=value` 의 오른쪽)은 두고 뜻이 다른 이름으로 드러나는 열세 곳만 바꿨습니다.

- **작업자 보고에는 "같은 결함 후보" 절을 필수로 둔다**(사용자 결정 2026-10-25 — 위치 · 모양 · 확인 수준 · 네 칸 분류 · 읽지 않은 범위). 고칠 수 있는 같은 모양은 작업자가 같은 브랜치에서 고치고, 큰 것은 통합 담당이 `NEXT.md` 로 모았다가 `Candidates-<영역>.md` 로 내린다. 보고 끝에 흘려 쓴 "다른 곳에도 있을 수 있다"는 인계되지 않아 사라졌다. 틀은 `plans/WorkerRules.md` 5절.

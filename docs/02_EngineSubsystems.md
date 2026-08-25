# 🧩 Engine Subsystems (서브시스템 개요)

SW Engine은 철저하게 모듈화되어 있으며, 각 기능은 서브시스템(Subsystem)으로 나뉘어 관리됩니다.
이 페이지는 엔진의 핵심 구성 요소들을 한눈에 파악할 수 있는 허브(Hub) 역할을 합니다.

## 핵심 시스템 목록

각 서브시스템에 대한 자세한 설계와 사용법은 아래 링크된 개별 `README.md`를 참고해 주세요.

### 1. 렌더링 및 그래픽스 (Graphics & RHI)
- **[RHI (Render Hardware Interface)](../Source/Engine/Graphics/README.md)**
  - DirectX 11/12, Vulkan 등을 추상화하는 하위 레벨 그래픽스 API입니다.
  - Bindless 텍스처 접근과 Compute 중심의 모던 렌더링을 지원합니다.
  - **문서 이동:** `Source/Engine/Graphics/README.md`

### 2. 엔티티 컴포넌트 시스템 (ECS)
- **[Object & ECS](../Source/Engine/Object/README.md)**
  - 게임 월드를 구성하는 기본 단위인 `GameObject`와 그에 부착되는 `Component`들의 관리 시스템입니다.
  - 데이터 지향 설계(SoA 기반 `ArchetypeChunkPool`)와 씬(Scene) 계층 구조를 다룹니다.
  - **문서 이동:** `Source/Engine/Object/README.md`

### 3. 비동기 작업 및 스레드 (Task System)
- **[TaskManager](../Source/Engine/Utility/Task/README.md)**
  - 워커 스레드 풀과 작업 스케줄러(DAG) 시스템입니다.
  - 병렬 틱(Parallel Tick) 처리, Work-Stealing, 안전한 동기화 및 큐 비우기(`clear`)를 지원하여 엔진의 퍼포먼스를 극대화합니다.
  - **문서 이동:** `Source/Engine/Utility/Task/README.md`

### 4. 리플렉션 및 파서 (Reflection & Parser)
- **[Reflection Core](../Source/Engine/Reflection/README.md)** 
  - C++의 한계를 넘어 런타임에 클래스의 프로퍼티와 함수를 조회할 수 있게 해주는 엔진 내부 시스템입니다.
- **[ReflectionParser 툴](../Tools/ReflectionParser/README.md)**
  - 헤더에 작성된 `REFLECT()`, `PROPERTY()` 매크로를 Clang으로 읽어 메타데이터를 자동 생성하는 호스트 툴입니다.
  - **문서 이동:** `Source/Engine/Reflection/README.md` / `Tools/ReflectionParser/README.md`

---
[◀ 이전: 시작하기](01_GettingStarted.md) | [🏠 위키 홈으로 돌아가기](../README.md) | [▶ 다음: 핫리로드 및 ABI 가이드](03_LiveReload_and_ABI.md)

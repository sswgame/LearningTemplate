# RuntimeAPI (런타임 API)

엔진(Engine)과 게임/에디터(DLL) 사이를 이어주는 **순수 Header-Only (INTERFACE) 인터페이스와 핸들 모음**입니다. CMake 타겟은 하나(`RuntimeAPI`)이며, 헤더 계약만 두 층으로 나눕니다.

## 왜 필요한가요?
엔진은 핫리로드(LiveReload)를 지원하기 때문에, `App` 실행 파일이 `Editor`나 `Game` 모듈의 구체적인 C++ 클래스에 강하게 결합(정적 링크)되면 안 됩니다.
이 폴더의 API는 C-ABI 경계를 지켜주며, 엔진이 모듈 내부 클래스를 몰라도 함수 테이블로 로직을 호출할 수 있게 합니다. 이 모듈에는 **.cpp 구현체가 단 하나도 존재하지 않으며**, 완전히 추상적인 계약(Contract) 역할만 수행합니다.

## 헤더 계층

| 층 | 헤더 | 의존 | 용도 |
|----|------|------|------|
| **Stable ABI** | `RuntimeHandles.h`, `GameAPI.h` | Engine 구체 타입 없음 (opaque 핸들) | App ↔ SWGame, Shipping에서도 유지 |
| **Dev typed** | `EditorAPI.h`, `EditorUIContext.h` | Engine 그래픽/RHI forward-decl · 동일 프로세스 | App ↔ EditorModule UI 컨텍스트 |

- Game 모듈은 `GameAPI` / `RuntimeHandles`만 include 한다 (`EditorUIContext` 금지).
- `EditorUIContext`는 POD/ABI 동결이 아니라 Dev 동일 프로세스 공유 타입이다.

## 핵심 규칙
- **구현체 없음**: 뼈대(인터페이스 선언)와 타입만 둔다. 동작 코드는 `Engine`, `Editor`, `SWGame` 쪽에 구현한다.
- **ABI 버전**: `GameAPI` 또는 `EditorAPI`의 구조체 레이아웃이나 함수 시그니처를 변경할 때는 해당 `k*APIAbiVersion`을 올린다. 호스트와 모듈은 같은 버전이어야 하며, 다르면 로드를 거부한다.

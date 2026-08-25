# GameFramework (장르 공통 뼈대)

여러 게임에서 반복적으로 사용되는 장르별 공통 로직이나 키트(Kits)가 모여있는 곳입니다.

App은 이 라이브러리를 링크하지 않습니다. 게임플레이 입력은 `Input/GameActions.h`의 프로세스 전역 `gameActions()`입니다.

## 하위 폴더 구성
- **Base**: 키트 공통 수명 (`IGame`, `GameInstanceBase`). `GameFramework` 타겟에 포함. 공유 타입은 `GameFrameworkMinimal.h`
- **Input**: 공유 `ActionMap` / `GameActionIds` (`gameActions()`, `gameActionIds()`)
- **Data** · **Events** · **Save** · **UI** · **Transition**
- **Kits**: 키트끼리 링크하지 않음. 공유 타입은 Base로.
  - `ActionCombat`: 액션 게임용 데미지 판정, 콤보 시스템 등
  - `Overworld`: 오픈월드형 필드 탐색 시스템
  - `TurnBattle`: 턴제 전투 시스템
- `GameFramework` 자체는 이런 키트들이 공통으로 깔고 앉는 기반 역할을 합니다.

## 사용법
`Source/Games/내게임/CMakeLists.txt`에서 빌드 시 필요한 키트만 골라서 링크(`target_link_libraries`)하면 해당 기능들을 가져다 쓸 수 있습니다.

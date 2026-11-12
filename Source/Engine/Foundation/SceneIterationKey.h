/**
 * @file SceneIterationKey.h
 * @brief 씬 전체 순회(`GameObjectManager::forEachGameObject` · `forEachComponent` · `forEachComponentOfType` · `getAllGameObjects`)를 여는 열쇠입니다.
 * @details 컴포넌트 틱은 오브젝트 단위로 워커에 나뉘어 돈다. 틱 하나가 씬 전체를 돌면 그 일이 한 워커에 몰리고(같은 타입 N 개면 N × 씬 크기), 공유 잠금 아래에서
 *          순서 없이 돈다. 그래서 순회는 열쇠를 받고, 열쇠는 아래 주인만 만든다 — 컴포넌트 코드에는 열쇠를 얻는 길이 없어 틱에서 씬을 도는 코드는 컴파일되지 않는다
 *          (예전 린트 `tick-scene-walk` 자리). 틱이 쓸 것은 `docs/agents/TickPlacement.md` — 찾기는 `ComponentRegistry`, 이름은 `findGameObjectsByBaseName`,
 *          자리는 씬 공간 색인(`SceneSpatialGrid`), 씬 전체 집계는 프레임 시스템이다.
 *          - **틀이 인자로 넘긴다:** 감시 값(`SW_AUTOMATION_SCENE_WATCH`), 개발 명령(`SW_DEV_SCENE_COMMAND`), 디버그 HUD 섹션(`DebugHUDSectionWriter::getSceneIterationKey` — 글쓴이를 만드는 HUD 틀은 개발 도구 · 에디터 공급자나 `hud.print` 명령의 열쇠를 쓴다),
 *            저장(`StatefulComponentRegistrar` 의 캡처 · 복원 함수)이 받는다 — 그 틀만 열쇠를 만든다.
 *          - **엔진 주인:** 씬 저장 · 로드 · 쿡 · 틱 등록 · 렌더 묶기 · 넷 세션 시작(번호표) · 내비 기하 · 초상 스튜디오 — 아래 친구 목록, 줄마다 까닭.
 *          - **공용 공급자는 위층에만:** 에디터(`editor::EditorSceneIterationAccess`)와 개발 도구(`DevSceneIterationAccess`)는 누구나 열쇠를 꺼내 쓰는 공급자다. 게임 · 키트 · 런타임
 *            타깃은 그 층을 include 할 수 없다(`Config/Rules/Layers/TargetLayers.toml`).
 *          - **테스트:** `Engine/Object/GameObject/Tests/SceneIterationTestAccess.h`(배포 구성에서 include 하면 `#error`).
 *          유니티 `FindObjectsOfType` · 언리얼 `TActorIterator` 는 아무 데서나 부르지만, 우리는 병렬 틱이 기본이라 순회의 주인을 형식으로 둔다.
 */
#pragma once

namespace sw
{
    class GameObjectManager;                // 매니저 안의 순회(모듈 내림 알림 · forEachComponent)
    class AutomationWatchRegistry;          // 씬을 도는 감시 값을 부를 때 넘긴다
    class DevCommandRegistry;               // 씬을 도는 개발 명령을 부를 때 넘긴다
    class GameStateSnapshot;                // 저장 · 복원(씬 오브젝트 직렬화, 컴포넌트 상태 캡처)
    class Scene;                            // 씬 저장 · 인스턴스 · 내림
    class SceneCooker;                      // 쿡 — 모르는 타입 보고 · 미리 읽기 목록
    class StateHashHistory;                 // 상태 해시 기록(틱마다 활성 씬 해시 — 결정성 검증, 켰을 때만)
    class TickRegistry;                     // 틱 등록 다시 짓기(구조가 바뀐 프레임에 한 번)
    class SceneRHISubsystem;                // 씬 렌더 기본값 묶기(씬을 받을 때 한 번)
    class PortraitStudio;                   // 초상 스튜디오 씬 전체의 경계(찍을 때 한 번)
    class SceneNavigation;                  // 내비 메시 기하 모으기(굽기)
    class NavTileRebaker;                   // 내비 타일 다시 굽기(더러운 영역)
    struct SceneNavigationCooker;           // 내비 쿡 — 표면 컴포넌트 모으기
    struct SceneObjectReference;            // 에디터의 "이 오브젝트를 가리키는 곳" 찾기
    class DestructionNetSession;            // 파괴 넷 세션 시작 — 번호표 잡기
    class PairedAnimationReplicationServer; // 짝 애니 복제 시작 — 번호표 잡기
    class PairedAnimationReplicationClient; // 짝 애니 복제 시작 — 번호표 잡기
    struct DevSceneIterationAccess;         // 개발 도구 층의 공용 공급자(`DevTools/Diagnostics/DevSceneIterationAccess.h`)
    struct SceneIterationTestAccess;        // 테스트(`Engine/Object/GameObject/Tests/SceneIterationTestAccess.h`)
    namespace editor
    {
        struct EditorSceneIterationAccess; // 에디터 층의 공용 공급자(`Editor/Common/EditorSceneIterationAccess.h`)
    } // namespace editor

    /**
     * @class SceneIterationKey
     * @brief 씬 전체 순회의 열쇠입니다(빈 형식, 실행 비용 0). 파일 머리말의 친구만 만든다.
     */
    class SceneIterationKey
    {
        friend class GameObjectManager;
        friend class AutomationWatchRegistry;
        friend class DevCommandRegistry;
        friend class GameStateSnapshot;
        friend class Scene;
        friend class SceneCooker;
        friend class StateHashHistory;
        friend class TickRegistry;
        friend class SceneRHISubsystem;
        friend class PortraitStudio;
        friend class SceneNavigation;
        friend class NavTileRebaker;
        friend struct SceneNavigationCooker;
        friend struct SceneObjectReference;
        friend class DestructionNetSession;
        friend class PairedAnimationReplicationServer;
        friend class PairedAnimationReplicationClient;
        friend struct DevSceneIterationAccess;
        friend struct SceneIterationTestAccess;
        friend struct editor::EditorSceneIterationAccess;
        SceneIterationKey() {} // NOLINT(modernize-use-equals-default) — `= default` 면 C++17 집합체라 `{}` 가 private 를 비켜 간다
    };
} // namespace sw

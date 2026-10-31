/**
 * @file SceneEngineServices.h
 * @brief 씬 매니저 · 씬 · 오브젝트 매니저가 쓰는 엔진 서비스 묶음입니다. 만드는 쪽이 생성자나 `initialize` 로 넘기고, 받는 쪽은 한 번 들고 씁니다.
 * @details 이 묶음을 받는 클래스(`SceneManager` · `Scene` · `GameObjectManager` · `ScenePhysics` · `SceneAudio`)는 전역 조회(`engine::getSubsystem<T>()`)를 하지 않습니다.
 *          호스트(`Engine` · 테스트 하네스)가 서비스 표를 묶은 뒤 `SceneManager::initializeSubsystem` 이 그 표에서 읽고, 씬 매니저가 만드는 씬과 그 오브젝트 매니저로
 *          그대로 내려갑니다. 인자 없는 생성자는 바인딩된 표에서 **만들 때 한 번** 읽습니다(`makeFromBoundEngine`) — 테스트가 손으로 만드는 씬을 위한 것이고, 엔진 코드(쿠커 · 초상화)는 묶음을 받아 넘깁니다.
 *          비어 있는 칸은 "그 서비스가 없다" 는 뜻이고, 받는 쪽은 그 일을 건너뜁니다(전역 표를 다시 찾아가지 않습니다).
 *          `EngineServices.h` 는 엔진 · 앱 · 테스트 전용이라 여기서는 전방 선언만 씁니다 — 게임 모듈도 이 헤더를 지나 `GameObjectManager.h` 를 읽습니다.
 *          칸은 이 층(월드 · 5 층) 이하의 서비스만 둔다. 기능 층(6 — 물리 · 오디오 …)의 서비스는 그 기능의 씬 서비스가 `_pEngineSubsystems` 에서 타입으로 찾는다
 *          (`locateSubsystem<T>`) — 아래 층이 위 층 타입을 칸으로 들면 층 표(`EngineLayers.toml`)를 전방 선언으로 우회하게 된다(언리얼 `UWorld::GetSubsystem<T>` 와 같은 타입 조회).
 */
#pragma once
#include "Core/Foundation/Macros.h"

namespace sw
{
    struct EngineDefaultAssets;

    class AssetManager;
    class ISubsystemLocator;
    class TaskManager;
    class TickBatchRegistry;
    class TypeRegistry;

    /** @struct SceneEngineServices @brief 씬 쪽이 쓰는 엔진 서비스 포인터 묶음입니다(소유하지 않음). 파일 머리말 참고. */
    struct SW_API SceneEngineServices
    {
        AssetManager*              _pAssetManager{ nullptr };      ///< 머티리얼 · 프리팹 캐시와 에셋 정리
        const EngineDefaultAssets* _pDefaultAssets{ nullptr };     ///< 씬 기본 머티리얼 경로
        TaskManager*               _pTaskManager{ nullptr };       ///< 비동기 씬 로드를 띄우고 기다리는 곳
        TypeRegistry*              _pTypeRegistry{ nullptr };      ///< 씬을 읽어도 되는지(모듈 타입 등록 완료)와 타입 표 세대
        TickBatchRegistry*         _pTickBatchRegistry{ nullptr }; ///< 타입별 일괄 틱 함수 표(비면 모든 오브젝트를 칸 목록으로 돌린다)
        const ISubsystemLocator*   _pEngineSubsystems{ nullptr };  ///< 엔진 범위 — 씬 범위의 부모(씬 범위에서 엔진 서비스를 타입으로 찾는다)

        /** @brief 지금 바인딩된 서비스 표에서 만듭니다. 표가 비어 있으면 모든 칸이 nullptr 입니다. */
        static SceneEngineServices makeFromBoundEngine();
    };
} // namespace sw

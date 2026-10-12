/**
 * @file SceneEngineServices.h
 * @brief 씬 매니저 · 씬 · 오브젝트 매니저가 쓰는 엔진 서비스 묶음입니다. 만드는 쪽이 생성자나 `initialize` 로 넘기고, 받는 쪽은 한 번 들고 씁니다.
 * @details 이 묶음을 받는 클래스(`SceneManager` · `Scene` · `GameObjectManager` · `ScenePhysics` · `SceneAudio`)는 전역 조회(`engine::get*()`)를 하지 않습니다.
 *          호스트(`EngineLoop` · 테스트 하네스)가 서비스 표를 묶은 뒤 `SceneManager::initialize` 로 넘기고, 씬 매니저가 만드는 씬과 그 오브젝트 매니저로
 *          그대로 내려갑니다. 인자 없는 생성자는 바인딩된 표에서 **만들 때 한 번** 읽습니다(`makeFromBoundEngine`) — 테스트 · 쿠커가 손으로 만드는 씬을 위한 것입니다.
 *          비어 있는 칸은 "그 서비스가 없다" 는 뜻이고, 받는 쪽은 그 일을 건너뜁니다(전역 표를 다시 찾아가지 않습니다).
 *          `EngineServices.h` 는 엔진 · 앱 · 테스트 전용이라 여기서는 전방 선언만 씁니다 — 게임 모듈도 이 헤더를 지나 `GameObjectManager.h` 를 읽습니다.
 */
#pragma once
#include "Core/Common/Macros.h"

namespace sw
{
    struct EngineDefaultAssets;
    struct EngineServices;

    class AssetManager;
    class IAudioService;
    class PhysicsSystem;
    class TaskManager;
    class TypeRegistry;

    /** @struct SceneEngineServices @brief 씬 쪽이 쓰는 엔진 서비스 포인터 묶음입니다(소유하지 않음). 파일 머리말 참고. */
    struct SW_API SceneEngineServices
    {
        AssetManager*              _pAssetManager{ nullptr };  ///< 머티리얼 · 프리팹 캐시와 에셋 정리
        PhysicsSystem*             _pPhysicsSystem{ nullptr }; ///< 씬마다의 3D · 2D 물리 씬을 만드는 백엔드
        IAudioService*             _pAudioService{ nullptr };  ///< 씬 오디오 · 애니메이션 알림의 소리
        const EngineDefaultAssets* _pDefaultAssets{ nullptr }; ///< 씬 기본 머티리얼 경로
        TaskManager*               _pTaskManager{ nullptr };   ///< 비동기 씬 로드를 띄우고 기다리는 곳
        TypeRegistry*              _pTypeRegistry{ nullptr };  ///< 씬을 읽어도 되는지(모듈 타입 등록 완료)와 타입 표 세대

        /** @brief 서비스 표 @p services 에서 이 묶음의 칸을 골라 만듭니다. 호스트가 자기 표로 부릅니다. */
        static SceneEngineServices makeFromEngineServices( const EngineServices& services );
        /** @brief 지금 바인딩된 서비스 표에서 만듭니다. 표가 비어 있으면 모든 칸이 nullptr 입니다. */
        static SceneEngineServices makeFromBoundEngine();
    };
} // namespace sw

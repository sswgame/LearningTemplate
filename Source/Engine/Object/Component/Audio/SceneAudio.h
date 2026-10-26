/**
 * @file SceneAudio.h
 * @brief 씬 하나의 오디오 묶기 — 리스너(리스너 컴포넌트, 없으면 게임 카메라) · 에미터 자리와 속도 · 레이캐스트 가림 · 리버브 존 세기를 프레임마다 엔진에 넣습니다.
 * @details 씬 서비스(`ISceneService`)입니다. 이 .cpp 의 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생깁니다(`manager.getSubsystem<sw::SceneAudio>()`).
 *          프레임 진행은 활성 게임 카메라가 필요해 프레임 단계 목록 밖에서 합니다 — 활성 씬의 틱이 끝난 뒤 `Scene::update` 이 게임 스레드에서
 *          카메라 · 물리 씬을 받는 `update` 를 한 번 부르고, 단계 목록이 부르는 `update( deltaTime )` 은 아무것도 하지 않습니다.
 *          오디오 컴포넌트는 틱하지 않고 여기에 등록합니다. 엔진은 생성자로 받은 오디오 서비스의 것이고, 테스트는 `setAudioEngine` 으로 바꿉니다.
 *
 *          **2D 게임**: 리스너 컴포넌트가 없고 게임 카메라가 직교면 리스너를 `Screen2D`(화면 평면 팬, 반폭 = 직교 높이 × 16/9 ÷ 2)로 둡니다.
 *          **가림**: 3D 물리 씬이 있으면 `PhysicsAudioOcclusionQuery`(레이 셋)로, 없고 2D 물리 씬이 있으면 `Physics2DAudioOcclusionQuery`(월드 XY)로,
 *          프레임마다 에미터 `kOcclusionChecksPerFrame` 개씩 돌아가며 측정합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IndexedPtrList.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Math/Math.h"
#include "Core/String/RegistrationList.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/GameObject/Service/ISceneService.h"
#include "Engine/Physics/IPhysicsQueryScene.h"

namespace sw
{
    class AudioEmitterComponent;
    class AudioEngine;
    class AudioListenerComponent;
    class AudioReverbZoneComponent;
    class CameraComponent;
    class GameObjectManager;
    class IAudioOcclusionQuery;
    class IAudioService;

    /** @class SceneAudio @brief 씬 하나의 오디오 묶기입니다. 파일 머리말 참고. */
    class SW_API SceneAudio final : public ISceneService, private SubscriptionSource
    {
    public:
        /** @brief 한 프레임에 가림을 다시 측정하는 에미터 수입니다(레이 셋씩). */
        static constexpr uint32 kOcclusionChecksPerFrame = 16;

        /** @brief 씬 서비스 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        /** @brief 서브시스템 이름입니다(씬 범위의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "SceneAudio";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

        /**
         * @brief 매니저 @p manager 의 서비스 묶음에 든 오디오 서비스로 소리를 낼 씬 오디오를 둡니다(소유하지 않음).
         * @details 오디오 서비스가 없으면(nullptr) `setAudioEngine` 으로 정한 엔진이 없는 한 아무것도 넣지 않습니다.
         */
        explicit SceneAudio( GameObjectManager& manager );
        ~SceneAudio() override;

        SceneAudio( const SceneAudio& )            = delete;
        SceneAudio& operator=( const SceneAudio& ) = delete;

        /**
         * @brief 프레임 하나를 넣습니다 — 리스너 · 에미터 자리와 속도 · 가림 · 리버브 존 세기.
         * @param pCamera 리스너 컴포넌트가 없을 때 리스너가 되는 게임 카메라입니다(nullptr 이면 리스너 0 을 끕니다).
         * @param pScene3D 가림 레이캐스트를 쏠 3D 물리 씬입니다.
         * @param pScene2D 3D 물리 씬이 없을 때 가림 레이캐스트를 쏠 2D 물리 씬입니다(월드 XY 평면). 둘 다 nullptr 이면 가림을 측정하지 않습니다 — 0 으로 둡니다.
         */
        void update( float32 deltaSeconds, const CameraComponent* pCamera, const IPhysicsQueryScene3D* pScene3D, const IPhysicsQueryScene2D* pScene2D = nullptr );
        /** @brief 단계 목록이 부르는 진행입니다. 아무것도 하지 않습니다 — 진행은 카메라를 받는 위 `update` 를 `Scene::update` 이 부른다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬을 비울 때 부릅니다. 등록은 컴포넌트가 스스로 풀므로 할 일이 없습니다. */
        void onSceneCleared() override {}

        /** @brief 쓸 엔진을 정합니다(테스트). nullptr 이면 생성자로 받은 오디오 서비스의 엔진입니다. */
        void setAudioEngine( AudioEngine* pEngine ) { _pEngineOverride = pEngine; }
        /** @brief 쓸 엔진입니다. 받은 서비스가 없거나 오디오가 내려가 있으면 nullptr 입니다. */
        AudioEngine* findAudioEngine() const;
        /** @brief 생성자로 받은 오디오 서비스입니다(빌림). 없으면 nullptr 입니다. 씬 안의 코드(애니메이션 알림)가 소리를 낼 때 씁니다. */
        IAudioService* findAudioService() const { return _pAudioService; }
        /** @brief 가림 질의를 바꿉니다(테스트 · 게임의 다른 기하). nullptr 이면 물리 씬의 레이캐스트입니다. */
        void setOcclusionQuery( const IAudioOcclusionQuery* pQuery ) { _pOcclusionOverride = pQuery; }
        /** @brief 마지막 프레임의 첫 리스너 자리입니다(앰비언트 가장 가까운 점 · 리버브 존 판정). */
        const float3& getPrimaryListenerPosition() const { return _primaryListenerPosition; }

        // 세 등록은 구독을 돌려준다. 구독이 사라지면 뺀다. 거절(nullptr · 이미 올라 있음)이면 빈 구독이다.
        /** @brief 리스너 컴포넌트를 올립니다. */
        [[nodiscard]] Subscription addListener( AudioListenerComponent* pListener );
        /** @brief 에미터 컴포넌트를 올립니다. 뺄 때 엔진에서 에미터를 지운다 — 소리는 마지막 자리에서 끝까지 간다. */
        [[nodiscard]] Subscription addEmitter( AudioEmitterComponent* pEmitter );
        /** @brief 리버브 존을 올립니다. */
        [[nodiscard]] Subscription addReverbZone( AudioReverbZoneComponent* pZone );

    private:
        /** @brief 스냅샷 하나에 지난 프레임에 넣은 세기입니다. */
        struct ZoneValue
        {
            hashed_string _snapshot{};
            float32       _intensity{ 0.0f };
        };

        /** @brief 등록 구독이 풀렸습니다(@p id 는 컴포넌트 주소). 세 목록은 서로 다른 컴포넌트 종류라 주소가 겹치지 않으므로 든 목록에서 뺍니다. */
        void releaseSubscription( uint64 id ) override;

        // `update` 의 단계들 — 잠금을 잡은 채로 부른다.
        /** @brief 리스너 — 리스너 컴포넌트가 있으면 그것들, 없으면 게임 카메라(직교면 2D 화면 팬). 지난 프레임에 켰는데 이번에 없는 칸은 끈다. */
        void updateListeners( AudioEngine& audioEngine, float32 deltaSeconds, const CameraComponent* pCamera );
        /** @brief 에미터 자리 · 속도를 엔진에 넣고 가림 측정이 돌 사본 목록을 만듭니다. */
        void updateEmitters( AudioEngine& audioEngine, float32 deltaSeconds );
        /** @brief 가림 — 프레임마다 몇 개씩 돌아가며 측정한다(엔진이 부드럽게 따라간다). */
        void updateOcclusion( AudioEngine& audioEngine, const IPhysicsQueryScene3D* pScene3D, const IPhysicsQueryScene2D* pScene2D );
        /** @brief 리버브 존 — 스냅샷마다 가장 큰 세기. 바뀐 것만 넣고, 이번에 거는 존이 없는 스냅샷은 0 으로 내린다. */
        void updateReverbZones( AudioEngine& audioEngine );

        RegistrationList<AudioListenerComponent>   _listener;                /**< 리스너 컴포넌트입니다. 화면 분할 칸마다 하나라 많아야 한 자릿수이므로 선형 등록 목록으로 충분하다. */
        IndexedPtrList<AudioEmitterComponent>      _emitter;                 /**< 에미터 컴포넌트입니다. 수가 많고 씬 중에 자주 오르내려 넣기 · 빼기가 O(1) 이다(순회 순서는 보존되지 않는다 — 가림 측정이 도는 순서만 달라진다). */
        RegistrationList<AudioReverbZoneComponent> _zone;                    /**< 리버브 존입니다. 레벨 디자이너가 방 · 구역마다 손으로 놓아 많아야 수십 개이고 씬 중에 오르내리지 않으므로 선형 등록 목록으로 충분하다(수천 개로 자라면 `IndexedPtrList`). */
        vector<AudioEmitterComponent*>             _listEmitterSnapshot;     /**< 프레임마다 잠금 밖에서 도는 에미터 사본입니다. */
        vector<ZoneValue>                          _listZoneValue;           /**< 지난 프레임의 스냅샷 세기입니다. */
        vector<ZoneValue>                          _listZoneCurrent;         /**< 이번 프레임의 스냅샷 세기(작업 목록)입니다. */
        IAudioService*                             _pAudioService;           /**< 생성자로 받은 오디오 서비스(빌림)입니다. */
        AudioEngine*                               _pEngineOverride;         /**< 테스트가 정한 엔진입니다. */
        const IAudioOcclusionQuery*                _pOcclusionOverride;      /**< 바꾼 가림 질의입니다. */
        float3                                     _primaryListenerPosition; /**< 첫 리스너 자리입니다. */
        float3                                     _lastCameraPosition;      /**< 카메라 리스너의 지난 자리입니다(속도). */
        mutable mutex                              _mutex;                   /**< 등록 목록을 지킵니다(비동기 씬 로드는 워커에서 등록한다). */
        uint32                                     _occlusionCursor;         /**< 가림을 측정할 다음 에미터 자리입니다. */
        uint32                                     _activeListenerMask;      /**< 지난 프레임에 켠 리스너 칸입니다. */
        bool                                       _bHasLastCameraPosition;  /**< `_lastCameraPosition` 이 유효한지입니다. */
    };
} // namespace sw

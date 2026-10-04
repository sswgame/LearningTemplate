/**
 * @file SceneAudio.h
 * @brief 씬 하나의 오디오 묶기 — 리스너(리스너 컴포넌트, 없으면 게임 카메라) · 에미터 자리와 속도 · 레이캐스트 가림 · 리버브 존 세기를 프레임마다 엔진에 넣습니다.
 * @details `GameObjectManager` 가 소유만 하고(`ScenePhysics` 와 같은 자리), 활성 씬의 틱이 끝난 뒤 `Scene::tick` 이 게임 스레드에서 한 번 부릅니다.
 *          오디오 컴포넌트는 틱하지 않고 여기에 등록합니다. 엔진은 서비스(`engine::getAudioSystem`)의 것이고, 시험은 `setAudioEngine` 으로 바꿉니다.
 *
 *          **2D 게임**: 리스너 컴포넌트가 없고 게임 카메라가 직교면 리스너를 `Screen2D`(화면 평면 팬, 반폭 = 직교 높이 × 16/9 ÷ 2)로 둡니다.
 *          **가림**: 3D 물리 씬이 있으면 `PhysicsAudioOcclusionQuery`(레이 셋)로, 프레임마다 에미터 `kOcclusionChecksPerFrame` 개씩 돌아가며 잽니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/RegistrationList.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Physics/IPhysicsScene.h"

namespace sw
{
    class AudioEmitterComponent;
    class AudioEngine;
    class AudioListenerComponent;
    class AudioReverbZoneComponent;
    class CameraComponent;
    class IAudioOcclusionQuery;

    /** @class SceneAudio @brief 씬 하나의 오디오 묶기입니다. 파일 머리말 참고. */
    class SW_API SceneAudio
    {
    public:
        /** @brief 한 프레임에 가림을 다시 재는 에미터 수입니다(레이 셋씩). */
        static constexpr uint32 kOcclusionChecksPerFrame = 16;

        SceneAudio();
        ~SceneAudio();

        SceneAudio( const SceneAudio& )            = delete;
        SceneAudio& operator=( const SceneAudio& ) = delete;

        /**
         * @brief 프레임 하나를 넣습니다 — 리스너 · 에미터 자리와 속도 · 가림 · 리버브 존 세기.
         * @param pCamera 리스너 컴포넌트가 없을 때 리스너가 되는 게임 카메라입니다(nullptr 이면 리스너 0 을 끕니다).
         * @param pScene3D 가림 레이캐스트를 쏠 3D 물리 씬입니다(nullptr 이면 가림을 재지 않습니다 — 0 으로 둡니다).
         */
        void update( float32 deltaSeconds, const CameraComponent* pCamera, const IPhysicsScene3D* pScene3D );

        /** @brief 쓸 엔진을 정합니다(시험). nullptr 이면 서비스의 엔진입니다. */
        void setAudioEngine( AudioEngine* pEngine ) { _pEngineOverride = pEngine; }
        /** @brief 쓸 엔진입니다. 서비스가 없거나 오디오가 내려가 있으면 nullptr 입니다. */
        AudioEngine* findAudioEngine() const;
        /** @brief 가림 질의를 바꿉니다(시험 · 게임의 다른 기하). nullptr 이면 물리 씬의 레이캐스트입니다. */
        void setOcclusionQuery( const IAudioOcclusionQuery* pQuery ) { _pOcclusionOverride = pQuery; }
        /** @brief 마지막 프레임의 첫 리스너 자리입니다(앰비언트 가장 가까운 점 · 리버브 존 판정). */
        const float3& getPrimaryListenerPosition() const { return _primaryListenerPosition; }

        /** @brief 리스너 컴포넌트를 올립니다. */
        void addListener( AudioListenerComponent* pListener );
        /** @brief 리스너 컴포넌트를 뺍니다. */
        void removeListener( AudioListenerComponent* pListener );
        /** @brief 에미터 컴포넌트를 올립니다. */
        void addEmitter( AudioEmitterComponent* pEmitter );
        /** @brief 에미터 컴포넌트를 뺍니다(엔진에서 에미터를 지운다 — 소리는 마지막 자리에서 끝까지 간다). */
        void removeEmitter( AudioEmitterComponent* pEmitter );
        /** @brief 리버브 존을 올립니다. */
        void addReverbZone( AudioReverbZoneComponent* pZone );
        /** @brief 리버브 존을 뺍니다. */
        void removeReverbZone( AudioReverbZoneComponent* pZone );

    private:
        /** @brief 스냅샷 하나에 지난 프레임에 넣은 세기입니다. */
        struct ZoneValue
        {
            hashed_string _snapshot{};
            float32       _intensity{ 0.0f };
        };

        RegistrationList<AudioListenerComponent>   _listener;                /**< 리스너 컴포넌트입니다. */
        RegistrationList<AudioEmitterComponent>    _emitter;                 /**< 에미터 컴포넌트입니다. */
        RegistrationList<AudioReverbZoneComponent> _zone;                    /**< 리버브 존입니다. */
        vector<AudioEmitterComponent*>             _listEmitterSnapshot;     /**< 프레임마다 잠금 밖에서 도는 에미터 사본입니다. */
        vector<ZoneValue>                          _listZoneValue;           /**< 지난 프레임의 스냅샷 세기입니다. */
        vector<ZoneValue>                          _listZoneCurrent;         /**< 이번 프레임의 스냅샷 세기(작업 목록)입니다. */
        AudioEngine*                               _pEngineOverride;         /**< 시험이 정한 엔진입니다. */
        const IAudioOcclusionQuery*                _pOcclusionOverride;      /**< 바꾼 가림 질의입니다. */
        float3                                     _primaryListenerPosition; /**< 첫 리스너 자리입니다. */
        float3                                     _lastCameraPosition;      /**< 카메라 리스너의 지난 자리입니다(속도). */
        mutable mutex                              _mutex;                   /**< 등록 목록을 지킵니다(비동기 씬 로드는 워커에서 등록한다). */
        uint32                                     _occlusionCursor;         /**< 가림을 잴 다음 에미터 자리입니다. */
        uint32                                     _activeListenerMask;      /**< 지난 프레임에 켠 리스너 칸입니다. */
        bool                                       _bHasLastCameraPosition;  /**< `_lastCameraPosition` 이 유효한지입니다. */
    };
} // namespace sw

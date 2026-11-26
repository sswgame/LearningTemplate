/**
 * @file SurfaceFootprintSystem.h
 * @brief 씬 하나의 발걸음 반응입니다. 발걸음마다 밟은 표면 종류와 땅의 젖음으로 반응 한 벌(`SurfaceStepResponse`)을 골라 발자국 도장 · 소리 · 젖은 자국을 내고, 고른 반응을 돌려줍니다.
 * @details 반응은 데이터다(`SurfaceKindDef::_step` · `_wetStep`) — 흙먼지 · 모래 · 눈 가루 · 물 튀김 · 진흙이 모두 종류의 칸이고 코드에 지형 이름이 없다(언리얼 Physical Material
 *          별 발걸음 데이터 · 유니티 Terrain Layer 별 발걸음 표 자리). 고르는 규칙은 하나다: 땅의 젖음(`SurfaceSample::_wetness`)이 표의 `_wetThreshold` 이상이고 종류에
 *          젖은 반응이 있으면 젖은 반응, 아니면 마른 반응이다.
 *          - **발자국**: 표면 변형 층(`SurfaceDeformSystem`)의 도장이다 — 높이장 · 충돌 · 내비를 고치지 않고 지면 머티리얼이 꺼져 보인다. 남는 시간은 도장의 메워지는 속도가 정한다.
 *          - **젖은 자국**: 반응이 발을 적시면(`_feetWetness` — 물 · 젖은 진흙) 그 걸음꾼은 표의 `_wetFeetSteps` 걸음 동안 마른 땅에 젖은 자국을 남긴다(같은 층의 젖음 채널,
 *            걸음마다 옅어지고 `_wetPrintDrySpeed` 로 마른다). 걸음꾼은 오브젝트 id 로 센다.
 *          - **효과**: 고른 반응의 `_effect` 는 부르는 쪽(알림 처리기 — 표면과 VFX 를 함께 보는 층)이 씬의 효과 풀(`VFXEffectPool`, 효과마다 표의 `_stepEffectPoolSize` 개)로 튼다.
 *            표면 층(Environment)은 VFX 층을 include 하지 않는다(같은 단 — `EngineLayers.toml`).
 *          - **소리**: 반응의 오디오 이벤트를 발 자리에서 낸다(`ISoundPlayer::postEventAt`).
 *          소리 · 자국은 보이고 들리는 것뿐이라 전용 서버(`build::kWithClientCode` 가 거짓)는 내지 않는다 — 세기와 마지막 반응(감시 값)은 서버도 적는다.
 *          씬 서브시스템(`ISceneSubsystem`)이고 `SW_SCENE_SUBSYSTEM` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::SurfaceFootprintSystem>()`).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Object/GameObject/Subsystem/ISceneSubsystem.h"

namespace sw
{
    struct SurfaceKindTable;
    struct SurfaceSample;
    struct SurfaceStepResponse;

    class GameObjectManager;

    /**
     * @brief 발걸음 기록 한 벌입니다 — 종류마다 센 수와 마지막 발걸음의 반응(종류 · 마름/젖음 · 소리 · 효과). 씬 전체 한 벌과 걸은 오브젝트마다 한 벌을 둔다
     *        (여럿이 함께 걸어도 한 오브젝트의 반응을 따로 본다 — 감시 값 `Surface.Object*`).
     */
    /** @brief 표면 종류 하나의 발걸음 기록입니다 — 센 수와 그 종류를 마지막으로 밟은 반응. */
    struct SurfaceFootstepKindNote
    {
        uint32        _count{ 0 };
        hashed_string _lastSound{};
        string        _lastEffect{};
        uint8         _bLastWet{ SW_FALSE };
    };

    struct SurfaceFootstepRecord
    {
        /// 종류 이름 → 그 종류의 기록. 두 발이 한 프레임에 다른 종류를 디디면 "마지막 발걸음" 은 나중 것 하나라, 종류마다의 반응은 여기서 본다.
        unordered_map<hashed_string, SurfaceFootstepKindNote> _mapKind{};
        hashed_string                                         _lastKind{};

        /** @brief 종류 @p kind 를 밟은 발걸음 수입니다. */
        uint32 getCount( const hashed_string& kind ) const;
        /** @brief 종류 @p kind 의 기록입니다. 밟은 적이 없으면 nullptr 입니다. */
        const SurfaceFootstepKindNote* findKind( const hashed_string& kind ) const;
        /** @brief 마지막 발걸음의 기록입니다. 발걸음이 없으면 nullptr 입니다. */
        const SurfaceFootstepKindNote* findLast() const { return findKind( _lastKind ); }
        /** @brief 발걸음 하나(종류 @p kind, 고른 반응 @p step, 젖음 @p bWet)를 적습니다. */
        void note( const hashed_string& kind, const SurfaceStepResponse& step, bool bWet );
    };

    /**
     * @class SurfaceFootprintSystem
     * @brief 씬마다 하나 있는 발걸음 반응입니다(파일 머리말 참고).
     */
    class SW_API SurfaceFootprintSystem final : public ISceneSubsystem
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SUBSYSTEM` 가 정의한다). */
        static const SceneSubsystemRegistration& getSceneSubsystemRegistration();
        static constexpr const utf8*             kSubsystemName = "SurfaceFootprintSystem";
        const utf8*                              getSubsystemName() const override { return kSubsystemName; }

        explicit SurfaceFootprintSystem( GameObjectManager& manager );

        SurfaceFootprintSystem( const SurfaceFootprintSystem& )            = delete;
        SurfaceFootprintSystem& operator=( const SurfaceFootprintSystem& ) = delete;

        /** @brief 할 일이 없다 — 자국의 나이는 변형 층이 메운다. */
        void update( float32 deltaTime ) override { (void)deltaTime; }
        /** @brief 씬을 비울 때 센 수 · 젖은 발을 버립니다. */
        void onSceneCleared() override;

        /**
         * @brief 발걸음 하나를 알립니다. 밟은 표면 @p sample(종류 · 젖음 · 맞은 점)로 반응을 골라 내고(파일 머리말), 종류마다 센 수를 올립니다.
         * @param yaw           발자국 방향(라디안, 몸의 앞)
         * @param ownerObjectID 걸은 오브젝트 — 젖은 발을 센다(0 이면 젖은 자국을 남기지 않는다)
         * @return 고른 반응입니다(효과는 부르는 쪽이 튼다). 표면 종류 표를 읽지 못했으면 nullptr 입니다.
         */
        const SurfaceStepResponse* addFootstep( const SurfaceSample& sample, float32 yaw, uint64 ownerObjectID );

        /** @brief 이 씬에서 찍은 발자국 수입니다(누적). */
        uint32 getPlacedCount() const { return _placedCount; }
        /** @brief 이 씬에서 찍은 젖은 자국 수입니다(누적). */
        uint32 getWetPrintCount() const { return _wetPrintCount; }
        /** @brief 종류 @p kind 를 밟은 발걸음 수입니다(씬 전체, 누적). */
        uint32 getFootstepCount( const hashed_string& kind ) const { return _sceneRecord.getCount( kind ); }
        /** @brief 씬 전체의 발걸음 기록입니다. */
        const SurfaceFootstepRecord& getSceneRecord() const { return _sceneRecord; }
        /** @brief 오브젝트 @p objectID 의 발걸음 기록입니다. 걸은 적이 없으면 nullptr 입니다. */
        const SurfaceFootstepRecord* findObjectRecord( uint64 objectID ) const;

    private:
        /** @brief 걸은 오브젝트 하나의 젖은 발입니다. */
        struct WetFeetState
        {
            uint32  _remainingSteps{ 0 }; ///< 젖은 자국을 더 남길 걸음 수
            float32 _wetness{ 0.0f };     ///< 젖은 뒤 첫 자국의 젖음
        };

        /** @brief 젖은 발을 고칩니다 — 반응이 발을 적시면 다시 채우고, 마른 땅의 걸음이면 자국 하나를 찍고 줄입니다. */
        void updateWetFeet( const SurfaceKindTable& table, const SurfaceStepResponse& step, bool bGroundWet, const float3& point, float32 yaw, uint64 ownerObjectID );
        /** @brief 소리 이벤트 @p sound 를 @p point 에서 냅니다. 소리 창구가 없으면(전용 서버) 아무것도 하지 않는다. */
        void playSound( const hashed_string& sound, const float3& point ) const;

    private:
        GameObjectManager*                           _pManager;
        SurfaceFootstepRecord                        _sceneRecord;
        unordered_map<uint64, SurfaceFootstepRecord> _mapObjectRecord; ///< 걸은 오브젝트 id → 그 오브젝트의 기록
        unordered_map<uint64, WetFeetState>          _mapWetFeet;      ///< 걸은 오브젝트 id → 젖은 발(자국을 다 남기면 지운다)
        uint32                                       _placedCount;
        uint32                                       _wetPrintCount;
    };
} // namespace sw

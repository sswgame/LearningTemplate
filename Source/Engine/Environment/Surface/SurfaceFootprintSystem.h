/**
 * @file SurfaceFootprintSystem.h
 * @brief 씬 하나의 발걸음 반응입니다. 발걸음을 표면 종류마다 세고, 발자국을 남기는 종류(눈)면 고정 용량 링에 발자국 하나를 놓습니다.
 * @details 발자국은 시각 효과다 — 높이장 · 충돌 · 내비를 고치지 않는다(발걸음마다 충돌을 다시 만들면 비용과 게임플레이 표면이 함께 흔들린다).
 *          용량은 표면 종류 표의 `_footprintCapacity` 이고, 가득 차면 가장 오래된 발자국의 오브젝트를 새 자리로 옮겨 다시 쓴다(새로 만들지 않는다) —
 *          몇 바퀴를 걸어도 발자국 수와 오브젝트 수가 용량을 넘지 않는다. 수명(`SurfaceKindDef::_footprintLifetime`, 게임 초)에 따라 줄어들다가 끝나면 꺼 둔다.
 *          언리얼 데칼 풀(`UDecalComponent` 를 재활용하는 발자국 매니저)과 같은 자리다. 투영 데칼이 생기면 오브젝트 대신 그것을 놓는다(같은 링).
 *          씬 서비스(`ISceneService`)이고 `SW_SCENE_SERVICE` 줄로 매니저마다 하나 생긴다(`manager.getSubsystem<sw::SurfaceFootprintSystem>()`).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/GameObject/Service/ISceneService.h"

namespace sw
{
    struct SurfaceKindDef;
    struct SurfaceKindTable;

    class GameObjectManager;

    /**
     * @class SurfaceFootprintSystem
     * @brief 씬마다 하나 있는 발걸음 반응입니다(파일 머리말 참고).
     */
    class SW_API SurfaceFootprintSystem final : public ISceneService
    {
    public:
        /** @brief 이 서비스의 등록 줄입니다(`SW_SCENE_SERVICE` 가 정의한다). */
        static const SceneServiceRegistration& getSceneServiceRegistration();
        static constexpr const utf8*           kSubsystemName = "SurfaceFootprintSystem";
        const utf8*                            getSubsystemName() const override { return kSubsystemName; }

        explicit SurfaceFootprintSystem( GameObjectManager& manager );

        SurfaceFootprintSystem( const SurfaceFootprintSystem& )            = delete;
        SurfaceFootprintSystem& operator=( const SurfaceFootprintSystem& ) = delete;

        /** @brief 발자국 나이를 먹이고 줄입니다. 수명이 끝난 발자국은 꺼 둔다(오브젝트는 링 칸에 남아 다음 발자국이 다시 쓴다). */
        void update( float32 deltaTime ) override;
        /** @brief 씬을 비울 때 링을 버립니다. 오브젝트는 매니저가 지운다. */
        void onSceneCleared() override;

        /**
         * @brief 발걸음 하나를 알립니다. 종류마다 센 수를 올리고, 종류가 발자국을 남기면 @p point 에 요 @p yaw(라디안) 방향으로 발자국을 놓습니다.
         * @details 용량은 처음 놓을 때 @p table 에서 정한다. 프리팹을 만들지 못하면 오류를 남기고 그 발자국은 기록에만 든다.
         */
        void addFootstep( const SurfaceKindTable& table, const SurfaceKindDef& kind, const float3& point, float32 yaw );

        /** @brief 지금 살아 있는(수명이 남은) 발자국 수입니다. */
        uint32 getFootprintCount() const;
        /** @brief 링 용량입니다(아직 놓은 적이 없으면 0). */
        uint32 getCapacity() const { return _capacity; }
        /** @brief 링에 든 발자국 오브젝트 수입니다(용량을 넘지 않는다 — 꺼 둔 것도 센다). */
        uint32 getFootprintObjectCount() const { return static_cast<uint32>( _listFootprint.size() ); }
        /** @brief 이 씬에서 놓은 발자국 수입니다(누적, 다시 쓴 것도 센다). */
        uint32 getPlacedCount() const { return _placedCount; }
        /** @brief 종류 @p kind 를 밟은 발걸음 수입니다(누적). */
        uint32 getFootstepCount( const hashed_string& kind ) const;
        /** @brief 마지막 발걸음의 종류입니다(없으면 비어 있다). */
        const hashed_string& getLastKind() const { return _lastKind; }

    private:
        /** @brief 링 칸 하나입니다. */
        struct FootprintRecord
        {
            GameObjectHandle _visual;
            float3           _baseScale; ///< 프리팹이 정한 크기 — 나이에 따라 이 값에서 줄인다
            float32          _age;
            float32          _lifetime;
            uint8            _bAlive   : 1;
            uint8            _reserved : 7;

            FootprintRecord();
        };

        /** @brief @p slot 칸의 오브젝트를 놓거나(처음) 다시 써서 @p point · @p yaw 에 켭니다. 만들지 못하면 false 입니다. */
        [[nodiscard]] bool placeVisual( FootprintRecord& slot, const SurfaceKindDef& kind, const float3& point, float32 yaw );

    private:
        GameObjectManager*                   _pManager;
        vector<FootprintRecord>              _listFootprint; ///< 링(용량까지 자란다)
        unordered_map<hashed_string, uint32> _mapFootstepCount;
        hashed_string                        _lastKind;
        uint32                               _capacity;
        uint32                               _nextSlot; ///< 가득 찼을 때 다음에 다시 쓸 칸(가장 오래된 것)
        uint32                               _placedCount;
    };
} // namespace sw

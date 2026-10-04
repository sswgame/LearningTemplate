/**
 * @file SkirmishWorld.h
 * @brief StarSkirmish 의 화면과 입력 — 한 판(`SkirmishMatch`)을 내장 도형으로 그리고, 끌어 고르기 · 오른쪽 클릭 명령 · 단축키 생산 · 건설 · 부대를 연결합니다.
 *
 * @details 화면은 북쪽(+Z)을 비스듬히 내려다보는 직교 카메라이고 칸 하나가 1 m 입니다. 사람은 0 번(파랑)을 맡고 1 번(빨강)은 `RtsAiController` 입니다.
 *          `-gv_skirmishAutoPlay=1` 이면 둘 다 AI 이고 입력은 카메라 · 속도만 받습니다. 사람 쪽은 전장의 안개가 적 유닛을 가립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"

#include "GameFramework/Kits/Strategy/RealTimeStrategy/RtsSelection.h"
#include "GameFramework/Stage/PrimitiveStage.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

namespace sw
{
    class InputManager;

    /**
     * @class SkirmishWorld
     * @brief 한 판의 모습 · 조작입니다. 판(월드 · AI)은 무대를 걷어도(핫 리로드 · 씬 바뀜) 남고, 무대는 다음 갱신에서 그 상태대로 다시 섭니다.
     */
    class SkirmishWorld
    {
    public:
        SkirmishWorld();
        ~SkirmishWorld();

        SkirmishWorld( const SkirmishWorld& )            = delete;
        SkirmishWorld& operator=( const SkirmishWorld& ) = delete;

        /** @brief 카탈로그를 빌려 새 판을 엽니다(`-gv_skirmishAutoPlay` 를 여기서 읽는다). */
        void initialize( const RtsCatalog* pCatalog );
        /** @brief 무대를 세웁니다. 이미 섰으면 true, 씬 서비스가 아직 없으면 false 입니다. */
        [[nodiscard]] bool spawn();
        void               despawn();
        /** @brief 한 프레임 — 입력 · 판 · 알림 · 모습 · 카메라. */
        void update( float32 deltaTime );
        bool isInitialized() const { return _pCatalog != nullptr; }

    private:
        /** @brief 유닛 자리(id 의 index) 하나의 모습입니다. */
        struct UnitView
        {
            RtsUnitId        _id{};
            GameObjectHandle _object{};
            uint32           _stamp{ 0 };
            int32            _lookKey{ -1 };
        };

        void spawnTerrain();
        void syncUnits();
        void updateInput( float32 deltaTime, const InputManager& input );
        void updateHumanCommands( const InputManager& input );
        void updateDrag( const InputManager& input, const float3& point, bool bPointValid );
        void issueRightClick( const float3& point, bool bQueue );
        void orderBuild( const utf8* pBuildingId, const float3& point );
        void trainFromPrimary( int32 productIndex );
        void updateCamera();
        void handleEvents();
        /** @brief 마우스가 가리키는 땅(y = 0) 자리입니다. 화면 밖이면 false 입니다. */
        [[nodiscard]] bool findGroundPoint( const InputManager& input, float3& outPoint ) const;

        PrimitiveStage    _stage;
        SkirmishMatch     _match;
        RtsSelection      _selection;
        vector<UnitView>  _listUnitView;
        vector<RtsEvent>  _listEvent;
        const RtsCatalog* _pCatalog;
        GameObjectHandle  _dragObject;
        float3            _cameraFocus;
        float3            _dragStart;
        float32           _cameraHeight; ///< 직교 화면 높이(m)
        float32           _timeScale;
        uint32            _frameStamp;
        uint8             _bHuman;
        uint8             _bDragging;
        uint8             _bAttackMovePending;
        uint8             _bPaused;
        uint8             _bSpawned;
    };
} // namespace sw

/**
 * @file ParkWorld.h
 * @brief ThemeParkTycoon 의 게임 규칙 — 공원 배치(XML) · 짓기 · 값 매기기 · 카메라(아이소메트릭 직교 · 코스터 탑승)와 손님 · 놀이기구의 모습입니다.
 *
 * @details 손님 · 줄 · 표 · 평점은 키트의 `ThemeParkSimulation`, 코스터 트랙 · 물리 · 평가는 `CoasterTrackBuilder` · `CoasterTrain` · `CoasterRideAnalyzer` 가
 *          맡습니다. 여기는 "무엇을 어디에 짓고 어떻게 보이는가" 만 압니다. 화면은 롤러코스터 타이쿤처럼 비스듬히 내려다보는 직교 카메라이고 Q/E 로
 *          90° 씩 돌립니다. V 는 고른 코스터에 올라탑니다(루프에서 뒤집히는 시점 — `OrientationUtil`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrack.h"
#include "GameFramework/Kits/Simulation/ThemePark/CoasterTrain.h"
#include "GameFramework/Kits/Simulation/ThemePark/ThemePark.h"
#include "GameFramework/Stage/PrimitiveStage.h"

namespace sw
{
    class InputManager;

    /**
     * @class ParkWorld
     * @brief 공원 한 판입니다. 시뮬레이션(돈 · 손님 · 놀이기구 상태)은 무대를 걷어도 남고, 무대는 그 상태대로 다시 섭니다.
     */
    class ParkWorld
    {
    public:
        ParkWorld();
        ~ParkWorld();

        ParkWorld( const ParkWorld& )            = delete;
        ParkWorld& operator=( const ParkWorld& ) = delete;

        /** @brief 코스터 레이아웃과 공원 배치를 읽고 시뮬레이션을 엽니다. 처음 둘(가장 싼 평지 · 코스터)을 짓습니다. */
        [[nodiscard]] bool loadData( string_view coasterPath, string_view parkPath );
        [[nodiscard]] bool spawn();
        void               despawn();
        void               update( float32 deltaTime );
        bool               isLoaded() const { return _bLoaded != SW_FALSE; }

    private:
        /** @brief 지을 수 있는 놀이기구 하나(배치 XML 의 한 줄)입니다. */
        struct RideBlueprint
        {
            ParkRide      _ride{};
            hashed_string _layoutId{}; ///< 비면 평지 놀이기구
            string        _shape{ "Cylinder" };
            float3        _position{};
            float3        _size{ 4.0f, 1.0f, 4.0f };
            float4        _color{ 1.0f, 1.0f, 1.0f, 1.0f };
            float32       _heading{ 0.0f }; ///< 코스터 스테이션 방향(도)
            float32       _spin{ 0.0f };    ///< 평지 놀이기구가 운행 중 도는 빠르기(rad/s)
            float32       _loadTime{ 15.0f };
            int32         _buildCost{ 1000 };
            int32         _rideIndex{ -1 }; ///< 지었으면 시뮬레이션의 놀이기구 번호
        };

        /** @brief 지은 코스터 하나 — 트랙 · 열차 · 모습입니다. 트랙은 열차가 가리키므로 힙에 둔다(목록이 자라도 주소가 그대로). */
        struct CoasterView
        {
            unique_ptr<CoasterTrack> _pTrack{};
            CoasterTrain             _train{};
            vector<GameObjectHandle> _listCar{};
            int32                    _blueprintIndex{ -1 };
        };

        /** @brief 평지 놀이기구의 모습입니다. */
        struct FlatRideView
        {
            GameObjectHandle _object{};
            float32          _angle{ 0.0f };
            int32            _blueprintIndex{ -1 };
        };

        /** @brief 손님 오브젝트 한 칸(손님 수만큼 쓰고 남으면 숨긴다)입니다. */
        struct GuestView
        {
            GameObjectHandle _object{};
            int32            _colorBucket{ -1 };
        };

        [[nodiscard]] bool loadParkLayout( string_view parkPath );
        /** @brief 설계도 하나를 짓습니다(돈이 모자라면 false). 무대가 서 있으면 모습도 세운다. */
        bool buildBlueprint( int32 blueprintIndex );
        /** @brief 아직 안 지은 것 중 가장 싼 것을 짓습니다. */
        bool buildCheapestRemaining();
        void spawnRideView( int32 blueprintIndex );
        void spawnCoasterView( CoasterView& view, const RideBlueprint& blueprint );
        void spawnPath( const float3& from, const float3& to );

        void  updateInput( float32 deltaTime, const InputManager& input );
        void  updateRides( float32 deltaTime );
        void  updateGuests();
        void  updateCamera();
        void  logStatus( float32 deltaTime, bool bForce );
        void  logThoughts() const;
        int32 findSelectedRideIndex() const;

        PrimitiveStage                  _stage;
        ThemeParkSimulation             _simulation;
        CoasterLayoutCatalog            _layoutCatalog;
        vector<RideBlueprint>           _listBlueprint;
        vector<unique_ptr<CoasterView>> _listCoasterView;
        vector<FlatRideView>            _listFlatView;
        vector<GuestView>               _listGuestView;
        ThemeParkSettings               _settings;
        float3                          _cameraFocus;
        float32                         _cameraYaw;    ///< 라디안 — 45° + 90° × k
        float32                         _cameraHeight; ///< 직교 화면 높이(m)
        float32                         _statusTimer;
        float32                         _autoBuildTimer;
        int32                           _startingCash;
        int32                           _selectedBlueprint;
        int32                           _ridingCoaster; ///< 0 이상이면 그 코스터에 타고 있다
        uint8                           _bLoaded;
        uint8                           _bSpawned;
    };
} // namespace sw

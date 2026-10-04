/**
 * @file FarmWorld.h
 * @brief HarvestValley 의 게임 규칙 — 농부 이동 · 도구 · 체력 · 시간 흐름 · 가게 · 출하 · 잠 · 날씨와 그 모습입니다.
 *
 * @details 밭 · 달력 · 인벤토리의 규칙은 키트(`GF_Farming`)가 맡고, 여기는 "농부가 어디 서서 무엇을 누르는가" 와 "그것이 어떻게 보이는가" 만 압니다.
 *          화면은 비스듬히 내려다보는 직교 카메라(2D 농장 게임의 시점)이고, 칸 · 작물 · 농부는 내장 도형입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Kits/Simulation/Farming/FarmCalendar.h"
#include "GameFramework/Kits/Simulation/Farming/FarmField.h"
#include "GameFramework/Kits/Simulation/Farming/FarmInventory.h"
#include "GameFramework/Stage/PrimitiveStage.h"
#include "GameFramework/Utility/GameRandom.h"

namespace sw
{
    class CropCatalog;
    class InputManager;

    /** @brief 손에 든 도구입니다. */
    enum class FarmTool : uint8
    {
        Hoe = 0,     ///< 갈기
        WateringCan, ///< 물 주기
        Seeds,       ///< 고른 씨앗 심기
        Hand         ///< 거두기
    };

    /**
     * @class FarmWorld
     * @brief 농장 한 판입니다. 밭 상태는 무대를 걷어도(핫 리로드 · 씬 바뀜) 남고, 무대는 다음 갱신에서 그 상태대로 다시 섭니다.
     */
    class FarmWorld
    {
    public:
        static constexpr int32   kFieldWidth       = 12;
        static constexpr int32   kFieldHeight      = 8;
        static constexpr int32   kMaxStamina       = 100;
        static constexpr int32   kStartingGold     = 500;
        static constexpr float32 kMinutesPerSecond = 10.0f; ///< 실제 1 초 = 게임 10 분(하루 20 시간 = 2 분)

        FarmWorld();
        ~FarmWorld();

        FarmWorld( const FarmWorld& )            = delete;
        FarmWorld& operator=( const FarmWorld& ) = delete;

        /** @brief 카탈로그를 빌리고 밭 · 달력 · 인벤토리를 새로 둡니다(봄 1 일, 무 씨앗 10 개). */
        void initialize( const CropCatalog* pCatalog );
        /** @brief 무대를 세웁니다. 이미 섰으면 true 입니다. */
        [[nodiscard]] bool spawn();
        void               despawn();
        /** @brief 한 프레임 — 시간 · 입력(또는 자동 농부) · 모습 · 카메라 · 해. */
        void update( float32 deltaTime );

        const FarmCalendar&  getCalendar() const { return _calendar; }
        const FarmField&     getField() const { return _field; }
        const FarmInventory& getInventory() const { return _inventory; }

    private:
        /** @brief 칸 하나의 모습 — 흙 · 작물 오브젝트와 마지막으로 칠한 상태입니다(바뀔 때만 다시 칠한다). */
        struct TileView
        {
            GameObjectHandle _soil{};
            GameObjectHandle _crop{};
            int32            _soilState{ -1 };
            int32            _cropState{ -1 };
        };

        /** @brief 울타리 · 나무 · 장작 · 덤불 · 꽃을 세웁니다(모습만, 규칙과 무관). */
        void spawnDecoration();
        void updatePlayerInput( float32 deltaTime, const InputManager& input );
        void updateAutoFarmer( float32 deltaTime );
        /** @brief 농부를 @p direction 으로 움직이고 바라보는 쪽을 네 방향으로 맞춥니다. */
        void movePlayer( const float3& direction, float32 deltaTime );
        /** @brief 바라보는 칸에 지금 도구를 씁니다. 체력이 모자라면 아무것도 하지 않습니다. */
        void useTool();
        /** @brief 출하함 옆이면 수확물을 모두 넣습니다. 넣은 개수입니다. */
        int32 shipAllProduce();
        /** @brief 가게 옆이면 고른 씨앗을 하나 삽니다. */
        bool buySelectedSeed();
        /** @brief 하루를 끝냅니다 — 정산 → 다음 날 → 날씨 → 밭 → 체력. @p bPassedOut 이면 체력을 반만 채운다. */
        void endDay( bool bPassedOut );
        void selectSeed( int32 offset );

        bool                 isNearShippingBin() const;
        bool                 isNearShop() const;
        bool                 findTargetTile( int32& outX, int32& outY ) const;
        const hashed_string& getSelectedSeed() const;

        void refreshViews();
        void updateCameraAndSun();
        void logStatus( bool bForce );

        PrimitiveStage        _stage;
        FarmCalendar          _calendar;
        FarmField             _field;
        FarmInventory         _inventory;
        vector<TileView>      _listTileView;
        vector<hashed_string> _listSeed; ///< 카탈로그 순서의 씨앗 아이템(가게 진열)
        const CropCatalog*    _pCatalog;
        float3                _playerPosition;
        float3                _facing;
        GameObjectHandle      _player;
        GameObjectHandle      _marker;
        GameObjectHandle      _sun;
        float32               _autoTimer;
        int32                 _stamina;
        int32                 _selectedSeedIndex;
        int32                 _lastLoggedHour;
        GameRandom            _random; ///< 날씨
        FarmTool              _tool;
        uint8                 _bRaining;
        uint8                 _bSpawned;
    };
} // namespace sw

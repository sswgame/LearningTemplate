/**
 * @file CityBuilderGameModeComponent.h
 * @brief 도시 건설 키트(`GF_CityBuilder`)의 디렉터 바탕 — 도시(`CitySimulation`) 하나와 카탈로그 · 배속을 들고, 짓기 · 철거 · 시간 명령을 받고, 화면이 읽을 도시 · 알림을 냅니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 모습과 입력은 클라이언트 모듈(`GF_Client_CityBuilder`)이
 *          이 바탕의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          파생은 둘이다: 공유 땅 · 지갑 · 시계 위의 도시(`CityGameModeComponent`)와, 자기 땅 · 금고 · 달력을 가진 게임의 디렉터(NileCity).
 */
#pragma once
#include "Core/Concurrency/DevCounter.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CityCatalog.h"
#include "GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameModeComponent.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameSpeedControl.h"

namespace sw
{
    class Wallet;

    /**
     * @class CityBuilderGameModeComponent
     * @brief 도시 하나를 돌리는 디렉터의 공통 몫입니다. 땅 · 금고 · 달력 · 상태 본문은 파생이 듭니다.
     */
    REFLECT( Abstract, Category = "CityBuilder", DisplayName = "City Builder Game Mode", Tooltip = "Base of the game modes that run one city" )
    class SW_KIT_API CityBuilderGameModeComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        CityBuilderGameModeComponent();
        virtual ~CityBuilderGameModeComponent() override;

        // ---- 명령(판이 열린 동안만 받는다, 게임 스레드) ----
        /** @brief 타일에 도로를 깝니다. 판이 열리기 전이면 `OutOfBounds` 입니다. */
        CityPlaceResult submitPlaceRoad( const int2& tile );
        /** @brief 타일을 왼쪽 위로 건물 @p buildingID 를 짓습니다. 판이 열리기 전이면 `OutOfBounds` 입니다. */
        CityPlaceResult submitPlaceBuilding( const hashed_string& buildingID, const int2& tile );
        /** @brief 타일의 건물 · 도로를 허뭅니다. 허물 것이 없거나 판이 열리기 전이면 false 입니다. */
        [[nodiscard]] bool submitDemolish( const int2& tile );
        /** @brief 일시정지 · 빠르게 · 느리게 입력입니다. 판이 열리기 전이면 false 입니다. 시계를 다른 길로 미는 디렉터는 덮어 거절한다. */
        [[nodiscard]] virtual bool submitTimeInput( bool bPause, bool bFaster, bool bSlower );
        /** @brief 자동 계획을 켜고 끕니다. 자동 계획이 없는 디렉터는 거절합니다(false). */
        [[nodiscard]] virtual bool submitAutoPlanToggle() { return false; }

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        const CitySimulation& getCity() const { return _city; }
        /** @brief 도시가 묶인 카탈로그입니다(도시가 든 행 번호 · 정의 포인터와 같은 판). 판을 열기 전에는 nullptr 입니다. */
        const CityCatalog* findCatalog() const { return _pCatalog.get(); }
        /** @brief `findCatalog` 를 소유 포인터로 돌려줍니다 — 어느 판으로 모습을 지었는지 기억하는 뷰가 쥔다. */
        shared_ptr<const CityCatalog> getCatalogSnapshot() const { return _pCatalog.snapshot(); }
        /** @brief 도시 배치(건물 · 도로)의 판입니다 — 놓거나 허물거나 복원할 때마다 오르고, 열린 판은 1 이상입니다. 뷰는 이 값이 바뀐 때만 맞춘다. */
        uint32 getLayoutRevision() const { return _layoutRevision; }
        /** @brief 이번 걸음의 도시 알림입니다(달 끝 · 집 진화 · 범람). 디렉터가 한 번 꺼내 두고 파생의 규칙 · 클라이언트의 소리가 같은 목록을 읽는다. */
        const vector<CityEvent>& getStepEvents() const { return _listStepEvent; }
        /** @brief 도시가 쓰는 금고입니다. 없으면 nullptr 입니다. */
        virtual const Wallet* findCityWallet() const = 0;
        /** @brief 도시 현황을 로그로 남깁니다(현황 키). */
        void logStatus() const override;
        /** @brief 도시의 인구입니다. */
        int32             getPopulation() const { return _city.getPopulation(); }
        const DevCounter& getBuildCount() const { return _buildCount; }       ///< 명령으로 깔거나 지은 수(성공만 — 감시 값만 읽는 개발 전용 칸)
        const DevCounter& getDemolishCount() const { return _demolishCount; } ///< 명령으로 허문 수(개발 전용 칸)

    protected:
        /** @brief 씬이 적은 도시 카탈로그를 받습니다. 비었거나 읽지 못하면 오류를 남기고 false 입니다(판을 열지 않는다). */
        [[nodiscard]] bool acquireCatalog();
        /** @brief 연 도시(`getMutableCity().initialize` 뒤)의 알림 소비자 표를 얻고 센 수 · 알림을 비웁니다. `startGame` 의 끝에 부른다. */
        [[nodiscard]] bool beginCity();
        /** @brief 이번 걸음의 도시 알림을 꺼냅니다. 걸음마다(시간이 멈춘 프레임도) 한 번 부른다 — 부르지 않으면 지난 알림이 남는다. */
        void collectStepEvents();
        /** @brief 배치가 바뀌었음을 알립니다(자동 계획 · 복원 — 명령은 스스로 올린다). */
        void                    markLayoutChanged() { ++_layoutRevision; }
        CitySimulation&         getMutableCity() { return _city; }
        const GameSpeedControl& getSpeed() const { return _speed; }
        GameSpeedControl&       getMutableSpeed() { return _speed; }
        /** @brief 새 판의 시작 돈입니다(카탈로그 규칙의 통화). */
        int32 getStartingMoney() const { return _startingMoney; }

    private:
        PROPERTY( Category = "Data", DisplayName = "City Data", Tooltip = "City catalog XML" )
        AssetRef<CityCatalog> _cityDataPath;
        PROPERTY( Category = "Economy", DisplayName = "Starting Money", Tooltip = "City money a fresh game starts with (the currency of the city catalog)", Min = 0 )
        int32 _startingMoney;

        PROPERTY( Category = "Time", DisplayName = "Speed Range", Tooltip = "Slowest and fastest time scale and the factor of one step; required" )
        GameSpeedRange _speedRange;

        BoundAssetRef<CityCatalog> _pCatalog;
        CitySimulation             _city;
        ConsumerLease              _cityEventLease; ///< 도시 이벤트를 꺼내는 표(이 디렉터 하나만 꺼낸다)
        vector<CityEvent>          _listStepEvent;  ///< `getStepEvents`
        GameSpeedControl           _speed;          ///< 배속 · 일시정지(디렉터가 쥐고 도시 · 달력이 받는다)
        uint32                     _layoutRevision; ///< `getLayoutRevision`
        DevCounter                 _buildCount;     ///< 감시 값 `City.BuildCount` — 저장하지 않는 이번 판 수
        DevCounter                 _demolishCount;  ///< 감시 값 `City.DemolishCount` — 저장하지 않는 이번 판 수
    };
} // namespace sw

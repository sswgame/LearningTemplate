/**
 * @file HousingLayout.h
 * @brief 부지 하나의 배치(부품 · 변환) · 배치 검증 · 비용과 회수 · 되돌리기를 정의합니다. 씬과 무관한 규칙이라 단위 테스트가 그대로 돈다.
 * @details 저장 정본은 부지 id · 안정 배치 id · 부품 id/판 · 변환이다 — 씬의 임시 `GameObjectHandle` 은 싣지 않고, 뷰(`HousingPlotComponent`)가 이 배치에서 다시 세운다.
 *          언리얼 Fortnite 건축 · 발하임 · 심즈 모두 배치 목록(조각 종류 + 변환)을 저장하고 액터는 불러올 때 다시 만든다(기억에 의존한 비교). 되돌리기는 부지 편집 세션의 것이라
 *          싣지 않는다 — 심즈도 건축 모드를 나가면 되돌리기가 사라진다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/World/Housing/HousingPart.h"
#include "GameFramework/Runtime/World/Land/LandRegistry.h"

namespace sw
{
    class Wallet;

    /** @brief 배치 모드입니다. 격자는 칸 · 90 도로 맞추고, 자유는 자리와 각을 그대로 둡니다(부지와 부품이 모두 허락할 때만). 소켓 스냅은 두 모드 모두 먼저 봅니다. */
    ENUM()
    enum class HousingPlacementMode : uint8
    {
        Grid = 0,
        Free
    };

    /** @brief 배치 · 치우기 · 옮기기 · 되돌리기의 결과입니다. `Ok` 가 아니면 아무것도 바뀌지 않았고 값이 그 까닭입니다(고스트가 보여 준다). */
    ENUM()
    enum class HousingPlaceResult : uint8
    {
        Ok = 0,
        UnknownPart,
        UnknownPlacement,
        OutOfPlot,
        BadLevel,
        Overlap,
        NoSupport,      ///< 바닥 없는 벽 · 가구, 받칠 벽 없는 위층 바닥
        SupportsOthers, ///< 위에 놓인 것을 받치고 있어 치우거나 옮길 수 없다
        FreePlacementNotAllowed,
        NotEnoughFunds,
        NoWallet,
        NothingToUndo,
        NothingToRedo,
        UnknownPlot, ///< 네트워크: 서버에 그 id 의 부지가 없다
        NotOwner,    ///< 네트워크: 요청한 연결이 부지의 주인이 아니다(남의 부지 · 주인 없는 부지) — 차지 요청이면 이미 주인이 있다
        OutOfReach,  ///< 네트워크: 서버가 아는 요청자 자리에서 부지의 `_maxReach` 보다 멀다
        RateLimited, ///< 네트워크: 연결의 틱당 요청 상한을 넘었다 — 판정하지 않았다
        Pending      ///< 네트워크 클라이언트: 요청을 보냈고 서버의 답을 기다린다(아무것도 바뀌지 않았다)
    };

    /** @brief 부지 안의 변환입니다. 자리는 부지 원점 기준 XZ(m, 점유 칸의 가운데), 층은 0 부터, 요는 라디안(0 ~ 2π)입니다. */
    REFLECT()
    struct HousingTransform
    {
        REFLECT_BODY();

        PROPERTY()
        float2 _position{ 0.0f, 0.0f };
        PROPERTY( Min = 0 )
        int32 _level{ 0 };
        PROPERTY( Min = 0 )
        float32 _yaw{ 0.0f }; ///< 위 끝(2π)은 열린 경계라 레이아웃 본문이 본다

        bool operator==( const HousingTransform& other ) const
        {
            return _position._x == other._position._x && _position._y == other._position._y && _level == other._level && _yaw == other._yaw;
        }
    };

    /** @brief 놓인 부품 하나입니다. `_placementID` 는 부지 안에서 다시 쓰지 않는 안정 id 입니다(0 은 없음). */
    REFLECT()
    struct HousingPlacement
    {
        REFLECT_BODY();

        PROPERTY( Min = 1 )
        uint32 _placementID{ 0 };
        PROPERTY()
        hashed_string _partID{};
        PROPERTY()
        int32 _partVersion{ 0 };
        PROPERTY()
        HousingTransform _transform{};
    };

    /** @brief 놓으려는 것입니다. 변환은 조준 · 지면 투영의 날 값이고 스냅은 레이아웃이 합니다. */
    struct HousingCandidate
    {
        hashed_string        _partID{};
        HousingTransform     _transform{};
        HousingPlacementMode _mode{ HousingPlacementMode::Grid };
    };

    /** @brief 검증 결과입니다. 스냅한 변환 · 덮는 칸 · 값과 까닭을 함께 돌려줘 고스트가 그대로 그립니다. */
    struct HousingPlaceCheck
    {
        HousingPlaceResult _result{ HousingPlaceResult::Ok };
        HousingTransform   _transform{};
        int2               _cellMin{};
        int2               _cellMax{};
        int32              _cost{ 0 };
        uint32             _socketPlacementID{ 0 }; ///< 소켓에 붙었으면 그 배치(0 은 격자 · 자유)
    };

    /** @brief 부지 하나의 설정입니다(씬 데이터 — 부지 컴포넌트의 PROPERTY). 칸 크기 · 층 높이는 카탈로그가 정본입니다. */
    REFLECT()
    struct HousingPlotSettings
    {
        REFLECT_BODY();

        PROPERTY( Required )
        hashed_string _plotID{}; ///< 세이브가 부지를 가리키는 id. 다른 부지의 세이브는 읽지 않는다
        PROPERTY( Min = 1, Max = 64 )
        int32 _sizeX{ 8 };
        PROPERTY( Min = 1, Max = 64 )
        int32 _sizeZ{ 8 };
        PROPERTY( Min = 1, Max = 8 )
        int32 _levelCount{ 2 };
        PROPERTY( Min = 1, Max = 1024 )
        int32 _historyLimit{ 64 }; ///< 되돌릴 수 있는 편집 수
        PROPERTY()
        bool _bFreePlacement{ false }; ///< 자유 배치를 허락한다(부품도 허락해야 한다)
        PROPERTY( Min = 0.0, Units = m )
        float32 _maxReach{ 0.0f }; ///< 네트워크: 서버가 아는 요청자 자리에서 편집할 자리까지의 XZ 거리 상한. 0 은 거리 제한이 없는 건축 모드(심즈 · 하우징 화면)다
    };
} // namespace sw

namespace sw
{
    /**
     * @class HousingLayout
     * @brief 부지 하나의 배치 목록과 칸 점유입니다. 놓기 · 치우기 · 옮기기는 검증하고, 값을 지갑에서 받고 돌려주며, 되돌리기 이력에 남깁니다.
     * @details 규칙(첫 판 — 안정성 시뮬레이션은 없다):
     *          - 바닥: 0 층은 땅에, 위층은 덮는 칸 하나 이상이 아래층 구조물(벽) 위여야 한다.
     *          - 구조물 · 가구: 덮는 모든 칸에 같은 층 바닥이 있어야 하고, 구조물 · 가구끼리 칸을 나누지 않는다. 바닥끼리도 나누지 않는다.
     *          - 받치는 것(바닥 위의 벽 · 가구, 벽 위의 바닥)이 남으면 치우거나 옮길 수 없다(`SupportsOthers`).
     *          되돌리기는 편집의 역(놓기 ↔ 치우기, 옮기기 ↔ 옛 자리)이고 값도 그대로 되돌린다(놓기의 역은 낸 값 전부를 돌려준다). 지갑이 모자라면 거절하고 이력은 그대로다.
     *          스레드: 게임 스레드에서만 쓴다.
     */
    REFLECT()
    class SW_KIT_API HousingLayout
    {
    public:
        REFLECT_BODY();

        static constexpr FormatDef kStateFormat{ "HOUS" };

        HousingLayout();

        /** @brief 부지를 엽니다. 배치 · 이력을 비우고 다음 id 를 1 로 둡니다. 카탈로그는 레이아웃보다 오래 살아야 합니다(공유 캐시의 한 벌). */
        void initialize( const HousingPlotSettings& settings, const HousingPartCatalog& catalog );
        /** @brief 빌린 카탈로그만 @p catalog 로 바꿉니다(배치 · 땅 · 지갑은 그대로). 배치가 새 판에 맞는지는 이어 읽는 `readState` 가 본다 — 디렉터의 읽기 길만 부른다. */
        void rebindCatalog( const HousingPartCatalog& catalog ) { _pCatalog = &catalog; }
        /** @brief 값을 내고 받는 지갑입니다(카탈로그 통화). 비면 값이 있는 부품은 `NoWallet` 입니다. */
        void bindWallet( Wallet* pWallet ) { _pWallet = pWallet; }
        /**
         * @brief 공유 땅(`LandRegistry` — 판 상태)에서 부지 전체를 차지합니다. 부지 칸 (0, 0) 이 땅 칸 @p origin 이고 칸 크기가 같아야 합니다(부르는 쪽이 본다).
         * @details 주인 이름은 `Housing.<부지 id>` 다. 막힘이 아닌 차지(다른 키트가 그 칸에 놓지 못하지만 길은 지난다)이고, 남의 칸이 하나라도 있으면 아무것도 차지하지 않고 false 다 —
         *          두 키트(밭 · 도시 · 전장)가 한 맵에서 같은 칸을 쓰지 않는다. @p pLand 가 nullptr 이면 단독 부지(차지 없음)이고 true 다.
         */
        [[nodiscard]] bool bindLand( LandRegistry* pLand, const int2& origin );
        /** @brief 차지한 땅을 반납합니다(부지를 닫을 때). 차지하지 않았으면 아무것도 하지 않는다. */
        void releaseLand();
        /** @brief 공유 땅을 차지했으면 true 입니다. */
        bool hasClaimedLand() const { return _land.isBound(); }

        /** @brief 스냅하고 검증합니다. 아무것도 바꾸지 않습니다(고스트 미리보기). */
        HousingPlaceCheck evaluate( const HousingCandidate& candidate ) const;
        /** @brief 놓습니다. `Ok` 면 @p outPlacementID 에 새 id 를 담습니다. */
        HousingPlaceResult place( const HousingCandidate& candidate, uint32& outPlacementID );
        /** @brief 치우고 부품의 회수 비율만큼(내림) 돌려받습니다. */
        HousingPlaceResult remove( uint32 placementID );
        /** @brief 같은 부품을 새 변환으로 옮깁니다(값 없음). */
        HousingPlaceResult move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode );
        HousingPlaceResult undo();
        HousingPlaceResult redo();
        /** @brief 되돌리기 이력을 비웁니다. 부지의 주인이 바뀌면 앞 주인의 편집을 되돌리지 못하게 한다. */
        void clearHistory();
        /**
         * @brief 다음 되돌리기(@p bRedo 면 다시 하기)가 건드릴 자리를 @p outListTarget 에 더합니다(놓기 · 치우기는 하나, 옮기기는 떠나는 자리와 닿는 자리 둘).
         *        서버가 그 편집을 거리로 판정할 때 쓴다. 할 것이 없으면 아무것도 더하지 않는다.
         */
        void collectHistoryTargets( bool bRedo, vector<HousingTransform>& outListTarget ) const;
        /**
         * @brief 배치 목록을 통째로 바꿉니다(네트워크 클라이언트가 받은 델타를 적용한 목록). 세이브 읽기와 같은 검사(id 순서 · 다음 id · 카탈로그 · 판 · 규칙)를 거치고,
         *        어기면 까닭을 남기고 지금 배치를 그대로 둔 채 false 입니다. 되면 이력을 비우고 판이 오른다.
         */
        [[nodiscard]] bool applyPlacements( uint32 nextPlacementID, vector<HousingPlacement> listPlacement );

        bool                            canUndo() const { return _editCursor > 0; }
        bool                            canRedo() const { return _editCursor < _listEdit.size(); }
        const HousingPlacement*         findPlacement( uint32 placementID ) const;
        const vector<HousingPlacement>& getPlacements() const { return _listPlacement; }
        const HousingPlotSettings&      getSettings() const { return _settings; }
        const HousingPartCatalog*       getCatalog() const { return _pCatalog; }
        uint32                          getNextPlacementID() const { return _nextPlacementID; }
        /** @brief 배치가 바뀔 때마다 오릅니다. 뷰가 다시 맞출 때를 압니다. */
        uint32 getRevision() const { return _revision; }
        /** @brief 부지 기준 3D 자리(층 높이를 더한 바닥)입니다. */
        float3 computeLocalPosition( const HousingTransform& transform ) const;
        /** @brief 부지 기준 광선(@p localOrigin, @p direction)이 층 @p level 의 바닥 평면과 만나는 XZ 입니다(화면 조준 → 지면 투영). 평면과 나란하거나 뒤쪽이면 false 입니다. */
        [[nodiscard]] bool projectRay( const float3& localOrigin, const float3& direction, int32 level, float2& outPosition ) const;

        /**
         * @brief 부지 id · 다음 id · 배치(id, 부품 id, 판, 변환)를 씁니다. 읽을 때 카탈로그로 부품을 찾고(지운 부품은 `replaces` 로 이어받은 부품), 판이 `minLoadVersion` ~ `version` 밖이면,
         *        다른 부지의 세이브이거나 배치가 지금 규칙(부지 밖 · 겹침 · 받침)을 어기면 거절하고 까닭을 남깁니다. 이력은 비웁니다.
         */
        SW_DECLARE_STATE_IO();

    private:
        enum class EditKind : uint8
        {
            Place,
            Remove,
            Move
        };
        /** @brief 이력 한 칸입니다. 놓기는 `_after`, 치우기는 `_before`, 옮기기는 둘 다를 씁니다. `_amount` 는 낸 값(놓기) · 돌려받은 값(치우기)입니다. */
        struct Edit
        {
            EditKind         _kind{ EditKind::Place };
            HousingPlacement _before{};
            HousingPlacement _after{};
            int64            _amount{ 0 };
        };

        /** @brief 소켓 · 격자 스냅을 합니다(검증 없음). 자유 배치를 허락하지 않으면 그 결과입니다. @p ignoreID 의 소켓에는 붙지 않습니다(옮기는 자신). */
        HousingPlaceCheck snap( const HousingPartDef& part, const HousingCandidate& candidate, uint32 ignoreID ) const;
        /** @brief 변환이 덮는 칸(양 끝 포함)입니다. 회전한 점유 상자의 축 정렬 상자로 셉니다. */
        void computeCells( const HousingPartDef& part, const HousingTransform& transform, int2& outMin, int2& outMax ) const;
        /** @brief 층 · 부지 범위 · 겹침 · 받침을 봅니다. @p ignoreID 의 칸은 빈 칸으로 봅니다. */
        HousingPlaceResult validateFootprint( const HousingPartDef& part, const HousingTransform& transform, uint32 ignoreID, int2& outMin, int2& outMax ) const;
        bool               isSupported( const HousingPartDef& part, int32 level, const int2& cellMin, const int2& cellMax, uint32 ignoreID ) const;
        /** @brief @p placement 를 빼면 받침을 잃는 배치가 있으면 true 입니다. */
        bool supportsOthers( const HousingPlacement& placement ) const;
        /** @brief 칸 점유 표의 자리입니다(층 · 층수 · z · x 순). */
        size_t computeCellIndex( HousingPartLayer layer, int32 level, int32 x, int32 z ) const;
        uint32 findOwner( HousingPartLayer layer, int32 level, int32 x, int32 z ) const;
        /** @brief 배치가 덮는 칸에 @p owner 를 씁니다(0 은 비움). */
        void occupy( const HousingPlacement& placement, uint32 owner );

        /** @brief 검증하고 @p charge 를 낸 뒤 넣습니다(놓기 · 치우기의 역). */
        HousingPlaceResult tryInsert( const HousingPlacement& placement, int64 charge );
        /** @brief 받침을 보고 뺀 뒤 @p refund 를 돌려줍니다(치우기 · 놓기의 역). */
        HousingPlaceResult tryErase( uint32 placementID, int64 refund );
        /** @brief 받침과 새 칸을 보고 옮깁니다. */
        HousingPlaceResult tryRelocate( uint32 placementID, const HousingTransform& transform );
        void               recordEdit( const Edit& edit );
        /** @brief 읽은 배치로 칸을 다시 채우며 모두 검증합니다. 어기는 배치가 있으면 까닭을 남기고 false 입니다. */
        [[nodiscard]] bool rebuildOccupancy();
        /** @brief 읽거나 받은 배치 목록을 봅니다 — id 순서 · 다음 id, 카탈로그(지운 부품은 이어받은 부품으로 바꾼다), 판, 그리고 칸 규칙(`rebuildOccupancy`). */
        [[nodiscard]] bool validatePlacements();

    private:
        HousingPlotSettings       _settings;
        const HousingPartCatalog* _pCatalog;
        Wallet*                   _pWallet;
        LandBinding               _land; ///< 빌린 공유 땅(없으면 단독 부지) — 상태에 싣지 않는다(땅은 판 상태가 싣는다)
        PROPERTY( RuntimeState )
        vector<HousingPlacement> _listPlacement; ///< id 오름차순
        vector<uint32>           _listCellOwner; ///< [층(바닥 · 구조물 · 가구)][층수][z][x] 의 배치 id(0 은 빈 칸)
        vector<Edit>             _listEdit;
        uint32                   _editCursor; ///< [0, 이것) 은 되돌릴 수 있고 [이것, 끝) 은 다시 할 수 있다
        PROPERTY( RuntimeState, Min = 1 )
        uint32 _nextPlacementID;
        uint32 _revision;
    };
} // namespace sw

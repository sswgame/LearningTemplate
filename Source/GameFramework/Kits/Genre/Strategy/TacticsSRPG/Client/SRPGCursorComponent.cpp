#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/Client/View/PrefabViewBatch.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    class DebugHUDSectionWriter;
    class GameObjectManager;
    class SRPGGameModeComponent;

    /**
     * @class SRPGCursorComponent
     * @brief `Move` 축을 한 번 기울일 때 커서를 한 칸 옮기고, 확정 액션은 커서 칸 확정(`confirmCell`), 차례 끝 액션은 아군 페이즈 끝(`endPlayerPhase`)으로 디렉터에 넘깁니다.
     */
    REFLECT( Category = "TacticsSRPG", DisplayName = "SRPG Cursor", Tooltip = "Client side of a TacticsSRPG battle: moves the cell cursor from input, sends commands to the SRPG Game Mode on the same object and draws the field" )
    class SRPGCursorComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        SRPGCursorComponent();
        virtual ~SRPGCursorComponent() override;

        void onBeginPlay() override;
        /** @brief 세운 모습을 지웁니다. */
        void onEndPlay() override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getCursorX() const { return _cursor._x; }
        int32 getCursorY() const { return _cursor._y; }

        /** @brief 디렉터 전장 유닛의 지금 레벨 · 경험치와 최근 성장 기록을 씁니다(HUD 섹션 `tacticsgrowth` — 성장 디버거). 디렉터가 없으면 그 줄 하나입니다. */
        void writeGrowthDebugLines( DebugHUDSectionWriter& writer, const GameObjectManager& manager ) const;

    private:
        /** @brief 틱 뒤 게임 스레드: 입력을 읽어 커서 · 명령을 내고 전장을 그립니다. */
        void updateCursor( GameObjectManager& manager, SRPGGameModeComponent& director );
        void submitInput( const GameModeInput& input, SRPGGameModeComponent& director );
        /** @brief `Move` 축을 한 번 기울일 때 커서를 한 칸 옮깁니다(놓았다가 다시 기울여야 다음 칸). */
        void moveCursor( const GameModeInput& input, const SRPGGameModeComponent& director );
        /** @brief 전장과 커서를 보입니다. 칸 (x, y) 는 절대 자리 (x, 0, y) 다. */
        void showField( const SRPGGameModeComponent& director, GameObjectManager& manager );

    private:
        PROPERTY( Category = "Input", DisplayName = "Move Action", Tooltip = "InputMap 2D action that steps the cursor one cell per tilt (required)" )
        hashed_string _moveAction;
        PROPERTY( Required, Category = "Input", DisplayName = "Confirm Action", Tooltip = "InputMap action that confirms the cursor cell: select, wait, move or attack (required)" )
        hashed_string _confirmAction;
        PROPERTY( Required, Category = "Input", DisplayName = "End Turn Action", Tooltip = "InputMap action that ends the player phase (required)" )
        hashed_string _endTurnAction;
        PROPERTY( Category = "View", DisplayName = "Cube Prefab", Required, Tooltip = "1 m cube stretched into the cells and the cursor" )
        AssetRef<PrefabAsset> _cubePrefab;
        PROPERTY( Category = "View", DisplayName = "Sphere Prefab", Required, Tooltip = "1 m diameter sphere scaled to a unit" )
        AssetRef<PrefabAsset> _spherePrefab;
        PROPERTY( Category = "View", DisplayName = "Tile Half Extent", Tooltip = "Half size of one drawn cell; its top is the floor", Units = m )
        float3 _tileHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Tile Color" )
        float4 _tileColor;
        PROPERTY( Category = "View", DisplayName = "Unit Radius", Min = 0.01, Units = m )
        float32 _unitRadius;
        PROPERTY( Category = "View", DisplayName = "Unit Height", Tooltip = "Height of a unit's center above the floor", Units = m )
        float32 _unitHeight;
        PROPERTY( Category = "View", DisplayName = "Player Color" )
        float4 _playerColor;
        PROPERTY( Category = "View", DisplayName = "Selected Color" )
        float4 _selectedColor;
        PROPERTY( Category = "View", DisplayName = "Enemy Color" )
        float4 _enemyColor;
        PROPERTY( Category = "View", DisplayName = "Cursor Half Extent", Units = m )
        float3 _cursorHalfExtent;
        PROPERTY( Category = "View", DisplayName = "Cursor Height", Units = m )
        float32 _cursorHeight;
        PROPERTY( Category = "View", DisplayName = "Cursor Color" )
        float4 _cursorColor;

        SW_TRANSIENT PrefabViewBatch _cubeViews;
        SW_TRANSIENT PrefabViewBatch _unitViews;
        SW_TRANSIENT int2            _cursor;
        SW_TRANSIENT int2            _lastStep;          ///< 지난 프레임의 축 방향(-1 · 0 · 1) — 놓아야 다음 칸을 옮긴다
        SW_TRANSIENT uint8           _bCursorPlaced : 1; ///< 판이 열린 뒤 커서를 첫 아군 칸에 놓았다
        [[maybe_unused]] uint8       _reserved      : 7;
    };

    namespace
    {
        struct SRPGCursorComponentInternal
        {
            /** @brief 축이 이보다 기울어야 커서를 한 칸 옮긴다(스틱 떨림을 칸 이동으로 읽지 않게). */
            static constexpr float32 kCursorThreshold = 0.5f;

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kCursorThreshold )
                    return 1;
                if ( axis < -kCursorThreshold )
                    return -1;
                return 0;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( srpgCursorX, "SRPG.CursorX", "Cursor cell X", SRPGCursorComponent, &SRPGCursorComponent::getCursorX );
    SW_AUTOMATION_GETTER_WATCH( srpgCursorY, "SRPG.CursorY", "Cursor cell Y", SRPGCursorComponent, &SRPGCursorComponent::getCursorY );

    SRPGCursorComponent::SRPGCursorComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&SRPGCursorComponent::updateCursor, &SRPGCursorComponent::submitInput>() )
        , _moveAction{ CommonInputActions::kMove }
        , _confirmAction{}
        , _endTurnAction{}
        , _cubePrefab{}
        , _spherePrefab{}
        , _tileHalfExtent{ 0.48f, 0.05f, 0.48f }
        , _tileColor{ 0.3f, 0.35f, 0.3f, 1.0f }
        , _unitRadius{ 0.35f }
        , _unitHeight{ 0.4f }
        , _playerColor{ 0.2f, 0.6f, 1.0f, 1.0f }
        , _selectedColor{ 0.6f, 0.9f, 1.0f, 1.0f }
        , _enemyColor{ 1.0f, 0.3f, 0.2f, 1.0f }
        , _cursorHalfExtent{ 0.5f, 0.02f, 0.5f }
        , _cursorHeight{ 0.05f }
        , _cursorColor{ 1.0f, 0.95f, 0.3f, 1.0f }
        , _cubeViews{}
        , _unitViews{}
        , _cursor{}
        , _lastStep{}
        , _bCursorPlaced{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SRPGCursorComponent::~SRPGCursorComponent() = default;

    void SRPGCursorComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _bCursorPlaced = SW_FALSE;
        _lastStep      = int2{};
    }

    void SRPGCursorComponent::onEndPlay()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
        {
            _cubeViews.destroyAll( *pManager );
            _unitViews.destroyAll( *pManager );
        }
        GameModeViewComponent::onEndPlay();
    }

    void SRPGCursorComponent::moveCursor( const GameModeInput& input, const SRPGGameModeComponent& director )
    {
        const float2 axis = input.getVector2D( _moveAction );
        const int2   step{ SRPGCursorComponentInternal::computeStep( axis._x ), SRPGCursorComponentInternal::computeStep( axis._y ) };
        const int2   fresh{ step._x != _lastStep._x ? step._x : 0, step._y != _lastStep._y ? step._y : 0 };
        _lastStep = step;
        const int2 moved{ _cursor._x + fresh._x, _cursor._y + fresh._y };
        if ( director.getField().isInside( moved ) )
            _cursor = moved;
    }

    void SRPGCursorComponent::updateCursor( GameObjectManager& manager, SRPGGameModeComponent& director )
    {
        if ( _bCursorPlaced == SW_FALSE )
        {
            // 첫 아군 유닛 칸에서 시작한다(디렉터는 아군을 먼저 놓는다).
            for ( const SRPGUnit& unit : director.getField().getUnits() )
            {
                if ( unit._team == SRPGTeam::Player )
                {
                    _cursor = unit._cell;
                    break;
                }
            }
            _bCursorPlaced = SW_TRUE;
        }
        dispatchInput( manager, director );
        showField( director, manager );
    }

    void SRPGCursorComponent::submitInput( const GameModeInput& input, SRPGGameModeComponent& director )
    {
        moveCursor( input, director );
        // 할 수 없는 칸 · 페이즈 밖 명령은 디렉터가 거절하고 까닭을 남긴다 — 커서는 결과로 바뀌지 않는다.
        if ( input.wasActionTriggered( _confirmAction ) )
            (void)director.confirmCell( _cursor );
        if ( input.wasActionTriggered( _endTurnAction ) )
            (void)director.endPlayerPhase();
    }

    void SRPGCursorComponent::showField( const SRPGGameModeComponent& director, GameObjectManager& manager )
    {
        const SRPGBattlefield& field = director.getField();
        // 칸 (x, y) 를 절대 자리 (x, 0, y) 에 둔다 — 위에서 내려다본다.
        const float3 tileScale = _tileHalfExtent * 2.0f;
        _cubeViews.begin();
        for ( int32 y = 0; y < field.getHeight(); ++y )
        {
            for ( int32 x = 0; x < field.getWidth(); ++x )
            {
                (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( static_cast<float32>( x ), -_tileHalfExtent._y, static_cast<float32>( y ) ) }, tileScale, 0.0f, _tileColor );
            }
        }
        (void)_cubeViews.show( manager, _cubePrefab, AbsolutePosition{ float3( static_cast<float32>( _cursor._x ), _cursorHeight, static_cast<float32>( _cursor._y ) ) }, _cursorHalfExtent * 2.0f, 0.0f,
                               _cursorColor );
        _cubeViews.end( manager );

        const float3 unitScale{ _unitRadius * 2.0f };
        _unitViews.begin();
        for ( size_t index = 0; index < field.getUnits().size(); ++index )
        {
            const SRPGUnit& unit = field.getUnits()[index];
            if ( unit._bAlive == SW_FALSE )
                continue;
            const bool   bSelected = static_cast<int32>( index ) == director.getSelectedUnit();
            const float4 color     = unit._team == SRPGTeam::Player ? ( bSelected ? _selectedColor : _playerColor ) : _enemyColor;
            (void)_unitViews.show( manager, _spherePrefab, AbsolutePosition{ float3( static_cast<float32>( unit._cell._x ), _unitHeight, static_cast<float32>( unit._cell._y ) ) }, unitScale, 0.0f, color );
        }
        _unitViews.end( manager );
    }

    void SRPGCursorComponent::writeGrowthDebugLines( DebugHUDSectionWriter& writer, const GameObjectManager& manager ) const
    {
        const SRPGGameModeComponent* const pDirector = static_cast<const SRPGGameModeComponent*>( findGameMode( manager ) );
        if ( pDirector == nullptr )
        {
            writer.addLine( "target", "none (the cursor has no SRPG game mode)" );
            return;
        }
        const SRPGCatalog* const pCatalog = pDirector->findCatalog();
        const vector<SRPGUnit>&  listUnit = pDirector->getField().getUnits();
        for ( size_t unitIndex = 0; unitIndex < listUnit.size(); ++unitIndex )
        {
            const SRPGUnit&                          unit = listUnit[unitIndex];
            StringBuilder<constant::buffer::kMax256> value;
            value.append( unit._pDef != nullptr ? unit._pDef->_id.c_str() : "?" );
            value.append( unit._bAlive == SW_TRUE ? "" : " (down)" );
            value.appendFormat( " pilot Lv %# xp %#", unit._pilotLevel.getLevel(), unit._pilotLevel.getXP() );
            if ( pCatalog != nullptr )
                value.appendFormat( "/%#", pCatalog->getPilotCurve().getXPToNext( unit._pilotLevel.getLevel() ) );
            value.appendFormat( ", unit Lv %# xp %#", unit._unitLevel.getLevel(), unit._unitLevel.getXP() );
            if ( pCatalog != nullptr )
                value.appendFormat( "/%#", pCatalog->getUnitCurve().getXPToNext( unit._unitLevel.getLevel() ) );
            fixed_string<constant::buffer::kMax32> label;
            formatstring( label.data(), label.capacity(), "unit %#", unitIndex );
            writer.addLine( label.view(), value.view() );
        }
        const uint32 used = static_cast<uint32>( listUnit.size() );
        pDirector->getGrowthLog().writeDebugLines( writer, GameplayDebugTarget::kMaxLines > used ? GameplayDebugTarget::kMaxLines - used : 0u );
    }

    namespace
    {
        /** @brief 성장 디버거(HUD 섹션 `tacticsgrowth`) — 이 판 전장 유닛의 레벨 · 경험치 · 다음까지와 최근 레벨 오름의 출처. 읽기만 한다. */
        struct TacticsGrowthDebugSectionInternal
        {
            static void writeGrowth( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const   pManager = GameplayDebugTarget::findActiveObjectManager();
                const SRPGCursorComponent* const pCursor  = pManager != nullptr ? GameplayDebugTarget::findTargetOrLowestIDComponent<SRPGCursorComponent>( writer.getSceneIterationKey(), *pManager ) : nullptr;
                if ( pCursor == nullptr )
                {
                    writer.addLine( "target", "none (no SRPG cursor in the active scene)" );
                    return;
                }
                pCursor->writeGrowthDebugLines( writer, *pManager );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( tacticsGrowth, "tacticsgrowth", "Tactics growth (levels, xp, sources)", 290, DebugHUDSectionFlag::kNone, &TacticsGrowthDebugSectionInternal::writeGrowth );
} // namespace sw

#include "SRPGCursorComponent.cpp.gen.inl"

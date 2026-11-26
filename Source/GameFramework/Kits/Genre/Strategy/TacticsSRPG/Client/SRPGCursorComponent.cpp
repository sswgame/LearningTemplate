#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Client/SRPGCursorComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
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

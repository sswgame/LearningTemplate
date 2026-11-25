#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Client/SRPGCursorComponent.h"

#include "Core/String/StringBuilder.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"
#include "GameFramework/Runtime/Foundation/Debug/GameplayDebugTarget.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"

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
        : _moveAction{ CommonInputActions::kMove }
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
        , _director{}
        , _cubeViews{}
        , _unitViews{}
        , _cursor{}
        , _lastStep{}
        , _bCursorPlaced{ SW_FALSE }
        , _bInputLive{ SW_FALSE }
        , _bActionsValid{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    SRPGCursorComponent::~SRPGCursorComponent() = default;

    void SRPGCursorComponent::onRegister( GameObjectManager& manager )
    {
        Component::onRegister( manager );
        GameObject* const                  pOwner    = getOwner();
        const SRPGGameModeComponent* const pDirector = pOwner != nullptr ? pOwner->getComponent<SRPGGameModeComponent>() : nullptr;
        if ( pDirector == nullptr )
        {
            SW_LOG_ERROR_ID( "SRPG.CursorWithoutGameMode", "SRPG cursor on '%#': no SRPGGameModeComponent in front of it on the same object - the cursor does nothing",
                             pOwner != nullptr ? pOwner->getName().c_str() : "?" );
            _director = TComponentHandle<SRPGGameModeComponent>{};
            return;
        }
        _director = TComponentHandle<SRPGGameModeComponent>( pDirector );
    }

    void SRPGCursorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _bActionsValid = _moveAction.empty() == false && _confirmAction.empty() == false && _endTurnAction.empty() == false ? SW_TRUE : SW_FALSE;
        if ( _bActionsValid == SW_FALSE )
            SW_LOG_ERROR_ID( "SRPG.CursorActionMissing", "SRPG cursor: move '%#', confirm '%#' and end turn '%#' actions must all be named - the cursor reads no input", _moveAction.c_str(),
                             _confirmAction.c_str(), _endTurnAction.c_str() );
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
        Component::onEndPlay();
    }

    void SRPGCursorComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        _bInputLive = context.getDeltaTime().count() > 0.0f ? SW_TRUE : SW_FALSE; // 자기 칸 쓰기 — 디렉터 명령은 틱 뒤에 낸다
        context.deferPostTickOnComponent( *this, []( SRPGCursorComponent& cursor, GameObjectManager& manager )
        { cursor.updateCursor( manager ); } );
    }

    void SRPGCursorComponent::moveCursor( const SRPGGameModeComponent& director )
    {
        const InputManager* const pInput = game::findSubsystem<InputManager>();
        if ( pInput == nullptr )
            return;
        const float2 axis = GameInputView( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() ).getVector2D( _moveAction );
        const int2   step{ SRPGCursorComponentInternal::computeStep( axis._x ), SRPGCursorComponentInternal::computeStep( axis._y ) };
        const int2   fresh{ step._x != _lastStep._x ? step._x : 0, step._y != _lastStep._y ? step._y : 0 };
        _lastStep = step;
        const int2 moved{ _cursor._x + fresh._x, _cursor._y + fresh._y };
        if ( director.getField().isInside( moved ) )
            _cursor = moved;
    }

    void SRPGCursorComponent::updateCursor( GameObjectManager& manager )
    {
        SRPGGameModeComponent* const pDirector = manager.resolveComponent( _director );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
            return;
        if ( _bCursorPlaced == SW_FALSE )
        {
            // 첫 아군 유닛 칸에서 시작한다(디렉터는 아군을 먼저 놓는다).
            for ( const SRPGUnit& unit : pDirector->getField().getUnits() )
            {
                if ( unit._team == SRPGTeam::Player )
                {
                    _cursor = unit._cell;
                    break;
                }
            }
            _bCursorPlaced = SW_TRUE;
        }
        const bool bReadInput = _bInputLive == SW_TRUE && _bActionsValid == SW_TRUE && pDirector->isAwaitingCommand();
        if ( bReadInput )
        {
            moveCursor( *pDirector );
            const InputManager* const pInput = game::findSubsystem<InputManager>();
            if ( pInput != nullptr )
            {
                const GameInputView input( pInput->getInputMap(), game::findSubsystem<IInputCaptureQuery>() );
                // 할 수 없는 칸 · 페이즈 밖 명령은 디렉터가 거절하고 까닭을 남긴다 — 커서는 결과로 바뀌지 않는다.
                if ( input.wasActionTriggered( _confirmAction ) )
                    (void)pDirector->confirmCell( _cursor );
                if ( input.wasActionTriggered( _endTurnAction ) )
                    (void)pDirector->endPlayerPhase();
            }
        }
        showField( *pDirector, manager );
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
        const SRPGGameModeComponent* const pDirector = manager.resolveComponent( _director );
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

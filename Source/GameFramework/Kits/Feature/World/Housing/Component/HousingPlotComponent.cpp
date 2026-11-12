#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/ComponentStateStore.h"
#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"

namespace sw
{
    namespace
    {
        struct HousingPlotInternal
        {
            /** @brief 부지 둘이 칸 경계에서 맞닿는 것은 겹침이 아니다(m). */
            static constexpr float32 kTouchEpsilon = 1.0e-3f;

            /** @brief 활성 씬에서 이름 @p objectName 의 열린 부지입니다. 없으면 nullptr 이고 부르는 쪽이 `makeNoPlotReply` 로 답한다. */
            static HousingPlotComponent* findPlot( const string& objectName )
            {
                GameObject*           pObject = engine::findActiveObject( hashed_string( objectName ) );
                HousingPlotComponent* pPlot   = pObject != nullptr ? pObject->getComponent<HousingPlotComponent>() : nullptr;
                return pPlot != nullptr && pPlot->isStarted() ? pPlot : nullptr;
            }

            /** @brief 그 이름의 열린 부지가 없을 때의 답입니다. */
            static string makeNoPlotReply( const string& objectName )
            {
                return "no open housing plot named '" + objectName + "' in the active scene";
            }

            /** @brief `<x> <z> <level> <yawDegrees> [free]` 를 @p first 자리부터 읽습니다. 날 변환(부지 기준)과 모드입니다. */
            [[nodiscard]] static bool readTransform( const vector<string>& listArgument, size_t first, HousingTransform& outTransform, HousingPlacementMode& outMode )
            {
                if ( listArgument.size() != first + 4 && listArgument.size() != first + 5 )
                    return false;
                float32    yawDegrees = 0.0f;
                const bool bRead      = StringUtil::parseFloat( listArgument[first], outTransform._position._x ) && StringUtil::parseFloat( listArgument[first + 1], outTransform._position._y ) &&
                                   StringUtil::parseInt( listArgument[first + 2], outTransform._level ) && StringUtil::parseFloat( listArgument[first + 3], yawDegrees );
                if ( bRead == false )
                    return false;
                outTransform._yaw = yawDegrees * MathUtil::kDegreeToRadian;
                outMode           = HousingPlacementMode::Grid;
                if ( listArgument.size() == first + 5 )
                {
                    if ( listArgument[first + 4] != "free" )
                        return false;
                    outMode = HousingPlacementMode::Free;
                }
                return true;
            }

            /** @brief 결과 한 줄입니다(개발 명령 답). */
            static string describe( const HousingPlotComponent& plot, HousingPlaceResult result )
            {
                StringBuilder<constant::buffer::kMax256> resultBuilder;
                resultBuilder.appendFormat( "%# placements %# funds %#", getEnumName( result ), plot.getLayout().getPlacements().size(), plot.getFunds() );
                return string( resultBuilder.view() );
            }

            /** @brief 배치 id 인자입니다(1 이상). */
            [[nodiscard]] static bool readPlacementID( const string& argument, uint32& outPlacementID )
            {
                int32 value = 0;
                if ( StringUtil::parseInt( argument, value ) == false || value <= 0 )
                    return false;
                outPlacementID = static_cast<uint32>( value );
                return true;
            }

            // ---- 개발 명령(시나리오 · 콘솔의 건축 모드 입력) ----

            /** @brief `housing.preview <plot> <part> <x> <z> <level> <yawDegrees> [free]` — 고스트를 세우고 까닭을 답한다. */
            static bool runPreview( const vector<string>& listArgument, string& outReply )
            {
                HousingCandidate candidate;
                if ( listArgument.size() < 2 || readTransform( listArgument, 2, candidate._transform, candidate._mode ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                if ( pPlot == nullptr )
                {
                    outReply = makeNoPlotReply( listArgument[0] );
                    return true;
                }
                candidate._partID                              = hashed_string( listArgument[1] );
                const HousingPlaceCheck                  check = pPlot->previewPlacement( candidate );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "%# at %#,%# level %# cost %#", getEnumName( check._result ), check._transform._position._x, check._transform._position._y, check._transform._level, check._cost );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `housing.place <plot> <part> <x> <z> <level> <yawDegrees> [free]` */
            static bool runPlace( const vector<string>& listArgument, string& outReply )
            {
                HousingCandidate candidate;
                if ( listArgument.size() < 2 || readTransform( listArgument, 2, candidate._transform, candidate._mode ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                if ( pPlot == nullptr )
                {
                    outReply = makeNoPlotReply( listArgument[0] );
                    return true;
                }
                candidate._partID                               = hashed_string( listArgument[1] );
                uint32                                   newID  = 0;
                const HousingPlaceResult                 result = pPlot->place( candidate, newID );
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "id %# - %#", newID, describe( *pPlot, result ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /**
             * @brief `housing.fill <plot> <part> <level>` — 층 @p level 의 칸마다(가운데, 격자 · 요 0) 부품을 놓아 본다. 대량 가구 부하 시나리오의 입력이다.
             * @details 놓인 수와 마지막 결과를 답한다. 겹침 · 받침 · 값이 막은 칸은 건너뛴다(규칙은 `place` 그대로 — 우회하지 않는다).
             */
            static bool runFill( const vector<string>& listArgument, string& outReply )
            {
                int32 level = 0;
                if ( listArgument.size() != 3 || StringUtil::parseInt( listArgument[2], level ) == false || level < 0 )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                if ( pPlot == nullptr )
                {
                    outReply = makeNoPlotReply( listArgument[0] );
                    return true;
                }
                const HousingPlotSettings& settings = pPlot->getLayout().getSettings();
                const HousingPartCatalog&  catalog  = *pPlot->getLayout().getCatalog(); // 열린 부지는 카탈로그가 있다(findPlot 이 본다)
                const HousingPartDef*      pPart    = catalog.findPart( hashed_string( listArgument[1] ) );
                if ( pPart == nullptr )
                {
                    outReply = "housing: no part '" + listArgument[1] + "' in the plot's catalog";
                    return true;
                }
                const float32    cellSize = catalog.getRoot()._cellSize;
                HousingCandidate candidate;
                candidate._partID           = pPart->_id;
                candidate._mode             = HousingPlacementMode::Grid;
                candidate._transform._level = level;
                uint32             placed   = 0;
                HousingPlaceResult result   = HousingPlaceResult::Ok;
                // 부품 크기 걸음으로 칸을 덮는다(회전 0) — 놓이는 수가 부지 크기로 정해진다(시나리오가 수를 본다).
                for ( int32 z = 0; z + pPart->_sizeZ <= settings._sizeZ; z += pPart->_sizeZ )
                {
                    for ( int32 x = 0; x + pPart->_sizeX <= settings._sizeX; x += pPart->_sizeX )
                    {
                        candidate._transform._position = float2{ ( static_cast<float32>( x ) + static_cast<float32>( pPart->_sizeX ) * 0.5f ) * cellSize,
                                                                 ( static_cast<float32>( z ) + static_cast<float32>( pPart->_sizeZ ) * 0.5f ) * cellSize };
                        uint32 newID                   = 0;
                        result                         = pPlot->place( candidate, newID );
                        placed += result == HousingPlaceResult::Ok ? 1 : 0;
                    }
                }
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "filled %# - %#", placed, describe( *pPlot, result ) );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `housing.remove <plot> <placementID>` */
            static bool runRemove( const vector<string>& listArgument, string& outReply )
            {
                uint32 placementID = 0;
                if ( listArgument.size() != 2 || readPlacementID( listArgument[1], placementID ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( describe( *pPlot, pPlot->remove( placementID ) ) ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            /** @brief `housing.move <plot> <placementID> <x> <z> <level> <yawDegrees> [free]` */
            static bool runMove( const vector<string>& listArgument, string& outReply )
            {
                uint32               placementID = 0;
                HousingTransform     transform;
                HousingPlacementMode mode = HousingPlacementMode::Grid;
                if ( listArgument.size() < 2 || readPlacementID( listArgument[1], placementID ) == false || readTransform( listArgument, 2, transform, mode ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( describe( *pPlot, pPlot->move( placementID, transform, mode ) ) ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            /** @brief `housing.undo <plot>` */
            static bool runUndo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( describe( *pPlot, pPlot->undo() ) ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            /** @brief `housing.redo <plot>` */
            static bool runRedo( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( describe( *pPlot, pPlot->redo() ) ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            /** @brief `housing.save <plot> <file>` — 부지 상태를 절대 경로(시나리오의 `{out}/…`)에 쓴다. */
            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                string path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( pPlot->saveToFile( path ) ? "housing: saved " + path : "housing: save failed (see the log)" ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            /** @brief `housing.load <plot> <file>` — 부지 상태를 바꾸고 뷰를 다시 세운다. */
            static bool runLoad( const vector<string>& listArgument, string& outReply )
            {
                string path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                outReply                    = pPlot != nullptr ? string( pPlot->loadFromFile( path ) ? "housing: loaded " + path : "housing: load failed (see the log)" ) : makeNoPlotReply( listArgument[0] );
                return true;
            }

            // ---- 감시 값(대상은 부지 오브젝트) ----

            static const HousingPlotComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*           pTarget = query.findTargetObject();
                const HousingPlotComponent* pPlot   = pTarget != nullptr ? pTarget->getComponent<HousingPlotComponent>() : nullptr;
                if ( pPlot == nullptr )
                    outValue.setText( pTarget == nullptr ? query.getTargetProblem() : query.describeMissing( "housing plot" ) );
                return pPlot;
            }

            /** @brief 감시 값 `Housing.PlacementCount` 입니다. */
            [[nodiscard]] static bool readPlacementCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlot->getLayout().getPlacements().size() ) );
                return true;
            }

            /** @brief 감시 값 `Housing.ViewCount` 입니다. 서 있는 부품 뷰 수(배치 수와 같아야 한다). */
            [[nodiscard]] static bool readViewCount( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlot->getViewCount() ) );
                return true;
            }

            /** @brief 감시 값 `Housing.Funds` 입니다. 카탈로그 통화의 잔액입니다. */
            [[nodiscard]] static bool readFunds( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pPlot->getFunds() ) );
                return true;
            }

            /** @brief 감시 값 `Housing.LastResult` 입니다. 마지막 미리보기 · 편집의 결과 이름(배치 불가 까닭)입니다. */
            [[nodiscard]] static bool readLastResult( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setText( getEnumName( pPlot->getLastResult() ) );
                return true;
            }

            /** @brief 감시 값 `Housing.GhostPlaceable` 입니다. 고스트가 서 있고 놓을 수 있으면 1 입니다. */
            [[nodiscard]] static bool readGhostPlaceable( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setNumber( pPlot->isGhostPlaceable() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Housing.LandBound` 입니다. 부지가 판 상태의 공유 땅을 차지했으면 1 입니다. */
            [[nodiscard]] static bool readLandBound( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                outValue.setNumber( pPlot->getLayout().hasClaimedLand() ? 1.0 : 0.0 );
                return true;
            }

            /** @brief 감시 값 `Housing.Placement` 입니다. 배치 id `key` 의 "x(cm) z(cm) 층 요(도)" 입니다(스냅 결과). 없는 id 면 값이 없습니다. */
            [[nodiscard]] static bool readPlacement( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const HousingPlotComponent* pPlot = findTarget( query, outValue );
                if ( pPlot == nullptr )
                    return false;
                uint32                  placementID = 0;
                const HousingPlacement* pPlacement  = readPlacementID( string( query._key ), placementID ) ? pPlot->getLayout().findPlacement( placementID ) : nullptr;
                if ( pPlacement == nullptr )
                {
                    outValue.setText( "no such placement" );
                    return false;
                }
                // 정수(cm · 도)로 적는다 — 실수 표기가 바뀌어도 시나리오의 글 비교가 흔들리지 않는다.
                const HousingTransform&                  transform = pPlacement->_transform;
                const int32                              xCm       = static_cast<int32>( MathUtil::round( transform._position._x * 100.0f ) );
                const int32                              zCm       = static_cast<int32>( MathUtil::round( transform._position._y * 100.0f ) );
                const int32                              yawDegree = static_cast<int32>( MathUtil::round( transform._yaw * MathUtil::kRadianToDegree ) );
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# %# %# %#", xCm, zCm, transform._level, yawDegree );
                outValue.setText( textBuilder.view() );
                return true;
            }
        };

        /** @brief 배치 · 지갑을 모든 게임의 상태 스냅샷에 싣고, 저장 전에 부품 뷰 · 고스트를 걷는다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<HousingPlotComponent> s_stateRegistration{ "HousingPlotComponent" };
    } // namespace

    SW_DEV_COMMAND( HousingPreview, "housing.preview", "housing.preview <plot> <part> <x> <z> <level> <yawDegrees> [free]", "Show the ghost of a part on a housing plot and why it can or cannot go there",
                    &HousingPlotInternal::runPreview );
    SW_DEV_COMMAND( HousingPlace, "housing.place", "housing.place <plot> <part> <x> <z> <level> <yawDegrees> [free]", "Place a catalog part on a housing plot (snap, cost)", &HousingPlotInternal::runPlace );
    SW_DEV_COMMAND( HousingFill, "housing.fill", "housing.fill <plot> <part> <level>", "Try a part at the centre of every cell of one level (bulk furniture load)", &HousingPlotInternal::runFill );
    SW_DEV_COMMAND( HousingRemove, "housing.remove", "housing.remove <plot> <placementID>", "Remove a placement from a housing plot and refund it", &HousingPlotInternal::runRemove );
    SW_DEV_COMMAND( HousingMove, "housing.move", "housing.move <plot> <placementID> <x> <z> <level> <yawDegrees> [free]", "Move a placement on a housing plot", &HousingPlotInternal::runMove );
    SW_DEV_COMMAND( HousingUndo, "housing.undo", "housing.undo <plot>", "Undo the last housing edit", &HousingPlotInternal::runUndo );
    SW_DEV_COMMAND( HousingRedo, "housing.redo", "housing.redo <plot>", "Redo the last undone housing edit", &HousingPlotInternal::runRedo );
    SW_DEV_COMMAND( HousingSave, "housing.save", "housing.save <plot> <file>", "Write a housing plot's layout and funds to a file", &HousingPlotInternal::runSave );
    SW_DEV_COMMAND( HousingLoad, "housing.load", "housing.load <plot> <file>", "Replace a housing plot's layout and funds from a file and rebuild its parts", &HousingPlotInternal::runLoad );

    SW_AUTOMATION_QUERY_WATCH( housingPlacementCount, "Housing.PlacementCount", "Placements on the target housing plot", &HousingPlotInternal::readPlacementCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingViewCount, "Housing.ViewCount", "Part objects standing for the target housing plot", &HousingPlotInternal::readViewCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingFunds, "Housing.Funds", "Balance of the target housing plot's wallet", &HousingPlotInternal::readFunds, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingLastResult, "Housing.LastResult", "Result of the target housing plot's last preview or edit", &HousingPlotInternal::readLastResult, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingGhostPlaceable, "Housing.GhostPlaceable", "1 when the target housing plot's ghost stands where the part can go", &HousingPlotInternal::readGhostPlaceable, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingLandBound, "Housing.LandBound", "1 when the target housing plot claimed its cells on the shared land", &HousingPlotInternal::readLandBound, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingPlacement, "Housing.Placement", "\"xCm zCm level yawDegrees\" of placement id key on the target housing plot", &HousingPlotInternal::readPlacement, true, true );
} // namespace sw

namespace sw
{
    HousingPlotComponent::HousingPlotComponent()
        : _settings{}
        , _partCatalog{}
        , _startingFunds{ 0 }
        , _ghostPlaceablePrefab{}
        , _ghostBlockedPrefab{}
        , _pCatalog{}
        , _layout{}
        , _wallet{}
        , _listView{}
        , _ghostCandidate{}
        , _ghostCheck{}
        , _ghost{}
        , _viewRevision{ 0 }
        , _lastResult{ HousingPlaceResult::Ok }
        , _bGhostShown{ SW_FALSE }
        , _bGhostDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    HousingPlotComponent::~HousingPlotComponent() = default;

    bool HousingPlotComponent::startGame()
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getPrimarySceneComponent() == nullptr )
        {
            SW_LOG_ERROR_ID( "HousingPlot.PlotNeedsSceneComponent", "Housing plot '%#': the plot object needs a scene component - its position is the plot origin", _settings._plotID.c_str() );
            return false;
        }
        if ( _ghostPlaceablePrefab.empty() || _ghostBlockedPrefab.empty() )
        {
            SW_LOG_ERROR_ID( "HousingPlot.GhostPrefabMissing", "Housing plot '%#': both ghost prefabs are required - the plot cannot open", _settings._plotID.c_str() );
            return false;
        }
        if ( acquireRequired( _partCatalog.path(), _pCatalog ) == false )
            return false;
        _layout.initialize( _settings, *_pCatalog );
        _layout.bindWallet( &_wallet );
        // 판 상태(같은 오브젝트의 SessionStateComponent)가 있으면 그 공유 땅에서 부지를 차지한다 — 밭 · 도시 · 전장 키트와 한 맵의 칸을 나눈다.
        // 판 상태가 없는 씬은 단독 부지다(땅이 없는 게임 — 선택 연동이지 빠진 필수 값이 아니다).
        SessionStateComponent* pState = SessionStateComponent::findOnOwner( *this );
        if ( pState != nullptr && pState->isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "HousingPlot.SessionStateNotOpen", "Housing plot '%#': the session state on this object is not open yet - put the plot after it so the plot can claim the shared land",
                             _settings._plotID.c_str() );
            return false;
        }
        if ( pState != nullptr && pState->getLand().isInitialized() )
        {
            LandRegistry& land = pState->getLand();
            if ( MathUtil::nearEqual( land.getCellSize(), _pCatalog->getRoot()._cellSize ) == false )
            {
                SW_LOG_ERROR_ID( "HousingPlot.LandCellSizeMismatch", "Housing plot '%#': the shared land cell (%#) and the catalog cell (%#) differ - the plot cannot claim land", _settings._plotID.c_str(),
                                 land.getCellSize(), _pCatalog->getRoot()._cellSize );
                return false;
            }
            if ( _layout.bindLand( &land, land.computeCell( getPlotOrigin() ) ) == false )
                return false;
        }
        _wallet.clear();
        _wallet.setBalance( _pCatalog->getRoot()._currency, _startingFunds );
        _lastResult = HousingPlaceResult::Ok;
        return true;
    }

    void HousingPlotComponent::onEndPlay()
    {
        _layout.releaseLand(); // 판 상태가 같은 오브젝트에서 먼저 닫혀도 땅 칸만 비운다(땅 리비전은 다음 판이 다시 칠한다)
        GameModeComponent::onEndPlay();
    }

    void HousingPlotComponent::serializeState( StateIO& io )
    {
        io.header( kStateTag, kStateVersion );
        if ( io.isWriting() )
        {
            io.value( _layout );
            io.value( _wallet );
            return;
        }
        // 컴포넌트는 복사하지 않는다 — 지역 사본에 읽고 끝까지 맞으면 옮긴다(반쯤 읽은 부지를 남기지 않는다).
        HousingLayout layout = _layout;
        Wallet        wallet = _wallet;
        io.value( layout );
        io.value( wallet );
        if ( io.isOk() == false )
            return;
        _layout = std::move( layout );
        _wallet = std::move( wallet );
        _layout.bindWallet( &_wallet );
    }

    void HousingPlotComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored == false )
            SW_LOG_ERROR_ID( "HousingPlot.StateNotRestored", "Housing plot '%#': the saved state could not be read - the plot keeps its current layout", _settings._plotID.c_str() );
    }

    bool HousingPlotComponent::saveToFile( string_view absolutePath ) const
    {
        Archive archive;
        writeState( archive );
        if ( FileUtil::writeFile( absolutePath, archive.getData(), archive.getSize() ) )
            return true;
        SW_LOG_ERROR_ID( "HousingPlot.SaveWriteFailed", "Housing plot '%#': cannot write '%#'", _settings._plotID.c_str(), absolutePath );
        return false;
    }

    bool HousingPlotComponent::loadFromFile( string_view absolutePath )
    {
        vector<uint8> bytes;
        if ( FileUtil::readFile( absolutePath, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "HousingPlot.LoadReadFailed", "Housing plot '%#': cannot read '%#'", _settings._plotID.c_str(), absolutePath );
            return false;
        }
        Archive archive( bytes.data(), bytes.size() );
        if ( readState( archive ) == false )
        {
            onStateRestored( false );
            return false;
        }
        _lastResult = HousingPlaceResult::Ok;
        despawnViews(); // 같은 입력에서 같은 오브젝트 — 다음 틱이 지금 배치대로 모두 다시 세운다(옛 뷰가 남아 겹치지 않는다)
        return true;
    }

    int64 HousingPlotComponent::getFunds() const
    {
        return _pCatalog != nullptr ? _wallet.getBalance( _pCatalog->getRoot()._currency ) : 0;
    }

    uint32 HousingPlotComponent::getViewCount() const
    {
        const GameObjectManager* pManager = getObjectManager();
        uint32                   count    = 0;
        for ( const PartView& view : _listView )
        {
            if ( pManager != nullptr && pManager->resolveGameObject( view._object ) != nullptr )
                ++count;
        }
        return count;
    }

    float3 HousingPlotComponent::getPlotOrigin() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        return pRoot != nullptr ? pRoot->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f }; // 열린 부지는 startGame 이 루트를 확인했다
    }

    bool HousingPlotComponent::tryGetPlotGrid( float3& outOrigin, float32& outCellSize ) const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getPrimarySceneComponent() == nullptr )
            return false;
        shared_ptr<const HousingPartCatalog> pCatalog = _pCatalog;
        if ( pCatalog == nullptr && _partCatalog.empty() == false )
            pCatalog = _partCatalog.acquire();
        if ( pCatalog == nullptr )
            return false;
        outOrigin   = getPlotOrigin();
        outCellSize = pCatalog->getRoot()._cellSize;
        return true;
    }

    float2 HousingPlotComponent::computeLocalPosition( const float3& worldPosition ) const
    {
        const float3 origin = getPlotOrigin();
        return float2{ worldPosition._x - origin._x, worldPosition._z - origin._z };
    }

    // ------------------------------------------------------------------------------
    // 편집
    // ------------------------------------------------------------------------------
    HousingPlaceCheck HousingPlotComponent::previewPlacement( const HousingCandidate& candidate )
    {
        _ghostCandidate = candidate;
        _ghostCheck     = _layout.evaluate( candidate );
        _lastResult     = _ghostCheck._result;
        _bGhostShown    = SW_TRUE;
        _bGhostDirty    = SW_TRUE;
        scheduleFlush();
        return _ghostCheck;
    }

    void HousingPlotComponent::clearPreview()
    {
        _bGhostShown = SW_FALSE;
        _bGhostDirty = SW_TRUE;
        scheduleFlush();
    }

    HousingPlaceResult HousingPlotComponent::finishEdit( HousingPlaceResult result )
    {
        _lastResult = result;
        if ( _bGhostShown == SW_TRUE )
        {
            _ghostCheck  = _layout.evaluate( _ghostCandidate ); // 놓은 뒤 같은 자리는 이제 겹친다 — 고스트도 새 까닭을 보인다
            _bGhostDirty = SW_TRUE;
        }
        scheduleFlush();
        return result;
    }

    HousingPlaceResult HousingPlotComponent::place( const HousingCandidate& candidate, uint32& outPlacementID )
    {
        return finishEdit( _layout.place( candidate, outPlacementID ) );
    }

    HousingPlaceResult HousingPlotComponent::remove( uint32 placementID )
    {
        return finishEdit( _layout.remove( placementID ) );
    }

    HousingPlaceResult HousingPlotComponent::move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode )
    {
        return finishEdit( _layout.move( placementID, rawTransform, mode ) );
    }

    HousingPlaceResult HousingPlotComponent::undo()
    {
        return finishEdit( _layout.undo() );
    }

    HousingPlaceResult HousingPlotComponent::redo()
    {
        return finishEdit( _layout.redo() );
    }

    // ------------------------------------------------------------------------------
    // 뷰(틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    bool HousingPlotComponent::hasPendingSpawn() const
    {
        return _viewRevision != _layout.getRevision() || _bGhostDirty == SW_TRUE;
    }

    void HousingPlotComponent::onViewsDespawned()
    {
        // 베이스가 세운 것을 모두 지웠다 — 핸들만 비우고, 다음 플러시가 지금 배치와 미리보기대로 다시 세운다.
        _listView.clear();
        _ghost       = GameObjectHandle{};
        _bGhostDirty = _bGhostShown;
    }

    void HousingPlotComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            onViewsDespawned();
        syncPartViews( manager );
        syncGhost( manager );
    }

    void HousingPlotComponent::placeObject( GameObject& object, const HousingTransform& transform ) const
    {
        SceneComponent* pRoot = object.getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return;
        pRoot->setWorldPosition( getPlotOrigin() + _layout.computeLocalPosition( transform ) );
        pRoot->setLocalRotation( float3{ 0.0f, transform._yaw, 0.0f } );
    }

    void HousingPlotComponent::syncPartViews( GameObjectManager& manager )
    {
        // 배치와 뷰는 모두 id 오름차순이다 — 한 번 훑어 사라진 · 옮긴 뷰를 지우고 없는 뷰를 세운다(대량 배치에서도 O(n)).
        _viewRevision                                 = _layout.getRevision();
        const vector<HousingPlacement>& listPlacement = _layout.getPlacements();
        vector<PartView>                listNext;
        listNext.reserve( listPlacement.size() );
        size_t viewIndex = 0;
        for ( const HousingPlacement& placement : listPlacement )
        {
            while ( viewIndex < _listView.size() && _listView[viewIndex]._placementID < placement._placementID )
                destroySpawned( manager, _listView[viewIndex++]._object );
            if ( viewIndex < _listView.size() && _listView[viewIndex]._placementID == placement._placementID )
            {
                PartView& view = _listView[viewIndex++];
                if ( view._transform == placement._transform && manager.resolveGameObject( view._object ) != nullptr )
                {
                    listNext.push_back( view );
                    continue;
                }
                destroySpawned( manager, view._object );
            }
            const HousingPartDef*                    pPart = _pCatalog->findPart( placement._partID );
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "%#_Part%#", getOwner()->getName().c_str(), placement._placementID );
            const string name{ nameBuilder.view() };
            GameObject*  pObject = pPart != nullptr ? spawnPrefab( manager, pPart->_prefabPath, name.c_str() ) : nullptr;
            if ( pObject == nullptr )
            {
                SW_LOG_ERROR_ID( "HousingPlot.PartPrefabNotSpawned", "Housing plot '%#': placement %# of part '%#' could not spawn its prefab", _settings._plotID.c_str(), placement._placementID,
                                 placement._partID.c_str() );
                continue;
            }
            placeObject( *pObject, placement._transform );
            PartView view;
            view._placementID = placement._placementID;
            view._transform   = placement._transform;
            view._object      = pObject->getHandle();
            listNext.push_back( view );
        }
        while ( viewIndex < _listView.size() )
            destroySpawned( manager, _listView[viewIndex++]._object );
        _listView = std::move( listNext );
    }

    void HousingPlotComponent::syncGhost( GameObjectManager& manager )
    {
        if ( _bGhostDirty == SW_FALSE )
            return;
        _bGhostDirty = SW_FALSE;
        destroySpawned( manager, _ghost );
        if ( _bGhostShown == SW_FALSE )
            return;
        const bool                               bPlaceable = _ghostCheck._result == HousingPlaceResult::Ok;
        StringBuilder<constant::buffer::kMax256> nameBuilder;
        nameBuilder.appendFormat( "%#_Ghost", getOwner()->getName().c_str() );
        const string    name{ nameBuilder.view() };
        GameObject*     pGhost = spawnPrefab( manager, bPlaceable ? _ghostPlaceablePrefab.path() : _ghostBlockedPrefab.path(), name.c_str() );
        SceneComponent* pRoot  = pGhost != nullptr ? pGhost->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
        {
            SW_LOG_ERROR_ID( "HousingPlot.GhostPrefabNotSpawned", "Housing plot '%#': the ghost prefab could not spawn or has no scene component", _settings._plotID.c_str() );
            return;
        }
        _ghost = pGhost->getHandle();
        placeObject( *pGhost, _ghostCheck._transform );
        // 고스트 상자는 부품의 점유 상자 크기다(모르는 부품이면 한 칸). 요는 루트에 들어가 상자도 함께 돈다.
        const HousingPartDef* pPart    = _pCatalog->findPart( _ghostCandidate._partID );
        const float32         cellSize = _pCatalog->getRoot()._cellSize;
        const float3          size     = pPart != nullptr ? float3{ static_cast<float32>( pPart->_sizeX ) * cellSize, pPart->_height, static_cast<float32>( pPart->_sizeZ ) * cellSize }
                                                          : float3{ cellSize, cellSize, cellSize };
        pRoot->setLocalScale( size );
    }

    // ------------------------------------------------------------------------------
    // 에디터 저작 검증(맵 검사)
    // ------------------------------------------------------------------------------
    void HousingPlotComponent::validatePlot( ValidationContext& context ) const
    {
        using Internal = HousingPlotInternal;
        if ( _ghostPlaceablePrefab.empty() || _ghostBlockedPrefab.empty() )
            context.addError( "both ghost prefabs are required" );
        if ( _partCatalog.empty() )
        {
            context.addError( "no part catalog" );
            return;
        }
        const shared_ptr<const HousingPartCatalog> pCatalog = _partCatalog.acquire();
        if ( pCatalog == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "part catalog '%#' cannot be loaded", _partCatalog.path() );
            context.addError( textBuilder.view() );
            return;
        }
        // 스냅 지점 · 판: 카탈로그 읽기와 같은 검사다(읽기를 지난 카탈로그는 비어 있다 — 핫 리로드 전 편집 사본을 보는 길이 같다).
        vector<string> listProblem;
        for ( const HousingPartDef& part : pCatalog->getParts() )
        {
            (void)pCatalog->checkPart( part, listProblem );
            const bool bFits        = part._sizeX <= _settings._sizeX && part._sizeZ <= _settings._sizeZ;
            const bool bFitsRotated = part._sizeZ <= _settings._sizeX && part._sizeX <= _settings._sizeZ;
            if ( bFits == false && bFitsRotated == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "part '%#' (%#x%#) never fits this %#x%# plot", part._id.c_str(), part._sizeX, part._sizeZ, _settings._sizeX, _settings._sizeZ );
                listProblem.push_back( string( textBuilder.view() ) );
            }
        }
        for ( const string& problem : listProblem )
            context.addError( problem );
        // 부지 경계: 같은 씬의 다른 부지와 겹치거나 같은 id 를 쓰면 세이브가 남의 부지에 들어간다.
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( pOwner->getPrimarySceneComponent() == nullptr )
        {
            context.addError( "the plot object needs a scene component - its position is the plot origin" );
            return;
        }
        const GameObjectManager* pManager = pOwner->getManager();
        if ( pManager == nullptr )
            return;
        const float32 cellSize = pCatalog->getRoot()._cellSize;
        const float3  origin   = getPlotOrigin();
        for ( const HousingPlotComponent* pOther : pManager->getComponentRegistry().getAll<HousingPlotComponent>() )
        {
            const GameObject* pOtherOwner = pOther->getOwner();
            if ( pOther == this || pOtherOwner == nullptr || pOtherOwner->getPrimarySceneComponent() == nullptr )
                continue; // 루트가 없는 부지는 제 검증이 알린다
            if ( pOther->_settings._plotID == _settings._plotID )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder2;
                textBuilder2.appendFormat( "plot id '%#' is also used by another plot in this scene", _settings._plotID.c_str() );
                context.addError( textBuilder2.view() );
            }
            const float3 otherOrigin = pOther->getPlotOrigin();
            const bool   bSeparateX  = origin._x + static_cast<float32>( _settings._sizeX ) * cellSize <= otherOrigin._x + Internal::kTouchEpsilon ||
                                    otherOrigin._x + static_cast<float32>( pOther->_settings._sizeX ) * cellSize <= origin._x + Internal::kTouchEpsilon;
            const bool bSeparateZ = origin._z + static_cast<float32>( _settings._sizeZ ) * cellSize <= otherOrigin._z + Internal::kTouchEpsilon ||
                                    otherOrigin._z + static_cast<float32>( pOther->_settings._sizeZ ) * cellSize <= origin._z + Internal::kTouchEpsilon;
            if ( bSeparateX == false && bSeparateZ == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "plot '%#' overlaps plot '%#'", _settings._plotID.c_str(), pOther->_settings._plotID.c_str() );
                context.addError( textBuilder.view() );
            }
        }
    }

    void HousingPlotComponent::onRegister( GameObjectManager& manager )
    {
        GameModeComponent::onRegister( manager );
        manager.getComponentRegistry().add<HousingPlotComponent>( this );
    }

    void HousingPlotComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<HousingPlotComponent>( this );
        GameModeComponent::onUnregister( manager );
    }
} // namespace sw

/**
 * @file HousingPlotComponentDevTools.cpp
 * @brief `HousingPlotComponent` 의 개발 명령 · 자동화 감시 값 · 맵 검사(`REFLECT( Validate )`)입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Serializer/Archive.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingNetSessionComponent.h"
#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        struct HousingPlotDevInternal
        {
            /** @brief 부지 둘이 칸 경계에서 맞닿는 것은 겹침이 아니다(m, 맵 검사 `validatePlot`). */
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
                StringBuilder<constant::buffer::kMax256> reply;
                reply.appendFormat( "no open housing plot named '%#' in the active scene", objectName );
                return string( reply.view() );
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
                    StringBuilder<constant::buffer::kMax256> replyBuilder;
                    replyBuilder.appendFormat( "housing: no part '%#' in the plot's catalog", listArgument[1] );
                    outReply = string( replyBuilder.view() );
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

            /** @brief `housing.save <plot> <file>` — 부지 상태를 절대 경로(시나리오의 `{out}/…`)에 쓴다. */
            static bool runSave( const vector<string>& listArgument, string& outReply )
            {
                AbsolutePath path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                if ( pPlot == nullptr )
                {
                    outReply = makeNoPlotReply( listArgument[0] );
                    return true;
                }
                if ( pPlot->saveToFile( path ) == false )
                {
                    outReply = "housing: save failed (see the log)";
                    return true;
                }
                StringBuilder<constant::buffer::kMax512> replyBuilder;
                replyBuilder.appendFormat( "housing: saved %#", path.str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            /** @brief `housing.load <plot> <file>` — 부지 상태를 바꾸고 뷰를 다시 세운다. */
            static bool runLoad( const vector<string>& listArgument, string& outReply )
            {
                AbsolutePath path;
                if ( listArgument.size() != 2 || DevCommandRegistry::readDiskPathArgument( listArgument[1], path, outReply ) == false )
                    return false;
                HousingPlotComponent* pPlot = findPlot( listArgument[0] );
                if ( pPlot == nullptr )
                {
                    outReply = makeNoPlotReply( listArgument[0] );
                    return true;
                }
                if ( pPlot->loadFromFile( path ) == false )
                {
                    outReply = "housing: load failed (see the log)";
                    return true;
                }
                StringBuilder<constant::buffer::kMax512> replyBuilder;
                replyBuilder.appendFormat( "housing: loaded %#", path.str() );
                outReply = string( replyBuilder.view() );
                return true;
            }

            static const HousingPlotComponent* findTarget( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<HousingPlotComponent>( "housing plot", outValue );
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
    } // namespace

    SW_DEV_COMMAND( HousingPreview, "housing.preview", "housing.preview <plot> <part> <x> <z> <level> <yawDegrees> [free]", "Show the ghost of a part on a housing plot and why it can or cannot go there",
                    &HousingPlotDevInternal::runPreview );
    SW_DEV_COMMAND( HousingPlace, "housing.place", "housing.place <plot> <part> <x> <z> <level> <yawDegrees> [free]", "Place a catalog part on a housing plot (snap, cost)", &HousingPlotDevInternal::runPlace );
    SW_DEV_COMMAND( HousingFill, "housing.fill", "housing.fill <plot> <part> <level>", "Try a part at the centre of every cell of one level (bulk furniture load)", &HousingPlotDevInternal::runFill );
    SW_DEV_COMMAND( HousingRemove, "housing.remove", "housing.remove <plot> <placementID>", "Remove a placement from a housing plot and refund it", &HousingPlotDevInternal::runRemove );
    SW_DEV_COMMAND( HousingMove, "housing.move", "housing.move <plot> <placementID> <x> <z> <level> <yawDegrees> [free]", "Move a placement on a housing plot", &HousingPlotDevInternal::runMove );
    SW_DEV_COMMAND( HousingSave, "housing.save", "housing.save <plot> <file>", "Write a housing plot's layout and funds to a file", &HousingPlotDevInternal::runSave );
    SW_DEV_COMMAND( HousingLoad, "housing.load", "housing.load <plot> <file>", "Replace a housing plot's layout and funds from a file and rebuild its parts", &HousingPlotDevInternal::runLoad );
    SW_AUTOMATION_QUERY_WATCH( housingPlacementCount, "Housing.PlacementCount", "Placements on the target housing plot", &HousingPlotDevInternal::readPlacementCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingViewCount, "Housing.ViewCount", "Part objects standing for the target housing plot", &HousingPlotDevInternal::readViewCount, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingFunds, "Housing.Funds", "Balance of the target housing plot's wallet", &HousingPlotDevInternal::readFunds, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingLastResult, "Housing.LastResult", "Result of the target housing plot's last preview or edit", &HousingPlotDevInternal::readLastResult, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingGhostPlaceable, "Housing.GhostPlaceable", "1 when the target housing plot's ghost stands where the part can go", &HousingPlotDevInternal::readGhostPlaceable, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingLandBound, "Housing.LandBound", "1 when the target housing plot claimed its cells on the shared land", &HousingPlotDevInternal::readLandBound, true, false );
    SW_AUTOMATION_QUERY_WATCH( housingPlacement, "Housing.Placement", "\"xCm zCm level yawDegrees\" of placement id key on the target housing plot", &HousingPlotDevInternal::readPlacement, true, true );

    // ------------------------------------------------------------------------------
    // 에디터 저작 검증(맵 검사)
    // ------------------------------------------------------------------------------
    void HousingPlotComponent::validatePlot( ValidationContext& context ) const
    {
        using Internal = HousingPlotDevInternal;
        if ( _ghostPlaceablePrefab.empty() || _ghostBlockedPrefab.empty() )
            context.addError( "both ghost prefabs are required" );
        if ( _partCatalog.empty() )
        {
            context.addError( "no part catalog" );
            return;
        }
        const shared_ptr<const HousingPartCatalog> pCatalog = _partCatalog.acquire().snapshot();
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
            (void)pCatalog->validatePart( part, listProblem );
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
} // namespace sw

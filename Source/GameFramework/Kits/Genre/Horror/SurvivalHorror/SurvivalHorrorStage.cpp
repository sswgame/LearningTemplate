#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorStage.h"

#include "Core/Log/Logger.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"
#include "GameFramework/Runtime/World/Land/AreaGraph.h"

namespace sw
{
    namespace
    {
        /** @brief 자리의 id 가 카탈로그에 있으면 true 입니다(타자기 · 상자는 id 가 없다). */
        bool hasSurvivalHorrorSpotID( const SurvivalHorrorCatalog& catalog, const SurvivalHorrorSpot& spot )
        {
            switch ( spot._kind )
            {
                case SurvivalHorrorSpotKind::Pickup:
                    return catalog.findItem( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::Document:
                    return catalog.findDocument( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::SequenceStep:
                    return catalog.findSequence( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::DialLock:
                    return catalog.findDialLock( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::Deduction:
                    return catalog.findDeduction( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::Monster:
                    return catalog.findMonster( spot._id ) != nullptr;
                case SurvivalHorrorSpotKind::SavePoint:
                case SurvivalHorrorSpotKind::ItemBox:
                    return true;
            }
            return false; // 열거자는 여덟뿐이다(covered-switch) — 닿지 않는다
        }
    } // namespace

    bool SurvivalHorrorStage::validate( const SurvivalHorrorCatalog& catalog, const AreaGraph& graph ) const
    {
        bool bValid = true;
        for ( const hashed_string* pArea : { &_startArea, &_goalArea } )
        {
            if ( graph.findArea( *pArea ) != nullptr )
                continue;
            SW_LOG_ERROR_ID( "SurvivalHorrorStage.UnknownArea", "SurvivalHorrorStage: start or goal area '%#' is not on the area graph", pArea->c_str() );
            bValid = false;
        }
        for ( const SurvivalHorrorSpot& spot : _listSpot )
        {
            if ( graph.findArea( spot._area ) != nullptr && hasSurvivalHorrorSpotID( catalog, spot ) )
                continue;
            SW_LOG_ERROR_ID( "SurvivalHorrorStage.UnknownSpot", "SurvivalHorrorStage: spot '%#' in area '%#' - the area is not on the graph or the id is not in the catalog", spot._id.c_str(),
                             spot._area.c_str() );
            bValid = false;
        }
        return bValid;
    }
} // namespace sw

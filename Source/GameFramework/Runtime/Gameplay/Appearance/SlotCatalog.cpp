#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SlotCatalog.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Foundation/Constants.h"

#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Runtime/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    void SlotCatalog::clear()
    {
        _listSlot.clear();
        _listOccupancy.clear();
    }

    bool SlotCatalog::loadFromXML( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const uint32 errorCountBefore = static_cast<uint32>( report.getErrors().size() );
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Slot" ) )
            {
                AppearanceSlotDef slot;
                (void)AppearanceXMLUtil::readFields( child, slot, report, sourceName ); // 오류는 보고서에 쌓인다
                if ( slot._accept.empty() )                                             // 적지 않으면 슬롯 이름
                    slot._accept = slot._name;
                if ( slot._name.empty() )
                    report.addErrorWithID( "Appearance.NameEmpty", "%#: <Slot> without a name", sourceName );
                else if ( hasSlot( slot._name ) )
                    report.addErrorWithID( "Appearance.SlotRepeated", "%#: slot '%#' is declared twice", sourceName, slot._name.c_str() );
                else
                    _listSlot.push_back( slot );
            }
            else if ( StringUtil::equals( child.getName(), "Occupancy" ) )
            {
                SlotOccupancyDef occupancy;
                (void)AppearanceXMLUtil::readFields( child, occupancy, report, sourceName, { "slots" } );
                AppearanceXMLUtil::readNameList( child, "slots", occupancy._listSlot );
                if ( occupancy._id.empty() || occupancy._listSlot.size() < 2 )
                    report.addErrorWithID( "Appearance.OccupancyShape", "%#: <Occupancy> needs an id and at least two slots", sourceName );
                else if ( findOccupancy( occupancy._id ) != nullptr )
                    report.addErrorWithID( "Appearance.OccupancyRepeated", "%#: occupancy '%#' is declared twice", sourceName, occupancy._id.c_str() );
                else
                    _listOccupancy.push_back( occupancy );
            }
            else
            {
                AppearanceXMLUtil::reportUnknownChild( root, child, report, sourceName );
            }
        }
        // 슬롯 선언이 점유 그룹보다 뒤에 와도 되도록, 이름 확인은 마지막에 한다.
        for ( const SlotOccupancyDef& occupancy : _listOccupancy )
        {
            for ( const hashed_string& slot : occupancy._listSlot )
            {
                if ( hasSlot( slot ) == false )
                    report.addErrorWithID( "Appearance.SlotUnknown", "%#: occupancy '%#' names unknown slot '%#'", sourceName, occupancy._id.c_str(), slot.c_str() );
            }
        }
        if ( _listSlot.empty() )
            report.addErrorWithID( "Appearance.SlotTableEmpty", "%#: <SlotCatalog> has no <Slot>", sourceName );
        return report.getErrors().size() == errorCountBefore;
    }

    optional<int32> SlotCatalog::findSlotIndex( const hashed_string& slot ) const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._name == slot )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    const SlotOccupancyDef* SlotCatalog::findOccupancy( const hashed_string& id ) const
    {
        return VectorUtil::findBy<&SlotOccupancyDef::_id>( _listOccupancy, id );
    }

    string SlotCatalog::makeEquipmentLayout() const
    {
        string layout;
        for ( const AppearanceSlotDef& slot : _listSlot )
        {
            if ( layout.empty() == false )
                layout += ",";
            layout += slot._name.c_str();
            if ( slot._accept != slot._name )
            {
                layout += ":";
                layout += slot._accept.c_str();
            }
        }
        return layout;
    }
} // namespace sw

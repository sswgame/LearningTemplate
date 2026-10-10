#include "pch.h"

#include "GameFramework/Base/Gameplay/Appearance/SlotTable.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceTypes.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceXMLUtil.h"

namespace sw
{
    namespace
    {
        struct SlotTableInternal
        {
            static constexpr const utf8* kArrSlotAttribute[]      = { "name", "accept" };
            static constexpr const utf8* kArrOccupancyAttribute[] = { "id", "slots" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    void SlotTable::clear()
    {
        _listSlot.clear();
        _listOccupancy.clear();
    }

    bool SlotTable::loadFromNode( const XMLNode& root, AppearanceLoadReport& report, string_view sourceName )
    {
        clear();
        const uint32 errorCountBefore = static_cast<uint32>( report.getErrors().size() );
        (void)AppearanceXMLUtil::reportUnknownAttributes( root, nullptr, 0, report, sourceName );
        for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
        {
            if ( StringUtil::equals( child.getName(), "Slot", true ) )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( child, SlotTableInternal::kArrSlotAttribute, report, sourceName );
                AppearanceSlotDef slot;
                slot._name   = AppearanceXMLUtil::readName( child, "name" );
                slot._accept = AppearanceXMLUtil::readName( child, "accept" );
                if ( slot._accept.empty() )
                    slot._accept = slot._name;
                if ( slot._name.empty() )
                    report.addError( "%#: <Slot> without a name", sourceName );
                else if ( hasSlot( slot._name ) )
                    report.addError( "%#: slot '%#' is declared twice", sourceName, slot._name.c_str() );
                else
                    _listSlot.push_back( slot );
            }
            else if ( StringUtil::equals( child.getName(), "Occupancy", true ) )
            {
                (void)AppearanceXMLUtil::reportUnknownAttributes( child, SlotTableInternal::kArrOccupancyAttribute, report, sourceName );
                SlotOccupancyDef occupancy;
                occupancy._id = AppearanceXMLUtil::readName( child, "id" );
                AppearanceXMLUtil::readNameList( child, "slots", occupancy._listSlot );
                if ( occupancy._id.empty() || occupancy._listSlot.size() < 2 )
                    report.addError( "%#: <Occupancy> needs an id and at least two slots", sourceName );
                else if ( findOccupancy( occupancy._id ) != nullptr )
                    report.addError( "%#: occupancy '%#' is declared twice", sourceName, occupancy._id.c_str() );
                else
                    _listOccupancy.push_back( occupancy );
            }
            else
            {
                AppearanceXMLUtil::reportUnknownChild( root, child, report, sourceName );
            }
        }
        // 칸 선언이 묶음보다 뒤에 와도 되게 이름 확인은 끝에서 한다.
        for ( const SlotOccupancyDef& occupancy : _listOccupancy )
        {
            for ( const hashed_string& slot : occupancy._listSlot )
            {
                if ( hasSlot( slot ) == false )
                    report.addError( "%#: occupancy '%#' names unknown slot '%#'", sourceName, occupancy._id.c_str(), slot.c_str() );
            }
        }
        if ( _listSlot.empty() )
            report.addError( "%#: <SlotTable> has no <Slot>", sourceName );
        return report.getErrors().size() == errorCountBefore;
    }

    int32 SlotTable::findSlotIndex( const hashed_string& slot ) const
    {
        for ( size_t index = 0; index < _listSlot.size(); ++index )
        {
            if ( _listSlot[index]._name == slot )
                return static_cast<int32>( index );
        }
        return -1;
    }

    const SlotOccupancyDef* SlotTable::findOccupancy( const hashed_string& id ) const
    {
        for ( const SlotOccupancyDef& occupancy : _listOccupancy )
        {
            if ( occupancy._id == id )
                return &occupancy;
        }
        return nullptr;
    }

    string SlotTable::makeEquipmentLayout() const
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

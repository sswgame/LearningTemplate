#include "pch.h"

#include "Engine/UI/Layout/UiLayoutDump.h"

#include "Core/Common/Defines.h"
#include "Core/Container/formatString.h"
#include "Core/String/fixed_string.h"

#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/UI/Base/PanelWidget.h"
#include "Engine/UI/Base/Widget.h"
#include "Engine/UI/Base/WidgetTree.h"

namespace sw
{
    namespace
    {
        struct UiLayoutDumpInternal
        {
            static void appendWidget( const Widget& widget, uint32 depth, float32 physicalScale, string& inoutText )
            {
                for ( uint32 level = 0; level < depth; ++level )
                {
                    inoutText += "  ";
                }
                const TypeInfo* pType = widget.getTypeInfo();
                const utf8*     pName = widget.getName().empty() == false ? widget.getName().c_str() : ( pType != nullptr ? pType->_name.c_str() : "Widget" );
                inoutText += pName;
                if ( widget.getVisibility() == WidgetVisibility::Collapsed )
                {
                    inoutText += " collapsed\n";
                    return;
                }
                const WidgetGeometry&                 geometry = widget.getGeometry();
                fixed_string<constant::kMaxBuffer128> line{};
                formatstring( line.data(), line.capacity(), " %.2f %.2f %.2f %.2f\n", geometry._position._x * physicalScale, geometry._position._y * physicalScale,
                              geometry._size._x * physicalScale, geometry._size._y * physicalScale );
                inoutText += line.c_str();

                const PanelWidget* pPanel = castTo<const PanelWidget>( &widget );
                if ( pPanel == nullptr )
                    return;
                for ( uint32 index = 0; index < pPanel->getChildCount(); ++index )
                {
                    appendWidget( *pPanel->getChild( index ), depth + 1, physicalScale, inoutText );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    string UiLayoutDump::makeDump( const WidgetTree& tree, float32 physicalScale )
    {
        string text;
        if ( tree.getRoot() != nullptr )
            UiLayoutDumpInternal::appendWidget( *tree.getRoot(), 0, physicalScale, text );
        return text;
    }
} // namespace sw

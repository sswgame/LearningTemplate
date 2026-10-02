#include "pch.h"

#include "TestFramework/TestPropertyCoverage.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/Core/SerializeContext.h"
#include "Engine/Serialization/Core/SerializerUtil.h"

namespace test
{
    PropertyCarryReport makePropertyCarryReport()
    {
        const sw::SerializeContext& ctx = sw::SerializeContext::getDefault();

        sw::vector<const sw::TypeInfo*> listType;
        sw::engine::getTypeRegistry().forEachType( [&listType]( const sw::TypeInfo& typeInfo )
        { listType.push_back( &typeInfo ); } );

        PropertyCarryReport report{};
        report._typeCount = static_cast<uint32>( listType.size() );
        for ( const sw::TypeInfo* pType : listType )
        {
            for ( const sw::PropertyInfo& prop : pType->_listProperty )
            {
                if ( prop._metadata._bTransient == SW_TRUE )
                    continue;
                ++report._checkedCount;
                if ( sw::SerializerUtil::canCarryProperty( prop, ctx ) )
                    continue;
                report._offender += sw::string( pType->_fullyQualifiedName.c_str() ) + "::" + prop._name.c_str() + " (" + prop._typeName.c_str() + ")\n";
            }
        }
        return report;
    }
} // namespace test

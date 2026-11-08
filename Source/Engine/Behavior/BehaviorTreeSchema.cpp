#include "pch.h"

#include "Engine/Behavior/BehaviorTreeSchema.h"

namespace sw
{
    DataInheritanceSchema BehaviorTreeSchema::make()
    {
        DataInheritanceSchema schema{ "BehaviorTree" };
        schema._keyAttribute = "id";
        schema._bSelfNested  = SW_TRUE;
        return schema;
    }

    SW_DATA_INHERITANCE_SCHEMA( BehaviorTree, &BehaviorTreeSchema::make );
} // namespace sw

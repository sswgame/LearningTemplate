// getTypeInfo — $FQN (COMPONENT_BODY)
const sw::TypeInfo* $FQN::getTypeInfo() const
{
	if ( _cachedTypeInfo != nullptr )
		return _cachedTypeInfo;
	return StaticType();
}


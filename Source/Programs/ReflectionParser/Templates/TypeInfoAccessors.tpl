// StaticType 의 놓친 경로 — $FQN (REFLECT_BODY, 적중 경로는 매크로의 인라인 StaticType)
const sw::TypeInfo* $FQN::swFindStaticType()
{
	static const ::sw::hashed_string s_fqn( "$FQN" );
	return swStaticTypeCache.findSlow( s_fqn );
}


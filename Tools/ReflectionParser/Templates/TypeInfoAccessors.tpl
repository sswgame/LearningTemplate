// StaticType — $FQN (REFLECT_BODY)
const sw::TypeInfo* $FQN::StaticType()
{
	static const ::sw::hashed_string s_fqn( "$FQN" );
	static ::sw::TypeLookupCache     s_cache;
	return s_cache.find( s_fqn );
}


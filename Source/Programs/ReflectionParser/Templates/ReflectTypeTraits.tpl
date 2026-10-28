// ReflectTypeTraits — $FQN
namespace sw
{
	template <>
	struct ReflectTypeTraits<$FQN>
	{
		static const TypeInfo* StaticType()
		{
			static const ::sw::hashed_string s_fqn( "$FQN" );
			static ::sw::TypeLookupCache     s_cache;
			return s_cache.find( s_fqn );
		}
	};
} // namespace sw


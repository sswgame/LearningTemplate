// ReflectTypeTraits — $FQN
namespace sw
{
	template <>
	struct ReflectTypeTraits<$FQN>
	{
		static const TypeInfo* StaticType()
		{
			return ::sw::engine::getTypeRegistry().findType( ::sw::hashed_string( "$FQN" ) );
		}
	};
} // namespace sw


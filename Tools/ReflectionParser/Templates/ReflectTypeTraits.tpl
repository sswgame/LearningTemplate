// ReflectTypeTraits — $FQN
namespace sw
{
	template <>
	struct ReflectTypeTraits<$FQN>
	{
		static const TypeInfo* StaticType()
		{
			return ::sw::getTypeRegistry().findType( ::sw::hashed_string( "$FQN" ) );
		}
	};
} // namespace sw


	// ── $FQN
	template <>
	struct Registrar<$Key>
	{
		static const ::sw::TypeDesc& getDesc()
		{
			using Self = $FQN;

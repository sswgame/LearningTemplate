	// ── $FQN ──────────────────────────────
	struct ${Id}_Registrar
	{
		static void RegisterEnum(::sw::TypeRegistry& registry)
		{
			::sw::EnumInfo info;
			info._name               = ::sw::hashed_string( "$Name" );
			info._fullyQualifiedName = ::sw::hashed_string( "$FQN" );
			info._moduleName         = ::sw::hashed_string( "$ModuleName" );
			info._bIsBitFlag         = $IsBitFlag;
			info._bHasInvalid        = $HasInvalid;
			info._invalidValue       = $InvalidValue;
			info._bHasCount          = $HasCount;
			info._countValue         = $CountValue;

	// ── $FQN ──────────────────────────────
	struct ${Id}_Registrar
	{
		static void RegisterType(::sw::TypeRegistry& registry)
		{
			::sw::TypeInfo info;
			info._name               = ::sw::hashed_string( "$Name" );
			info._fullyQualifiedName = ::sw::hashed_string( "$FQN" );
			info._parentFQN          = ::sw::hashed_string( "$ParentFQN" );
			info._moduleName         = ::sw::hashed_string( "$ModuleName" );
			info._size               = sizeof( $FQN );
			info._destroyInstance    = [](void* p) { std::destroy_at(static_cast<$FQN*>(p)); };
$Flags

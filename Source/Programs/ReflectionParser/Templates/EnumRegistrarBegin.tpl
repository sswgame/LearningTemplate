	// ── $FQN ──────────────────────────────
	template <>
	struct Registrar<$Key>
	{
		using E = ::$FQN;

		static void registerEnum( ::sw::TypeRegistry& registry )
		{
			::sw::EnumInfo info = ::sw::EnumInfo::create<E>( { "$Name", "$FQN", "$ModuleName", $EntryTable, $RedirectTable, $InvalidName, $CountName, $IsBitFlag } );

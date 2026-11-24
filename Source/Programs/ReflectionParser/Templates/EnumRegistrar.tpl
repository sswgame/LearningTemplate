	// ── $FQN
	template <>
	struct Registrar<$Key>
	{
		static const ::sw::EnumInfoDesc& getDesc()
		{
			using E = ::$FQN;
$Tables			static constexpr ::sw::EnumInfoDesc kEnum{ "$Name", "$FQN", "$ModuleName", $EntryTable, $RedirectTable, $InvalidName, $CountName, $IsBitFlag, sizeof( E ), std::is_signed_v<std::underlying_type_t<E>>$Aliases };
			return kEnum;
		}
	};
	static ::sw::EnumRegistrar s_${ID}_registrar{ &Registrar<$Key>::getDesc };


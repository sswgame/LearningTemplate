	// --- $Name ---
	static const ::sw::TypeDesc& get${ID}Desc()
	{
$AliasTable		static constexpr ::sw::TypeDesc kType = ::sw::TypeDesc::makePrimitive( "$Name", "$ModuleName", sizeof( $CppType ) )$Aliases;
		return kType;
	}
	static ::sw::TypeRegistrar s_${ID}_registrar{ &get${ID}Desc };


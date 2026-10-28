	void forceLinkBuiltinTypes();
	void forceLinkBuiltinTypes()
	{
		volatile const void* pAnchor = &s_Builtin_int8_registrar;
		(void)pAnchor;
	}
} // namespace sw::generated

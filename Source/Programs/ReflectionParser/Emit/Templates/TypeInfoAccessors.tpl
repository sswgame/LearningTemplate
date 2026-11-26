// StaticType 의 놓친 경로(REFLECT_BODY, 적중 경로는 매크로의 인라인 StaticType)
const sw::TypeInfo* $FQN::swFindStaticType() { return swStaticTypeCache.findSlow( "$FQN" ); }

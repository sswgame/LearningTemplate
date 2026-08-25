#pragma once
#include "Core/Common/Types.h"

#include <type_traits>
namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) SFINAE — tick / beginPlay / endPlay / owner 멤버 존재 여부
	// ------------------------------------------------------------------------------
	template <typename C, typename Signature>
	struct has_tick
	{
	private:
		template <typename T>
		static constexpr decltype( &T::tick, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_beginPlay
	{
	private:
		template <typename T>
		static constexpr decltype( &T::beginPlay, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_endPlay
	{
	private:
		template <typename T>
		static constexpr decltype( &T::endPlay, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C, typename Signature>
	struct has_onTick
	{
	private:
		template <typename T>
		static constexpr decltype( &T::onTick, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_onBeginPlay
	{
	private:
		template <typename T>
		static constexpr decltype( &T::onBeginPlay, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_onEndPlay
	{
	private:
		template <typename T>
		static constexpr decltype( &T::onEndPlay, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_owner
	{
	private:
		template <typename T>
		static constexpr decltype( &T::owner, std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_isActive
	{
	private:
		template <typename T>
		static constexpr decltype( std::declval<T>().isActive(), std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_isPendingKill
	{
	private:
		template <typename T>
		static constexpr decltype( std::declval<T>().isPendingKill(), std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};

	template <typename C>
	struct has_setOwner
	{
	private:
		template <typename T>
		static constexpr decltype( std::declval<T>().setOwner( static_cast<GameObject*>( nullptr ) ), std::true_type{} ) check( int32 );

		template <typename T>
		static constexpr std::false_type check( ... );

	public:
		static constexpr bool value = decltype( check<C>( 0 ) )::value;
	};
} // namespace sw

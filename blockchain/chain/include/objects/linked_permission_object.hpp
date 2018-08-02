/**
*  @file
*  @copyright defined in xmax/LICENSE
*/
#pragma once
#include <blockchain_types.hpp>
#include <authoritys.hpp>

#include "multi_index_includes.hpp"

namespace Xmaxplatform {
namespace Chain {

	class linked_permission_object : public Basechain::object<linked_permission_object_type, linked_permission_object> {
		OBJECT_CCTOR(linked_permission_object,)
	public:
		id_type			id;	

		account_name    account;

		account_name    code;

		func_name       func;

		authority_name required_auth;
	};

	struct by_action_name;
	struct by_permission_name;
	using permission_link_index = Basechain::shared_multi_index_container <
		linked_permission_object,
		indexed_by<
		ordered_unique<tag<by_id>,
			BOOST_MULTI_INDEX_MEMBER(linked_permission_object, linked_permission_object::id_type, id)
		>,
		ordered_unique<tag<by_action_name>,
			composite_key<linked_permission_object,
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, account_name, account),
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, account_name, code),
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, action_name, func)
			>
		>,
		ordered_unique<tag<by_permission_name>,
			composite_key<linked_permission_object,
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, account_name, account),
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, authority_name, required_auth),
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, account_name, code),
				BOOST_MULTI_INDEX_MEMBER(linked_permission_object, func_name, func)
			>
		>

		>
	>;

}
} //

BASECHAIN_SET_INDEX_TYPE(Xmaxplatform::Chain::linked_permission_object, Xmaxplatform::Chain::permission_link_index)

FC_REFLECT(Xmaxplatform::Chain::linked_permission_object, (id)(account)(code)(func)(required_auth))

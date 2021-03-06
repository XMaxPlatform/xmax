/**
*  @file
*  @copyright defined in xmax/LICENSE
*/
#include <objects/account_contract.hpp>


namespace Xmaxplatform {
namespace Chain {

	void account_contract::set_abi(const Xmaxplatform::Basetypes::abi& _abi) {

		// Added resize(0) here to avoid bug in boost vector container
		abi.resize(0);
		abi.resize(fc::raw::pack_size(_abi));
		fc::datastream<char*> ds(abi.data(), abi.size());
		fc::raw::pack(ds, _abi);
	}
}
}
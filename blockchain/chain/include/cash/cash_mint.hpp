/**
*  @file
*  @copyright defined in xmax/LICENSE
*/
#pragma once
#include <cash/cash_types.hpp>
#include <vector>

namespace Xmaxplatform {
namespace Chain {

	struct cash_mint
	{
		cash_mint() = default;

		cash_mint(uint64_t seq, const address& ow, cash_token tk)
			: sequence(seq)
			, owner(ow)
			, token(tk)
		{

		}

		uint64_t sequence = 0; // sequence for owner.
		address owner;
		cash_token token;
		cash_digest digest() const;

		cash_signature sign(const fc::ecc::private_key& signer) const;
	};
}
}

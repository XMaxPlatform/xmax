/**
*  @file
*  @copyright defined in xmax/LICENSE
*/
#pragma once
#include <blockchain_types.hpp>
#include <builder_rule.hpp>
#include <blockchain_config.hpp>

namespace Xmaxplatform {
namespace Chain {


	namespace utils
	{
		static chain_timestamp get_delta_slot_time(chain_timestamp begin_time, uint32_t delta_slot)
		{
			begin_time += chain_timestamp::create(delta_slot);
			return begin_time;

		}

		static uint32_t get_delta_slot_at_time(chain_timestamp begin_time, chain_timestamp when)
		{
			chain_timestamp first_slot_time = utils::get_delta_slot_time(begin_time, 1);
			if (when < first_slot_time)
				return 0;

			chain_timestamp sub = when - first_slot_time;
			return sub.get_stamp() + 1;
		}

		template<typename T>
		const T& select_builder_rule(const T& current_builders, const T& next_builders, uint32_t order_slot)
		{
			if (order_slot < Config::blocks_per_round || next_builders.is_empty())
			{
				return current_builders;
			}
			return next_builders;
		}

		template<typename T>
		const builder_info& select_builder(const T& current_builders, const T& next_builders, uint32_t index)
		{
			if (index < Config::blocks_per_round || next_builders.is_empty())
			{
				uint32_t bias = (index / Config::blocks_per_builder) % current_builders.number();
				return current_builders.builders[bias];
			}

			// get builder in next list.
			uint32_t deltaslot = index - current_builders.number();

			uint32_t bias = (index / Config::blocks_per_builder) % next_builders.number();

			return next_builders.builders[bias];
		}

	}
}
}
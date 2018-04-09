/**
 *  @file
 *  @copyright defined in xmax/LICENSE
 */

#include <blockchain_types.hpp>
#include <block.hpp>
#include <chain_xmax.hpp>


#include <rand.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>
#include <fc/crypto/digest.hpp>
#include <fc/exception/exception.hpp>

#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm_ext/is_sorted.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/adaptor/map.hpp>

#include <fstream>
#include <functional>
#include <iostream>
#include <chrono>

#include <transaction.hpp>

#include <vm_xmax.hpp>

namespace Xmaxplatform { namespace Chain {


        void chain_xmax::setup_data_indexes() {
            _data.add_index<account_index>();
            _data.add_index<key_value_index>();
            _data.add_index<keystr_value_index>();
            _data.add_index<key128x128_value_index>();
            _data.add_index<key64x64x64_value_index>();

            _data.add_index<static_config_multi_index>();
            _data.add_index<dynamic_states_multi_index>();
            _data.add_index<xmx_token_multi_index>();

        }

        void chain_xmax::initialize_chain(chain_init& initer)
        { try {
                if (!_data.find<static_config_object>()) {
                    _data.with_write_lock([this,&initer] {

                        // Create global properties
                        _data.create<static_config_object>([&](static_config_object &p) {
                            p.setup = initer.get_blockchain_setup();
                            p.active_builders = initer.get_chain_init_builders();
                        });
                        _data.create<dynamic_states_object>([&](dynamic_states_object &p) {
                            p.time = initer.get_chain_init_time();
                        });

                        signed_block block{};
                        block.builder = Config::xmax_contract_name;
                        block.threads.emplace_back();
                        block.threads[0].emplace_back();

                        auto messages = initer.prepare_database(*this, _data);
                        std::for_each(messages.begin(), messages.end(), [&](const message_xmax& m) {
                            message_output output;
                            processed_transaction trx; /// dummy transaction required for scope validation
                            std::sort(trx.scope.begin(), trx.scope.end() );
                            with_skip_flags(0,[&](){
                                process_message(trx,m.code,m,output);
                            });

                            trx.messages.push_back(m);
                        });
                    });
                }

            } FC_CAPTURE_AND_RETHROW()

        }

        chain_xmax::chain_xmax(database& database,chain_init& init) : _data(database){

            setup_data_indexes();
                    with_applying_block([&] {
                        initialize_chain(init);
                    });

        }

        chain_xmax::~chain_xmax() {

            _data.flush();
        }

        const static_config_object& chain_xmax::get_static_config()const {
            return _data.get<static_config_object>();
        }

        const dynamic_states_object& chain_xmax::get_dynamic_states() const {
            return _data.get<dynamic_states_object>();
        }

        signed_block chain_xmax::generate_block(
                fc::time_point_sec when,
                const account_name& builder
        )
        { try {
                _data.start_undo_session(true);
                auto b = _data.with_write_lock( [&](){
                    return _generate_block( when, builder );
                });
                push_block(b);
                return b;
            } FC_CAPTURE_AND_RETHROW( (when) ) }

        signed_block chain_xmax::_generate_block(
                fc::time_point_sec when,
                const account_name& builder
        )
        {
            try {

                auto start = fc::time_point::now();

                signed_block pending_block;
                const auto& gprops = get_static_config();

                uint32_t pending_block_size = fc::raw::pack_size( pending_block );

                pending_block.previous = get_dynamic_states().head_block_id;
                pending_block.timestamp = when;
                pending_block.transaction_merkle_root = pending_block.calculate_merkle_root();

                pending_block.builder = builder;

                const auto end = fc::time_point::now();
                const auto gen_time = end - start;
                if( gen_time > fc::milliseconds(10) ) {
                    ilog("generation took ${x} ms", ("x", gen_time.count() / 1000));
                    FC_ASSERT(gen_time < fc::milliseconds(250), "block took too long to build");
                }


                //pending_block.sign( block_signing_private_key );

                return pending_block;
            } FC_CAPTURE_AND_RETHROW( (builder) ) }

        bool chain_xmax::push_block(const signed_block& new_block)
        { try {
                return _data.with_write_lock( [&]() {
                    return _push_block(new_block);
                        });

            } FC_CAPTURE_AND_RETHROW((new_block)) }

        bool chain_xmax::_push_block(const signed_block& new_block)
        { try {

                try {
                    auto session = _data.start_undo_session(true);
                    auto exec_start = std::chrono::high_resolution_clock::now();
                    apply_block(new_block);

                    session.push();
                } catch ( const fc::exception& e ) {
                    elog("Failed to push new block:\n${e}", ("e", e.to_detail_string()));
                    throw;
                }

                return false;
            } FC_CAPTURE_AND_RETHROW((new_block)) }

        void chain_xmax::apply_block(const signed_block& next_block)
        {

            with_applying_block([&] {
                    _apply_block(next_block);
            });
        }

        void chain_xmax::_apply_block(const signed_block& next_block)
        { try {

                next_block.transaction_merkle_root == next_block.calculate_merkle_root();


                update_dynamic_states(next_block);



            } FC_CAPTURE_AND_RETHROW( (next_block.block_num()) )  }


        void chain_xmax::update_dynamic_states(const signed_block& b) {
            const dynamic_states_object& _dgp = _data.get<dynamic_states_object>();

            // update dynamic states
            _data.modify( _dgp, [&]( dynamic_states_object& dgp ){
                dgp.head_block_number = b.block_num();
                dgp.head_block_id = b.id();
                dgp.time = b.timestamp;
                dgp.current_builder = b.builder;
            });

        }

        void chain_xmax::process_message(const transaction& trx, account_name code,
                                         const message_xmax& message, message_output& output,
                                         message_context_xmax* parent_context, int depth,
                                         const fc::time_point& start_time ) {

            auto us_duration = (fc::time_point::now() - start_time).count();

            message_context_xmax xmax_ctx(*this, _data, trx, message, code);
            apply_message(xmax_ctx);

            output.notify.reserve( xmax_ctx.notified.size() );

            for( uint32_t i = 0; i < xmax_ctx.notified.size(); ++i ) {
                try {
                    auto notify_code = xmax_ctx.notified[i];
                    output.notify.push_back( {notify_code} );
                    process_message(trx, notify_code, message, output.notify.back().output, &xmax_ctx, depth + 1, start_time );
                } FC_CAPTURE_AND_RETHROW((xmax_ctx.notified[i]))
            }

            // combine inline messages and process
            if (xmax_ctx.inline_messages.size() > 0) {
                output.inline_trx = inline_transaction(trx);
                (*output.inline_trx).messages = std::move(xmax_ctx.inline_messages);
            }


        }


        void chain_xmax::apply_message(message_context_xmax& context)
        { try {
                /// context.code => the execution namespace
                /// message.code / message.type => Event
                const auto& m = context.msg;
                auto contract_handlers_itr = message_handlers.find(context.code);
                if (contract_handlers_itr != message_handlers.end()) {
                    auto message_handler_itr = contract_handlers_itr->second.find({m.code, m.type});
                    if (message_handler_itr != contract_handlers_itr->second.end()) {
                        message_handler_itr->second(context);
                        return;
                    }
                }
                const auto& recipient = _data.get<account_object,by_name>(context.code);
                if (recipient.code.size()) {
                    idump((context.code)(context.msg.type));
                    const uint32_t execution_time = 10000;//TODO
                    try {
                        vm_xmax::get().apply(context, execution_time, true );
                    } catch (const fc::exception &ex) {

                    }
                }

            } FC_CAPTURE_AND_RETHROW((context.msg)) }


        chain_init::~chain_init() {}

} }

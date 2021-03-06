/**
 *  @file
 *  @copyright defined in xmax/LICENSE
 */
#include <xmax_contract.hpp>
#include <blockchain_exceptions.hpp>
#include <chain_xmax.hpp>
#include <message_context_xmax.hpp>
#include <message_xmax.hpp>
#include <authoritys_utils.hpp>

#include <objects/object_utility.hpp>
#include <objects/account_object.hpp>
#include <objects/authority_object.hpp>
#include <objects/linked_permission_object.hpp>
#include <objects/xmx_token_object.hpp>
#include <objects/resource_token_object.hpp>
#include <objects/chain_object_table.hpp>
#include <objects/static_config_object.hpp>
#include <objects/erc20_token_object.hpp>
#include <objects/erc20_token_account_object.hpp>
#include <objects/erc721_token_object.hpp>
#include <objects/erc721_token_account_object.hpp>
#include <xmax_voting.hpp>
#include <vm_xmax.hpp>
#include <safemath.hpp>

#include <abi_serializer.hpp>

#ifdef USE_V8
#include <jsvm_xmax.hpp>
#endif


namespace Xmaxplatform {
    namespace Native_contract {
using namespace Chain;
namespace Config = ::Xmaxplatform::Config;
namespace Chain = ::Xmaxplatform::Chain;
namespace SafeMath = Xmaxplatform::Basetypes::SafeMath;
using namespace ::Xmaxplatform::Basetypes;

typedef mutable_db_table <xmx_token_object, by_owner_name> xmax_token_table;
typedef mutable_db_table <resource_token_object, by_owner_name> resource_token_table;


static void validate_authority(const message_context_xmax& context, const Basetypes::authority& auth) {

	for (const account_permission_weight& a : auth.accounts)
	{
		const account_object* acct = context.db.find<account_object, by_name>(a.permission.account);
		XMAX_ASSERT( acct != nullptr, message_validate_exception, "account '${account}' does not exist", ("account", a.permission.account) );

		if (a.permission.authority == Config::xmax_owner_name || a.permission.authority == Config::xmax_active_name)
			continue; // all account has owner and active permissions.

		try {
			utils::get_authority_object(context.db, a.permission);
		}
		catch (...) 
		{
			XMAX_THROW(message_validate_exception,
				"permission '${perm}' does not exist",
				("perm", a.permission)
			);
		}
	}
}

static const account_object& account_assert(const Basechain::database& db, const account_name accout)
{
	const account_object* acc = db.find<account_object, by_name>(accout);
	XMAX_ASSERT(acc != nullptr, message_validate_exception,
		"Account not found: ${account}", ("account", accout));
	return *acc;
}

template <class ErcObjectType>
static void validate_token_not_revoke(const ErcObjectType& erc_object) {
	XMAX_ASSERT(erc_object.revoked == 0, message_validate_exception,
		"ERC token:${token_name} already revoked!", ("token_name", erc_object.token_name));
}

template <class ErcObjectType>
static void validate_token_canmint(const ErcObjectType& erc_object) {
	XMAX_ASSERT(erc_object.stopmint == 0, message_validate_exception,
		"ERC token:${token_name} already revoked!", ("token_name", erc_object.token_name));
}

template <class Key,class Value>
static Value GetSharedMapValue (shared_map<Key, Value>& mapobj, const Key& keyobj) {
	if (mapobj.find(keyobj)==mapobj.end())
	{
		mapobj[keyobj] = 0;
	}
	return mapobj[keyobj];
}

static void validate_name(const name& n) {
	XMAX_ASSERT(n.valid(), message_validate_exception,
		"Name:${name} is invalid", ("name", n));
}

static const account_object& xmax_new_account(Basechain::database& db, account_name accname, time creation_time,  account_type acc_type)
{
	const auto& new_account = db.create<account_object>([&](account_object& a) {
		a.name = accname;
		a.type = acc_type;
		a.creation_date = creation_time;
	});

	return new_account;
}

//API handler implementations
void xmax_system_addaccount(message_context_xmax& context) {
	Types::addaccount create = context.msg.as<Types::addaccount>();
	Basechain::database& db = context.mutable_db;
	time current_time = context.current_time();

	context.require_authorization(create.creator);

	auto existing_account = db.find<account_object, by_name>(create.name);
	XMAX_ASSERT(existing_account == nullptr, account_name_exists_exception,
		"Cannot create account named ${name}, as that name is already taken",
		("name", create.name.to_string()));

	XMAX_ASSERT(create.deposit.amount > 0, message_validate_exception,
		"account creation deposit must > 0, deposit: ${a}", ("a", create.deposit));

	const auto& new_account = xmax_new_account(db, create.name, current_time, Chain::acc_personal);


	const auto& creatorToken = db.get<xmx_token_object, by_owner_name>(create.creator);

	XMAX_ASSERT(creatorToken.main_token >= create.deposit.amount, message_validate_exception,
		"Creator '${c}' has insufficient funds to make account creation deposit of ${a}",
		("c", create.creator.to_string())("a", create.deposit));

	db.modify(creatorToken, [&create](xmx_token_object& b) {
		b.main_token -= create.deposit.amount;
	});

	db.create<xmx_token_object>([&create](xmx_token_object& b) {
		b.owner_name = create.name;
		b.main_token = create.deposit.amount;
	});

	auto owner_p = db.create<authority_object>([&](authority_object& obj)
	{
		obj.auth_name = Config::xmax_owner_name;

		obj.parent = 0;
		obj.owner_name = create.name;
		obj.authoritys = create.owner;
		obj.last_updated = current_time;
	});

	auto owner_a = db.create<authority_object>([&](authority_object& obj)
	{
		obj.auth_name = Config::xmax_active_name;

		obj.parent = owner_p.id;
		obj.owner_name = create.name;
		obj.authoritys = create.active;
		obj.last_updated = current_time;
	});

}


void xmax_system_addcontract(Chain::message_context_xmax& context)
{
	const Types::addcontract& msgdata = context.msg.as<Types::addcontract>();
	Basechain::database& db = context.mutable_db;
	time current_time = context.current_time();

	account_name contract_name = msgdata.name;

	context.require_authorization(msgdata.creator);

	const auto& new_account = xmax_new_account(db, contract_name, current_time, Chain::acc_contract);

	abi_serializer(msgdata.code_abi).validate();

	const auto& contract = db.get<account_object, by_name>(contract_name);
	db.modify(contract, [&](account_object& a) {

		a.set_contract(msgdata.code, msgdata.code_abi);
	});

	message_context_xmax init_context(context.mutable_chain, context.mutable_db, context.trx, context.msg, contract_name, 0);
	jsvm_xmax::get().init(init_context);

}

void xmax_system_adderc20(Chain::message_context_xmax& context)
{
	const Types::adderc20& msgdata = context.msg.as<Types::adderc20>();
	Basechain::database& db = context.mutable_db;
	time current_time = context.current_time();
	fc::string strname(msgdata.token_name);
	account_name contract_name = strname;

	context.require_scope(msgdata.creator);
	context.require_authorization(msgdata.creator);

	const auto& new_account = xmax_new_account(db, contract_name, current_time, Chain::acc_erc20);
	
	//Check existence
	auto existing_token_obj = db.find<erc20_token_object, by_token_name>(msgdata.token_name);
	XMAX_ASSERT(existing_token_obj == nullptr, message_validate_exception,
		"Erc20 token:'${t}' already exist, the owner is ${owner}",
		("t", msgdata.token_name)("owner", existing_token_obj->owner_name));

	db.create<erc20_token_object>([&msgdata](erc20_token_object& obj) {
		obj.token_name = msgdata.token_name;
		obj.owner_name = msgdata.creator;
		obj.total_supply = msgdata.total_balance.amount;
	});

	auto account_owner = MakeErcTokenIndex(msgdata.token_name, msgdata.creator);
	auto token_account_owner_ptr = db.find<erc20_token_account_object, by_token_and_owner>(account_owner);
	XMAX_ASSERT(token_account_owner_ptr == nullptr, message_validate_exception, "From account already have tokens!");

	db.create<erc20_token_account_object>([&msgdata](erc20_token_account_object& obj) {
		obj.balance = msgdata.total_balance.amount;
		obj.owner_name = msgdata.creator;
		obj.token_name = msgdata.token_name;
	});
}
void xmax_system_adderc721(Chain::message_context_xmax& context)
{
	const Types::adderc721& msgdata = context.msg.as<Types::adderc721>();
	Basechain::database& db = context.mutable_db;
	time current_time = context.current_time();

	context.require_scope(msgdata.creator);
	context.require_authorization(msgdata.creator);
	fc::string strname(msgdata.token_name);
	account_name contract_name = strname;

	context.require_authorization(msgdata.creator);

	const auto& new_account = xmax_new_account(db, contract_name, current_time, Chain::acc_erc721);

	//Check precondition
	auto existing_token_obj = db.find<erc721_token_object, by_token_name>(msgdata.token_name);
	XMAX_ASSERT(existing_token_obj == nullptr, message_validate_exception,
		"Erc721 token:'${t}' already exist, the owner is ${owner}",
		("t", msgdata.token_name)("owner", existing_token_obj->owner_name));

	//Create erc721 token object
	db.create<erc721_token_object>([&msgdata](erc721_token_object& token) {
		token.token_name = msgdata.token_name;
		token.owner_name = msgdata.creator;
	});
}

void xmax_system_updateauth(Chain::message_context_xmax& context)
{
	Types::updateauth msg = context.msg.as<Types::updateauth>();

	context.require_authorization(msg.account);

	auto& data_db = context.db;

	XMAX_ASSERT(!msg.permission.empty(), message_validate_exception, "Cannot create authority with empty name");

	XMAX_ASSERT(utils::check_authority_name(msg.permission), message_validate_exception, "Permission names cannot be started with 'xmax'");

	XMAX_ASSERT(msg.permission != msg.parent, message_validate_exception, "Cannot set an authority as its own parent");


	account_assert(data_db, msg.account); // check account.

	XMAX_ASSERT(utils::validate_weight_format(msg.new_authority), message_validate_exception, "Invalid authority: ${auth}", ("auth", msg.new_authority));

	if (msg.permission == Config::xmax_owner_name)
		XMAX_ASSERT(msg.parent.empty(), message_validate_exception, "Owner authority's parent must be empty");
	else
		XMAX_ASSERT(!msg.parent.empty(), message_validate_exception, "Only owner authority's parent can be empty");

	if (msg.permission == Config::xmax_active_name)
		XMAX_ASSERT(msg.parent == Config::xmax_owner_name, message_validate_exception, "Active authority's parent must be owner", ("msg.parent", msg.parent));


	validate_authority(context, msg.new_authority);

	const authority_object* old_auth = utils::find_authority_object(context.db, {msg.account, msg.permission});

	authority_object::id_type parent_id = 0;
	if (msg.permission != Config::xmax_owner_name)
	{
		auto auth = utils::get_authority_object(context.db, { msg.account, msg.parent });
		parent_id = auth.id;
	}

	if (old_auth) // update auth.
	{
		XMAX_ASSERT(parent_id == old_auth->parent, message_validate_exception, "Changing parent authority is not supported");

		utils::modify_authority_object(context.mutable_db, *old_auth, msg.new_authority, context.chain.building_block_timestamp().time_point());

	}
	else // new auth.
	{
		utils::new_authority_object(context.mutable_db, msg.account, msg.permission, parent_id, msg.new_authority, context.chain.building_block_timestamp().time_point());
	}
}

void xmax_system_deleteauth(Chain::message_context_xmax& context)
{
	Types::deleteauth msg = context.msg.as<Types::deleteauth>();

	context.require_authorization(msg.account);

	XMAX_ASSERT(msg.permission != Config::xmax_active_name, message_validate_exception, "Cannot delete active authority");
	XMAX_ASSERT(msg.permission != Config::xmax_owner_name, message_validate_exception, "Cannot delete owner authority");

	utils::remove_linked_object(context.mutable_db, msg.account, msg.permission);

	const auto& auth = utils::get_authority_object(context.db, { msg.account, msg.permission });

	utils::remove_authority_object(context.mutable_db, auth);

}

void xmax_system_linkauth(Chain::message_context_xmax& context)
{
	Types::linkauth msg = context.msg.as<Types::linkauth>();
	try {
		XMAX_ASSERT(!msg.requirement.empty(), message_validate_exception, "Required authority name cannot be empty");

		context.require_authorization(msg.account);

		account_assert(context.mutable_db, msg.account);
		account_assert(context.mutable_db, msg.code);

		if (msg.type != Config::xmax_sysany_name) {
			const auto *permission = context.mutable_db.find<authority_object, by_name>(msg.requirement);
			XMAX_ASSERT(permission != nullptr, message_validate_exception,
				"Failed to retrieve authority: ${permission}", ("permission", msg.requirement));
		}

		{
			const authority_object* auth = context.db.find<authority_object, by_owner>(msg.requirement);
			XMAX_ASSERT(auth != nullptr, message_validate_exception, "Auth not found: ${auth}", ("auth", msg.requirement));
		}

		auto tlkey = std::make_tuple(msg.account, msg.code, msg.type);

		const auto linked = context.db.find<linked_permission_object, by_func>(tlkey);

		if (linked)
		{
			XMAX_ASSERT(linked->required_auth != msg.requirement, message_validate_exception, "Auth used: ${auth}", ("auth", msg.requirement));
			context.mutable_db.modify(*linked, [auth = msg.requirement](linked_permission_object& linked) {
				linked.required_auth = auth;
			});
		}
		else
		{
			context.mutable_db.create<linked_permission_object>([&msg](linked_permission_object& link) {
				link.account = msg.account;
				link.code = msg.code;
				link.func = msg.type;
				link.required_auth = msg.requirement;
			});
		}
		  

	} FC_CAPTURE_AND_RETHROW((msg))
}

void xmax_system_unlinkauth(Chain::message_context_xmax& context)
{
	Types::unlinkauth msg = context.msg.as<Types::unlinkauth>();

	context.require_authorization(msg.account);

	auto tlkey = std::make_tuple(msg.account, msg.code, msg.type);

	const auto linked = context.db.find<linked_permission_object, by_func>(tlkey);

	XMAX_ASSERT(linked != nullptr, message_validate_exception, "Linked permission not found");
	context.mutable_db.remove(*linked);
}

void xmax_system_transfer(message_context_xmax& context) {
   auto transfer = context.msg.as<Types::transfer>();

   try {
      XMAX_ASSERT(transfer.amount > 0, message_validate_exception, "Must transfer a positive amount");
      context.require_scope(transfer.to);
      context.require_scope(transfer.from);

      context.require_recipient(transfer.to);
      context.require_recipient(transfer.from);
   } FC_CAPTURE_AND_RETHROW((transfer))


   try {
      auto& db = context.mutable_db;
      const auto& from = db.get<xmx_token_object, by_owner_name>(transfer.from);

      XMAX_ASSERT(from.main_token >= transfer.amount, message_precondition_exception, "Insufficient Funds",
                 ("from.main_token",from.main_token)("transfer.amount",transfer.amount));

      const auto& to = db.get<xmx_token_object, by_owner_name>(transfer.to);
      db.modify(from, [&](xmx_token_object& a) {
         a.main_token -= share_type(transfer.amount);
      });
      db.modify(to, [&](xmx_token_object& a) {
         a.main_token += share_type(transfer.amount);
      });
   } FC_CAPTURE_AND_RETHROW( (transfer) ) 
}

void xmax_system_lock(message_context_xmax& context) {
    auto lock = context.msg.as<Types::lock>();

    XMAX_ASSERT(lock.amount > 0, message_validate_exception, "Locked amount must be positive");

    context.require_scope(lock.to);
    context.require_scope(lock.from);
    context.require_scope(Config::xmax_contract_name);

    context.require_authorization(lock.from);

    context.require_recipient(lock.to);
    context.require_recipient(lock.from);

    xmax_token_table xmax_token_tbl(context.mutable_db);
    resource_token_table resource_token_tbl(context.mutable_db);

    const xmx_token_object& locker = xmax_token_tbl.get(lock.from);

    XMAX_ASSERT( locker.main_token >= lock.amount, message_precondition_exception,
                 "Account ${a} lacks sufficient funds to lock ${amt} SUP", ("a", lock.from)("amt", lock.amount)("available",locker.main_token) );

    const auto& resource_token = resource_token_tbl.get(lock.to);

    xmax_token_tbl.modify(locker, [&lock](xmx_token_object& a) {
        a.main_token -= share_type(lock.amount);
    });

    resource_token_tbl.modify(resource_token, [&lock](resource_token_object& a){
       a.locked_token += share_type(lock.amount);
    });


    xmax_voting::increase_votes(context, lock.to, share_type(lock.amount));
}

void xmax_system_unlock(message_context_xmax& context)      {
    auto unlock = context.msg.as<Types::unlock>();

    context.require_authorization(unlock.account);

    XMAX_ASSERT(unlock.amount >= 0, message_validate_exception, "Unlock amount cannot be negative");

    resource_token_table resource_token_tbl(context.mutable_db);

    const auto& unlocker = resource_token_tbl.get(unlock.account);

    XMAX_ASSERT(unlocker.locked_token  >= unlock.amount, message_precondition_exception,
                "Insufficient locked funds to unlock ${a}", ("a", unlock.amount));

    resource_token_tbl.modify(unlocker, [&unlock, &context](resource_token_object& a) {
        a.locked_token -= share_type(unlock.amount);
        a.unlocked_token += share_type(unlock.amount);
        a.last_unlocked_time = context.current_time();
    });


    xmax_voting::decrease_votes(context, unlock.account, share_type(unlock.amount));
}

#ifdef USE_V8
void xmax_system_setjscode(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto  msg = context.msg.as<Types::setcode>();

	context.require_authorization(msg.account);

	FC_ASSERT(msg.vm_type == 0);
	FC_ASSERT(msg.vm_version == 0);

	abi_serializer(msg.code_abi).validate();

	const auto& contract = db.get<account_object, by_name>(msg.account);
	db.modify(contract, [&](account_object& a) {

		a.set_contract(msg.code, msg.code_abi);
	});

	message_context_xmax init_context(context.mutable_chain, context.mutable_db, context.trx, context.msg, msg.account,0);
	jsvm_xmax::get().init(init_context);

}
#endif

void xmax_system_setcode(message_context_xmax& context) {
	auto& db = context.mutable_db;
	auto  msg = context.msg.as<Types::setcode>();

	context.require_authorization(msg.account);

	FC_ASSERT(msg.vm_type == 0);
	FC_ASSERT(msg.vm_version == 0);

	/// if an ABI is specified make sure it is well formed and doesn't
	/// reference any undefined types
	abi_serializer(msg.code_abi).validate();


	const auto& contract = db.get<account_object, by_name>(msg.account);
	//   wlog( "set code: ${size}", ("size",msg.code.size()));
	db.modify(contract, [&](account_object& a) {

		a.set_contract(msg.code, msg.code_abi);
	});

	message_context_xmax init_context(context.mutable_chain, context.mutable_db, context.trx, context.msg, 0);
	vm_xmax::get().init(init_context);
}


void xmax_system_votebuilder(message_context_xmax& context)    {
    xmax_voting::vote_builder(context);
}
void xmax_system_regbuilder(message_context_xmax& context)    {
    xmax_voting::reg_builder(context);
}
void xmax_system_unregbuilder(message_context_xmax& context)    {
    xmax_voting::unreg_builder(context);
}
void xmax_system_regproxy(message_context_xmax& context)    {
    xmax_voting::reg_proxy(context);
}
void xmax_system_unregproxy(message_context_xmax& context)    {
    xmax_voting::unreg_proxy(context);
}

//--------------------------------------------------
void xmax_erc20_mint(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto minterc20 = context.msg.as<Types::minterc20>();

	//Check precondition
	auto& existing_token_obj = db.get<erc20_token_object, by_token_name>(minterc20.token_name);
	validate_token_not_revoke(existing_token_obj);

	validate_token_canmint(existing_token_obj);
	//Check owner authorization
	context.require_scope(existing_token_obj.owner_name);
	context.require_authorization(existing_token_obj.owner_name);//must signed by token sender.
	context.require_recipient(existing_token_obj.owner_name);


	auto account_owner = MakeErcTokenIndex(minterc20.token_name, existing_token_obj.owner_name);
	auto token_account_owner_ptr = db.find<erc20_token_account_object, by_token_and_owner>(account_owner);
	XMAX_ASSERT(token_account_owner_ptr != nullptr, message_validate_exception, "From have no tokens!");

	const auto& token_account_owner = db.get<erc20_token_account_object, by_token_and_owner>(account_owner);
	db.modify(token_account_owner, [&minterc20](erc20_token_account_object& obj) {
		obj.balance = SafeMath::add(obj.balance, minterc20.mint_amount.amount);
	});

	db.modify(existing_token_obj, [&minterc20](erc20_token_object& obj) {
		obj.total_supply = SafeMath::add(obj.total_supply, minterc20.mint_amount.amount);
	});
}
//--------------------------------------------------
void xmax_erc20_stopmint(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto stopminterc20 = context.msg.as<Types::stopminterc20>();

	//Check precondition
	auto& existing_token_obj = db.get<erc20_token_object, by_token_name>(stopminterc20.token_name);

	validate_token_not_revoke(existing_token_obj);

	//Check owner authorization
	validate_token_canmint(existing_token_obj);
	context.require_scope(existing_token_obj.owner_name);
	context.require_authorization(existing_token_obj.owner_name);//must signed by token sender.
	context.require_recipient(existing_token_obj.owner_name);


	db.modify(existing_token_obj, [](erc20_token_object& obj) {
		obj.stopmint = 1;
	});
}

//--------------------------------------------------
void xmax_erc20_revoke(Chain::message_context_xmax& context) {
	auto& db = context.mutable_db;
	auto revokeerc2o = context.msg.as<Types::revokeerc20>();

	//Todo: Check owner authorization

	//Check precondition
	auto& erc20_obj = db.get<erc20_token_object, by_token_name>(revokeerc2o.token_name);
	validate_token_not_revoke(erc20_obj);
	db.modify(erc20_obj, [](erc20_token_object& obj) {
		obj.revoked = 1;
	});

}
//--------------------------------------------------
void xmax_erc20_transferfrom(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto transfer_erc20 = context.msg.as<Types::transferfromerc20>();

	//Check sender authorization
	context.require_authorization(transfer_erc20.from);//must signed by token sender.

	context.require_scope(transfer_erc20.from);
	context.require_scope(transfer_erc20.to);
	context.require_recipient(transfer_erc20.from);
	context.require_recipient(transfer_erc20.to);

	validate_name(transfer_erc20.from);
	validate_name(transfer_erc20.to);

	auto& erc20_obj = db.get<erc20_token_object, by_token_name>(transfer_erc20.token_name);
	validate_token_not_revoke(erc20_obj);

	auto account_from = MakeErcTokenIndex(transfer_erc20.token_name, transfer_erc20.from);
	auto token_account_ptr_from = db.find<erc20_token_account_object, by_token_and_owner>(account_from);
	XMAX_ASSERT(token_account_ptr_from != nullptr, message_validate_exception, "From have no tokens!");

	auto account_to = MakeErcTokenIndex(transfer_erc20.token_name, transfer_erc20.to);
	auto token_account_ptr_to = db.find<erc20_token_account_object, by_token_and_owner>(account_to);
	if (!token_account_ptr_to)
	{
		db.create<erc20_token_account_object>([&transfer_erc20](erc20_token_account_object &obj) {
			obj.token_name = transfer_erc20.token_name;
			obj.owner_name = transfer_erc20.to;
			obj.balance = 0;
		});
	}
	const auto& token_account_from = db.get<erc20_token_account_object, by_token_and_owner>(account_from);
	const auto& token_account_to = db.get<erc20_token_account_object, by_token_and_owner>(account_to);

	db.modify(token_account_from, [&transfer_erc20](erc20_token_account_object& obj) {
		XMAX_ASSERT(obj.balance > transfer_erc20.value.amount, message_validate_exception, "From have no enough tokens!");
		obj.balance = SafeMath::sub(obj.balance, transfer_erc20.value.amount);
	});

	db.modify(token_account_to, [&transfer_erc20](erc20_token_account_object& obj) {
		obj.balance = SafeMath::add(obj.balance, transfer_erc20.value.amount);
	});

	//TODO: emit transfer event
}
//--------------------------------------------------
void xmax_erc721_mint(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto minterc721 = context.msg.as<Types::minterc721>();


	const auto& existing_token_obj = db.get<erc721_token_object, by_token_name>(minterc721.token_name);
	validate_token_not_revoke(existing_token_obj);
	validate_token_canmint(existing_token_obj);

	// check auth
	context.require_scope(existing_token_obj.owner_name);
	context.require_authorization(existing_token_obj.owner_name);//must signed by token sender.
	context.require_recipient(existing_token_obj.owner_name);


	xmax_erc721_id token_id{ minterc721.token_id };
	auto account_from = std::make_tuple(minterc721.token_name, token_id); 
	auto token_obj_ptr = db.find<erc721_token_account_object, by_token_and_tokenid>(account_from);


	XMAX_ASSERT(token_obj_ptr==nullptr,
		message_validate_exception,
		"The ERC721 token: ${token_name} with token id:${token_id} has already minted",
		("token_name", minterc721.token_name)("token_id", minterc721.token_id));

	db.create<erc721_token_account_object>([&minterc721,&existing_token_obj](erc721_token_account_object& token) {
		token.token_name = minterc721.token_name;
		token.token_owner = existing_token_obj.owner_name;
		xmax_erc721_id token_id{ minterc721.token_id };
		token.token_id = token_id;
	});
}

void xmax_erc721_stopmint(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto stopminterc721 = context.msg.as<Types::stopminterc721>();

	//Check precondition
	auto& existing_token_obj = db.get<erc721_token_object, by_token_name>(stopminterc721.token_name);

	validate_token_not_revoke(existing_token_obj);

	//Check owner authorization
	validate_token_canmint(existing_token_obj);
	context.require_authorization(existing_token_obj.owner_name);//must signed by token sender.
	context.require_scope(existing_token_obj.owner_name);
	context.require_recipient(existing_token_obj.owner_name);

	db.modify(existing_token_obj, [](erc721_token_object& obj) {
		obj.stopmint = 1;
	});
}

void xmax_erc721_transferfrom(Chain::message_context_xmax& context)
{
	auto& db = context.mutable_db;
	auto transferform721 = context.msg.as<Types::transferfromerc721>();
	// check auth
	context.require_authorization(transferform721.from);
	context.require_scope(transferform721.from);
	context.require_scope(transferform721.to);
	context.require_recipient(transferform721.from);
	context.require_recipient(transferform721.to);

	const auto& existing_token_obj = db.get<erc721_token_object, by_token_name>(transferform721.token_name);
	validate_token_not_revoke(existing_token_obj);

	xmax_erc721_id token_id{ transferform721.token_id };

	auto token_find_id = std::make_tuple(transferform721.token_name, token_id);
	auto token_obj_ptr = db.find<erc721_token_account_object, by_token_and_tokenid>(token_find_id);


	XMAX_ASSERT(token_obj_ptr != nullptr,
		message_validate_exception,
		"The ERC721 token: ${token_name} with token id:${token_id} has already minted",
		("token_name", transferform721.token_name)("token_id", transferform721.token_id));

	const auto& erc721_token = db.get<erc721_token_account_object, by_token_and_tokenid>(token_find_id);

	XMAX_ASSERT(erc721_token.token_owner == transferform721.from,
		message_validate_exception,
		"The ERC721 token: ${token_name} with token id:${token_id} not belong to account::${account}",
		("token_name", transferform721.token_name)("token_id", transferform721.token_id)("account", transferform721.from));

	XMAX_ASSERT(erc721_token.token_name == transferform721.token_name,
		message_validate_exception,
		"token_name table broken! all erc721 in danger!"
		);

	XMAX_ASSERT(erc721_token.token_id == token_id,
		message_validate_exception,
		"token_id table broken! all erc721 in danger!"
		);

	db.modify(erc721_token, [&transferform721](erc721_token_account_object& obj) {
		obj.token_owner = transferform721.to;
	});
}


//--------------------------------------------------
void xmax_erc721_revoke(Chain::message_context_xmax& context) {
	auto& db = context.mutable_db;
	auto revokeerc721 = context.msg.as<Types::revokeerc721>();

	//Todo: Check owner authorization
	
	//Check precondition
	auto& erc721_obj = db.get<erc721_token_object, by_token_name>(revokeerc721.token_name);
	validate_token_not_revoke(erc721_obj);
	db.modify(erc721_obj, [](erc721_token_object& obj) {
		obj.revoked = 1;
	});
}

	} // namespace Native
} // namespace Xmaxplatform


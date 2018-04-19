/**
*  @file
*  @copyright defined in xmax/LICENSE
*/

#include <vm_native_interface.hpp>

namespace Xmaxplatform {namespace Chain {

	void vm_native_log()
	{

	}

	void vm_prints(const std::string& str)
	{
		ilog(str);
	}
	BIND_VM_NATIVE_FUCTION_VOID_R(vm_prints, prints, vm_arg::ds_string)


	vm_arg::time_t vm_xmax_now()
	{
		return vm_xmax::get().current_validate_context->current_time().sec_since_epoch();
	}
	BIND_VM_NATIVE_FUCTION_RT_VIOD(vm_xmax_now, xmax_now, vm_arg::ds_time)


	DEFINE_INTRINSIC_FUNCTION2(evn, floatMinxx, floatMinxx, f32, f32, left, f32, right) 
	{ 
		return 0; 
	}

	void checktime(int64_t duration, uint32_t checktime_limit)
	{
		if (duration > checktime_limit) {
			wlog("checktime called ${d}", ("d", duration));
			throw checktime_exceeded();
		}
	}

	DEFINE_INTRINSIC_FUNCTION0(env, checktime, checktime, none) {
		checktime(vm_xmax::get().current_execution_time(), vm_xmax::get().checktime_limit);
	}
}}
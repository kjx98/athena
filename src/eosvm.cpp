/*
 * Copyright 2019-2020 Jesse Kuang
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <eosio/vm/backend.hpp>
#include <eosio/vm/error_codes.hpp>
#include <eosio/vm/host_function.hpp>
#include <eosio/vm/watchdog.hpp>

#include "eosvm.h"

#include <chrono>
#include <iostream>

using namespace eosio;
using namespace eosio::vm;
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"

namespace eosio::vm {

template <typename T>
struct wasm_type_converter<T*> {
   static T*    from_wasm(void* val) { return (T*)val; }
   static void* to_wasm(T* val) { return (void*)val; }
};

template <typename T>
struct wasm_type_converter<T&> {
   static T& from_wasm(T* val) { return *val; }
   static T* to_wasm(T& val) { return std::addressof(val); }
};
} // namespace eosio::vm

using namespace std;

namespace athena {

class EOSvmEthereumInterface;
using backend_t = eosio::vm::backend<EOSvmEthereumInterface, eosio::vm::jit>;

class EOSvmEthereumInterface : public EthereumInterface {
public:
  explicit EOSvmEthereumInterface(
    evmc::HostContext& _context,
    bytes_view _code,
    evmc_message const& _msg,
    ExecutionResult & _result,
    bool _meterGas
  ):
    EthereumInterface(_context, _code, _msg, _result, _meterGas)
  {}

  void setBackend(backend_t* bkPtr) {
    m_backend = bkPtr;
  }

private:
  // These assume that m_wasmMemory was set prior to execution.
  size_t memorySize() const override {
	return m_backend->get_context().current_linear_memory();
  }
  void memorySet(size_t offset, uint8_t value) override {
	auto memPtr = m_backend->get_context().linear_memory();
	memPtr[offset] = static_cast<char>(value);
  }
  uint8_t memoryGet(size_t offset) override {
	auto memPtr = m_backend->get_context().linear_memory();
	return static_cast<uint8_t>(memPtr[offset]);
  }
  uint8_t* memoryPointer(size_t offset, size_t length) override {
    ensureCondition(memorySize() >= (offset + length), InvalidMemoryAccess, "Memory is shorter than requested segment");
	auto memPtr = m_backend->get_context().linear_memory();
    return reinterpret_cast<uint8_t*>(&memPtr[offset]);
  }

  backend_t* m_backend;
};


unique_ptr<WasmEngine> EOSvmEngine::create()
{
  return unique_ptr<WasmEngine>{new EOSvmEngine};
}

ExecutionResult EOSvmEngine::execute(
  evmc::HostContext& context,
  bytes_view code,
  bytes_view state_code,
  evmc_message const& msg,
  bool meterInterfaceGas
) {

   wasm_allocator wa;
   using rhf_t = eosio::vm::registered_host_functions<EOSvmEthereumInterface>;
    instantiationStarted();

#ifdef	ommit
   // register eth_finish
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_finish,
		   wasm_allocator>("ethereum", "finish");
   // register eth_getCallDataSize
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_getCallDataSize,
		   wasm_allocator>("ethereum",
                                                                                            "getCallDataSize");
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_callDataCopy,
		   wasm_allocator>("ethereum", "callDataCopy");
#endif
   backend_t bkend(code);
   bkend.set_wasm_allocator(&wa);

      rhf_t::resolve(bkend.get_module());
      bkend.get_module().finalize();
      bkend.initialize();
    ExecutionResult result; // = internalExecute(context, code, state_code, msg, meterInterfaceGas);
  EOSvmEthereumInterface interface{context, state_code, msg, result, meterInterfaceGas};
         try {
            // bkend.execute_all(null_watchdog());
            bkend.call(&interface, "test", "main");
         } catch (wasm_exit_exception const&) {
            // This exception is ignored here because we consider it to be a success.
            // It is only a clutch for POSIX style exit()

  } catch (EndExecution const&) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
   } catch (const eosio::vm::exception& ex) {
      std::cerr << "eos-vm interpreter error\n";
      std::cerr << ex.what() << " : " << ex.detail() << "\n";
  }
  executionFinished();
  return result;
}

} // namespace athena

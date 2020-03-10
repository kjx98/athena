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


struct ewasm_host_methods {
   // example of a host "method"
   void eth_finish(const char* msg, uint32_t l) {
	if (l >= 4) {
      uint32_t r = *(uint32_t*)(msg + l - 4);
#ifndef NDEBUG
      std::cerr << "finish value: " << __builtin_bswap32(r) << std::endl;
#endif
	}
#ifndef NDEBUG
	else
      std::cerr << "finish w/out value or less than 4" << std::endl;
#endif
	throw wasm_exit_exception{ "Exit" };
   }
   int32_t eth_getCallDataSize() { return field.size(); }
   void    eth_callDataCopy(void* res, int32_t _off, uint32_t l) {
      uint32_t ll = field.size();
      if (_off >= ll)
         return;
      if (_off + l > ll)
         l = ll - _off;
      void* src = (void*)(field.data() + _off);
      memcpy(res, src, l);
   }
   // example of another type of host function
   static void* memset(void* ptr, int x, size_t n) { return ::memset(ptr, x, n); }
   std::string  field = "";
};

int main11(int argc, char** argv) {
   wasm_allocator wa;
   using backend_t = eosio::vm::backend<ewasm_host_methods, eosio::vm::jit>;
   //using backend_t = eosio::vm::backend<ewasm_host_methods>;
   using rhf_t = eosio::vm::registered_host_functions<ewasm_host_methods>;
   inputs[4+31] = 15;
   std::string	in_("test", sizeof(inputs));
   ewasm_host_methods myHost{ in_ };

   if (argc < 2) {
      std::cerr << "Error, no wasm file provided\n";
      return -1;
   }
   // register eth_finish
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_finish,
		   wasm_allocator>("ethereum", "finish");
   // register eth_getCallDataSize
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_getCallDataSize,
		   wasm_allocator>("ethereum",
                                                                                            "getCallDataSize");
   rhf_t::add<ewasm_host_methods, &ewasm_host_methods::eth_callDataCopy,
		   wasm_allocator>("ethereum", "callDataCopy");
   auto t3 = std::chrono::high_resolution_clock::now();
   try {

      auto code = backend_t::read_wasm(argv[1]);

      auto      t1 = std::chrono::high_resolution_clock::now();
      backend_t bkend(code);
      auto      t2 = std::chrono::high_resolution_clock::now();
      std::cout << "Startup " << std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() << " ns\n";

      bkend.set_wasm_allocator(&wa);

      auto t3 = std::chrono::high_resolution_clock::now();
      rhf_t::resolve(bkend.get_module());
      bkend.get_module().finalize();
      bkend.initialize();
      auto t32 = std::chrono::high_resolution_clock::now();
      std::cout << "Resolv module import " << std::chrono::duration_cast<std::chrono::nanoseconds>(t32 - t3).count() << " ns\n";
      t3 = std::chrono::high_resolution_clock::now();
#ifdef NDEBUG
      for (int i = 0; i < 400; ++i)
#endif
      {
         try {
            // bkend.execute_all(null_watchdog());
            bkend.call(&myHost, "test", "main");
         } catch (wasm_exit_exception const&) {
            // This exception is ignored here because we consider it to be a success.
            // It is only a clutch for POSIX style exit()
#ifndef NDEBUG
            auto t4 = std::chrono::high_resolution_clock::now();
            std::cout << "finish Exit " << std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count()
                      << "\n";
#endif
         }
      }
      auto t4 = std::chrono::high_resolution_clock::now();
#ifdef NDEBUG
      std::cout << "Execution " << std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count() / 400 << " ns\n";
#else
      std::cout << "Execution " << std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count() << " ns\n";
#endif

   } catch (const eosio::vm::exception& ex) {
      auto t4 = std::chrono::high_resolution_clock::now();
      std::cout << "Execution " << std::chrono::duration_cast<std::chrono::nanoseconds>(t4 - t3).count() << "\n";
      std::cerr << "eos-vm interpreter error\n";
      std::cerr << ex.what() << " : " << ex.detail() << "\n";
   }
   return 0;
}

using namespace std;

namespace athena {

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

  void setWasmMemory(Runtime::MemoryInstance* _wasmMemory) {
    m_wasmMemory = _wasmMemory;
  }

private:
  // These assume that m_wasmMemory was set prior to execution.
  size_t memorySize() const override { return Runtime::getMemoryNumPages(m_wasmMemory) * 65536; }
  void memorySet(size_t offset, uint8_t value) override { Runtime::memoryRef<U8>(m_wasmMemory, offset) = value; }
  uint8_t memoryGet(size_t offset) override { return Runtime::memoryRef<U8>(m_wasmMemory, offset); }
  uint8_t* memoryPointer(size_t offset, size_t length) override {
    ensureCondition(memorySize() >= (offset + length), InvalidMemoryAccess, "Memory is shorter than requested segment");
    return Runtime::memoryArrayPtr<U8>(m_wasmMemory, offset, length);
  }

  Runtime::MemoryInstance* m_wasmMemory;
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
  try {
    instantiationStarted();
    //ExecutionResult result = internalExecute(context, code, state_code, msg, meterInterfaceGas);
    // And clean up mess left by this run.
    //Runtime::collectGarbage();
    executionFinished();
    return result;
  } catch (exception const&) {
    // And clean up mess left by this run.
    //Runtime::collectGarbage();
    // We only catch this exception here in order to clean up garbage..
    // TODO: hopefully WAVM is fixed so that this isn't needed
    throw;
  }
}

} // namespace athena

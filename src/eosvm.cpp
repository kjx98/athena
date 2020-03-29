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

#include "debugging.h"
#include "eosvm.h"

#include <chrono>
#include <iostream>

using namespace eosio;
using namespace eosio::vm;
//#pragma GCC diagnostic ignored "-Wunused-parameter"
//#pragma GCC diagnostic ignored "-Wunused-variable"

namespace eosio::vm {

template <typename T> struct wasm_type_converter<T *> {
  static T *from_wasm(void *val) { return (T *)val; }
  static void *to_wasm(T *val) { return (void *)val; }
};

template <typename T> struct wasm_type_converter<T &> {
  static T &from_wasm(T *val) { return *val; }
  static T *to_wasm(T &val) { return std::addressof(val); }
};
} // namespace eosio::vm

using namespace std;
using namespace evmc;

namespace athena {

const string ethMod = "ethereum";
const string dbgMod = "debug";

class EOSvmEthereumInterface;
using backend_t = eosio::vm::backend<EOSvmEthereumInterface, eosio::vm::jit>;
// using backend_t = eosio::vm::backend<EOSvmEthereumInterface>;

class EOSvmEthereumInterface : public EthereumInterface {
public:
  explicit EOSvmEthereumInterface(evmc::HostContext &_context, bytes_view _code,
                                  evmc_message const &_msg,
                                  ExecutionResult &_result, bool _meterGas)
      : EthereumInterface(_context, _code, _msg, _result, _meterGas) {}

#if H_DEBUGGING
  void dbgPrint(char *, uint32_t length);
  void dbgPrintMem(uint8_t *dp, uint32_t length) {
    debugPrintMemImpl(false, dp, length);
  }
  void dbgPrintMemHex(uint8_t *dp, uint32_t length) {
    debugPrintMemImpl(true, dp, length);
  }
  void dbgPrintStorage(uint8_t *dp) { debugPrintStorageImpl(false, dp); }
  void dbgPrintStorageHex(uint8_t *dp) { debugPrintStorageImpl(true, dp); }
#endif
  void eGetAddress(uint8_t *result);
  void eStorageStore(bytes32 *path, bytes32 *valuePtr);
  void eStorageLoad(bytes32 *path, bytes32 *result);
  void eGetCaller(uint8_t *result);
  void eSelfDestruct(address *result);
  void eCallDataCopy(uint8_t *result, uint32_t dataOffset, uint32_t length);
  void eFinish(void *dp, uint32_t siz) { eRevertOrFinish(false, dp, siz); }
  void eRevert(void *dp, uint32_t siz) { eRevertOrFinish(true, dp, siz); }

private:
#if H_DEBUGGING
  void debugPrintMemImpl(bool, uint8_t *, uint32_t);
  void debugPrintStorageImpl(bool, uint8_t *);
#endif
  void eRevertOrFinish(bool revert, void *dp, uint32_t size);
  size_t memorySize() const override { return 0; }
  void memorySet(size_t offset, uint8_t value) override {}
  uint8_t memoryGet(size_t offset) override { return 0; }
  uint8_t *memoryPointer(size_t offset, size_t length) override {
    return nullptr;
  }
};

#if H_DEBUGGING
void EOSvmEthereumInterface::dbgPrint(char *dp, uint32_t length) {
  H_DEBUG << depthToString() << " DEBUG print: ";
  {
    cerr << hex;
    for (uint32_t i = 0; i < length; i++) {
      cerr << dp[i];
    }
    cerr << dec;
  }
  H_DEBUG << endl;
}

void EOSvmEthereumInterface::debugPrintMemImpl(bool useHex, uint8_t *dp,
                                               uint32_t length) {
  cerr << depthToString() << " DEBUG printMem" << (useHex ? "Hex(" : "(") << hex
       << "0x" << (uint32_t)((uint64_t)dp) << ":0x" << length << "): " << dec;
  if (useHex) {
    cerr << hex;
  }
  for (uint32_t i = 0; i < length; i++) {
    cerr << dp[i] << " ";
  }
  if (useHex) {
    cerr << dec;
  }
  cerr << endl;
}

void EOSvmEthereumInterface::debugPrintStorageImpl(bool useHex, uint8_t *dp) {
  evmc_uint256be path;
  memcpy(&path, dp, sizeof(path));

  H_DEBUG << depthToString() << " DEBUG printStorage" << (useHex ? "Hex" : "")
          << "(0x" << hex;

  // Print out the path
  for (uint8_t b : path.bytes)
    cerr << static_cast<int>(b);

  H_DEBUG << "): " << dec;

  evmc_bytes32 result = m_host.get_storage(m_msg.destination, path);

  if (useHex) {
    cerr << hex;
    for (uint8_t b : result.bytes)
      cerr << static_cast<int>(b) << " ";
    cerr << dec;
  } else {
    for (uint8_t b : result.bytes)
      cerr << b << " ";
  }
  cerr << endl;
}
#endif

void EOSvmEthereumInterface::eCallDataCopy(uint8_t *result, uint32_t dataOffset,
                                           uint32_t length) {
#if H_DEBUGGING
  H_DEBUG << depthToString() << " callDataCopy " << hex
          << (uint32_t)((uint64_t)result) << " " << dataOffset << " " << length
          << dec << "\n";
#endif
  if (dataOffset >= m_msg.input_size)
    return; // no copy

  safeChargeDataCopy(length, GasSchedule::verylow);

  if (dataOffset + length > m_msg.input_size)
    length = m_msg.input_size - dataOffset;
  memcpy(result, m_msg.input_data + dataOffset, length);
}

void EOSvmEthereumInterface::eGetCaller(uint8_t *result) {
  H_DEBUG << depthToString() << " getCaller " << hex
          << (uint32_t)((uint64_t)result) << dec << "\n";

  takeInterfaceGas(GasSchedule::base);
  memcpy(result, &m_msg.sender, sizeof(m_msg.sender));
}

void EOSvmEthereumInterface::eGetAddress(uint8_t *result) {
  H_DEBUG << depthToString() << " getAddress " << hex
          << (uint32_t)((uint64_t)result) << dec << "\n";

  takeInterfaceGas(GasSchedule::base);
  memcpy(result, &m_msg.destination, sizeof(m_msg.destination));
}

void EOSvmEthereumInterface::eSelfDestruct(address *result) {
  H_DEBUG << depthToString() << " selfDestruct " << hex
          << (uint32_t)((uint64_t)result) << dec << "\n";

  takeInterfaceGas(GasSchedule::balance);
  m_host.selfdestruct(m_msg.destination, *result);
  throw EndExecution{};
}

void EOSvmEthereumInterface::eStorageStore(bytes32 *path, bytes32 *valuePtr) {
  H_DEBUG << depthToString() << " storageStore " << hex
          << (uint32_t)((uint64_t)path) << " " << (uint32_t)((uint64_t)valuePtr)
          << dec << "\n";

  // Charge this here as it is the minimum cost.
  takeInterfaceGas(GasSchedule::storageStoreChange);

  ensureCondition(!(m_msg.flags & EVMC_STATIC), StaticModeViolation,
                  "storageStore");

  const auto current = m_host.get_storage(m_msg.destination, *path);

  // Charge the right amount in case of the create case.
  if (is_zero(current) && !is_zero(*valuePtr))
    takeInterfaceGas(GasSchedule::storageStoreCreate -
                     GasSchedule::storageStoreChange);

  // We do not need to take care about the delete case (gas refund), the client
  // does it.

  m_host.set_storage(m_msg.destination, *path, *valuePtr);
}

void EOSvmEthereumInterface::eStorageLoad(bytes32 *path, bytes32 *result) {
  H_DEBUG << depthToString() << " storageLoad " << hex
          << (uint32_t)((uint64_t)path) << " " << (uint32_t)((uint64_t)result)
          << dec << "\n";

  takeInterfaceGas(GasSchedule::storageLoad);

  *result = m_host.get_storage(m_msg.destination, *path);
}

void EOSvmEthereumInterface::eRevertOrFinish(bool revert, void *dp,
                                             uint32_t size) {
#if H_DEBUGGING
  H_DEBUG << depthToString() << " " << (revert ? "revert " : "finish ") << hex
          << (uint32_t)((uint64_t)dp) << " " << size << dec << "\n";
#endif

  m_result.returnValue = bytes(size, '\0');
  memcpy(m_result.returnValue.data(), dp, size);

  m_result.isRevert = revert;

  throw EndExecution{};
}

unique_ptr<WasmEngine> EOSvmEngine::create() {
  return unique_ptr<WasmEngine>{new EOSvmEngine};
}

ExecutionResult EOSvmEngine::execute(evmc::HostContext &context,
                                     bytes_view code, bytes_view state_code,
                                     evmc_message const &msg,
                                     bool meterInterfaceGas) {

  wasm_allocator wa;
  using rhf_t = eosio::vm::registered_host_functions<EOSvmEthereumInterface>;
#if H_DEBUGGING
  H_DEBUG << "Executing with eosvm...\n";
#endif
  instantiationStarted();

  // register eth_finish
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eFinish,
             wasm_allocator>(ethMod, "finish");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eRevert,
             wasm_allocator>(ethMod, "revert");
  // register eth_getCallDataSize
  rhf_t::add<EOSvmEthereumInterface,
             &EOSvmEthereumInterface::eeiGetCallDataSize, wasm_allocator>(
      ethMod, "getCallDataSize");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eCallDataCopy,
             wasm_allocator>(ethMod, "callDataCopy");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eGetAddress,
             wasm_allocator>(ethMod, "getAddress");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eStorageStore,
             wasm_allocator>(ethMod, "storageStore");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eStorageLoad,
             wasm_allocator>(ethMod, "storageLoad");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eGetCaller,
             wasm_allocator>(ethMod, "getCaller");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eSelfDestruct,
             wasm_allocator>(ethMod, "selfDestruct");
#if H_DEBUGGING
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::dbgPrint,
             wasm_allocator>(dbgMod, "print");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::debugPrint32,
             wasm_allocator>(dbgMod, "print32");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::debugPrint64,
             wasm_allocator>(dbgMod, "print64");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::dbgPrintMem,
             wasm_allocator>(dbgMod, "printMem");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::dbgPrintMemHex,
             wasm_allocator>(dbgMod, "printMemHex");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::dbgPrintStorage,
             wasm_allocator>(dbgMod, "printStorage");
  rhf_t::add<EOSvmEthereumInterface,
             &EOSvmEthereumInterface::dbgPrintStorageHex, wasm_allocator>(
      dbgMod, "printStorageHex");
#endif
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eeiUseGas,
             wasm_allocator>(ethMod, "useGas");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eeiGetGasLeft,
             wasm_allocator>(ethMod, "getGasLeft");
  rhf_t::add<EOSvmEthereumInterface, &EOSvmEthereumInterface::eeiGetBlockNumber,
             wasm_allocator>(ethMod, "getBlockNumber");
#if H_DEBUGGING
  H_DEBUG << "Reading ewasm with eosvm...\n";
#endif
  wasm_code_ptr wcodePtr((uint8_t *)code.data(), code.size());
  // wasm_code wcode(code.begin(), code.end());
  backend_t bkend(wcodePtr, code.size());
  bkend.set_wasm_allocator(&wa);

#if H_DEBUGGING
  H_DEBUG << "Resolving ewasm with eosvm...\n";
#endif
  rhf_t::resolve(bkend.get_module());
  bkend.get_module().finalize();
  bkend.initialize();
#if H_DEBUGGING
  H_DEBUG << "Resolved with eosvm...\n";
#endif
  ExecutionResult result; // = internalExecute(context, code, state_code, msg,
                          // meterInterfaceGas);
  EOSvmEthereumInterface interface{context, state_code, msg, result,
                                   meterInterfaceGas};
  executionStarted();
  try {
    uint32_t main_idx = bkend.get_module().get_exported_function("main");
    // bkend.execute_all(null_watchdog());
    // bkend.call(&interface, "test", "main");
    auto res = bkend.call(&interface, main_idx);
    // Wrap any non-EEI exception under VMTrap.
    ensureCondition(res, VMTrap, "The VM invocation had a trap.");
  } catch (wasm_exit_exception const &) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for POSIX style exit()
    ensureCondition(bkend.get_context().get_error_code().value() == 0, VMTrap,
                    "The VM exit code not zero.");
  } catch (EndExecution const &) {
    // This exception is ignored here because we consider it to be a success.
    // It is only a clutch for eth_finish() and eth_revert()
  } catch (const eosio::vm::exception &ex) {
    std::cerr << "eos-vm interpreter error\n";
    std::cerr << ex.what() << " : " << ex.detail() << "\n";
    result.isRevert = true;
    // result.gasLeft = 0;
  }
  executionFinished();
  return result;
}

} // namespace athena

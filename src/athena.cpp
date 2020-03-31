/*
 * Copyright 2016-2018 Alex Beregszaszi et al.
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

#include <athena/athena.h>

#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unistd.h>
#include <csignal>
#include <execinfo.h>

#include <evmc/evmc.h>

#include "debugging.h"
#include "eei.h"
#include "exceptions.h"
#include "helpers.h"
#if H_EOS
#include "eosvm.h"
#endif
#if H_WABT
#include "wabt.h"
#endif

#include <athena/buildinfo.h>

using namespace std;
using namespace athena;

namespace {

enum class athena_evm1mode {
  reject,
  fallback,
  evm2wasm_contract,
  runevm_contract,
};

const map<string, athena_evm1mode> evm1mode_options{
    {"reject", athena_evm1mode::reject},
    {"fallback", athena_evm1mode::fallback},
    {"evm2wasm", athena_evm1mode::evm2wasm_contract},
    {"runevm", athena_evm1mode::runevm_contract},
};

using WasmEngineCreateFn = unique_ptr<WasmEngine> (*)();

const map<string, WasmEngineCreateFn> wasm_engine_map {
#if H_EOS
  {"eosvm", EOSvmEngine::create},
#endif
#if H_WABT
      {"wabt", WabtEngine::create},
#endif
};

WasmEngineCreateFn wasmEngineCreateFn =
// This is the order of preference.
#if H_WABT
    WabtEngine::create
#elif H_EOS
    EOSvmEngine::create
#else
#error "No engine requested."
#endif
    ;

struct athena_instance : evmc_vm {
  unique_ptr<WasmEngine> engine = wasmEngineCreateFn();
  athena_evm1mode evm1mode = athena_evm1mode::reject;
  bool metering = false;
  map<evmc::address, bytes> contract_preload_list;

  athena_instance() noexcept
      : evmc_vm({EVMC_ABI_VERSION, "athena",
                 athena_get_buildinfo()->project_version, nullptr, nullptr,
                 nullptr, nullptr}) {}
};

using namespace evmc::literals;

const auto sentinelAddress = 0x000000000000000000000000000000000000000a_address;
const auto evm2wasmAddress = 0x000000000000000000000000000000000000000b_address;
const auto runevmAddress = 0x000000000000000000000000000000000000000c_address;

// Calls a system contract at @address with input data @input.
// It is a "staticcall" with sender 000...000 and no value.
// @returns output data from the contract and update the @gas variable with the
// gas left.
pair<evmc_status_code, bytes> callSystemContract(evmc::HostContext &context,
                                                 evmc_address const &address,
                                                 int64_t &gas,
                                                 bytes_view input) {
  evmc_message message = {
      .kind = EVMC_CALL,
      .flags = EVMC_STATIC,
      .depth = 0,
      .gas = gas,
      .destination = address,
      .sender = {},
      .input_data = input.data(),
      .input_size = input.size(),
      .value = {},
      .create2_salt = {},
  };

  evmc::result result = context.call(message);

  bytes ret;
  if (result.status_code == EVMC_SUCCESS && result.output_data)
    ret.assign(result.output_data, result.output_data + result.output_size);

  gas = result.gas_left;

  return {result.status_code, ret};
}

pair<evmc_status_code, bytes> locallyExecuteSystemContract(
    evmc::HostContext &context, evmc_address const &address, int64_t &gas,
    bytes_view input, bytes_view code, bytes_view state_code) {
  const evmc_message message = {
      .kind = EVMC_CALL,
      .flags = EVMC_STATIC,
      .depth = 0,
      .gas = gas,
      .destination = address,
      .sender = {},
      .input_data = input.data(),
      .input_size = input.size(),
      .value = {},
      .create2_salt = {},
  };

  unique_ptr<WasmEngine> engine = wasmEngineCreateFn();
  // TODO: should we catch exceptions here?
  ExecutionResult result =
      engine->execute(context, code, state_code, message, false);

  bytes ret;
  evmc_status_code status = result.isRevert ? EVMC_REVERT : EVMC_SUCCESS;
  if (status == EVMC_SUCCESS && result.returnValue.size() > 0)
    ret = move(result.returnValue);

  return {status, move(ret)};
}

// Calls the Sentinel contract with input data @input.
// @returns the validated and metered output or empty output otherwise.
bytes sentinel(evmc::HostContext &context, bytes_view input) {
#if H_DEBUGGING
  H_DEBUG << "Metering (input " << input.size() << " bytes)...\n";
#endif

  int64_t startgas =
      numeric_limits<int64_t>::max(); // do not charge for metering yet (give
                                      // unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  bytes ret;

  tie(status, ret) = callSystemContract(context, sentinelAddress, gas, input);

#if H_DEBUGGING
  H_DEBUG << "Metering done (output " << ret.size() << " bytes, used "
          << (startgas - gas) << " gas) with code=" << status << "\n";
#endif

  ensureCondition(status == EVMC_SUCCESS, ContractValidationFailure,
                  "Sentinel has failed on contract. It is invalid.");

  return ret;
}

// Calls the evm2wasm contract with input data @input.
// @returns the compiled output or empty output otherwise.
bytes evm2wasm(evmc::HostContext &context, bytes_view input) {
  H_DEBUG << "Calling evm2wasm (input " << input.size() << " bytes)...\n";

  int64_t startgas =
      numeric_limits<int64_t>::max(); // do not charge for metering yet (give
                                      // unlimited gas)
  int64_t gas = startgas;
  evmc_status_code status;
  bytes ret;

  tie(status, ret) = callSystemContract(context, evm2wasmAddress, gas, input);

  H_DEBUG << "evm2wasm done (output " << ret.size() << " bytes, used "
          << (startgas - gas) << " gas) with status=" << status << "\n";

  ensureCondition(status == EVMC_SUCCESS, ContractValidationFailure,
                  "evm2wasm has failed.");

  return ret;
}

// Calls the runevm contract.
// @returns a wasm-based evm interpreter.
bytes runevm(evmc::HostContext &context, bytes code) {
  H_DEBUG << "Calling runevm (code " << code.size() << " bytes)...\n";

  int64_t gas = numeric_limits<int64_t>::max(); // do not charge for metering
                                                // yet (give unlimited gas)
  evmc_status_code status;
  bytes ret;

  tie(status, ret) =
      locallyExecuteSystemContract(context, runevmAddress, gas, {}, code, code);

  H_DEBUG << "runevm done (output " << ret.size()
          << " bytes) with status=" << status << "\n";

  ensureCondition(status == EVMC_SUCCESS, ContractValidationFailure,
                  "runevm has failed.");
  ensureCondition(ret.size() > 0, ContractValidationFailure,
                  "Runevm returned empty.");
  ensureCondition(hasWasmPreamble(ret), ContractValidationFailure,
                  "Runevm result has no wasm preamble.");

  return ret;
}

void athena_destroy_result(evmc_result const *result) noexcept {
  delete[] result->output_data;
}

evmc_result athena_execute(evmc_vm *instance,
                           const evmc_host_interface *host_interface,
                           evmc_host_context *context, enum evmc_revision rev,
                           const evmc_message *msg, const uint8_t *code,
                           size_t code_size) noexcept {
  athena_instance *athena = static_cast<athena_instance *>(instance);
  evmc::HostContext host{*host_interface, context};

#if H_DEBUGGING
  H_DEBUG << "Executing message in Athena\n";
#endif

  evmc_result ret;
  memset(&ret, 0, sizeof(evmc_result));

  try {
    athenaAssert(rev == EVMC_BYZANTIUM, "Only Byzantium supported.");
    athenaAssert(msg->gas >= 0, "EVMC supplied negative startgas");

    bool meterInterfaceGas = true;

    // the bytecode residing in the state - this will be used by interface
    // methods (i.e. codecopy)
    bytes_view state_code{code, code_size};

    // the actual executable code - this can be modified (metered or evm2wasm
    // compiled)
    bytes run_code{state_code};

    // replace executable code if replacement is supplied
    auto preload = athena->contract_preload_list.find(msg->destination);
    if (preload != athena->contract_preload_list.end()) {
#if H_DEBUGGING
      H_DEBUG << "Overriding contract.\n";
#endif
      run_code = preload->second;
    }

    // ensure we can only handle WebAssembly version 1
    bool isWasm = hasWasmPreamble(run_code);

    if (!isWasm) {
      switch (athena->evm1mode) {
      case athena_evm1mode::evm2wasm_contract:
        run_code = evm2wasm(host, run_code);
        ensureCondition(run_code.size() > 8, ContractValidationFailure,
                        "Transcompiling via evm2wasm failed");
        // TODO: enable this once evm2wasm does metering of interfaces
        // meterInterfaceGas = false;
        break;
      case athena_evm1mode::fallback:
        H_DEBUG << "Non-WebAssembly input, but fallback mode enabled, asking "
                   "client to deal with it.\n";
        ret.status_code = EVMC_REJECTED;
        return ret;
      case athena_evm1mode::reject:
        H_DEBUG << "Non-WebAssembly input, failure.\n";
        ret.status_code = EVMC_FAILURE;
        return ret;
      case athena_evm1mode::runevm_contract:
        run_code = runevm(host, athena->contract_preload_list[runevmAddress]);
        ensureCondition(run_code.size() > 8, ContractValidationFailure,
                        "Interpreting via runevm failed");
        // Runevm does interface metering on its own
        meterInterfaceGas = false;
        break;
      }
    }

    ensureCondition(hasWasmVersion(run_code, 1), ContractValidationFailure,
                    "Contract has an invalid WebAssembly version.");

    // Avoid this in case of evm2wasm translated code
    if (msg->kind == EVMC_CREATE && isWasm) {
      // Meter the deployment (constructor) code if it is WebAssembly
      if (athena->metering)
        run_code = sentinel(host, run_code);
      ensureCondition(hasWasmPreamble(run_code) && hasWasmVersion(run_code, 1),
                      ContractValidationFailure,
                      "Invalid contract or metering failed.");
    }

    athenaAssert(athena->engine, "Wasm engine not set.");
    WasmEngine &engine = *athena->engine;

    ExecutionResult result;
    // should move after execution if want remember owner's address
    if (msg->kind == EVMC_CREATE) {
      ensureCondition(msg->input_size == 0, ContractValidationFailure,
                      "create must without input");
      result.gasLeft = msg->gas;
      result.isRevert = false;
      result.returnValue = run_code;
    } else {
      result =
          engine.execute(host, run_code, state_code, *msg, meterInterfaceGas);
      athenaAssert(result.gasLeft >= 0, "Negative gas left after execution.");
    }

    // copy call result
    if (result.returnValue.size() > 0) {
      bytes returnValue;

      if (msg->kind == EVMC_CREATE && !result.isRevert &&
          hasWasmPreamble(result.returnValue)) {
        ensureCondition(hasWasmVersion(result.returnValue, 1),
                        ContractValidationFailure,
                        "Contract has an invalid WebAssembly version.");

        // Meter the deployed code if it is WebAssembly
        returnValue = athena->metering ? sentinel(host, result.returnValue)
                                       : move(result.returnValue);
        ensureCondition(
            hasWasmPreamble(returnValue) && hasWasmVersion(returnValue, 1),
            ContractValidationFailure, "Invalid contract or metering failed.");
        // FIXME: this should be done by the sentinel
        // no verifyContract
      } else {
        returnValue = move(result.returnValue);
      }

      uint8_t *output_data = new uint8_t[returnValue.size()];
      copy(returnValue.begin(), returnValue.end(), output_data);

      ret.output_size = returnValue.size();
      ret.output_data = output_data;
      ret.release = athena_destroy_result;
    }

    ret.status_code = result.isRevert ? EVMC_REVERT : EVMC_SUCCESS;
    ret.gas_left = result.gasLeft;
  } catch (EndExecution const &) {
    ret.status_code = EVMC_INTERNAL_ERROR;
#if H_DEBUGGING
    H_DEBUG << "EndExecution exception has leaked through.\n";
#endif
  } catch (VMTrap const &e) {
    // TODO: use specific error code? EVMC_INVALID_INSTRUCTION or
    // EVMC_TRAP_INSTRUCTION?
    ret.status_code = EVMC_FAILURE;
    H_DEBUG << e.what() << "\n";
  } catch (ArgumentOutOfRange const &e) {
    ret.status_code = EVMC_ARGUMENT_OUT_OF_RANGE;
    H_DEBUG << e.what() << "\n";
  } catch (OutOfGas const &e) {
    ret.status_code = EVMC_OUT_OF_GAS;
    H_DEBUG << e.what() << "\n";
  } catch (ContractValidationFailure const &e) {
    ret.status_code = EVMC_CONTRACT_VALIDATION_FAILURE;
    H_DEBUG << e.what() << "\n";
  } catch (InvalidMemoryAccess const &e) {
    ret.status_code = EVMC_INVALID_MEMORY_ACCESS;
    H_DEBUG << e.what() << "\n";
  } catch (StaticModeViolation const &e) {
    ret.status_code = EVMC_STATIC_MODE_VIOLATION;
    H_DEBUG << e.what() << "\n";
  } catch (InternalErrorException const &e) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    H_DEBUG << "InternalError: " << e.what() << "\n";
  } catch (exception const &e) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    H_DEBUG << "Unknown exception: " << e.what() << "\n";
  } catch (...) {
    ret.status_code = EVMC_INTERNAL_ERROR;
    H_DEBUG << "Totally unknown exception\n";
  }

  return ret;
}

bool athena_parse_sys_option(athena_instance *athena, string const &_name,
                             string const &value) {
  athenaAssert(_name.find("sys:") == 0, "");
  string name = _name.substr(4, string::npos);
  evmc_address address{};

  if (name.find("0x") == 0) {
    // hex address
    bytes ret = parseHexString(name.substr(2, string::npos));
    if (ret.empty()) {
      H_DEBUG << "Failed to parse hex address: " << name << "\n";
      return false;
    }
    if (ret.size() != 20) {
      H_DEBUG << "Invalid address: " << name << "\n";
      return false;
    }

    copy(ret.begin(), ret.end(), address.bytes);
  } else {
    // alias
    const map<string, evmc_address> aliases = {
        {string("sentinel"), sentinelAddress},
        {string("evm2wasm"), evm2wasmAddress},
        {string("runevm"), runevmAddress},
    };

    if (aliases.count(name) == 0) {
      H_DEBUG << "Failed to resolve system contract alias: " << name << "\n";
      return false;
    }

    address = aliases.at(name);
  }

  bytes contents = loadFileContents(value);
  if (contents.size() == 0) {
    H_DEBUG << "Failed to load contract source (or empty): " << value << "\n";
    return false;
  }

  H_DEBUG << "Loaded contract for " << name << " from " << value << " ("
          << contents.size() << " bytes)\n";

  athena->contract_preload_list[address] = move(contents);

  return true;
}

evmc_set_option_result athena_set_option(evmc_vm *instance, char const *name,
                                         char const *value) noexcept {
  athena_instance *athena = static_cast<athena_instance *>(instance);

  if (strcmp(name, "evm1mode") == 0) {
    if (evm1mode_options.count(value)) {
      athena->evm1mode = evm1mode_options.at(value);
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strcmp(name, "metering") == 0) {
    if (strcmp(value, "true") == 0)
      athena->metering = true;
    else if (strcmp(value, "false") != 0)
      return EVMC_SET_OPTION_INVALID_VALUE;
    return EVMC_SET_OPTION_SUCCESS;
  }

  if (strcmp(name, "benchmark") == 0) {
    if (strcmp(value, "true") == 0) {
      WasmEngine::enableBenchmarking();
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strcmp(name, "engine") == 0) {
    auto it = wasm_engine_map.find(value);
    if (it != wasm_engine_map.end()) {
      wasmEngineCreateFn = it->second;
      athena->engine = wasmEngineCreateFn();
      return EVMC_SET_OPTION_SUCCESS;
    }
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  if (strncmp(name, "sys:", 4) == 0) {
    if (athena_parse_sys_option(athena, string(name), string(value)))
      return EVMC_SET_OPTION_SUCCESS;
    return EVMC_SET_OPTION_INVALID_VALUE;
  }

  return EVMC_SET_OPTION_INVALID_NAME;
}

void athena_destroy(evmc_vm *instance) noexcept {
  athena_instance *athena = static_cast<athena_instance *>(instance);
  delete athena;
}

evmc_capabilities_flagset athena_get_capabilities(evmc_vm *instance) {
  evmc_capabilities_flagset caps = EVMC_CAPABILITY_EWASM;
  if (static_cast<athena_instance *>(instance)->evm1mode !=
      athena_evm1mode::reject)
    caps |= EVMC_CAPABILITY_EVM1;
  return caps;
}

} // anonymous namespace

void sig_abrt(int sig)
{
	const int	BT_BUF_SIZE=100;
	if (sig == SIGABRT) {
		void *buffer[BT_BUF_SIZE];
		char **strings;
		auto nptrs = backtrace(buffer, BT_BUF_SIZE);
		cerr << "backtrace() returned " << nptrs << " addresses\n";
		strings = backtrace_symbols(buffer, nptrs);
		if (strings == nullptr) {
			perror("backtrace_symbols");
		} else {
			for (auto i=0;i<nptrs; ++i)
				cerr << strings[i] << std::endl;
			free(strings);
		}
	} else {
		cerr << "Unexpected signal " << sig << " received\n";
	}
	std::_Exit(EXIT_FAILURE);
}


extern "C" {

EVMC_EXPORT evmc_vm *evmc_create_athena() noexcept {
  auto prev_hdl = std::signal(SIGABRT, sig_abrt);
  if (prev_hdl == SIG_ERR) {
	cerr << "setup SIGABRT failed\n";
	return nullptr;
  }
  athena_instance *instance = new athena_instance;
  instance->destroy = athena_destroy;
  instance->execute = athena_execute;
  instance->get_capabilities = athena_get_capabilities;
  instance->set_option = athena_set_option;
  return instance;
}

#if athena_EXPORTS
// If compiled as shared library, also export this symbol.
EVMC_EXPORT evmc_vm *evmc_create() noexcept { return evmc_create_athena(); }
#endif
}

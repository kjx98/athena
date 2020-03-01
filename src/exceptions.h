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

#pragma once

#include <string>

namespace athena {

class AthenaException : public std::exception {
public:
  explicit AthenaException(): msg({}) {}
  explicit AthenaException(std::string _msg): msg(std::move(_msg)) {}
  const char* what() const noexcept override { return msg.c_str(); }
protected:
  std::string msg;
};

class InternalErrorException : public AthenaException {
  using AthenaException::AthenaException;
};
class VMTrap : public AthenaException {
  using AthenaException::AthenaException;
};
class ArgumentOutOfRange : public AthenaException {
  using AthenaException::AthenaException;
};
class OutOfGas : public AthenaException {
  using AthenaException::AthenaException;
};
class ContractValidationFailure : public AthenaException {
  using AthenaException::AthenaException;
};
class InvalidMemoryAccess : public AthenaException {
  using AthenaException::AthenaException;
};
class EndExecution : public AthenaException {
  using AthenaException::AthenaException;
};

/// Static Mode Violation.
///
/// This exception is thrown when state modifying EEI function is called
/// in static mode.
class StaticModeViolation : public AthenaException {
public:
  explicit StaticModeViolation(std::string const& _functionName):
    AthenaException("Static mode violation in " + _functionName + ".")
  {}
};

#define athenaAssert(condition, msg) { \
  if (!(condition)) throw athena::InternalErrorException{msg}; \
}

#define ensureCondition(condition, ex, msg) { \
  if (!(condition)) throw athena::ex{msg}; \
}

}
